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
#include "UnderwaterOptics.h"

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

    // Sun disc and the glow around it (wider and warmer at dusk).
    float cs = max(dot(ray, uSunDir), 0.0);
    col += uSunColor * (pow(cs, 400.0) * 3.0 + pow(cs, 24.0) * 0.25 + pow(cs, 4.0) * 0.15 * uDusk);
    col += uSunColor * smoothstep(0.99955, 0.9997, cs) * 40.0;

    // Moon and stars at night.
    float night = 1.0 - uDay;
    float cm = dot(ray, uMoonDir);
    col += vec3(0.9, 0.95, 1.1) * smoothstep(0.9993, 0.9995, cm) * 1.5 * night;
    col += vec3(0.25, 0.3, 0.45) * pow(max(cm, 0.0), 60.0) * 0.15 * night;
    if (y > 0.0 && night > 0.01) {
        vec3 d = ray * 260.0;
        vec3 cell = floor(d);
        float h = hash3(cell);
        float star = step(0.9975, h) * smoothstep(0.45, 0.1, length(fract(d) - 0.5));
        float twinkle = 0.6 + 0.4 * sin(uTime * 3.0 + h * 400.0);
        col += vec3(star * twinkle * 1.2 * night * smoothstep(0.0, 0.15, y));
    }

    // Clouds: fractal noise on a flat layer high above, drifting with the wind.
    if (y > 0.01) {
        vec2 p = ray.xz / y * 0.9 + vec2(uTime * 0.012, uTime * 0.004);
        float n = fbm(p * 1.3);
        float cover = smoothstep(0.48 - 0.42 * uOvercast, 0.78 - 0.36 * uOvercast, n);
        float thick = fbm(p * 1.3 + uSunDir.xz * 0.12);
        vec3 lit = uSunColor * 0.9 + uZenith * 0.8 + uHorizon * 0.3;
        vec3 cloudCol = mix(lit, lit * 0.55, smoothstep(0.4, 0.8, thick));
        cloudCol *= 1.0 - 0.35 * uOvercast;
        float alpha = cover * smoothstep(0.01, 0.18, y) * mix(0.92, 1.0, uOvercast);
        col = mix(col, cloudCol, alpha);
    }

    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

// Shared by the water's vertex and fragment shaders: the large waves.
// Gerstner waves move each point of the surface round a circle instead of
// just up and down, which sharpens the crests and flattens the troughs.
const char *kWaveGlsl = R"GLSL(
void gerstner(vec2 dir, float len, float amp, vec2 p, float t, inout vec3 disp, inout vec3 slope)
{
    float k = 6.2831853 / len;
    float w = sqrt(9.8 * k) * 0.6;
    float ph = k * dot(dir, p) - w * t;
    float q = 0.1375 / (k * amp);   // steepness: the four waves share 0.55
    disp.xz += dir * (q * amp * cos(ph));
    disp.y += amp * sin(ph);
    slope.xz += dir * (k * amp * cos(ph));
    slope.y += q * k * amp * sin(ph);
}

// Displacement and normal of the surface at rest position p.
void oceanWaves(vec2 p, float t, out vec3 disp, out vec3 normal)
{
    disp = vec3(0.0);
    vec3 s = vec3(0.0);
    gerstner(vec2(0.944, 0.330), 46.0, 0.34, p, t, disp, s);
    gerstner(vec2(0.600, -0.800), 27.0, 0.20, p, t, disp, s);
    gerstner(vec2(-0.287, 0.958), 17.0, 0.12, p, t, disp, s);
    gerstner(vec2(0.874, 0.486), 11.0, 0.07, p, t, disp, s);
    normal = normalize(vec3(-s.x, 1.0 - s.y, -s.z));
}
)GLSL";

