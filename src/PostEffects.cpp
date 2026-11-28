/************************************************************************
     File:        PostEffects.cpp

     Comment:     Sky, water, SSAO, bloom, god rays and tone mapping.
                  See PostEffects.h for an overview.

*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <math.h>
#include <stdio.h>

#include "PostEffects.h"
#include "MatrixMath.h"
#include "PerfStats.h"
#include "LandWaterMotion.h"
#include "SkyHorizon.h"
#include "UnderwaterOptics.h"
#include "WorldShape.h"

namespace
{

// ---------------------------------------------------------------------
// Shaders. All of them are GLSL 1.20 so they run on the same
// compatibility context as the rest of the program.
// ---------------------------------------------------------------------

// Full screen triangle pair: the quad is drawn in clip space.
const char *kQuadVertex = R"GLSL(
varying vec2 vUv;
void main()
{
    vUv = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)GLSL";

// Shared noise helpers, pasted in front of the shaders that use them.
const char *kNoise = R"GLSL(
float hash2(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float hash3(vec3 p)
{
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}
float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), u.x),
               mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}
)GLSL";

const char *kSkyFragment = R"GLSL(
varying vec2 vUv;
uniform mat4 uInvViewProj;
uniform vec3 uSunDir;
uniform vec3 uMoonDir;
uniform vec3 uSunColor;    // zero while the sun is down
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform float uDay;
uniform float uDusk;
uniform float uTime;
uniform float uOvercast;   // rain clouds closing in

void main()
{
    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 nearP = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 farP = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ray = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);
    float y = ray.y;

    vec3 col = mix(uHorizon, uZenith, pow(clamp(y, 0.0, 1.0), 0.5));
    if (y < 0.0)
        col = uHorizon * mix(1.0, 0.5, clamp(-y * 3.0, 0.0, 1.0));

    // The golden hour: the band of sky along the horizon glows orange on
    // the sun's side and rose to violet on the other (SkyHorizon.h: the fog
    // over the sea and the land takes the same colour at the horizon), and
    // fades upwards over a wide band.
    vec2 dirXZ = ray.xz / max(length(ray.xz), 1e-4);
    vec2 sunFlat = uSunDir.xz / max(length(uSunDir.xz), 1e-4);
    float toward = dot(dirXZ, sunFlat) * 0.5 + 0.5;
    float band = exp(-max(y, 0.0) * 3.0) * (y < 0.0 ? mix(1.0, 0.4, clamp(-y * 3.0, 0.0, 1.0)) : 1.0);
    col = mix(col, horizonWarm(ray, uSunDir), band * uDusk * 0.9);
    // higher up the golden-hour sky turns violet
    col = mix(col, vec3(0.09, 0.07, 0.28), (1.0 - band) * uDusk * 0.35);

    // Sun: a square, as in a block game, with a glow round it that is wider
    // and warmer at dusk.
    vec3 east = normalize(cross(vec3(0.0, 1.0, 0.0), uSunDir));
    vec3 north = cross(uSunDir, east);
    float cs = max(dot(ray, uSunDir), 0.0);
    col += uSunColor * pow(cs, 400.0) * 2.0 + sunGlow(ray, uSunDir, uSunColor, uDusk);
    if (cs > 0.99) {
        vec2 q = abs(vec2(dot(ray, east), dot(ray, north)));
        col += uSunColor * vec3(1.0, 0.85 + 0.15 * uDay, 0.5 + 0.5 * uDay) * (1.0 - smoothstep(0.034, 0.037, max(q.x, q.y))) * (6.0 + 12.0 * (1.0 - uDusk));
    }

    // Moon: a pale square with darker patches (its seas), a soft halo, and
    // the stars; they come out as the sky darkens.
    float night = 1.0 - uDay;
    vec3 mEast = normalize(cross(vec3(0.0, 1.0, 0.0), uMoonDir));
    vec3 mNorth = cross(uMoonDir, mEast);
    float cm = dot(ray, uMoonDir);
    if (cm > 0.99) {
        vec2 q = vec2(dot(ray, mEast), dot(ray, mNorth));
        float disc = 1.0 - smoothstep(0.030, 0.033, max(abs(q.x), abs(q.y)));
        vec2 pix = floor((q + 0.0315) / 0.063 * 8.0); // its 8 x 8 pixels
        float sea = step(0.62, hash2(pix + 3.0));
        vec3 face = mix(vec3(1.25, 1.3, 1.4), vec3(0.75, 0.8, 0.95), sea);
        col = mix(col, face * (0.35 + 0.65 * night), disc * night);
    }
    col += vec3(0.30, 0.38, 0.60) * (pow(max(cm, 0.0), 200.0) * 0.6 + pow(max(cm, 0.0), 12.0) * 0.12) * night;
    if (y > 0.0 && night > 0.01) {
        vec3 d = ray * 260.0;
        vec3 cell = floor(d);
        float h = hash3(cell);
        float star = step(0.9972, h) * smoothstep(0.45, 0.1, length(fract(d) - 0.5));
        float twinkle = 0.6 + 0.4 * sin(uTime * 3.0 + h * 400.0);
        col += vec3(star * twinkle * 1.3 * night * night * smoothstep(0.0, 0.15, y));
    }

    // Clouds: fractal noise on a flat layer high above, drifting with the wind.
    if (y > 0.01) {
        vec2 p = ray.xz / y * 0.9 + vec2(uTime * 0.012, uTime * 0.004);
        float n = fbm(p * 1.3);
        float cover = smoothstep(0.48 - 0.42 * uOvercast, 0.78 - 0.36 * uOvercast, n);
        float thick = fbm(p * 1.3 + uSunDir.xz * 0.12);
        vec3 lit = uSunColor * 0.9 + uZenith * 0.8 + uHorizon * 0.3;
        // at dusk the clouds catch the low sun from below: gold towards it,
        // pink overhead and away from it
        lit = mix(lit, mix(vec3(0.85, 0.32, 0.38), vec3(1.6, 0.66, 0.26), toward), uDusk * 0.75);
        // at night the moon gives them a silver edge
        lit += vec3(0.05, 0.06, 0.10) * night;
        vec3 cloudCol = mix(lit, lit * 0.55, smoothstep(0.4, 0.8, thick));
        cloudCol *= 1.0 - 0.35 * uOvercast;
        float alpha = cover * smoothstep(0.01, 0.18, y) * mix(0.92, 1.0, uOvercast);
        col = mix(col, cloudCol, alpha);
    }

    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

// The ocean's eight Gerstner waves live in UnderwaterOptics.h (kOceanWavesGlsl)
// so the water surface and underwater caustics/light shafts stay in lockstep.

// Coastal shoaling wave model shared by kWaterVertex and kWaterFragment so the
// lifted 3D wave ridge, its tilted sunlit normal, breaking foam lip and shore
// impact swash stay in exact lockstep all the way into bays and shore blocks.
const char *kCoastWaveGlsl = R"GLSL(
uniform sampler2D uCoastSdf; // continuous offshore distance (WorldShape::inland)

void coastWave(vec2 p, float sd, float t, out float h, out float dh, out float crest,
               out float wake, out float envOut)
{
    // Stays active all the way into recessed voxel coves (sd down to -3.5) so
    // incoming rollers never fade out before hitting the rocks or sand beach.
    float env = smoothstep(-9.0, -3.5, sd) * (1.0 - smoothstep(14.0, 36.0, sd));
    if (env <= 0.001) {
        h = 0.0;
        dh = 0.0;
        crest = 0.0;
        wake = 0.0;
        envOut = 0.0;
        return;
    }
    // Nonlinear shoaling: waves slow and compress gently as depth drops near shore.
    float shoal = exp(-max(sd + 5.0, 0.0) * 0.09);
    float sEff = sd - 3.2 * shoal;
    float dsEff = 1.0 + 0.288 * shoal;
    float warp = (noise(p * 0.042 + vec2(t * 0.032, -t * 0.024)) - 0.5) * 4.2 +
                 (noise(p * 0.115 + vec2(-t * 0.048, t * 0.036)) - 0.5) * 1.3;
    float waveSet =
        0.45 + 0.55 * smoothstep(0.18, 0.78, noise(p * 0.022 + vec2(-t * 0.045, t * 0.035)));
    // Periodic skewed Stokes/shoaling wave rolling shoreward (sd decreasing):
    // steep front face facing shore (dh > 0), sharp crest, gentle seaward back.
    float theta = (sEff + warp) * 0.40 + t * 1.18;
    float rawH = 0.62 * sin(theta) + 0.28 * sin(2.0 * theta - 0.65) +
                 0.12 * sin(3.0 * theta - 1.30);
    float rawDh = (0.62 * cos(theta) + 0.56 * cos(2.0 * theta - 0.65) +
                   0.36 * cos(3.0 * theta - 1.30)) *
                  0.40 * dsEff;
    float amp = env * waveSet;
    h = rawH * amp;
    dh = rawDh * amp;
    float wPh = fract((theta - 1.05) / 6.2831853);
    crest = smoothstep(0.32, 0.92, rawH) * amp;
    wake = smoothstep(0.0, 0.09, wPh) * pow(1.0 - smoothstep(0.09, 0.74, wPh), 1.65) * amp;
    envOut = amp;
}
)GLSL";

// The surface is a grid of quads around the camera; the waves move its
// vertices. Further out the grid ends in flat skirts, so the waves fade to
// nothing before they get there.
// The water on land (uIsland) is the faces of its blocks instead, in world
// space, moved as LandWaterMotion.h says.
const char *kWaterVertex = R"GLSL(
uniform vec2 uCenter;      // where the grid is, snapped to its cell size
uniform float uTime;
uniform float uWaveReach;  // half the size of the grid
uniform int uIsland;
varying vec3 vWorld;
varying vec2 vBase;
varying float vWave;
varying vec3 vFaceNormal;
varying float vStirred;    // the water on land: how stirred up it is, 0 to 0.4
void main()
{
    vStirred = 0.0;
    if (uIsland != 0) {
        vec3 land = gl_Vertex.xyz;
        float stirred;
        land += landWaterMotion(gl_MultiTexCoord1.x, land, uTime, stirred);
        vStirred = stirred;
        vBase = land.xz;
        vWave = 0.0;
        vWorld = land;
        vFaceNormal = gl_Normal;
        gl_Position = gl_ProjectionMatrix * (gl_ModelViewMatrix * vec4(land, 1.0));
        return;
    }
    vFaceNormal = vec3(0.0, 1.0, 0.0);
    // The modelview matrix holds only the camera here.
    vec3 base = vec3(gl_Vertex.x + uCenter.x, gl_Vertex.y, gl_Vertex.z + uCenter.y);
    float fade = 1.0 - smoothstep(0.55 * uWaveReach, 0.95 * uWaveReach, length(gl_Vertex.xz));
    vec3 disp;
    vec3 normal;
    oceanWaves(base.xz, uTime, 12.0, disp, normal); // four grid cells or longer
    vec2 sdfUv = base.xz / 1280.0 + 0.5;
    float sd = texture2DLod(uCoastSdf, sdfUv, 0.0).r;
    if (sd > -9.0 && sd < 36.0) {
        float cH, cDh, cCrest, cWake, cEnv;
        coastWave(base.xz, sd, uTime, cH, cDh, cCrest, cWake, cEnv);
        float du = 1.0 / 256.0;
        vec2 g = vec2(texture2DLod(uCoastSdf, sdfUv + vec2(du, 0.0), 0.0).r -
                          texture2DLod(uCoastSdf, sdfUv - vec2(du, 0.0), 0.0).r,
                      texture2DLod(uCoastSdf, sdfUv + vec2(0.0, du), 0.0).r -
                          texture2DLod(uCoastSdf, sdfUv - vec2(0.0, du), 0.0).r);
        float gl = length(g);
        vec2 seaDir = gl > 1e-4 ? g / gl : vec2(1.0, 0.0);
        // Lift the rolling coastal crest and pitch its lip gently shoreward.
        disp.y += cH * 0.56;
        disp.xz -= seaDir * (cCrest * 0.45);
    }
    vec3 world = base + disp * fade;
    vBase = base.xz;
    vWave = fade;
    vWorld = world;
    gl_Position = gl_ProjectionMatrix * (gl_ModelViewMatrix * vec4(world, 1.0));
}
)GLSL";

// Screen space reflections: follow the mirrored ray through the depth
// buffer of what is already on screen and take the colour where it lands.
// Used by the water and by wet ground. Everything is in view space.
const char *kSsrGlsl = R"GLSL(
uniform sampler2D uRefraction; // the scene so far
uniform sampler2D uDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;

vec3 viewPos(vec2 uv, float depth)
{
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

// How far the point on the ray at distance t lies behind what is on screen
// there (positive: behind it). uv: where on screen; 1e9 off screen or sky.
float behindScene(vec3 P, vec3 R, float t, out vec2 uv, out vec3 scene)
{
    vec3 S = P + R * t;
    vec4 c = uProj * vec4(S, 1.0);
    uv = vec2(-1.0);
    scene = S;
    if (c.w <= 0.0)
        return 1e9;
    uv = c.xy / c.w * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1e9;
    float d = texture2D(uDepth, uv).r;
    if (d >= 1.0)
        return -1e9;
    scene = viewPos(uv, d);
    return scene.z - S.z;
}

// rgb: the reflected colour, a: how much to trust it (0 when the ray left
// the screen or hit nothing). planeN: only things in front of this plane
// through P count, so water does not mirror the sea bed under it.
vec4 traceReflection(vec3 P, vec3 R, vec3 planeN)
{
    float prev = 0.3;
    float t = 1.2;
    for (int i = 0; i < 30; ++i) {
        vec2 uv;
        vec3 scene;
        float behind = behindScene(P, R, t, uv, scene);
        if (behind > 1e8)
            break;
        if (behind > 0.0 && behind < (t - prev) + 3.0 && dot(scene - P, planeN) > 0.3) {
            // Close in on the surface between the last two steps.
            float lo = prev;
            float hi = t;
            for (int k = 0; k < 5; ++k) {
                float mid = 0.5 * (lo + hi);
                if (behindScene(P, R, mid, uv, scene) > 0.0)
                    hi = mid;
                else
                    lo = mid;
            }
            behindScene(P, R, hi, uv, scene);
            vec2 e = smoothstep(vec2(0.0), vec2(0.1), uv) * (1.0 - smoothstep(vec2(0.9), vec2(1.0), uv));
            return vec4(texture2D(uRefraction, uv).rgb, e.x * e.y);
        }
        prev = t;
        t = t * 1.2 + 0.6;
    }
    return vec4(0.0);
}
)GLSL";

// Split into parts passed to glShaderSource() so every raw string literal stays
// well below MSVC's 16,380-byte literal limit (C2026).
const char *kWaterHelpers = R"GLSL(
varying vec3 vWorld;
varying vec2 vBase;
varying float vWave;
varying vec3 vFaceNormal;
varying float vStirred;
uniform int uIsland;       // the water on land: no big waves, nothing under it to see from below
uniform sampler2D uReflection;
uniform sampler2DShadow uShadowMap;
uniform mat4 uLightMatrix;
uniform mat4 uView;
uniform int uShadowsOn;
uniform float uShadowTexel;
uniform vec2 uScreen;
uniform vec3 uCamPos;
uniform float uTime;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunDir;      // for the colour the fog takes (SkyHorizon.h)
uniform vec3 uSunColor;    // zero while the sun is down
uniform float uDusk;
uniform float uFogDensity;
uniform int uUnder;        // the camera is below the surface
uniform vec3 uDeepColor;   // colour of the water around the camera
uniform int uSsrOn;        // screen space reflections (not in the top view)
uniform float uRain;

vec2 noiseSlope(vec2 p)
{
    float e = 0.08;
    float h = noise(p);
    return vec2(noise(p + vec2(e, 0.0)) - h, noise(p + vec2(0.0, e)) - h) / e;
}

// Fine ripples: three rotated, domain-warped layers of turbulent chop so the
// surface shows no axis-aligned noise grid regularity.
vec2 rippleSlope(vec2 p, float t)
{
    vec2 q1 = mat2(0.93, 0.37, -0.37, 0.93) * (p * 0.52) + vec2(t * 0.33, t * 0.21);
    vec2 g1 = noiseSlope(q1);
    vec2 q2 = mat2(0.57, -0.82, 0.82, 0.57) * (p * 1.18) + vec2(-t * 0.42, t * 0.29) + g1 * 0.35;
    vec2 g2 = noiseSlope(q2);
    vec2 q3 = mat2(-0.41, 0.91, -0.91, -0.41) * (p * 2.55) + vec2(t * 0.27, -t * 0.51) - g2 * 0.28;
    vec2 g3 = noiseSlope(q3);
    vec2 s1 = mat2(0.93, -0.37, 0.37, 0.93) * g1 * 0.062;
    vec2 s2 = mat2(0.57, 0.82, -0.82, 0.57) * g2 * 0.038;
    vec2 s3 = mat2(-0.41, -0.91, 0.91, -0.41) * g3 * 0.020;
    return s1 + s2 + s3;
}

// High-frequency sharp-crested capillary & short-gravity wave cascade for the
// open ocean. Height falls off quickly with frequency, but wave slope stays
// strong well into the capillary band (Phillips/JONSWAP), shattering specular
// reflections into crisp multi-scale facets with distance LOD to prevent far shimmer.
vec2 oceanDetailSlope(vec2 p, float t, float dist)
{
    vec2 q = p;
    vec2 g = vec2(0.0);
    float freq = 0.72;
    float amp = 0.165;
    float angle = 0.35;
    for (int i = 0; i < 12; ++i) {
        float lod = 1.0 - smoothstep(35.0, 180.0, dist * freq * 0.14);
        if (lod <= 0.001)
            break;
        vec2 dir = vec2(cos(angle), sin(angle));
        float speed = sqrt(9.8 * freq) * 0.58 + 0.30 * freq;
        float x = dot(dir, q) * freq - t * speed + float(i) * 1.73;
        float e = exp(sin(x) - 1.0);
        float slope1d = e * cos(x);
        // Domain drag: each wave pushes higher-frequency wavelets for realistic chop.
        q -= dir * (slope1d * (0.55 / freq));
        g += dir * (slope1d * amp * lod);
        freq *= 1.38;
        amp *= 0.89;
        angle += 2.3999632;
    }
    return g;
}

// Lacy cellular sea foam with porous open holes and stretched bubble strands
// so coves and trailing surf wakes never turn into solid flat grey-white slabs.
float foamLace(vec2 p, float t)
{
    vec2 warp = vec2(noise(p * 0.28 + vec2(t * 0.12, -t * 0.09)),
                     noise(p * 0.28 + vec2(-t * 0.10, t * 0.13) + vec2(7.1, 3.4))) -
                0.5;
    vec2 q = p + warp * 1.8;
    vec2 a1 = mat2(0.86, 0.51, -0.51, 0.86) * (q * 0.72) + vec2(t * 0.16, -t * 0.11);
    float w1 = 1.0 - abs(noise(a1) * 2.0 - 1.0);
    w1 *= w1;
    vec2 a2 = mat2(0.42, -0.91, 0.91, 0.42) * (q * 1.75) + vec2(-t * 0.22, t * 0.18) +
              vec2(4.3, -2.7);
    float w2 = 1.0 - abs(noise(a2) * 2.0 - 1.0);
    w2 *= w2;
    float holes = noise(q * 3.1 + vec2(-t * 0.25, t * 0.21));
    return (w1 * 0.58 + w2 * 0.42) * (0.55 + 0.65 * holes);
}

// Rings spreading from where rain drops land: one drop per cell at a time.
vec2 rainSlope(vec2 p, float t)
{
    vec2 g = vec2(0.0);
    for (int layer = 0; layer < 2; ++layer) {
        vec2 q = p * 0.5 + float(layer) * vec2(7.3, 3.1);
        vec2 cell = floor(q);
        vec2 f = fract(q) - 0.5;
        float h = hash2(cell + float(layer) * 17.0);
        float age = fract(t * 0.9 + h);
        vec2 d = f - (vec2(hash2(cell + 3.1), hash2(cell + 7.7)) - 0.5) * 0.4;
        float r = length(d);
        float ring = sin((r - age * 0.45) * 40.0) * smoothstep(0.08, 0.0, abs(r - age * 0.45)) * (1.0 - age);
        g += d / max(r, 0.001) * ring;
    }
    return g * 0.22;
}
)GLSL";

const char *kWaterFragment = R"GLSL(
void main()
{
    vec3 P = vWorld;
    vec3 toCam = uCamPos - P;
    float dist = length(toCam);
    vec3 V = toCam / dist;

    vec2 suv = gl_FragCoord.xy / uScreen;
    vec3 waterView = viewPos(suv, gl_FragCoord.z);
    vec3 upView = uView[1].xyz;
    vec2 sdfUv = vBase / 1280.0 + 0.5;
    float coastSdf = uIsland == 0 ? texture2D(uCoastSdf, sdfUv).r : 100.0;
    float cH = 0.0;
    float cDh = 0.0;
    float cCrest = 0.0;
    float cWake = 0.0;
    float cEnv = 0.0;
    float shallowMask = 0.0;
    if (uIsland == 0 && coastSdf > -9.0 && coastSdf < 36.0) {
        coastWave(vBase, coastSdf, uTime, cH, cDh, cCrest, cWake, cEnv);
        float cVert = max(0.0, dot(waterView - viewPos(suv, texture2D(uDepth, suv).r), upView));
        shallowMask = uUnder != 0 ? 1.0 : (1.0 - smoothstep(14.0, 24.0, cVert));
    }

    // Normal: the big waves, coastal shoaling rollers, then high-frequency
    // sharp-crested capillary chop (and rain rings) on top. The water on land
    // keeps smoother ripples; on a fall or the side of a step they run down it.
    vec3 disp = vec3(0.0);
    vec3 N = vec3(0.0, 1.0, 0.0);
    // The water on land's normal is smooth over the bend where it pours
    // over an edge (LandWorld gives the corners there a normal between up
    // and out), so the flat water, the bend and the fall are one surface:
    // `fall` goes from 0 on the flat to 1 on an upright face, and the
    // ripples, the colour and the foam go over with it.
    vec3 faceN = normalize(vFaceNormal);
    float fall = uIsland != 0 ? 1.0 - smoothstep(0.35, 0.85, faceN.y) : 0.0;
    if (uIsland != 0) {
        // where falling water lands the ripples are stronger and quicker
        vec2 g = rippleSlope(vBase, uTime * (1.0 + vStirred * 3.0)) * (2.5 + vStirred * 8.0);
        if (uRain > 0.0)
            g += rainSlope(vBase, uTime) * uRain;
        vec3 flatRipple = vec3(-g.x, 0.0, -g.y);
        // on a fall the same ripples run down it
        vec3 across = normalize(vec3(-faceN.z, 0.0, faceN.x) + vec3(1e-4, 0.0, 0.0));
        vec2 s = rippleSlope(vec2(dot(P, across), P.y + uTime * 3.0), uTime) * 2.5;
        vec3 fallRipple = across * s.x + vec3(0.0, s.y, 0.0);
        N = normalize(faceN + mix(flatRipple, fallRipple, fall));
    } else {
        oceanWaves(vBase, uTime, 0.0, disp, N);
        vec2 g = oceanDetailSlope(vBase, uTime, dist);
        if (cEnv > 0.001 && shallowMask > 0.001) {
            float du = 1.0 / 256.0;
            vec2 cg = vec2(texture2D(uCoastSdf, sdfUv + vec2(du, 0.0)).r -
                               texture2D(uCoastSdf, sdfUv - vec2(du, 0.0)).r,
                           texture2D(uCoastSdf, sdfUv + vec2(0.0, du)).r -
                               texture2D(uCoastSdf, sdfUv - vec2(0.0, du)).r);
            float gl = length(cg);
            vec2 seaDir = gl > 1e-4 ? cg / gl : vec2(1.0, 0.0);
            // Steep shoreward-facing front face and gentle seaward back give
            // each rolling breaker 3D curvature, refraction bending and highlights.
            g += seaDir * (cDh * 0.78 * shallowMask);
            disp.y += cH * 0.56 * shallowMask;
        }
        if (uRain > 0.0)
            g += rainSlope(vBase, uTime) * uRain;
        N = normalize(vec3(N.x - g.x, N.y, N.z - g.y));
    }
    // Calm the waves far away so they do not shimmer.
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), clamp(dist / 500.0, 0.0, 0.85)));

    if (uUnder != 0) {
        // Seen from below, light from above comes through Snell's window,
        // a wide undulating cone around straight up; outside it the underside
        // of the waves acts as a rippling silvery-blue liquid mirror.
        vec3 nUnder = -N;
        vec3 L = normalize(uLightDir);
        float cosView = max(dot(V, nUnder), 0.0);
        float directSun = max(dot(-V, L), 0.0);
        // Let the sun shine smoothly through the window even at lower angles.
        float window = max(smoothstep(0.24, 0.56, cosView), pow(directSun, 14.0) * 0.85);
        vec2 ruv = clamp(suv + N.xz * 0.026, vec2(0.002), vec2(0.998));
        if (texture2D(uDepth, ruv).r < gl_FragCoord.z)
            ruv = suv;
        vec3 above = texture2D(uRefraction, ruv).rgb;
        if (directSun > 0.88) {
            // Soften the crisp square sky sun through the moving water surface
            // into a luminous liquid glow rather than a jagged warped polygon.
            vec2 o1 = vec2(0.006, 0.003) + N.xz * 0.006;
            vec2 o2 = vec2(-0.004, 0.007) - N.xz * 0.005;
            vec2 u1 = clamp(ruv + o1, vec2(0.002), vec2(0.998));
            vec2 u2 = clamp(ruv - o1, vec2(0.002), vec2(0.998));
            vec2 u3 = clamp(ruv + o2, vec2(0.002), vec2(0.998));
            vec2 u4 = clamp(ruv - o2, vec2(0.002), vec2(0.998));
            vec3 soft = above * 0.28 +
                        (texture2D(uDepth, u1).r < gl_FragCoord.z ? above : texture2D(uRefraction, u1).rgb) * 0.18 +
                        (texture2D(uDepth, u2).r < gl_FragCoord.z ? above : texture2D(uRefraction, u2).rgb) * 0.18 +
                        (texture2D(uDepth, u3).r < gl_FragCoord.z ? above : texture2D(uRefraction, u3).rgb) * 0.18 +
                        (texture2D(uDepth, u4).r < gl_FragCoord.z ? above : texture2D(uRefraction, u4).rgb) * 0.18;
            above = mix(above, soft, smoothstep(0.88, 0.96, directSun));
        }
        // Wave-facet sun glitter aligned with the visible sky sun (no second
        // displaced sun or abrupt critical-angle cut across the sun).
        vec3 sunRay = normalize(-V + vec3(N.x, 0.0, N.z) * 0.42);
        float sunHit = max(dot(sunRay, L), 0.0);
        vec3 glitter =
            uLightColor * (pow(sunHit, 160.0) * 6.5 + pow(sunHit, 26.0) * 0.85 + pow(directSun, 10.0) * 0.25);
        // Total internal reflection outside the window: undulating wave facets
        // catch silvery turquoise light on crests and darken in troughs.
        vec3 R = reflect(-V, nUnder);
        vec3 reflWater = mix(uDeepColor * 0.75,
                             uDeepColor * 2.4 + vec3(0.015, 0.075, 0.115),
                             smoothstep(-0.45, 0.15, R.y));
        float waveTilt = max(dot(N, normalize(vec3(L.x, 0.45, L.z))), 0.0);
        float facet = smoothstep(0.45, 0.92, waveTilt);
        vec3 silver = (uDeepColor * 2.2 + uLightColor * vec3(0.05, 0.16, 0.22)) * facet;
        vec3 mirror = reflWater + silver + above * (0.14 + 0.14 * facet);
        vec3 col = mix(mirror, above + glitter, window);
        float rim = smoothstep(0.18, 0.30, cosView) - smoothstep(0.30, 0.46, cosView);
        col += (uDeepColor * 2.5 + uLightColor * vec3(0.06, 0.16, 0.20)) * rim * (0.65 + 0.7 * facet);
        gl_FragColor = vec4(col, 1.0);
        return;
    }
    // (on the water on land only the ripples bend the view, not the slope
    // of the face, or the bend over an edge would show what is far away)
    vec2 distort = (uIsland != 0 ? N.xz - faceN.xz : N.xz) * 0.03;

    // Refraction: look through the surface at the sea bed. If the bent ray
    // lands on something in front of the water, fall back to the straight one.
    vec2 ruv = suv + distort;
    float sceneDepth = texture2D(uDepth, ruv).r;
    if (sceneDepth < gl_FragCoord.z) {
        ruv = suv;
        sceneDepth = texture2D(uDepth, suv).r;
    }
    float thickness = length(viewPos(ruv, sceneDepth) - waterView);
    vec3 refr = texture2D(uRefraction, ruv).rgb;

    // Smooth true vertical depth (guarded against foreground geometry) across
    // a two-ring kernel (~3.5 world units) to soften 5-unit voxel staircase
    // steps along shallow shelves without lateral offset bias.
    float vertDepth = max(0.0, dot(waterView - viewPos(ruv, sceneDepth), upView));
    float smoothVert = vertDepth;
    if (uIsland == 0) {
        vec2 stepUv = clamp(vec2(3200.0 / max(dist, 24.0)), vec2(2.5), vec2(16.0)) / uScreen;
        float tSum = vertDepth * 0.24;
        float wSum = 0.24;
        for (int k = 0; k < 8; ++k) {
            vec2 dir = k < 4
                           ? vec2(k == 0 || k == 2 ? 0.5 : -0.5, k < 2 ? 0.5 : -0.5)
                           : (k == 4 ? vec2(1.0, 0.0)
                                     : (k == 5 ? vec2(-1.0, 0.0)
                                               : (k == 6 ? vec2(0.0, 1.0) : vec2(0.0, -1.0))));
            float w = k < 4 ? 0.11 : 0.08;
            vec2 nuv = clamp(ruv + dir * stepUv, vec2(0.002), vec2(0.998));
            float nd = texture2D(uDepth, nuv).r;
            if (nd >= gl_FragCoord.z) {
                tSum += max(0.0, dot(waterView - viewPos(nuv, nd), upView)) * w;
                wSum += w;
            }
        }
        smoothVert = tSum / wSum;
        shallowMask = 1.0 - smoothstep(14.0, 24.0, smoothVert);
    }
    float coastNoise = (noise(P.xz * 0.14 + vec2(3.1, -1.7)) - 0.5) * 1.4 +
                       (noise(P.xz * 0.36 + vec2(-2.3, 4.9)) - 0.5) * 0.5;
    float smoothShore = max(0.0, smoothVert + coastNoise * 0.55);

    float shadow = 1.0;
    if (uShadowsOn != 0) {
        vec4 sc = uLightMatrix * vec4(P, 1.0);
        vec3 c = sc.xyz / sc.w;
        if (c.z < 1.0) {
            shadow = 0.0;
            for (int i = 0; i < 4; ++i) {
                vec2 o = vec2(float(i / 2) - 0.5, float(i - (i / 2) * 2) - 0.5) * (2.0 * uShadowTexel);
                shadow += shadow2D(uShadowMap, vec3(c.xy + o, c.z - 0.0015)).r * 0.25;
            }
        }
    }

    // Light is absorbed with depth, red first: shallow water over sand stays
    // clear turquoise while open ocean deepens into rich sapphire blue.
    float sunUp = clamp(uLightDir.y * 2.0, 0.0, 1.0);
    vec3 scatterColour = uIsland != 0 ? vec3(0.004, 0.050, 0.11) : vec3(0.0025, 0.032, 0.072);
    vec3 scatter = scatterColour * (uLightColor * sunUp * mix(0.6, 1.0, shadow) + uZenith * 2.0);
    // The water on land is only a few blocks deep: it takes its colour as
    // if it were deeper, so a shallow pond still looks like water.
    // A fall has the rock right behind it, so it takes the colour of a
    // pool about a block deep as well, and looks like the water it pours
    // from.
    float smoothPath = smoothVert / max(V.y, 0.28);
    float looksDeep = uIsland != 0 ? thickness * 3.0 + mix(0.8, 3.0, fall)
                                   : mix(thickness, max(0.0, smoothPath + coastNoise * 0.55), 0.72);
    vec3 extinction = uIsland != 0 ? vec3(0.12, 0.030, 0.020) : vec3(0.15, 0.038, 0.026);
    vec3 absorb = exp(-looksDeep * extinction);
    vec3 body = refr * absorb + scatter * (1.0 - absorb);
    if (uIsland == 0) {
        // Turquoise subsurface glow through thin sunlit wave crests, plus subtle
        // trough darkening ahead of each coastal breaker for crisp 3D relief.
        float back = pow(max(dot(-V, normalize(uLightDir)), 0.0), 3.0);
        float crestLift = max(disp.y, 0.0) + max(cH, 0.0) * 1.15 * shallowMask;
        float sss = crestLift * (0.34 + 0.66 * back) * sunUp * mix(0.4, 1.0, shadow);
        body += vec3(0.016, 0.115, 0.135) * uLightColor * sss * (0.35 + 0.65 * (1.0 - absorb.g));
        body *= 1.0 - 0.16 * max(-cH, 0.0) * shallowMask;
    }
)GLSL";

const char *kWaterFragmentTail = R"GLSL(
    // Reflection: what is on screen where the mirrored ray lands (SSR);
    // where that finds nothing, the mirrored view drawn beforehand (it is
    // mirrored in the sea's surface, so the water on land, higher up, takes
    // the sky's colour instead).
    vec3 refl = texture2D(uReflection, suv + distort * (uIsland != 0 ? 1.0 : 1.85)).rgb;
    if (uIsland != 0)
        refl = mix(uHorizon, uZenith, clamp(reflect(-V, N).y * 1.5, 0.0, 1.0));
    if (uSsrOn != 0) {
        mat3 viewRot = mat3(uView[0].xyz, uView[1].xyz, uView[2].xyz);
        vec3 nView = viewRot * N;
        vec3 plane = uIsland != 0 ? faceN : vec3(0.0, 1.0, 0.0);
        vec4 hit = traceReflection(waterView, reflect(normalize(waterView), nView), viewRot * plane);
        refl = mix(refl, hit.rgb, hit.a);
    }
    // Fresnel (Schlick): a mirror at grazing angles, clear looking straight down.
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    fresnel *= 1.0 - 2.4 * fall * (1.0 - fall); // the bend over an edge mirrors less of the sky
    vec3 col = mix(body, refl, fresnel);

    // Sun glints: crisp primary specular plus high-angle micro-facet diamond
    // sparkles so steep top-down views catch dancing sunlit flecks across the sea.
    vec3 Ldir = normalize(uLightDir);
    vec3 H = normalize(Ldir + V);
    float nh = max(dot(N, H), 0.0);
    float spec = pow(nh, 520.0) * 44.0 + pow(nh, 72.0) * 0.75;
    if (uIsland == 0) {
        vec2 sp1 = mat2(0.84, 0.54, -0.54, 0.84) * (P.xz * 2.4) + vec2(uTime * 1.35, -uTime * 0.95);
        vec2 sp2 = mat2(-0.46, 0.89, -0.89, -0.46) * (P.xz * 4.1) + vec2(-uTime * 1.65, uTime * 1.15);
        vec2 micro = (noiseSlope(sp1) * 0.14 + noiseSlope(sp2) * 0.10) *
                     (1.0 - smoothstep(140.0, 420.0, dist));
        vec3 nSpark = normalize(vec3(N.x - micro.x, N.y, N.z - micro.y));
        float nhSpark = max(dot(nSpark, H), 0.0);
        spec += pow(nhSpark, 210.0) * 18.0 * smoothstep(0.55, 0.94, nh);
    }
    // (hardly any on the bend over an edge and down a fall, where the
    // midday sun would make a white band across the water)
    float glints = uIsland != 0 ? mix(0.1, 1.0, smoothstep(0.93, 0.995, faceN.y)) : 1.0;
    float specFloor = uIsland == 0 ? (0.58 + 0.72 * fresnel) : (0.3 + fresnel);
    col += uLightColor * spec * shadow * specFloor * glints;

    // Porous lacy shore swash (surging in and draining back in lockstep with
    // each incoming coastal roller instead of vanishing at the boundary),
    // breaking coastal rollers, and gusty wind whitecaps out at sea.
    float lace = foamLace(P.xz, uTime);
    float edgeFoam = uIsland == 0 ? 1.0 : 0.4 * (1.0 - fall);
    float hit = smoothstep(0.08, 0.85, cH) * shallowMask;
    float backwash = cWake * shallowMask;
    float swash = max(hit, backwash * 0.80);
    float lipReach = uIsland == 0 ? mix(0.60, 1.85, swash) : 0.65;
    float webReach = uIsland == 0 ? mix(1.90, 4.60, max(backwash, hit * 0.70)) : 2.20;
    float shoreLip = (1.0 - smoothstep(0.0, lipReach, smoothShore)) *
                     smoothstep(mix(0.28, 0.08, hit), mix(0.62, 0.40, hit), lace) *
                     (0.66 + 0.34 * hit);
    float shoreWeb = (1.0 - smoothstep(0.0, webReach, smoothShore)) *
                     smoothstep(0.36, 0.78, lace) * (0.52 + 0.46 * swash);
    float edge = max(shoreLip, shoreWeb) * edgeFoam;

    // Breaking coastal rollers: build smoothly from offshore swells into foamy
    // breakers and carry all the way into the shore impact swash without fading out.
    float surf = 0.0;
    if (uIsland == 0 && cEnv > 0.001 && shallowMask > 0.001) {
        float breakAmp = (1.0 - smoothstep(10.0, 28.0, coastSdf)) * shallowMask;
        float surfCrest = cCrest * smoothstep(0.18, 0.56, lace) * 0.95;
        float surfWake = cWake * smoothstep(0.36, 0.78, lace) * 0.58;
        surf = max(surfCrest, surfWake) * breakAmp;
    }

    float gust = noise(P.xz * 0.011 + vec2(uTime * 0.021, uTime * 0.013)) * 0.65 +
                 noise(P.xz * 0.037 - vec2(uTime * 0.017, -uTime * 0.029)) * 0.35;
    gust = mix(smoothstep(0.44, 0.72, gust), 1.0, uRain * 0.6);
    float breaking = smoothstep(0.38, 0.68, noise(P.xz * 0.13 + vec2(uTime * 0.06, -uTime * 0.04)));
    float crest = smoothstep(0.60, 0.96, disp.y) * vWave * 0.55 * gust * breaking *
                  smoothstep(0.32, 0.74, lace);
    float foam = clamp(max(max(edge, surf), crest), 0.0, 0.94);
    vec3 foamLight = uLightColor * max(uLightDir.y, 0.0) * shadow + uZenith * 1.5 + uHorizon * 0.3;
    // Thin lacy foam picks up a subtle turquoise tint; thick breaking lips stay crisp white.
    vec3 foamTint = mix(vec3(0.76, 0.92, 0.96), vec3(0.98, 0.99, 1.0), foam);
    col = mix(col, foamLight * foamTint * 0.94, foam * 0.88);

    // Fade in over the first few centimetres so the shore line is soft.
    col = mix(refr, col, smoothstep(0.0, 0.25, thickness));

    float f = dist * uFogDensity;
    // into the sky at the horizon behind it, as the land does
    col = mix(horizonSky(-V, uHorizon, uSunDir, uSunColor, uDusk), col, exp(-f * f));
    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

// Rain on the ground and the walls, mirrored with the same screen space
// reflections as the sea, blended over the scene by alpha. Only where the
// rain reaches: the scene's alpha holds how open each solid surface is to
// the sky (AdvancedLighting), so the ground under eaves, in tunnels and
// indoors stays dry. Floors get a film of water pooling into puddles,
// ringed by the drops while it rains; walls a thin film running down in
// streaks, which, as on real wet stone, shows mostly at grazing angles and
// mirrors softly (its normal broken up a little), not like glass.
const char *kWetFragment = R"GLSL(
varying vec2 vUv;
uniform mat4 uInvView;
uniform vec2 uTexel;
uniform float uWetness;
uniform float uRain;     // how hard it rains now: the drops' rings
uniform float uTime;
uniform float uWaterLevel;
uniform vec3 uZenith;
uniform vec3 uHorizon;

float hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// The slope a raindrop's ring adds to a puddle at p (world x, z): one drop
// a cell, each at its own time, spreading and fading.
vec2 dropRings(vec2 p, float offset)
{
    vec2 q = p * 0.9 + offset;
    vec2 cell = floor(q);
    vec2 f = fract(q) - 0.5;
    float t = fract(uTime * 0.9 + hash12(cell + offset * 7.0));
    float r = length(f);
    float ring = exp(-pow((r - t * 0.45) * 22.0, 2.0)) * (1.0 - t);
    return f / max(r, 0.001) * ring;
}

void main()
{
    float d = texture2D(uDepth, vUv).r;
    if (d >= 1.0)
        discard;
    float exposure = texture2D(uRefraction, vUv).a;
    if (exposure < 0.05)
        discard; // sheltered: dry
    vec3 P = viewPos(vUv, d);
    // Normal from the depth buffer, using the nearer neighbour on each axis.
    vec3 px = viewPos(vUv + vec2(uTexel.x, 0.0), texture2D(uDepth, vUv + vec2(uTexel.x, 0.0)).r) - P;
    vec3 nx = P - viewPos(vUv - vec2(uTexel.x, 0.0), texture2D(uDepth, vUv - vec2(uTexel.x, 0.0)).r);
    vec3 py = viewPos(vUv + vec2(0.0, uTexel.y), texture2D(uDepth, vUv + vec2(0.0, uTexel.y)).r) - P;
    vec3 ny = P - viewPos(vUv - vec2(0.0, uTexel.y), texture2D(uDepth, vUv - vec2(0.0, uTexel.y)).r);
    vec3 N = normalize(cross(abs(px.z) < abs(nx.z) ? px : nx, abs(py.z) < abs(ny.z) ? py : ny));
    vec3 toEye = -normalize(P);
    if (dot(N, toEye) < 0.0)
        N = -N;

    mat3 invRot = mat3(uInvView[0].xyz, uInvView[1].xyz, uInvView[2].xyz);
    vec3 world = (uInvView * vec4(P, 1.0)).xyz;
    vec3 worldN = invRot * N;
    if (world.y < uWaterLevel + 0.3)
        discard;
    bool floorLike = worldN.y >= 0.8;
    if (!floorLike && abs(worldN.y) > 0.3)
        discard; // undersides, and the slivers along block edges

    float strength;
    vec3 bentN = worldN;
    if (floorLike) {
        // Puddles gather in the low patches of a noise field; between them
        // there is only a thin film.
        float pool = noise(world.xz * 0.11) + 0.35 * noise(world.xz * 0.41);
        float puddle = smoothstep(0.62, 0.80, pool);
        strength = uWetness * exposure * mix(0.22, 1.0, puddle);
        vec2 slope = (dropRings(world.xz, 0.0) + dropRings(world.xz, 0.5)) * 0.3;
        bentN = normalize(worldN + vec3(slope.x, 0.0, slope.y) * uRain * exposure * mix(0.3, 1.0, puddle));
    } else {
        // A film running down the wall in streaks, its sheen broken up.
        float along = dot(world.xz, vec2(-worldN.z, worldN.x));
        float run = noise(vec2(along * 1.3, world.y * 0.12 + uTime * 0.5));
        strength = uWetness * exposure * mix(0.4, 1.0, smoothstep(0.3, 0.75, run));
        vec3 side = vec3(-worldN.z, 0.0, worldN.x);
        bentN = normalize(worldN + side * (noise(vec2(along, world.y) * 2.3) - 0.5) * 0.25 +
                          vec3(0.0, noise(vec2(along, world.y) * 1.7 + 3.1) - 0.5, 0.0) * 0.2);
    }
    N = normalize(bentN * invRot); // back into view space (invRot is a rotation)

    vec3 R = reflect(-toEye, N);
    vec4 hit = traceReflection(P + N * 0.05, R, N);
    vec3 worldR = invRot * R;
    vec3 sky = mix(uHorizon, uZenith, pow(clamp(worldR.y, 0.0, 1.0), 0.5));
    // a wet wall's film is rough and catches the light at any angle a
    // little, on top of what the water's Fresnel term mirrors
    float fresnel = (floorLike ? 0.04 : 0.1) + 0.96 * pow(1.0 - max(dot(N, toEye), 0.0), 5.0);
    gl_FragColor = vec4(mix(sky, hit.rgb, hit.a), clamp(fresnel * strength * 1.4, 0.0, 1.0));
}
)GLSL";

// Under water, part 1 (half size): volumetric light shafts. Each view ray is
// marched with denser steps near the camera, checking both shadow cascades and
// modulating sunlight by broad drifting wave swells so rays stream down as
// smooth curtains rather than noisy high-frequency slices.
// Under water, part 0: the shaft map. waterShaftField() (the beams the
// waves focus, see UnderwaterOptics.h) at eight depths, over a square round
// the camera, drawn once a frame into two textures (four depths each). The
// light shafts below read it instead of working out the waves at every step
// of every pixel, which cost about 4 ms a frame at 2560x1559.
const char *kShaftMapFragment = R"GLSL(
varying vec2 vUv;
uniform vec2 uMapCenter;
uniform float uMapSize;
uniform float uTime;
uniform vec4 uDepths;
void main()
{
    vec2 s = uMapCenter + (vUv - 0.5) * uMapSize;
    gl_FragColor = vec4(waterShaftField(s, uDepths.x, uTime), waterShaftField(s, uDepths.y, uTime),
                        waterShaftField(s, uDepths.z, uTime), waterShaftField(s, uDepths.w, uTime));
}
)GLSL";

const char *kShaftsFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uDepth;
uniform sampler2D uShaftNear; // the shaft map at depths 2, 6, 11, 17
uniform sampler2D uShaftFar;  // ... and 24, 32, 42, 55
uniform vec2 uMapCenter;
uniform float uMapSize;
uniform sampler2DShadow uShadowMap;
uniform mat4 uLightMatrix;
uniform int uShadowsOn;
uniform sampler2DShadow uFarShadowMap;
uniform mat4 uFarMatrix;
uniform int uFarOn;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uWaterLevel;
uniform float uTime;
uniform int uHeadOn;       // the train headlight throws a beam too
uniform vec3 uHeadPos;
uniform vec3 uHeadDir;

float sunAt(vec3 p)
{
    if (uShadowsOn == 0)
        return 1.0;
    vec4 sc = uLightMatrix * vec4(p, 1.0);
    vec3 c = sc.xyz / sc.w;
    float edge = max(abs(c.x - 0.5), abs(c.y - 0.5));
    float fine = c.z >= 1.0 ? 1.0 : shadow2D(uShadowMap, vec3(c.xy, c.z - 0.002)).r;
    float toCoarse = smoothstep(0.42, 0.5, edge);
    if (toCoarse <= 0.0)
        return fine;
    float coarse = 1.0;
    if (uFarOn != 0) {
        vec4 fc = uFarMatrix * vec4(p, 1.0);
        vec3 f = fc.xyz / fc.w;
        float farEdge = max(abs(f.x - 0.5), abs(f.y - 0.5));
        coarse = f.z >= 1.0 ? 1.0 : shadow2D(uFarShadowMap, vec3(f.xy, f.z - 0.003)).r;
        coarse = mix(coarse, 1.0, smoothstep(0.44, 0.5, farEdge));
    }
    return mix(fine, coarse, toCoarse);
}

// waterShaftField(s, depth) from the shaft map: the two depths either
// side, mixed.
float shaftFromMap(vec2 s, float depth)
{
    vec2 uv = (s - uMapCenter) / uMapSize + 0.5;
    vec4 a = texture2D(uShaftNear, uv);
    vec4 b = texture2D(uShaftFar, uv);
    if (depth < 6.0)
        return mix(a.x, a.y, clamp((depth - 2.0) / 4.0, 0.0, 1.0));
    if (depth < 11.0)
        return mix(a.y, a.z, (depth - 6.0) / 5.0);
    if (depth < 17.0)
        return mix(a.z, a.w, (depth - 11.0) / 6.0);
    if (depth < 24.0)
        return mix(a.w, b.x, (depth - 17.0) / 7.0);
    if (depth < 32.0)
        return mix(b.x, b.y, (depth - 24.0) / 8.0);
    if (depth < 42.0)
        return mix(b.y, b.z, (depth - 32.0) / 10.0);
    return mix(b.z, b.w, clamp((depth - 42.0) / 13.0, 0.0, 1.0)) * exp(-max(depth - 55.0, 0.0) * 0.017);
}

void main()
{
    float d = texture2D(uDepth, vUv).r;
    vec4 hit = uInvViewProj * vec4(vUv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 ray = hit.xyz / hit.w - uCamPos;
    float dist = d >= 1.0 ? 2000.0 : length(ray);
    vec3 V = normalize(ray);
    float wdist = dist;
    if (V.y > 0.0001)
        wdist = min(dist, (uWaterLevel - uCamPos.y) / V.y);

    vec3 L = normalize(uLightDir);
    vec3 lr = refractedLightDir(L);
    const int steps = 40;
    float maxDist = min(wdist, 145.0);
    // Interleaved gradient noise: a fixed, even dither that hides banding.
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec3 shafts = vec3(0.0);
    vec3 lamp = vec3(0.0);
    for (int i = 0; i < steps; ++i) {
        float u = (float(i) + jitter) / float(steps);
        float t = maxDist * mix(u, u * u, 0.55);
        float dt = maxDist * mix(1.0, 2.0 * u, 0.55) / float(steps);
        vec3 p = uCamPos + V * t;
        float depth = uWaterLevel - p.y;
        if (depth < 0.0)
            break;
        vec2 s = p.xz + lr.xz / lr.y * depth;
        float beam = shaftFromMap(s, depth);
        float surfaceFade = smoothstep(0.0, 1.0, depth);
        float farFade = exp(-t * 0.011) * (1.0 - smoothstep(95.0, 145.0, t));
        shafts += sunAt(p) * beam * surfaceFade * farFade * sunThroughWater(depth, L) *
                  exp(-kFogExtinction * t) * dt;
        if (uHeadOn != 0) {
            // The headlight cone lighting up the water it passes through.
            vec3 toP = p - uHeadPos;
            float hd = length(toP);
            float cone = smoothstep(0.90, 0.985, dot(toP / max(hd, 0.001), uHeadDir));
            lamp += cone * (1.0 - smoothstep(50.0, 95.0, hd)) / (1.0 + hd * hd * 0.004) *
                    exp(-kFogExtinction * t) * dt;
        }
    }
    // Forward-peaked Henyey-Greenstein scattering, strongest towards the sun and surface.
    float cosTheta = dot(V, lr);
    float g = 0.58;
    float hg = (1.0 - g * g) / (4.0 * pow(max(1.0 + g * g - 2.0 * g * cosTheta, 0.05), 1.5));
    float upBias = 0.68 + 0.48 * smoothstep(-0.25, 0.55, V.y);
    float phase = (0.16 + 0.84 * hg) * upBias;
    shafts *= uLightColor * phase * 0.021;
    shafts += lamp * vec3(0.55, 0.52, 0.42) * 0.011;
    gl_FragColor = vec4(shafts, 1.0);
}
)GLSL";

// Under water, part 2: distance fog that swallows red first, plus the shafts.
const char *kUnderwaterFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform sampler2D uShafts;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uShallowColor; // the water looking up, and looking down
uniform vec3 uDeepColor;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uWaterLevel;

void main()
{
    vec3 col = texture2D(uScene, vUv).rgb;
    float d = texture2D(uDepth, vUv).r;
    vec4 hit = uInvViewProj * vec4(vUv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 ray = hit.xyz / hit.w - uCamPos;
    float dist = d >= 1.0 ? 2000.0 : length(ray);
    vec3 V = normalize(ray);

    // Only the stretch of the ray that is in the water fogs.
    float wdist = dist;
    if (V.y > 0.0001)
        wdist = min(dist, (uWaterLevel - uCamPos.y) / V.y);
    vec3 T = exp(-kFogExtinction * wdist);
    vec3 water = openWater(V, normalize(uLightDir), uLightColor, uShallowColor, uDeepColor);
    gl_FragColor = vec4(col * T + water * (1.0 - T) + texture2D(uShafts, vUv).rgb, 1.0);
}
)GLSL";

// Drifting specks of sea snow around the camera (mode 0) and bubbles rising
// from fixed sources to the surface (mode 1), drawn as point sprites.
const char *kParticleVertex = R"GLSL(
uniform vec3 uCamPos;
uniform float uTime;
uniform float uBox;
uniform float uPointScale;
uniform float uWaterLevel;
uniform int uMode;
varying float vFade;
void main()
{
    vec3 p;
    float size;
    if (uMode == 0) {
        // gl_Vertex holds a random seed; the specks wrap around the camera.
        vec3 seed = gl_Vertex.xyz;
        vec3 drift = vec3(sin(uTime * 0.11 + seed.y * 6.3) * 1.2,
                          -uTime * (0.15 + 0.25 * seed.z),
                          cos(uTime * 0.13 + seed.x * 6.3) * 1.2);
        p = uCamPos + mod(seed * uBox + drift - uCamPos + 0.5 * uBox, uBox) - 0.5 * uBox;
        size = 0.05 + 0.07 * seed.x;
        vFade = 1.0 - smoothstep(0.3 * uBox, 0.5 * uBox, length(p - uCamPos));
    } else if (uMode == 2) {
        // Rain: gl_Vertex.xyz is a random seed, w picks the end of the streak.
        vec3 fall = vec3(0.22, -1.0, 0.08);
        vec3 seed = gl_Vertex.xyz;
        p = uCamPos + mod(seed * uBox + fall * (uTime * (34.0 + 10.0 * seed.x)) - uCamPos + 0.5 * uBox, uBox) - 0.5 * uBox;
        p -= fall * (gl_Vertex.w * 1.5);
        size = 0.0;
        vFade = (1.0 - smoothstep(0.3 * uBox, 0.5 * uBox, length(p - uCamPos))) * step(uWaterLevel, p.y);
    } else {
        // gl_Vertex.xyz is the source, w a random seed.
        float seed = gl_Vertex.w;
        float height = max(uWaterLevel - gl_Vertex.y, 1.0);
        float rise = mod(uTime * (2.5 + 2.0 * fract(seed * 7.3)) + seed * height, height);
        p = gl_Vertex.xyz + vec3(sin(uTime * 3.0 + seed * 40.0) * 0.3 + (fract(seed * 13.7) - 0.5) * 1.5,
                                 rise,
                                 cos(uTime * 2.6 + seed * 31.0) * 0.3 + (fract(seed * 5.1) - 0.5) * 1.5);
        size = 0.12 + 0.16 * fract(seed * 3.7) + rise * 0.004;
        vFade = 1.0 - smoothstep(height - 1.5, height, rise);
    }
    if (uMode != 2 && p.y > uWaterLevel)
        vFade = 0.0;
    vec4 eye = gl_ModelViewMatrix * vec4(p, 1.0);
    gl_Position = gl_ProjectionMatrix * eye;
    gl_PointSize = clamp(uPointScale * size / max(-eye.z, 0.1), 1.0, 24.0);
}
)GLSL";

const char *kParticleFragment = R"GLSL(
uniform int uMode;
uniform vec3 uColor;
varying float vFade;
void main()
{
    if (uMode == 2) {
        gl_FragColor = vec4(uColor * vFade, 1.0);
        return;
    }
    vec2 c = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(c, c);
    if (r2 > 1.0)
        discard;
    float a = 1.0 - r2;
    if (uMode != 0) {
        // A thin bright rim and a highlight: a bubble.
        vec2 h = c + vec2(0.35, -0.35);
        a = smoothstep(0.45, 0.9, r2) * 0.9 + 0.8 * smoothstep(0.1, 0.0, dot(h, h));
    }
    gl_FragColor = vec4(uColor * a * vFade, 1.0);
}
)GLSL";

const char *kSsaoFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform vec2 uTexel;
uniform float uRadius;

vec3 unproject(vec2 uv, float d)
{
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
vec3 viewPos(vec2 uv)
{
    return unproject(uv, texture2D(uDepth, uv).r);
}

// Comparisons use window depth (smaller is closer), so this works for the
// perspective cameras and for the top view's flipped orthographic camera.
void main()
{
    float d = texture2D(uDepth, vUv).r;
    if (d >= 1.0) {
        gl_FragColor = vec4(1.0);
        return;
    }
    vec3 P = viewPos(vUv);
    vec3 toCamera = normalize(unproject(vUv, d - 0.001) - P);

    // Normal from the neighbour on whichever side is closer (keeps edges crisp).
    vec3 px = viewPos(vUv + vec2(uTexel.x, 0.0)) - P;
    vec3 nx = P - viewPos(vUv - vec2(uTexel.x, 0.0));
    vec3 py = viewPos(vUv + vec2(0.0, uTexel.y)) - P;
    vec3 ny = P - viewPos(vUv - vec2(0.0, uTexel.y));
    vec3 dx = abs(px.z) < abs(nx.z) ? px : nx;
    vec3 dy = abs(py.z) < abs(ny.z) ? py : ny;
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, toCamera) < 0.0)
        N = -N;

    // Probe 16 points in the hemisphere above the surface and count how many
    // end up behind other geometry.
    float rnd = hash2(gl_FragCoord.xy) * 6.2831853;
    float occlusion = 0.0;
    for (int i = 0; i < 16; ++i) {
        float fi = float(i);
        float z = 1.0 - (fi + 0.5) / 8.0;
        float r = sqrt(max(1.0 - z * z, 0.0));
        float a = fi * 2.39996 + rnd;
        vec3 dir = vec3(r * cos(a), r * sin(a), z);
        if (dot(dir, N) < 0.0)
            dir = -dir;
        float scale = mix(0.15, 1.0, (fi + 1.0) / 16.0);
        vec3 S = P + dir * (uRadius * scale * scale) + N * 0.02;
        vec4 c = uProj * vec4(S, 1.0);
        vec3 ndc = c.xyz / c.w;
        vec2 suv = ndc.xy * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;
        float sampleDepth = ndc.z * 0.5 + 0.5;
        float sceneDepth = texture2D(uDepth, suv).r;
        float range = smoothstep(0.0, 1.0, uRadius / max(length(unproject(suv, sceneDepth) - P), 0.0001));
        occlusion += (sceneDepth < sampleDepth - 0.00002 ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occlusion / 16.0;
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
)GLSL";

// Blurs the half size AO and outputs a multiplier (blended with DST * SRC).
const char *kAoApplyFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uAO;
uniform vec2 uTexel;
uniform float uStrength;
void main()
{
    float s = 0.0;
    for (int x = -1; x <= 2; ++x)
        for (int y = -1; y <= 2; ++y)
            s += texture2D(uAO, vUv + (vec2(float(x), float(y)) - 0.5) * uTexel).r;
    float ao = mix(1.0, s / 16.0, uStrength);
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
)GLSL";

const char *kBrightFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uScene;
uniform vec2 uTexel;
void main()
{
    vec3 c = texture2D(uScene, vUv + vec2(-0.5, -0.5) * uTexel).rgb +
             texture2D(uScene, vUv + vec2(0.5, -0.5) * uTexel).rgb +
             texture2D(uScene, vUv + vec2(-0.5, 0.5) * uTexel).rgb +
             texture2D(uScene, vUv + vec2(0.5, 0.5) * uTexel).rgb;
    c = min(c * 0.25, vec3(40.0));
    float l = max(c.r, max(c.g, c.b));
    gl_FragColor = vec4(c * (max(l - 0.9, 0.0) / max(l, 0.0001)), 1.0);
}
)GLSL";

const char *kBlurFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uSrc;
uniform vec2 uStep;
void main()
{
    vec3 c = texture2D(uSrc, vUv).rgb * 0.227027;
    c += (texture2D(uSrc, vUv + uStep).rgb + texture2D(uSrc, vUv - uStep).rgb) * 0.1945946;
    c += (texture2D(uSrc, vUv + uStep * 2.0).rgb + texture2D(uSrc, vUv - uStep * 2.0).rgb) * 0.1216216;
    c += (texture2D(uSrc, vUv + uStep * 3.0).rgb + texture2D(uSrc, vUv - uStep * 3.0).rgb) * 0.054054;
    c += (texture2D(uSrc, vUv + uStep * 4.0).rgb + texture2D(uSrc, vUv - uStep * 4.0).rgb) * 0.016216;
    gl_FragColor = vec4(c, 1.0);
}
)GLSL";

// Depth-aware separable blur for underwater light shafts: smooths ray-march
// dither without haloing across foreground arches, corals or the train.
const char *kShaftBlurFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uSrc;
uniform sampler2D uDepth;
uniform mat4 uInvProj;
uniform vec2 uStep;

float linearDepth(vec2 uv)
{
    float d = texture2D(uDepth, uv).r;
    if (d >= 1.0)
        return 2000.0;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return -p.z / p.w;
}

void main()
{
    float z0 = linearDepth(vUv);
    float sigmaZ = max(z0 * 0.08, 2.0);
    vec3 sum = texture2D(uSrc, vUv).rgb * 0.227027;
    float wSum = 0.227027;
    for (int i = 1; i <= 4; ++i) {
        float fi = float(i);
        float gw = i == 1 ? 0.1945946 : (i == 2 ? 0.1216216 : (i == 3 ? 0.054054 : 0.016216));
        vec2 off = uStep * fi;
        vec2 uvP = vUv + off;
        vec2 uvN = vUv - off;
        float wP = gw * exp(-abs(linearDepth(uvP) - z0) / sigmaZ);
        float wN = gw * exp(-abs(linearDepth(uvN) - z0) / sigmaZ);
        sum += texture2D(uSrc, uvP).rgb * wP + texture2D(uSrc, uvN).rgb * wN;
        wSum += wP + wN;
    }
    gl_FragColor = vec4(sum / max(wSum, 0.0001), 1.0);
}
)GLSL";

// Marches from each pixel towards the sun and collects the open sky it
// passes: anything in between (trees, the train) leaves a dark streak.
const char *kRaysFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uDepth;
uniform sampler2D uScene;
uniform vec2 uSunUV;
uniform float uAspect;
void main()
{
    const int steps = 48;
    vec2 delta = (uSunUV - vUv) / float(steps);
    vec2 uv = vUv;
    float light = 0.0;
    float decay = 1.0;
    for (int i = 0; i < steps; ++i) {
        uv += delta;
        if (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) {
            float sky = texture2D(uDepth, uv).r >= 0.99999 ? 1.0 : 0.0;
            vec3 s = texture2D(uScene, uv).rgb;
            float lum = min(dot(s, vec3(0.2126, 0.7152, 0.0722)), 4.0);
            vec2 d = (uv - uSunUV) * vec2(uAspect, 1.0);
            light += sky * lum * exp(-dot(d, d) * 10.0) * decay;
        }
        decay *= 0.965;
    }
    gl_FragColor = vec4(vec3(light / float(steps)), 1.0);
}
)GLSL";

const char *kCompositeFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uRays;
uniform int uBloomOn;
uniform vec3 uRayColor;
uniform float uExposure;
uniform float uVibrance;   // > 1 deepens the colours (under water)

// Narkowicz's fit of the ACES filmic curve.
vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec3 c = texture2D(uScene, vUv).rgb;
    if (uBloomOn != 0) {
        c += texture2D(uBloom, vUv).rgb * 0.25;
        c += texture2D(uRays, vUv).r * uRayColor;
    }
    c = aces(c * uExposure);
    c = pow(c, vec3(1.0 / 2.2));
    c = clamp(mix(vec3(dot(c, vec3(0.2126, 0.7152, 0.0722))), c, uVibrance), 0.0, 1.0);
    vec2 q = vUv - 0.5;
    c *= 1.0 - dot(q, q) * 0.35;
    gl_FragColor = vec4(c, 1.0);
}
)GLSL";

GLuint compile(GLenum type, const char *const *parts, int count, const char *name)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, count, parts, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        char log[2048];
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        fprintf(stderr, "PostEffects: %s shader failed:\n%s\n", name, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Builds a program from a vertex and a fragment shader, each given as a list
// of source strings ("#version 120" goes in front of both).
GLuint makeProgramParts(const char *const *vertex, int vertexCount, const char *const *fragment,
                        int fragmentCount, const char *name)
{
    const char *parts[12] = {"#version 120\n"};
    for (int i = 0; i < vertexCount && i < 11; ++i)
        parts[i + 1] = vertex[i];
    GLuint vs = compile(GL_VERTEX_SHADER, parts, vertexCount + 1, name);
    for (int i = 0; i < fragmentCount && i < 11; ++i)
        parts[i + 1] = fragment[i];
    GLuint fs = compile(GL_FRAGMENT_SHADER, parts, fragmentCount + 1, name);
    if (!vs || !fs)
    {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        char log[2048];
        glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        fprintf(stderr, "PostEffects: %s link failed:\n%s\n", name, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// A full screen pass: the quad vertex shader and one fragment shader, with
// optional helper sources in front of it.
GLuint makeProgram(const char *fragment, const char *name, const char *helperA = nullptr,
                   const char *helperB = nullptr)
{
    const char *vertex[1] = {kQuadVertex};
    const char *parts[3];
    int count = 0;
    if (helperA)
        parts[count++] = helperA;
    if (helperB)
        parts[count++] = helperB;
    parts[count++] = fragment;
    return makeProgramParts(vertex, 1, parts, count, name);
}

GLint loc(GLuint program, const char *name)
{
    return glGetUniformLocation(program, name);
}

void bindTexture(int unit, GLuint texture)
{
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, texture);
}

void unbindTextures(int count)
{
    for (int i = count - 1; i >= 0; --i)
    {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + i));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

float smooth01(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// The fine part of the water surface: this many cells of this size.
const int kWaterGridCells = 160;
const float kWaterGridCell = 3.0f;

} // namespace

PostEffects::PostEffects() : waterLevel(-1.0f)
{
    invalidate();
}

void PostEffects::invalidate()
{
    m_triedInit = false;
    m_ok = false;
    m_width = 0;
    m_height = 0;
    m_outputFbo = 0;
    for (int i = 0; i < 4; ++i)
        m_outputViewport[i] = 0;
    m_scene = Target();
    m_copy = Target();
    m_reflection = Target();
    m_halfA = Target();
    m_halfB = Target();
    m_rays = Target();
    m_shaftNear = Target();
    m_shaftFar = Target();
    m_shaftMapProgram = 0;
    m_skyProgram = m_waterProgram = m_ssaoProgram = m_aoApplyProgram = 0;
    m_brightProgram = m_blurProgram = m_shaftBlurProgram = m_raysProgram = m_compositeProgram = 0;
    m_underwaterProgram = m_shaftsProgram = m_particleProgram = m_wetProgram = 0;
    m_waterList = 0;
    m_coastSdf = 0;
    for (int i = 0; i < 3; ++i)
        m_camera[i] = m_deepColor[i] = m_shallowColor[i] = 0.0f;
    identity(m_view);
    identity(m_proj);
    identity(m_invProj);
}

bool PostEffects::init()
{
    if (m_triedInit)
        return m_ok;
    m_triedInit = true;

    if (!glCreateShader || !glGenFramebuffers || !glBlitFramebuffer)
    {
        fprintf(stderr, "PostEffects: OpenGL 3.0 is not available\n");
        return false;
    }

    m_skyProgram = makeProgram(kSkyFragment, "sky", kNoise, kSkyHorizonGlsl);
    {
        const char *vertex[5] = {kNoise, kOceanWavesGlsl, kLandWaterMotionGlsl, kCoastWaveGlsl,
                                 kWaterVertex};
        const char *fragment[8] = {kNoise,         kOceanWavesGlsl,   kSsrGlsl,
                                   kCoastWaveGlsl, kSkyHorizonGlsl,   kWaterHelpers,
                                   kWaterFragment, kWaterFragmentTail};
        m_waterProgram = makeProgramParts(vertex, 5, fragment, 8, "water");
    }
    m_wetProgram = makeProgram(kWetFragment, "wet ground", kNoise, kSsrGlsl);
    m_ssaoProgram = makeProgram(kSsaoFragment, "ssao", kNoise);
    m_aoApplyProgram = makeProgram(kAoApplyFragment, "ao apply");
    m_brightProgram = makeProgram(kBrightFragment, "bright");
    m_blurProgram = makeProgram(kBlurFragment, "blur");
    m_shaftBlurProgram = makeProgram(kShaftBlurFragment, "shaft blur");
    m_raysProgram = makeProgram(kRaysFragment, "god rays");
    m_compositeProgram = makeProgram(kCompositeFragment, "composite");
    {
        const char *vertex[1] = {kQuadVertex};
        const char *fragment[4] = {kNoise, kOceanWavesGlsl, kUnderwaterOpticsGlsl, kShaftsFragment};
        m_shaftsProgram = makeProgramParts(vertex, 1, fragment, 4, "light shafts");
        const char *mapFragment[4] = {kNoise, kOceanWavesGlsl, kUnderwaterOpticsGlsl,
                                      kShaftMapFragment};
        m_shaftMapProgram = makeProgramParts(vertex, 1, mapFragment, 4, "shaft map");
    }
    m_underwaterProgram =
        makeProgram(kUnderwaterFragment, "underwater", kOceanWavesGlsl, kUnderwaterOpticsGlsl);
    {
        const char *vertex[1] = {kParticleVertex};
        const char *fragment[1] = {kParticleFragment};
        m_particleProgram = makeProgramParts(vertex, 1, fragment, 1, "particles");
    }
    buildCoastSdf();

    m_ok = m_skyProgram && m_waterProgram && m_ssaoProgram && m_aoApplyProgram && m_brightProgram &&
           m_blurProgram && m_shaftBlurProgram && m_raysProgram && m_compositeProgram &&
           m_underwaterProgram && m_shaftsProgram && m_shaftMapProgram && m_particleProgram &&
           m_wetProgram && m_coastSdf;
    return m_ok;
}

void PostEffects::createTarget(Target &t, int width, int height, bool withDepth,
                               bool depthRenderbuffer)
{
    t.width = width;
    t.height = height;
    t.depthIsRenderbuffer = depthRenderbuffer;

    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);

    if (withDepth)
    {
        if (depthRenderbuffer)
        {
            glGenRenderbuffers(1, &t.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                      t.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
        else
        {
            glGenTextures(1, &t.depth);
            glBindTexture(GL_TEXTURE_2D, t.depth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL,
                         GL_UNSIGNED_INT_24_8, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                   t.depth, 0);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "PostEffects: framebuffer incomplete (0x%x)\n", status);
}

void PostEffects::releaseTarget(Target &t)
{
    if (t.fbo)
        glDeleteFramebuffers(1, &t.fbo);
    if (t.color)
        glDeleteTextures(1, &t.color);
    if (t.depth)
    {
        if (t.depthIsRenderbuffer)
            glDeleteRenderbuffers(1, &t.depth);
        else
            glDeleteTextures(1, &t.depth);
    }
    t = Target();
}

void PostEffects::releaseTargets()
{
    releaseTarget(m_scene);
    releaseTarget(m_copy);
    releaseTarget(m_reflection);
    releaseTarget(m_halfA);
    releaseTarget(m_halfB);
    releaseTarget(m_rays);
    releaseTarget(m_shaftNear);
    releaseTarget(m_shaftFar);
}

void PostEffects::bindTarget(const Target &t)
{
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.width, t.height);
}

void PostEffects::fullscreenQuad()
{
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    glPopAttrib();
}

void PostEffects::beginFrame(int width, int height)
{
    if (width < 2)
        width = 2;
    if (height < 2)
        height = 2;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_outputFbo);
    glGetIntegerv(GL_VIEWPORT, m_outputViewport);

    if (width != m_width || height != m_height)
    {
        releaseTargets();
        m_width = width;
        m_height = height;
        createTarget(m_scene, width, height, true, false);
        createTarget(m_copy, width, height, true, false);
        createTarget(m_reflection, width / 2, height / 2, true, true);
        createTarget(m_halfA, width / 2, height / 2, false, false);
        createTarget(m_halfB, width / 2, height / 2, false, false);
        createTarget(m_rays, width / 2, height / 2, false, false);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_outputFbo));
    }
}

void PostEffects::beginReflection()
{
    bindTarget(m_reflection);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void PostEffects::endReflection()
{
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_outputFbo));
}

void PostEffects::beginScene()
{
    bindTarget(m_scene);
    glDepthMask(GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glGetFloatv(GL_MODELVIEW_MATRIX, m_view);
    glGetFloatv(GL_PROJECTION_MATRIX, m_proj);
    if (!invert(m_proj, m_invProj))
        identity(m_invProj);
}

void PostEffects::drawSky(const SkyState &sky, float time)
{
    float view[16];
    float proj[16];
    float viewProj[16];
    float invViewProj[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    multiply(proj, view, viewProj);
    if (!invert(viewProj, invViewProj))
        identity(invViewProj);

    float sunColor[3] = {0.0f, 0.0f, 0.0f};
    if (sky.sunDir[1] > 0.0f)
        for (int i = 0; i < 3; ++i)
            sunColor[i] = sky.lightColor[i];

    glUseProgram(m_skyProgram);
    glUniformMatrix4fv(loc(m_skyProgram, "uInvViewProj"), 1, GL_FALSE, invViewProj);
    glUniform3fv(loc(m_skyProgram, "uSunDir"), 1, sky.sunDir);
    glUniform3fv(loc(m_skyProgram, "uMoonDir"), 1, sky.moonDir);
    glUniform3fv(loc(m_skyProgram, "uSunColor"), 1, sunColor);
    glUniform3fv(loc(m_skyProgram, "uZenith"), 1, sky.zenith);
    glUniform3fv(loc(m_skyProgram, "uHorizon"), 1, sky.horizon);
    glUniform1f(loc(m_skyProgram, "uDay"), sky.day);
    glUniform1f(loc(m_skyProgram, "uDusk"), sky.dusk);
    glUniform1f(loc(m_skyProgram, "uTime"), time);
    glUniform1f(loc(m_skyProgram, "uOvercast"), sky.overcast);
    fullscreenQuad();
    glUseProgram(0);
}

void PostEffects::applySSAO()
{
    bindTarget(m_halfA);
    glUseProgram(m_ssaoProgram);
    bindTexture(0, m_scene.depth);
    glUniform1i(loc(m_ssaoProgram, "uDepth"), 0);
    glUniformMatrix4fv(loc(m_ssaoProgram, "uProj"), 1, GL_FALSE, m_proj);
    glUniformMatrix4fv(loc(m_ssaoProgram, "uInvProj"), 1, GL_FALSE, m_invProj);
    glUniform2f(loc(m_ssaoProgram, "uTexel"), 2.0f / static_cast<float>(m_width),
                2.0f / static_cast<float>(m_height));
    glUniform1f(loc(m_ssaoProgram, "uRadius"), 2.5f);
    fullscreenQuad();

    // Multiply the blurred occlusion into the scene colour.
    bindTarget(m_scene);
    glUseProgram(m_aoApplyProgram);
    bindTexture(0, m_halfA.color);
    glUniform1i(loc(m_aoApplyProgram, "uAO"), 0);
    glUniform2f(loc(m_aoApplyProgram, "uTexel"), 1.0f / static_cast<float>(m_halfA.width),
                1.0f / static_cast<float>(m_halfA.height));
    glUniform1f(loc(m_aoApplyProgram, "uStrength"), 0.85f);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    fullscreenQuad();
    glPopAttrib();
    unbindTextures(1);
    glUseProgram(0);
}

void PostEffects::applyWetGround(const AdvancedLighting &lighting, float wetness, float rain,
                                 float time)
{
    // The reflections read the scene, so they need a copy of it.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_copy.fbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                      GL_NEAREST);
    bindTarget(m_scene);

    float invView[16];
    if (!invert(m_view, invView))
        identity(invView);
    const SkyState &sky = lighting.sky();

    const GLuint p = m_wetProgram;
    glUseProgram(p);
    bindTexture(0, m_copy.color);
    bindTexture(1, m_copy.depth);
    glUniform1i(loc(p, "uRefraction"), 0);
    glUniform1i(loc(p, "uDepth"), 1);
    glUniformMatrix4fv(loc(p, "uProj"), 1, GL_FALSE, m_proj);
    glUniformMatrix4fv(loc(p, "uInvProj"), 1, GL_FALSE, m_invProj);
    glUniformMatrix4fv(loc(p, "uInvView"), 1, GL_FALSE, invView);
    glUniform2f(loc(p, "uTexel"), 1.0f / static_cast<float>(m_width),
                1.0f / static_cast<float>(m_height));
    glUniform1f(loc(p, "uWetness"), wetness);
    glUniform1f(loc(p, "uRain"), rain);
    glUniform1f(loc(p, "uTime"), time);
    glUniform1f(loc(p, "uWaterLevel"), waterLevel);
    glUniform3fv(loc(p, "uZenith"), 1, sky.zenith);
    glUniform3fv(loc(p, "uHorizon"), 1, sky.horizon);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    fullscreenQuad();
    glPopAttrib();
    unbindTextures(2);
    glUseProgram(0);
}

// The water surface in a display list: a fine grid (the waves move its
// vertices) with four big flat quads round it out to the horizon.
void PostEffects::buildWaterMesh()
{
    const float reach = kWaterGridCell * static_cast<float>(kWaterGridCells) * 0.5f;
    const float horizon = 3500.0f;
    const float y = waterLevel;
    m_waterList = glGenLists(1);
    glNewList(m_waterList, GL_COMPILE);
    glBegin(GL_QUADS);
    for (int i = 0; i < kWaterGridCells; ++i)
        for (int k = 0; k < kWaterGridCells; ++k)
        {
            const float x0 = -reach + kWaterGridCell * static_cast<float>(i);
            const float z0 = -reach + kWaterGridCell * static_cast<float>(k);
            glVertex3f(x0, y, z0);
            glVertex3f(x0, y, z0 + kWaterGridCell);
            glVertex3f(x0 + kWaterGridCell, y, z0 + kWaterGridCell);
            glVertex3f(x0 + kWaterGridCell, y, z0);
        }
    const float skirts[4][4] = {{-horizon, -horizon, horizon, -reach},
                                {-horizon, reach, horizon, horizon},
                                {-horizon, -reach, -reach, reach},
                                {reach, -reach, horizon, reach}};
    for (const float *r : skirts)
    {
        glVertex3f(r[0], y, r[1]);
        glVertex3f(r[0], y, r[3]);
        glVertex3f(r[2], y, r[3]);
        glVertex3f(r[2], y, r[1]);
    }
    glEnd();
    glEndList();
}

void PostEffects::buildCoastSdf()
{
    const int size = 256;
    const float span = 1280.0f;
    std::vector<float> data(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u, 0.0f);
    for (int z = 0; z < size; ++z)
    {
        const float wz = ((static_cast<float>(z) + 0.5f) / static_cast<float>(size) - 0.5f) * span;
        for (int x = 0; x < size; ++x)
        {
            const float wx =
                ((static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 0.5f) * span;
            const size_t idx =
                (static_cast<size_t>(z) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4u;
            data[idx] = -WorldShape::inland(wx, wz);
            data[idx + 3u] = 1.0f;
        }
    }
    glGenTextures(1, &m_coastSdf);
    glBindTexture(GL_TEXTURE_2D, m_coastSdf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PostEffects::copyScene()
{
    // The water needs to see what is under it: copy colour and depth first,
    // because a texture cannot be read while it is being drawn into.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_copy.fbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                      GL_NEAREST);
    bindTarget(m_scene);
}

void PostEffects::useWaterProgram(const AdvancedLighting &lighting, float time, bool orthographic,
                                  bool underwater, float rain, int island)
{
    float invView[16];
    if (!invert(m_view, invView))
        identity(invView);
    const SkyState &sky = lighting.sky();
    const GLuint p = m_waterProgram;
    glUseProgram(p);
    bindTexture(0, m_reflection.color);
    bindTexture(1, m_copy.color);
    bindTexture(2, m_copy.depth);
    bindTexture(3, lighting.shadowTexture());
    bindTexture(4, m_coastSdf);
    glUniform1i(loc(p, "uReflection"), 0);
    glUniform1i(loc(p, "uRefraction"), 1);
    glUniform1i(loc(p, "uDepth"), 2);
    glUniform1i(loc(p, "uShadowMap"), 3);
    glUniform1i(loc(p, "uCoastSdf"), 4);
    glUniformMatrix4fv(loc(p, "uLightMatrix"), 1, GL_FALSE, lighting.lightMatrix());
    glUniform1i(loc(p, "uShadowsOn"), lighting.shadowMapValid() ? 1 : 0);
    glUniform1f(loc(p, "uShadowTexel"), 1.0f / 2048.0f);
    glUniformMatrix4fv(loc(p, "uProj"), 1, GL_FALSE, m_proj);
    glUniformMatrix4fv(loc(p, "uInvProj"), 1, GL_FALSE, m_invProj);
    glUniformMatrix4fv(loc(p, "uView"), 1, GL_FALSE, m_view);
    glUniform2f(loc(p, "uScreen"), static_cast<float>(m_width), static_cast<float>(m_height));
    glUniform3f(loc(p, "uCamPos"), invView[12], invView[13], invView[14]);
    glUniform1f(loc(p, "uTime"), time);
    glUniform3fv(loc(p, "uLightDir"), 1, sky.lightDir);
    glUniform3fv(loc(p, "uLightColor"), 1, sky.lightColor);
    glUniform3fv(loc(p, "uZenith"), 1, sky.zenith);
    glUniform3fv(loc(p, "uHorizon"), 1, sky.horizon);
    {
        float sunColor[3] = {0.0f, 0.0f, 0.0f};
        if (sky.sunDir[1] > 0.0f)
            for (int i = 0; i < 3; ++i)
                sunColor[i] = sky.lightColor[i];
        glUniform3fv(loc(p, "uSunDir"), 1, sky.sunDir);
        glUniform3fv(loc(p, "uSunColor"), 1, sunColor);
        glUniform1f(loc(p, "uDusk"), sky.dusk);
    }
    glUniform1f(loc(p, "uFogDensity"), orthographic ? 0.0f : 0.0009f + 0.0016f * rain);
    glUniform1i(loc(p, "uUnder"), underwater ? 1 : 0);
    glUniform3fv(loc(p, "uDeepColor"), 1, m_deepColor);
    glUniform1i(loc(p, "uSsrOn"), orthographic ? 0 : 1);
    glUniform1f(loc(p, "uRain"), rain);
    glUniform1i(loc(p, "uIsland"), island);
}

void PostEffects::drawWater(const AdvancedLighting &lighting, float time, bool orthographic,
                            bool underwater, float rain)
{
    PerfScope part(PerfStage::Water);
    copyScene();

    float invView[16];
    if (!invert(m_view, invView))
        identity(invView);

    // Put the fine grid where the camera is looking: under it when it looks
    // down, a little ahead when it looks along the surface. Snapping to the
    // cell size keeps the vertices from swimming as the camera moves.
    const float reach = kWaterGridCell * static_cast<float>(kWaterGridCells) * 0.5f;
    float cx = invView[12];
    float cz = invView[14];
    if (!orthographic)
    {
        const float fx = -invView[8], fy = -invView[9], fz = -invView[10];
        const float flat = sqrtf(fx * fx + fz * fz);
        float ahead = reach * 0.5f;
        if (fy < -0.01f && invView[13] > waterLevel)
            ahead = fminf(ahead * 2.4f, (invView[13] - waterLevel) / -fy * flat);
        if (flat > 0.0001f)
        {
            cx += fx / flat * ahead;
            cz += fz / flat * ahead;
        }
    }
    cx = floorf(cx / kWaterGridCell) * kWaterGridCell;
    cz = floorf(cz / kWaterGridCell) * kWaterGridCell;

    useWaterProgram(lighting, time, orthographic, underwater, rain, 0);
    const GLuint p = m_waterProgram;
    glUniform2f(loc(p, "uCenter"), cx, cz);
    glUniform1f(loc(p, "uWaveReach"), reach);

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    if (!m_waterList)
        buildWaterMesh();
    glCallList(m_waterList);
    glPopAttrib();

    unbindTextures(5);
    glUseProgram(0);
}

void PostEffects::drawSurfaceWater(const AdvancedLighting &lighting, float time, bool orthographic,
                                   float rain, SceneCallbacks &scene)
{
    PerfScope part(PerfStage::Water);
    // after the sea, so the water on land sees the sea through it too
    copyScene();
    useWaterProgram(lighting, time, orthographic, false, rain, 1);

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    // the water's display lists bind their textures: on a unit the
    // program does not read
    glActiveTexture(GL_TEXTURE7);
    scene.drawSurfaceWater();
    glBindTexture(GL_TEXTURE_2D, 0);
    glPopAttrib();

    unbindTextures(5);
    glUseProgram(0);
}

void PostEffects::setUnderwaterCamera(const float cameraPos[3], const SkyState &sky)
{
    // The water around the camera: turquoise towards the surface and deep
    // blue below by day, a little darker the deeper the camera is, nearly
    // black at night.
    const float depth = fmaxf(waterLevel - cameraPos[1], 0.0f);
    const float shallow[3] = {0.016f, 0.145f, 0.295f};
    const float deep[3] = {0.003f, 0.036f, 0.135f};
    const float absorb[3] = {0.020f, 0.0070f, 0.0045f};
    const float light = (0.06f + 0.94f * sky.day) * (1.0f - 0.35f * sky.overcast);
    for (int i = 0; i < 3; ++i)
    {
        const float fade = light * expf(-absorb[i] * depth * 0.5f);
        m_camera[i] = cameraPos[i];
        m_shallowColor[i] = shallow[i] * fade;
        m_deepColor[i] = deep[i] * fade;
    }
}

void PostEffects::drawRain(const SkyState &sky, float time, float rain)
{
    float invView[16];
    if (!invert(m_view, invView))
        identity(invView);
    const int streaks = static_cast<int>(1400.0f * rain);
    const float light = 0.03f + 0.10f * sky.day;

    const GLuint p = m_particleProgram;
    glUseProgram(p);
    glUniform3f(loc(p, "uCamPos"), invView[12], invView[13], invView[14]);
    glUniform1f(loc(p, "uTime"), time);
    glUniform1f(loc(p, "uBox"), 60.0f);
    glUniform1f(loc(p, "uPointScale"), 1.0f);
    glUniform1f(loc(p, "uWaterLevel"), waterLevel);
    glUniform1i(loc(p, "uMode"), 2);
    glUniform3f(loc(p, "uColor"), light, light * 1.05f, light * 1.15f);

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < streaks; ++i)
    {
        float seed[3];
        for (int k = 0; k < 3; ++k)
        {
            unsigned int h = static_cast<unsigned int>(i * 3 + k + 7919) * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            seed[k] = static_cast<float>(h % 10000u) / 10000.0f;
        }
        glVertex4f(seed[0], seed[1], seed[2], 0.0f);
        glVertex4f(seed[0], seed[1], seed[2], 1.0f);
    }
    glEnd();
    glPopAttrib();
    glUseProgram(0);
}

void PostEffects::drawParticles(const SkyState &sky, float time)
{
    if (m_specks.empty())
    {
        for (int i = 0; i < 1400; ++i)
            for (int k = 0; k < 3; ++k)
            {
                unsigned int h = static_cast<unsigned int>(i * 3 + k) * 2654435761u;
                h ^= h >> 15;
                h *= 2246822519u;
                h ^= h >> 13;
                m_specks.push_back(static_cast<float>(h % 10000u) / 10000.0f);
            }
    }

    const GLuint p = m_particleProgram;
    glUseProgram(p);
    glUniform3fv(loc(p, "uCamPos"), 1, m_camera);
    glUniform1f(loc(p, "uTime"), time);
    glUniform1f(loc(p, "uBox"), 36.0f);
    glUniform1f(loc(p, "uPointScale"), m_proj[5] * 0.5f * static_cast<float>(m_height));
    glUniform1f(loc(p, "uWaterLevel"), waterLevel);

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);

    const float light = 0.1f + 0.9f * sky.day;
    glUniform1i(loc(p, "uMode"), 0);
    glUniform3f(loc(p, "uColor"), 0.06f * light, 0.11f * light, 0.12f * light);
    glBegin(GL_POINTS);
    for (size_t i = 0; i + 2 < m_specks.size(); i += 3)
        glVertex3f(m_specks[i], m_specks[i + 1], m_specks[i + 2]);
    glEnd();

    glUniform1i(loc(p, "uMode"), 1);
    glUniform3f(loc(p, "uColor"), 0.25f * light + 0.03f, 0.42f * light + 0.05f,
                0.5f * light + 0.06f);
    glBegin(GL_POINTS);
    for (size_t s = 0; s + 2 < bubbleSources.size(); s += 3)
        for (int i = 0; i < 16; ++i)
        {
            const float seed = static_cast<float>((s / 3) * 16 + static_cast<size_t>(i)) * 0.137f;
            glVertex4f(bubbleSources[s], bubbleSources[s + 1], bubbleSources[s + 2],
                       seed - floorf(seed));
        }
    glEnd();

    glPopAttrib();
    glUseProgram(0);
}

void PostEffects::applyUnderwater(const AdvancedLighting &lighting, float time)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scene.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_copy.fbo);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                      GL_NEAREST);

    float viewProj[16];
    float invViewProj[16];
    multiply(m_proj, m_view, viewProj);
    if (!invert(viewProj, invViewProj))
        identity(invViewProj);
    const SkyState &sky = lighting.sky();

    // The shaft map: a square round the camera, shifted towards where the
    // sun rays it sees came through the surface, moved in whole texels so
    // the beams do not crawl as the camera moves.
    const int mapTexels = 512;
    const float mapSize = 400.0f;
    float mapCenter[2];
    {
        // as refractedLightDir() in UnderwaterOptics.h
        const float *l = sky.lightDir;
        const float inv = 1.0f / fmaxf(sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]), 1e-6f);
        const float hx = l[0] * inv * 0.75f;
        const float hz = l[2] * inv * 0.75f;
        const float vy = sqrtf(fmaxf(1.0f - hx * hx - hz * hz, 0.16f));
        const float depth = fmaxf(waterLevel - m_camera[1], 0.0f) + 15.0f;
        const float texel = mapSize / static_cast<float>(mapTexels);
        mapCenter[0] = floorf((m_camera[0] + hx / vy * depth) / texel) * texel;
        mapCenter[1] = floorf((m_camera[2] + hz / vy * depth) / texel) * texel;
    }
    if (m_shaftNear.fbo == 0)
    {
        createTarget(m_shaftNear, mapTexels, mapTexels, false, false);
        createTarget(m_shaftFar, mapTexels, mapTexels, false, false);
    }
    GLuint p = m_shaftMapProgram;
    glUseProgram(p);
    glUniform2fv(loc(p, "uMapCenter"), 1, mapCenter);
    glUniform1f(loc(p, "uMapSize"), mapSize);
    glUniform1f(loc(p, "uTime"), time);
    bindTarget(m_shaftNear);
    glUniform4f(loc(p, "uDepths"), 2.0f, 6.0f, 11.0f, 17.0f);
    fullscreenQuad();
    bindTarget(m_shaftFar);
    glUniform4f(loc(p, "uDepths"), 24.0f, 32.0f, 42.0f, 55.0f);
    fullscreenQuad();

    // Light shafts at half size (the expensive part), smoothed with a
    // depth-aware blur to hide dither without haloing foreground geometry.
    bindTarget(m_rays);
    p = m_shaftsProgram;
    glUseProgram(p);
    bindTexture(3, m_shaftNear.color);
    bindTexture(4, m_shaftFar.color);
    glUniform1i(loc(p, "uShaftNear"), 3);
    glUniform1i(loc(p, "uShaftFar"), 4);
    glUniform2fv(loc(p, "uMapCenter"), 1, mapCenter);
    glUniform1f(loc(p, "uMapSize"), mapSize);
    bindTexture(0, m_copy.depth);
    bindTexture(1, lighting.shadowTexture());
    bindTexture(2, lighting.farShadowTexture());
    glUniform1i(loc(p, "uDepth"), 0);
    glUniform1i(loc(p, "uShadowMap"), 1);
    glUniformMatrix4fv(loc(p, "uLightMatrix"), 1, GL_FALSE, lighting.lightMatrix());
    glUniform1i(loc(p, "uShadowsOn"), lighting.shadowMapValid() ? 1 : 0);
    glUniform1i(loc(p, "uFarShadowMap"), 2);
    glUniformMatrix4fv(loc(p, "uFarMatrix"), 1, GL_FALSE, lighting.farMatrix());
    glUniform1i(loc(p, "uFarOn"), (lighting.shadowMapValid() && lighting.farShadowValid()) ? 1 : 0);
    glUniformMatrix4fv(loc(p, "uInvViewProj"), 1, GL_FALSE, invViewProj);
    glUniform3fv(loc(p, "uCamPos"), 1, m_camera);
    glUniform3fv(loc(p, "uLightDir"), 1, sky.lightDir);
    glUniform3fv(loc(p, "uLightColor"), 1, sky.lightColor);
    glUniform1f(loc(p, "uWaterLevel"), waterLevel);
    glUniform1f(loc(p, "uTime"), time);
    glUniform1i(loc(p, "uHeadOn"), lighting.headlightOn() ? 1 : 0);
    glUniform3fv(loc(p, "uHeadPos"), 1, lighting.headlightPosition());
    glUniform3fv(loc(p, "uHeadDir"), 1, lighting.headlightDirection());
    fullscreenQuad();
    unbindTextures(5);
    bilateralBlur(m_rays, m_halfB, m_copy.depth, 0.95f);

    // Fog the scene and add the shafts.
    bindTarget(m_scene);
    p = m_underwaterProgram;
    glUseProgram(p);
    bindTexture(0, m_copy.color);
    bindTexture(1, m_copy.depth);
    bindTexture(2, m_rays.color);
    glUniform1i(loc(p, "uScene"), 0);
    glUniform1i(loc(p, "uDepth"), 1);
    glUniform1i(loc(p, "uShafts"), 2);
    glUniformMatrix4fv(loc(p, "uInvViewProj"), 1, GL_FALSE, invViewProj);
    glUniform3fv(loc(p, "uCamPos"), 1, m_camera);
    glUniform3fv(loc(p, "uShallowColor"), 1, m_shallowColor);
    glUniform3fv(loc(p, "uDeepColor"), 1, m_deepColor);
    glUniform3fv(loc(p, "uLightDir"), 1, sky.lightDir);
    glUniform3fv(loc(p, "uLightColor"), 1, sky.lightColor);
    glUniform1f(loc(p, "uWaterLevel"), waterLevel);
    fullscreenQuad();
    unbindTextures(3);
    glUseProgram(0);
}

void PostEffects::blur(Target &src, Target &tmp, float radius)
{
    glUseProgram(m_blurProgram);
    glUniform1i(loc(m_blurProgram, "uSrc"), 0);

    bindTarget(tmp);
    bindTexture(0, src.color);
    glUniform2f(loc(m_blurProgram, "uStep"), radius / static_cast<float>(src.width), 0.0f);
    fullscreenQuad();

    bindTarget(src);
    bindTexture(0, tmp.color);
    glUniform2f(loc(m_blurProgram, "uStep"), 0.0f, radius / static_cast<float>(src.height));
    fullscreenQuad();
}

void PostEffects::bilateralBlur(Target &src, Target &tmp, unsigned int depthTex, float radius)
{
    const GLuint p = m_shaftBlurProgram;
    glUseProgram(p);
    glUniform1i(loc(p, "uSrc"), 0);
    glUniform1i(loc(p, "uDepth"), 1);
    glUniformMatrix4fv(loc(p, "uInvProj"), 1, GL_FALSE, m_invProj);
    bindTexture(1, depthTex);

    bindTarget(tmp);
    bindTexture(0, src.color);
    glUniform2f(loc(p, "uStep"), radius / static_cast<float>(src.width), 0.0f);
    fullscreenQuad();

    bindTarget(src);
    bindTexture(0, tmp.color);
    glUniform2f(loc(p, "uStep"), 0.0f, radius / static_cast<float>(src.height));
    fullscreenQuad();
    unbindTextures(2);
}

void PostEffects::finish(const SkyState &sky, bool bloom, bool underwater)
{
    float rayColor[3] = {0.0f, 0.0f, 0.0f};

    if (bloom)
    {
        // Bloom: keep the bright part at half size and blur it twice.
        bindTarget(m_halfA);
        glUseProgram(m_brightProgram);
        bindTexture(0, m_scene.color);
        glUniform1i(loc(m_brightProgram, "uScene"), 0);
        glUniform2f(loc(m_brightProgram, "uTexel"), 1.0f / static_cast<float>(m_width),
                    1.0f / static_cast<float>(m_height));
        fullscreenQuad();
        blur(m_halfA, m_halfB, 1.0f);
        blur(m_halfA, m_halfB, 2.5f);

        // God rays: only when the sun is up and in front of the camera.
        float viewProj[16];
        multiply(m_proj, m_view, viewProj);
        const float d[4] = {sky.sunDir[0], sky.sunDir[1], sky.sunDir[2], 0.0f};
        float clip[4];
        for (int r = 0; r < 4; ++r)
            clip[r] = viewProj[r] * d[0] + viewProj[4 + r] * d[1] + viewProj[8 + r] * d[2] +
                      viewProj[12 + r] * d[3];
        if (clip[3] > 0.0f && sky.sunDir[1] > 0.0f && !underwater)
        {
            const float u = clip[0] / clip[3] * 0.5f + 0.5f;
            const float v = clip[1] / clip[3] * 0.5f + 0.5f;
            const float off = fmaxf(fabsf(u - 0.5f), fabsf(v - 0.5f));
            const float visible = 1.0f - smooth01(0.6f, 1.2f, off);
            if (visible > 0.0f)
            {
                bindTarget(m_rays);
                glUseProgram(m_raysProgram);
                bindTexture(0, m_scene.depth);
                bindTexture(1, m_scene.color);
                glUniform1i(loc(m_raysProgram, "uDepth"), 0);
                glUniform1i(loc(m_raysProgram, "uScene"), 1);
                glUniform2f(loc(m_raysProgram, "uSunUV"), u, v);
                glUniform1f(loc(m_raysProgram, "uAspect"),
                            static_cast<float>(m_width) / static_cast<float>(m_height));
                fullscreenQuad();
                const float strength = 0.7f * visible * (0.4f + 0.6f * sky.dusk);
                for (int i = 0; i < 3; ++i)
                    rayColor[i] = sky.lightColor[i] * strength;
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_outputFbo));
    glViewport(m_outputViewport[0], m_outputViewport[1], m_outputViewport[2], m_outputViewport[3]);
    const GLuint p = m_compositeProgram;
    glUseProgram(p);
    bindTexture(0, m_scene.color);
    bindTexture(1, m_halfA.color);
    bindTexture(2, m_rays.color);
    glUniform1i(loc(p, "uScene"), 0);
    glUniform1i(loc(p, "uBloom"), 1);
    glUniform1i(loc(p, "uRays"), 2);
    glUniform1i(loc(p, "uBloomOn"), bloom ? 1 : 0);
    glUniform3fv(loc(p, "uRayColor"), 1, rayColor);
    // a little brighter at night, as eyes adjust to the moonlight
    glUniform1f(loc(p, "uExposure"),
                (underwater ? 1.25f : 1.0f) * (1.0f + 0.35f * (1.0f - sky.day)));
    glUniform1f(loc(p, "uVibrance"), underwater ? 1.18f : 1.0f);
    fullscreenQuad();
    unbindTextures(3);
    glUseProgram(0);
}

void renderAdvancedFrame(AdvancedLighting &lighting, PostEffects &effects, LightingFrame frame,
                         const EffectToggles &toggles, int width, int height, SceneCallbacks &scene)
{
    // Where the camera is, to know whether it is under water.
    float view[16];
    float invView[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    if (!invert(view, invView))
        identity(invView);
    const float camera[3] = {invView[12], invView[13], invView[14]};
    const bool underwater = toggles.water && !frame.orthographic && camera[1] < effects.waterLevel;
    if (toggles.water)
        frame.waterLevel = effects.waterLevel;
    frame.cameraUnderwater = underwater;
    // The shadow map follows the camera: centred a little ahead of it, where
    // most of what it sees lies.
    frame.hasFocus = true;
    for (int k = 0; k < 3; ++k)
        frame.focus[k] = camera[k] - invView[8 + k] * kShadowReach * 0.55f;

    lighting.prepare(frame);
    if (frame.shadows)
    {
        PerfScope phase(PerfStage::Shadows);
        for (int cascade = 0; cascade < lighting.shadowCascades(); ++cascade)
        {
            lighting.beginShadowPass(cascade);
            scene.drawObjects(true);
            if (toggles.water)
                scene.drawUnderwater(true);
            lighting.endShadowPass();
        }
    }

    const bool post = (toggles.water || toggles.bloom || toggles.ssao) && effects.init();
    if (!post)
    {
        PerfScope phase(PerfStage::Scene);
        frame.hdr = false;
        frame.waterLevel = -1e9f;
        frame.cameraUnderwater = false;
        lighting.begin(frame);
        scene.drawGround();
        scene.drawObjects(false);
        lighting.end();
        glDisable(GL_STENCIL_TEST);
        return;
    }

    frame.hdr = true;
    effects.beginFrame(width, height);
    const SkyState &sky = lighting.sky();
    if (underwater)
        effects.setUnderwaterCamera(camera, sky);

    if (toggles.water && !underwater)
    {
        PerfScope phase(PerfStage::Reflection);
        // Mirror the camera in the water plane: y -> 2h - y.
        effects.beginReflection();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        float mirror[16];
        identity(mirror);
        mirror[5] = -1.0f;
        mirror[13] = 2.0f * effects.waterLevel;
        glMultMatrixf(mirror);
        effects.drawSky(sky, frame.time);
        LightingFrame reflected = frame;
        reflected.clipBelowY = effects.waterLevel;
        lighting.begin(reflected);
        scene.drawGround();
        scene.drawUnderwater(false); // the islet sticks out of the water
        scene.drawObjects(false);
        lighting.end();
        glDisable(GL_STENCIL_TEST);
        glPopMatrix();
        effects.endReflection();
    }

    {
        PerfScope phase(PerfStage::Scene);
        effects.beginScene();
        effects.drawSky(sky, frame.time);
        lighting.begin(frame);
        scene.drawGround();
        if (toggles.water)
            scene.drawUnderwater(false);
        scene.waterLater = true; // drawn below, as the sea is
        scene.drawObjects(false);
        scene.waterLater = false;
        lighting.end();
        glDisable(GL_STENCIL_TEST);
    }

    PerfScope phase(PerfStage::Post);
    if (toggles.ssao)
        effects.applySSAO();
    if (frame.wetness > 0.01f && !underwater && !frame.orthographic)
        effects.applyWetGround(lighting, frame.wetness, frame.rain, frame.time);
    if (underwater)
        effects.drawParticles(sky, frame.time);
    if (toggles.water)
        effects.drawWater(lighting, frame.time, frame.orthographic, underwater, frame.rain);
    effects.drawSurfaceWater(lighting, frame.time, frame.orthographic, frame.rain, scene);
    if (frame.rain > 0.01f && !underwater && !frame.orthographic)
        effects.drawRain(sky, frame.time, frame.rain);
    if (underwater)
        effects.applyUnderwater(lighting, frame.time);
    effects.finish(sky, toggles.bloom, underwater);
}
