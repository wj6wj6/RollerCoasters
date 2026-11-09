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

namespace
{

// ---------------------------------------------------------------------
// Shaders. All of them are GLSL 1.20 so they run on the same
// compatibility context as the rest of the program.
// ---------------------------------------------------------------------

// Full screen triangle pair: the quad is drawn in clip space.
const char* kQuadVertex = R"GLSL(
#version 120
varying vec2 vUv;
void main()
{
    vUv = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)GLSL";

// Shared noise helpers, pasted in front of the shaders that use them.
const char* kNoise = R"GLSL(
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

const char* kSkyFragment = R"GLSL(
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
        float cover = smoothstep(0.48, 0.78, n);
        float thick = fbm(p * 1.3 + uSunDir.xz * 0.12);
        vec3 lit = uSunColor * 0.9 + uZenith * 0.8 + uHorizon * 0.3;
        vec3 cloudCol = mix(lit, lit * 0.55, smoothstep(0.4, 0.8, thick));
        float alpha = cover * smoothstep(0.01, 0.18, y) * 0.92;
        col = mix(col, cloudCol, alpha);
    }

    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

const char* kWaterVertex = R"GLSL(
#version 120
varying vec3 vWorld;
void main()
{
    // The modelview matrix holds only the camera here, so gl_Vertex is world space.
    vWorld = gl_Vertex.xyz;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)GLSL";

const char* kWaterFragment = R"GLSL(
varying vec3 vWorld;
uniform sampler2D uReflection;
uniform sampler2D uRefraction;
uniform sampler2D uDepth;
uniform sampler2DShadow uShadowMap;
uniform mat4 uLightMatrix;
uniform int uShadowsOn;
uniform float uShadowTexel;
uniform mat4 uInvProj;
uniform vec2 uScreen;
uniform vec3 uCamPos;
uniform float uTime;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform float uFogDensity;

vec3 viewPos(vec2 uv, float depth)
{
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

// Slope of a sum of travelling sine waves: long swells plus short chop.
vec2 waveSlope(vec2 p, float t)
{
    vec2 g = vec2(0.0);
    float amp = 0.12;
    float freq = 0.12;
    float speed = 1.1;
    float ang = 0.4;
    for (int i = 0; i < 10; ++i) {
        vec2 d = vec2(cos(ang), sin(ang));
        float ph = dot(d, p) * freq + t * speed;
        g += d * (amp * freq * cos(ph));
        amp *= 0.78;
        freq *= 1.45;
        speed *= 1.13;
        ang += 2.1;
    }
    return g;
}

void main()
{
    vec3 P = vWorld;
    vec3 toCam = uCamPos - P;
    float dist = length(toCam);
    vec3 V = toCam / dist;

    vec2 g = waveSlope(P.xz, uTime);
    vec3 N = normalize(vec3(-g.x, 1.0, -g.y));
    // Calm the waves far away so they do not shimmer.
    N = normalize(mix(N, vec3(0.0, 1.0, 0.0), clamp(dist / 500.0, 0.0, 0.85)));

    vec2 suv = gl_FragCoord.xy / uScreen;
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
    // turquoise, deep water dark blue.
    float sunUp = clamp(uLightDir.y * 2.0, 0.0, 1.0);
    vec3 scatter = vec3(0.005, 0.045, 0.09) * (uLightColor * sunUp * mix(0.6, 1.0, shadow) + uZenith * 2.0);
    vec3 absorb = exp(-thickness * vec3(0.35, 0.09, 0.06));
    vec3 body = refr * absorb + scatter * (1.0 - absorb);

    vec3 refl = texture2D(uReflection, suv + distort).rgb;
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
    vec3 col = mix(body, refl, fresnel);

    // Sun glints.
    vec3 H = normalize(normalize(uLightDir) + V);
    float nh = max(dot(N, H), 0.0);
    float spec = pow(nh, 600.0) * 40.0 + pow(nh, 80.0) * 0.6;
    col += uLightColor * spec * shadow * (0.3 + fresnel);

    // Foam where the water is shallow, broken up by moving noise.
    float edge = 1.0 - smoothstep(0.0, 1.2, thickness);
    float pattern = noise(P.xz * 0.9 + vec2(uTime * 0.3, uTime * 0.2)) *
                    noise(P.xz * 2.3 - vec2(uTime * 0.25));
    float foam = edge * smoothstep(0.15, 0.45, pattern + edge * 0.35);
    vec3 foamLight = uLightColor * max(uLightDir.y, 0.0) * shadow + uZenith * 1.5 + uHorizon * 0.3;
    col = mix(col, foamLight * 0.9, foam * 0.85);

    // Fade in over the first few centimetres so the shore line is soft.
    col = mix(refr, col, smoothstep(0.0, 0.25, thickness));

    float f = dist * uFogDensity;
    col = mix(uHorizon, col, exp(-f * f));
    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

const char* kSsaoFragment = R"GLSL(
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
const char* kAoApplyFragment = R"GLSL(
#version 120
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

const char* kBrightFragment = R"GLSL(
#version 120
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

const char* kBlurFragment = R"GLSL(
#version 120
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
const char* kRaysFragment = R"GLSL(
#version 120
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

const char* kCompositeFragment = R"GLSL(
#version 120
varying vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uRays;
uniform int uBloomOn;
uniform vec3 uRayColor;
uniform float uExposure;

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
    vec2 q = vUv - 0.5;
    c *= 1.0 - dot(q, q) * 0.35;
    gl_FragColor = vec4(c, 1.0);
}
)GLSL";