// The surface is a grid of quads around the camera; the waves move its
// vertices. Further out the grid ends in flat skirts, so the waves fade to
// nothing before they get there.
const char *kWaterVertex = R"GLSL(
uniform vec2 uCenter;      // where the grid is, snapped to its cell size
uniform float uTime;
uniform float uWaveReach;  // half the size of the grid
varying vec3 vWorld;
varying vec2 vBase;
varying float vWave;
void main()
{
    // The modelview matrix holds only the camera here.
    vec3 base = vec3(gl_Vertex.x + uCenter.x, gl_Vertex.y, gl_Vertex.z + uCenter.y);
    float fade = 1.0 - smoothstep(0.55 * uWaveReach, 0.95 * uWaveReach, length(gl_Vertex.xz));
    vec3 disp;
    vec3 normal;
    oceanWaves(base.xz, uTime, disp, normal);
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

const char *kWaterFragment = R"GLSL(
varying vec3 vWorld;
varying vec2 vBase;
varying float vWave;
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

// Fine ripples: the slopes of three layers of noise sliding over each other
// in different directions.
vec2 rippleSlope(vec2 p, float t)
{
    vec2 g = noiseSlope(p * 0.55 + vec2(t * 0.35, t * 0.22)) * 0.060;
    g += noiseSlope(p * 1.30 + vec2(-t * 0.45, t * 0.30)) * 0.035;
    g += noiseSlope(p * 2.90 + vec2(t * 0.25, -t * 0.55)) * 0.018;
    return g;
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

void main()
{
    vec3 P = vWorld;
    vec3 toCam = uCamPos - P;
    float dist = length(toCam);
    vec3 V = toCam / dist;

    // Normal: the big waves, then the ripples (and rain rings) on top.
    vec3 disp;
    vec3 N;
    oceanWaves(vBase, uTime, disp, N);
    vec2 g = rippleSlope(vBase, uTime);
    if (uRain > 0.0)
        g += rainSlope(vBase, uTime) * uRain;
    N = normalize(vec3(N.x - g.x, N.y, N.z - g.y));
    // Calm the waves far away so they do not shimmer.
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), clamp(dist / 500.0, 0.0, 0.85)));

    vec2 suv = gl_FragCoord.xy / uScreen;
    if (uUnder != 0) {
        // Seen from below, light from above only gets through inside Snell's
        // window, a cone of about 48 degrees around straight up. Outside it
        // the surface mirrors the water below.
        float cosView = abs(dot(V, N));
        float window = smoothstep(0.62, 0.70, cosView);
        vec3 above = texture2D(uRefraction, suv + N.xz * 0.08).rgb;
        // The sun through the waves: each facet that happens to bend the
        // line of sight towards the sun flashes.
        vec3 out1 = refract(-V, -N, 1.33);
        float sunHit = max(dot(out1, normalize(uLightDir)), 0.0);
        vec3 glitter = uLightColor * (pow(sunHit, 180.0) * 14.0 + pow(sunHit, 14.0) * 0.5);
        vec3 col = mix(uDeepColor * 1.6, above * 1.15 + glitter, window);
        float rim = smoothstep(0.54, 0.65, cosView) - smoothstep(0.65, 0.74, cosView);
        col += (uDeepColor * 3.0 + uLightColor * 0.12) * rim;
        gl_FragColor = vec4(col, 1.0);
        return;
    }
    vec3 waterView = viewPos(suv, gl_FragCoord.z);
    vec2 distort = N.xz * 0.03;

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

    // Light is absorbed with depth, red first: shallow water is clear and
    // turquoise, deep water blue.
    float sunUp = clamp(uLightDir.y * 2.0, 0.0, 1.0);
    vec3 scatter = vec3(0.004, 0.050, 0.11) * (uLightColor * sunUp * mix(0.6, 1.0, shadow) + uZenith * 2.0);
    vec3 absorb = exp(-thickness * vec3(0.12, 0.030, 0.020));
    vec3 body = refr * absorb + scatter * (1.0 - absorb);

    // Reflection: what is on screen where the mirrored ray lands (SSR);
    // where that finds nothing, the mirrored view drawn beforehand.
    vec3 refl = texture2D(uReflection, suv + distort).rgb;
    if (uSsrOn != 0) {
        mat3 viewRot = mat3(uView[0].xyz, uView[1].xyz, uView[2].xyz);
        vec3 nView = viewRot * N;
        vec4 hit = traceReflection(waterView, reflect(normalize(waterView), nView), viewRot * vec3(0.0, 1.0, 0.0));
        refl = mix(refl, hit.rgb, hit.a);
    }
    // Fresnel (Schlick): a mirror at grazing angles, clear looking straight down.
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 col = mix(body, refl, fresnel);

    // Sun glints.
    vec3 H = normalize(normalize(uLightDir) + V);
    float nh = max(dot(N, H), 0.0);
    float spec = pow(nh, 600.0) * 40.0 + pow(nh, 80.0) * 0.6;
    col += uLightColor * spec * shadow * (0.3 + fresnel);

    // Foam where the water is shallow and on the steepest crests, broken up
    // by moving noise.
    float edge = 1.0 - smoothstep(0.0, 1.2, thickness);
    float pattern = noise(P.xz * 0.9 + vec2(uTime * 0.3, uTime * 0.2)) *
                    noise(P.xz * 2.3 - vec2(uTime * 0.25));
    float crest = smoothstep(0.38, 0.6, disp.y) * vWave * 0.5;
    float foam = max(edge, crest) * smoothstep(0.15, 0.45, pattern + edge * 0.35);
    vec3 foamLight = uLightColor * max(uLightDir.y, 0.0) * shadow + uZenith * 1.5 + uHorizon * 0.3;
    col = mix(col, foamLight * 0.9, foam * 0.85);

    // Fade in over the first few centimetres so the shore line is soft.
    col = mix(refr, col, smoothstep(0.0, 0.25, thickness));

    float f = dist * uFogDensity;
    col = mix(uHorizon, col, exp(-f * f));
    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

// Rain on the ground: up-facing surfaces above the water get a film of
// water, pooling into puddles, that mirrors the scene with the same screen
// space reflections as the sea. Output is blended over the scene by alpha.
const char *kWetFragment = R"GLSL(
varying vec2 vUv;
uniform mat4 uInvView;
uniform vec2 uTexel;
uniform float uWetness;
uniform float uWaterLevel;
uniform vec3 uZenith;
uniform vec3 uHorizon;

void main()
{
    float d = texture2D(uDepth, vUv).r;
    if (d >= 1.0)
        discard;
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
    if (worldN.y < 0.8 || world.y < uWaterLevel + 0.3)
        discard;

    // Puddles gather in the low patches of a noise field; between them
    // there is only a thin film.
    float pool = noise(world.xz * 0.11) + 0.35 * noise(world.xz * 0.41);
    float puddle = smoothstep(0.62, 0.80, pool);
    float strength = uWetness * mix(0.22, 1.0, puddle);

    vec3 R = reflect(-toEye, N);
    vec4 hit = traceReflection(P + N * 0.05, R, N);
    vec3 worldR = invRot * R;
    vec3 sky = mix(uHorizon, uZenith, pow(clamp(worldR.y, 0.0, 1.0), 0.5));
    float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(N, toEye), 0.0), 5.0);
    gl_FragColor = vec4(mix(sky, hit.rgb, hit.a), clamp(fresnel * strength * 1.4, 0.0, 1.0));
}
)GLSL";