GLuint compile(GLenum type, const char* const* parts, int count, const char* name)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, count, parts, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048];
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        fprintf(stderr, "PostEffects: %s shader failed:\n%s\n", name, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Builds a program. useNoise puts "#version 120" and the noise helpers in
// front of the fragment source.
GLuint makeProgram(const char* vertex, const char* fragment, bool useNoise, const char* name)
{
    const char* vparts[1] = {vertex};
    GLuint vs = compile(GL_VERTEX_SHADER, vparts, 1, name);
    GLuint fs = 0;
    if (useNoise) {
        const char* fparts[3] = {"#version 120\n", kNoise, fragment};
        fs = compile(GL_FRAGMENT_SHADER, fparts, 3, name);
    } else {
        const char* fparts[1] = {fragment};
        fs = compile(GL_FRAGMENT_SHADER, fparts, 1, name);
    }
    if (!vs || !fs) {
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
    if (linked != GL_TRUE) {
        char log[2048];
        glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        fprintf(stderr, "PostEffects: %s link failed:\n%s\n", name, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

GLint loc(GLuint program, const char* name)
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
    for (int i = count - 1; i >= 0; --i) {
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

} // namespace

PostEffects::PostEffects() : waterLevel(-1.0f), islandHalfSize(100.0f)
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
    identity(m_view);
    identity(m_proj);
    identity(m_invProj);
}

bool PostEffects::init()
{
    if (m_triedInit)
        return m_ok;
    m_triedInit = true;

    if (!glCreateShader || !glGenFramebuffers || !glBlitFramebuffer) {
        fprintf(stderr, "PostEffects: OpenGL 3.0 is not available\n");
        return false;
    }

    m_skyProgram = makeProgram(kQuadVertex, kSkyFragment, true, "sky");
    m_waterProgram = makeProgram(kWaterVertex, kWaterFragment, true, "water");
    m_ssaoProgram = makeProgram(kQuadVertex, kSsaoFragment, true, "ssao");
    m_aoApplyProgram = makeProgram(kQuadVertex, kAoApplyFragment, false, "ao apply");
    m_brightProgram = makeProgram(kQuadVertex, kBrightFragment, false, "bright");
    m_blurProgram = makeProgram(kQuadVertex, kBlurFragment, false, "blur");
    m_raysProgram = makeProgram(kQuadVertex, kRaysFragment, false, "god rays");
    m_compositeProgram = makeProgram(kQuadVertex, kCompositeFragment, false, "composite");

    m_ok = m_skyProgram && m_waterProgram && m_ssaoProgram && m_aoApplyProgram &&
           m_brightProgram && m_blurProgram && m_raysProgram && m_compositeProgram;
    return m_ok;
}

void PostEffects::createTarget(Target& t, int width, int height, bool withDepth,
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

    if (withDepth) {
        if (depthRenderbuffer) {
            glGenRenderbuffers(1, &t.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                      t.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        } else {
            glGenTextures(1, &t.depth);
            glBindTexture(GL_TEXTURE_2D, t.depth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0,
                         GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
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

void PostEffects::releaseTarget(Target& t)
{
    if (t.fbo)
        glDeleteFramebuffers(1, &t.fbo);
    if (t.color)
        glDeleteTextures(1, &t.color);
    if (t.depth) {
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

void PostEffects::bindTarget(const Target& t)
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

    if (width != m_width || height != m_height) {
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

void PostEffects::drawSky(const SkyState& sky, float time)
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
    fullscreenQuad();
    glUseProgram(0);
}

void PostEffects::drawSeabed()
{
    // Grid lines spaced tightly near the island and loosely far out. The bed
    // drops from a shallow beach to deep water.
    const float steps[] = {0.0f,   3.0f,   8.0f,   15.0f,  25.0f,  40.0f,  60.0f,
                           85.0f,  120.0f, 170.0f, 250.0f, 400.0f, 700.0f, 1200.0f};
    const int stepCount = static_cast<int>(sizeof(steps) / sizeof(steps[0]));
    float coords[2 * 14];
    int n = 0;
    for (int i = stepCount - 1; i >= 0; --i)
        coords[n++] = -islandHalfSize - steps[i];
    for (int i = 0; i < stepCount; ++i)
        coords[n++] = islandHalfSize + steps[i];

    const float half = islandHalfSize;
    const float beach = waterLevel - 1.2f;
    struct Bed
    {
        static float height(float x, float z, float half, float beach)
        {
            const float dx = fabsf(x) - half;
            const float dz = fabsf(z) - half;
            const float d = (dx > dz ? dx : dz) < 0.0f ? 0.0f : (dx > dz ? dx : dz);
            return beach - 12.0f * smooth01(0.0f, 160.0f, d);
        }
    };

    glBegin(GL_QUADS);
    for (int i = 0; i + 1 < n; ++i) {
        for (int j = 0; j + 1 < n; ++j) {
            const float x0 = coords[i], x1 = coords[i + 1];
            const float z0 = coords[j], z1 = coords[j + 1];
            const float y00 = Bed::height(x0, z0, half, beach);
            const float y10 = Bed::height(x1, z0, half, beach);
            const float y01 = Bed::height(x0, z1, half, beach);
            const float y11 = Bed::height(x1, z1, half, beach);
            // Normal of the quad from its diagonals.
            const float ax = x1 - x0, ay = y11 - y00, az = z1 - z0;
            const float bx = x1 - x0, by = y10 - y01, bz = z0 - z1;
            float nx = ay * bz - az * by;
            float ny = az * bx - ax * bz;
            float nz = ax * by - ay * bx;
            if (ny < 0.0f) {
                nx = -nx;
                ny = -ny;
                nz = -nz;
            }
            glNormal3f(nx, ny, nz);
            glColor3ub(194, 178, 128);
            glVertex3f(x0, y00, z0);
            glVertex3f(x0, y01, z1);
            glVertex3f(x1, y11, z1);
            glVertex3f(x1, y10, z0);
        }
    }

    // Earth sides of the island down to the beach.
    glColor3ub(120, 86, 56);
    glNormal3f(0.0f, 0.0f, -1.0f);
    glVertex3f(-half, beach, -half);
    glVertex3f(-half, 0.0f, -half);
    glVertex3f(half, 0.0f, -half);
    glVertex3f(half, beach, -half);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(half, beach, half);
    glVertex3f(half, 0.0f, half);
    glVertex3f(-half, 0.0f, half);
    glVertex3f(-half, beach, half);
    glNormal3f(-1.0f, 0.0f, 0.0f);
    glVertex3f(-half, beach, half);
    glVertex3f(-half, 0.0f, half);
    glVertex3f(-half, 0.0f, -half);
    glVertex3f(-half, beach, -half);
    glNormal3f(1.0f, 0.0f, 0.0f);
    glVertex3f(half, beach, -half);
    glVertex3f(half, 0.0f, -half);
    glVertex3f(half, 0.0f, half);
    glVertex3f(half, beach, half);
    glEnd();
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

void PostEffects::drawWater(const AdvancedLighting& lighting, float time, bool orthographic)
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
    const SkyState& sky = lighting.sky();

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
    glUniformMatrix4fv(loc(p, "uInvProj"), 1, GL_FALSE, m_invProj);
    glUniform2f(loc(p, "uScreen"), static_cast<float>(m_width), static_cast<float>(m_height));
    glUniform3f(loc(p, "uCamPos"), invView[12], invView[13], invView[14]);
    glUniform1f(loc(p, "uTime"), time);
    glUniform3fv(loc(p, "uLightDir"), 1, sky.lightDir);
    glUniform3fv(loc(p, "uLightColor"), 1, sky.lightColor);
    glUniform3fv(loc(p, "uZenith"), 1, sky.zenith);
    glUniform3fv(loc(p, "uHorizon"), 1, sky.horizon);
    glUniform1f(loc(p, "uFogDensity"), orthographic ? 0.0f : 0.002f);

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    const float e = 1500.0f;
    const float y = waterLevel;
    glBegin(GL_QUADS);
    glVertex3f(-e, y, -e);
    glVertex3f(-e, y, e);
    glVertex3f(e, y, e);
    glVertex3f(e, y, -e);
    glEnd();
    glPopAttrib();

    unbindTextures(4);
    glUseProgram(0);
}

void PostEffects::blur(Target& src, Target& tmp, float radius)
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

void PostEffects::finish(const SkyState& sky, bool bloom)
{
    float rayColor[3] = {0.0f, 0.0f, 0.0f};

    if (bloom) {
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
        if (clip[3] > 0.0f && sky.sunDir[1] > 0.0f) {
            const float u = clip[0] / clip[3] * 0.5f + 0.5f;
            const float v = clip[1] / clip[3] * 0.5f + 0.5f;
            const float off = fmaxf(fabsf(u - 0.5f), fabsf(v - 0.5f));
            const float visible = 1.0f - smooth01(0.6f, 1.2f, off);
            if (visible > 0.0f) {
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
    glViewport(m_outputViewport[0], m_outputViewport[1], m_outputViewport[2],
               m_outputViewport[3]);
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
    glUniform1f(loc(p, "uExposure"), 1.0f);
    fullscreenQuad();
    unbindTextures(3);
    glUseProgram(0);
}

void renderAdvancedFrame(AdvancedLighting& lighting, PostEffects& effects, LightingFrame frame,
                         const EffectToggles& toggles, int width, int height,
                         SceneCallbacks& scene)
{
    lighting.prepare(frame);
    if (frame.shadows) {
        lighting.beginShadowPass();
        scene.drawObjects(true);
        lighting.endShadowPass();
    }

    const bool post = (toggles.water || toggles.bloom || toggles.ssao) && effects.init();
    if (!post) {
        frame.hdr = false;
        lighting.begin(frame);
        scene.drawGround();
        scene.drawObjects(false);
        lighting.end();
        glDisable(GL_STENCIL_TEST);
        return;
    }

    frame.hdr = true;
    effects.beginFrame(width, height);
    const SkyState& sky = lighting.sky();

    if (toggles.water) {
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
        effects.drawSeabed();
    scene.drawObjects(false);
    lighting.end();
    glDisable(GL_STENCIL_TEST);

    if (toggles.ssao)
        effects.applySSAO();
    if (toggles.water)
        effects.drawWater(lighting, frame.time, frame.orthographic);
    effects.finish(sky, toggles.bloom);
}