// Under water, part 1 (half size): light shafts. Each view ray is marched,
// asking the shadow map whether the sun reaches each step and modulating it by
// the caustic pattern at the surface above, so the shafts stream down in
// moving bands.
const char *kShaftsFragment = R"GLSL(
varying vec2 vUv;
uniform sampler2D uDepth;
uniform sampler2DShadow uShadowMap;
uniform mat4 uLightMatrix;
uniform int uShadowsOn;
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
    if (c.z >= 1.0)
        return 1.0;
    return shadow2D(uShadowMap, vec3(c.xy, c.z - 0.002)).r;
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
    const int steps = 36;
    float dt = min(wdist, 170.0) / float(steps);
    // Interleaved gradient noise: a fixed, even dither that hides banding.
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    vec3 shafts = vec3(0.0);
    vec3 lamp = vec3(0.0);
    for (int i = 0; i < steps; ++i) {
        float t = (float(i) + jitter) * dt;
        vec3 p = uCamPos + V * t;
        float depth = uWaterLevel - p.y;
        if (depth < 0.0)
            break;
        // Fine caustic streaks inside broad beams: the pattern is looked up
        // where the sun ray through p crossed the surface.
        float c = caustics(p, L, uWaterLevel, uTime);
        vec2 s = p.xz + L.xz / max(L.y, 0.2) * depth;
        float beam = 0.25 + 1.5 * causticLayer(s * 0.034, uTime * 0.5);
        shafts += sunAt(p) * c * c * beam * sunThroughWater(depth, L) * exp(-kFogExtinction * t) * dt;
        if (uHeadOn != 0) {
            // The headlight cone lighting up the water it passes through.
            vec3 toP = p - uHeadPos;
            float hd = length(toP);
            float cone = smoothstep(0.90, 0.985, dot(toP / max(hd, 0.001), uHeadDir));
            lamp += cone * (1.0 - smoothstep(50.0, 95.0, hd)) / (1.0 + hd * hd * 0.004) * exp(-kFogExtinction * t) * dt;
        }
    }
    // Scattering is strongest looking towards the sun.
    float phase = 0.3 + 0.7 * pow(max(dot(V, L), 0.0), 3.0);
    shafts *= uLightColor * phase * 0.0042;
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
    const char *parts[8] = {"#version 120\n"};
    for (int i = 0; i < vertexCount && i < 7; ++i)
        parts[i + 1] = vertex[i];
    GLuint vs = compile(GL_VERTEX_SHADER, parts, vertexCount + 1, name);
    for (int i = 0; i < fragmentCount && i < 7; ++i)
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
    m_skyProgram = m_waterProgram = m_ssaoProgram = m_aoApplyProgram = 0;
    m_brightProgram = m_blurProgram = m_raysProgram = m_compositeProgram = 0;
    m_underwaterProgram = m_shaftsProgram = m_particleProgram = m_wetProgram = 0;
    m_waterList = 0;
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

    m_skyProgram = makeProgram(kSkyFragment, "sky", kNoise);
    {
        const char *vertex[2] = {kWaveGlsl, kWaterVertex};
        const char *fragment[4] = {kNoise, kWaveGlsl, kSsrGlsl, kWaterFragment};
        m_waterProgram = makeProgramParts(vertex, 2, fragment, 4, "water");
    }
    m_wetProgram = makeProgram(kWetFragment, "wet ground", kNoise, kSsrGlsl);
    m_ssaoProgram = makeProgram(kSsaoFragment, "ssao", kNoise);
    m_aoApplyProgram = makeProgram(kAoApplyFragment, "ao apply");
    m_brightProgram = makeProgram(kBrightFragment, "bright");
    m_blurProgram = makeProgram(kBlurFragment, "blur");
    m_raysProgram = makeProgram(kRaysFragment, "god rays");
    m_compositeProgram = makeProgram(kCompositeFragment, "composite");
    m_shaftsProgram = makeProgram(kShaftsFragment, "light shafts", kNoise, kUnderwaterOpticsGlsl);
    m_underwaterProgram = makeProgram(kUnderwaterFragment, "underwater", kUnderwaterOpticsGlsl);
    {
        const char *vertex[1] = {kParticleVertex};
        const char *fragment[1] = {kParticleFragment};
        m_particleProgram = makeProgramParts(vertex, 1, fragment, 1, "particles");
    }

    m_ok = m_skyProgram && m_waterProgram && m_ssaoProgram && m_aoApplyProgram && m_brightProgram &&
           m_blurProgram && m_raysProgram && m_compositeProgram && m_underwaterProgram &&
           m_shaftsProgram && m_particleProgram && m_wetProgram;
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

void PostEffects::applyWetGround(const AdvancedLighting &lighting, float wetness)
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
    const float far = 1500.0f;
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
    const float skirts[4][4] = {{-far, -far, far, -reach},
                                {-far, reach, far, far},
                                {-far, -reach, -reach, reach},
                                {reach, -reach, far, reach}};
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

void PostEffects::drawWater(const AdvancedLighting &lighting, float time, bool orthographic,
                            bool underwater, float rain)
{
    // The water needs to see what is under it: copy colour and depth first,
    // because a texture cannot be read while it is being drawn into.
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

    const GLuint p = m_waterProgram;
    glUseProgram(p);
    bindTexture(0, m_reflection.color);
    bindTexture(1, m_copy.color);
    bindTexture(2, m_copy.depth);
    bindTexture(3, lighting.shadowTexture());
    glUniform1i(loc(p, "uReflection"), 0);
    glUniform1i(loc(p, "uRefraction"), 1);
    glUniform1i(loc(p, "uDepth"), 2);
    glUniform1i(loc(p, "uShadowMap"), 3);
    glUniformMatrix4fv(loc(p, "uLightMatrix"), 1, GL_FALSE, lighting.lightMatrix());
    glUniform1i(loc(p, "uShadowsOn"), lighting.shadowMapValid() ? 1 : 0);
    glUniform1f(loc(p, "uShadowTexel"), 1.0f / 2048.0f);
    glUniformMatrix4fv(loc(p, "uProj"), 1, GL_FALSE, m_proj);
    glUniformMatrix4fv(loc(p, "uInvProj"), 1, GL_FALSE, m_invProj);
    glUniformMatrix4fv(loc(p, "uView"), 1, GL_FALSE, m_view);
    glUniform2f(loc(p, "uScreen"), static_cast<float>(m_width), static_cast<float>(m_height));
    glUniform3f(loc(p, "uCamPos"), invView[12], invView[13], invView[14]);
    glUniform2f(loc(p, "uCenter"), cx, cz);
    glUniform1f(loc(p, "uWaveReach"), reach);
    glUniform1f(loc(p, "uTime"), time);
    glUniform3fv(loc(p, "uLightDir"), 1, sky.lightDir);
    glUniform3fv(loc(p, "uLightColor"), 1, sky.lightColor);
    glUniform3fv(loc(p, "uZenith"), 1, sky.zenith);
    glUniform3fv(loc(p, "uHorizon"), 1, sky.horizon);
    glUniform1f(loc(p, "uFogDensity"), orthographic ? 0.0f : 0.002f + 0.002f * rain);
    glUniform1i(loc(p, "uUnder"), underwater ? 1 : 0);
    glUniform3fv(loc(p, "uDeepColor"), 1, m_deepColor);
    glUniform1i(loc(p, "uSsrOn"), orthographic ? 0 : 1);
    glUniform1f(loc(p, "uRain"), rain);

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

    unbindTextures(4);
    glUseProgram(0);
}

void PostEffects::setUnderwaterCamera(const float cameraPos[3], const SkyState &sky)
{
    // The water around the camera: turquoise towards the surface and deep
    // blue below by day, a little darker the deeper the camera is, nearly
    // black at night.
    const float depth = fmaxf(waterLevel - cameraPos[1], 0.0f);
    const float shallow[3] = {0.020f, 0.170f, 0.330f};
    const float deep[3] = {0.003f, 0.040f, 0.150f};
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

    // Light shafts at half size (the expensive part), blurred a little to
    // hide the dither.
    bindTarget(m_rays);
    GLuint p = m_shaftsProgram;
    glUseProgram(p);
    bindTexture(0, m_copy.depth);
    bindTexture(1, lighting.shadowTexture());
    glUniform1i(loc(p, "uDepth"), 0);
    glUniform1i(loc(p, "uShadowMap"), 1);
    glUniformMatrix4fv(loc(p, "uLightMatrix"), 1, GL_FALSE, lighting.lightMatrix());
    glUniform1i(loc(p, "uShadowsOn"), lighting.shadowMapValid() ? 1 : 0);
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
    unbindTextures(2);
    blur(m_rays, m_halfB, 1.0f);

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
    glUniform1f(loc(p, "uExposure"), underwater ? 1.25f : 1.0f);
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

    lighting.prepare(frame);
    if (frame.shadows)
    {
        lighting.beginShadowPass();
        scene.drawObjects(true);
        if (toggles.water)
            scene.drawUnderwater(true);
        lighting.endShadowPass();
    }

    const bool post = (toggles.water || toggles.bloom || toggles.ssao) && effects.init();
    if (!post)
    {
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

    effects.beginScene();
    effects.drawSky(sky, frame.time);
    lighting.begin(frame);
    scene.drawGround();
    if (toggles.water)
        scene.drawUnderwater(false);
    scene.drawObjects(false);
    lighting.end();
    glDisable(GL_STENCIL_TEST);

    if (toggles.ssao)
        effects.applySSAO();
    if (frame.wetness > 0.01f && !underwater && !frame.orthographic)
        effects.applyWetGround(lighting, frame.wetness);
    if (underwater)
        effects.drawParticles(sky, frame.time);
    if (toggles.water)
        effects.drawWater(lighting, frame.time, frame.orthographic, underwater, frame.rain);
    if (frame.rain > 0.01f && !underwater && !frame.orthographic)
        effects.drawRain(sky, frame.time, frame.rain);
    if (underwater)
        effects.applyUnderwater(lighting, frame.time);
    effects.finish(sky, toggles.bloom, underwater);
}
