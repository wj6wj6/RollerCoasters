/************************************************************************
     File:        AdvancedLighting.cpp

     Comment:     Shadow mapping, per-pixel lighting, time of day and the
                  train headlight. See AdvancedLighting.h for an overview.

*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <math.h>
#include <initializer_list>
#include <stdio.h>

#include "AdvancedLighting.h"
#include "MatrixMath.h"
#include "LandWaterMotion.h"
#include "UnderwaterOptics.h"

namespace
{

const int kShadowMapSize = 2048;
const int kFarShadowMapSize = 4096; // over kFarShadowReach: texels of about a third of a unit

// Pasted after "#version 120" and the island water's motion (LandWaterMotion.h).
const char *kVertexShader = R"GLSL(
// World space is recovered from the fixed function modelview matrix, so the
// existing glBegin/glEnd drawing code works unchanged.
uniform mat4 uInvView;
uniform mat4 uView;
uniform float uTime;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec4 vColor;
// emissive (negative on the island's water: -1 at its open surface, -0.5
// elsewhere); sway (when positive) or the warm light of lava and lanterns on
// a block that does not sway (as minus that); cool block light; sky light
varying vec4 vExtra;

void main()
{
    vec4 eyePos = gl_ModelViewMatrix * gl_Vertex;
    vec4 world = uInvView * eyePos;
    mat3 invViewRot = mat3(uInvView[0].xyz, uInvView[1].xyz, uInvView[2].xyz);
    vWorldNormal = invViewRot * (gl_NormalMatrix * gl_Normal);
    vColor = gl_Color;
    gl_TexCoord[0] = gl_MultiTexCoord0;
    // Texture unit 1's coordinate carries extra per-vertex data. Code that
    // never sets it gets (0, 0, 0, 1): no glow, no sway, full sky light.
    vExtra = gl_MultiTexCoord1;

    // Only what asks for it sways (the y of texture unit 1, below). Swaying
    // every green vertex up high, as for the old cone trees, also bent the
    // train's green cab and the riders' green shirts.

    // Kelp and sea grass sway in the current, the tips the most.
    float sway = gl_MultiTexCoord1.y;
    if (sway > 0.0) {
        float phase = uTime * 0.9 + world.x * 0.05 + world.z * 0.04;
        world.x += sin(phase) * 1.4 * sway;
        world.z += cos(phase * 0.8 + 1.3) * 1.0 * sway;
        eyePos = uView * world;
    }

    // The island's water moves a little, as the sea does (LandWaterMotion.h);
    // the picture on its surface drifts slowly.
    if (gl_MultiTexCoord1.x < -0.25) {
        float stirred;
        world.xyz += landWaterMotion(gl_MultiTexCoord1.x, world.xyz, uTime, stirred);
        eyePos = uView * world;
        if (vWorldNormal.y > 0.5)
            gl_TexCoord[0].s += uTime * 0.02;
    }

    vWorldPos = world.xyz;
    gl_Position = gl_ProjectionMatrix * eyePos;
}
)GLSL";

// Pasted after "#version 120" and the underwater optics (UnderwaterOptics.h).
const char *kFragmentShader = R"GLSL(
uniform mat4 uInvView;
uniform mat4 uLightMatrix;     // world -> shadow map texture coordinates
uniform sampler2DShadow uShadowMap;
uniform int uShadowsOn;
uniform float uShadowTexel;    // 1 / shadow map size
uniform float uNormalOffset;   // world units, pushes the lookup off the surface
uniform float uDepthBias;      // shadow map depth units
uniform mat4 uFarMatrix;       // the coarse cascade round the fine one
uniform sampler2DShadow uFarShadowMap;
uniform int uFarOn;
uniform float uFarTexel;
uniform float uFarNormalOffset;
uniform float uFarDepthBias;

uniform vec3 uLightDir;        // towards the sun or moon
uniform vec3 uWaterLightDir;   // the same, as bent by the surface (below the water)
uniform vec3 uLightColor;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform vec3 uFogColor;        // linear
uniform float uFogDensity;

uniform int uHeadlightOn;
uniform vec3 uHeadPos;
uniform vec3 uHeadDir;
uniform vec3 uHeadColor;

uniform int uUnlit;
uniform int uHdr;              // 1: linear HDR out for the post effects
uniform float uClipY;          // reflections: nothing below the water
uniform sampler2D uTexture;    // unit 0; plain white when nothing is bound
uniform sampler2D uCausticMap; // unit 3; see LightingFrame::causticMap
uniform vec3 uCausticMapArea;
uniform int uCausticMapOn;
uniform float uTime;
uniform float uWaterLevel;     // very low when there is no water
uniform vec3 uBlockLightColor; // glow cast by crystals
uniform float uWetness;        // 0 dry, 1 soaked by rain
uniform int uTranslucent;      // 1 while the see-through water and glass are drawn
uniform float uDaylight;       // 1 in full day, 0 at night

varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec4 vColor;
varying vec4 vExtra;

// Percentage closer filtering over a 12 tap spiral. Each tap is itself a
// hardware 2x2 comparison, so the edge comes out soft. edge: how near the
// edge of the map p lies (0.5 at it), 1 past its far plane.
float shadowIn(sampler2DShadow map, mat4 m, vec3 p, vec3 n, float offset, float bias, float texel,
               int taps, out float edge)
{
    vec4 sc = m * vec4(p + n * offset, 1.0);
    vec3 c = sc.xyz / sc.w;
    edge = max(abs(c.x - 0.5), abs(c.y - 0.5));
    if (c.z >= 1.0) {
        edge = 1.0;
        return 1.0;
    }
    c.z -= bias;
    float lit = 0.0;
    for (int i = 0; i < 12; ++i) {
        if (i >= taps)
            break;
        float a = float(i) * 2.39996;
        float r = sqrt((float(i) + 0.5) / float(taps)) * 1.8;
        vec2 o = vec2(cos(a), sin(a)) * (r * texel);
        lit += shadow2D(map, vec3(c.xy + o, c.z)).r;
    }
    return lit / float(taps);
}

// The fine map round the camera, and past its edge the coarse one, which
// fades out at its own edge; between them they blend, so no line shows
// where one hands over to the other.
float shadowAt(vec3 p, vec3 n)
{
    float edge;
    float fine = shadowIn(uShadowMap, uLightMatrix, p, n, uNormalOffset, uDepthBias, uShadowTexel,
                          12, edge);
    float toCoarse = smoothstep(0.42, 0.5, edge);
    if (toCoarse <= 0.0)
        return fine;
    float coarse = 1.0;
    if (uFarOn != 0) {
        float farEdge;
        coarse = shadowIn(uFarShadowMap, uFarMatrix, p, n, uFarNormalOffset, uFarDepthBias,
                          uFarTexel, 5, farEdge);
        coarse = mix(coarse, 1.0, smoothstep(0.44, 0.5, farEdge));
    }
    return mix(fine, coarse, toCoarse);
}

// A smooth value noise in 0..1, for the water running down wet walls.
float hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}

// wet: a film of water makes the surface smoother, so the highlight gets
// both tighter and much brighter.
vec3 blinnPhong(vec3 albedo, vec3 n, vec3 l, vec3 v, float wet)
{
    float ndl = max(dot(n, l), 0.0);
    vec3 h = normalize(l + v);
    float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), mix(48.0, 260.0, wet)) * mix(0.3, 1.8, wet) : 0.0;
    return albedo * ndl + vec3(spec);
}

void main()
{
    if (vWorldPos.y < uClipY)
        discard;
    vec3 n = normalize(vWorldNormal);
    if (vExtra.x < -0.25 && n.y > 0.5) {
        // the island's water: small ripples running over it catch the sun
        vec2 p = vWorldPos.xz;
        float a = cos((p.x + p.y) * 0.6 - uTime * 1.2) * 0.35;
        vec2 slope = vec2(cos(p.x * 0.9 + uTime * 1.6) * 0.5 + a, cos(p.y * 0.8 - uTime * 1.4) * 0.5 + a);
        n = normalize(vec3(-slope.x * 0.12, 1.0, -slope.y * 0.12));
    }
    vec3 cameraPos = uInvView[3].xyz;
    vec4 texel = texture2D(uTexture, gl_TexCoord[0].st);
    if (texel.a < 0.5)
        discard; // cut-out plants
    vec4 base = vColor * texel;
    vec3 albedo = pow(base.rgb, vec3(2.2));

    // Wet surfaces are darker and glossy: whatever the rain reaches, and
    // always the band just above the water line. How open a surface is to
    // the sky (its sky light, 1 out in the open) says how much rain reaches
    // it: under eaves and trees less, in tunnels and indoors none. Walls are
    // wet in streaks where the water runs down them.
    float exposure = smoothstep(0.5, 0.95, vExtra.w);
    float wet = 0.0;
    if (vWorldPos.y > uWaterLevel) {
        float shore = 1.0 - smoothstep(0.2, 1.8, vWorldPos.y - uWaterLevel);
        float rain = uWetness * exposure * (0.6 + 0.4 * max(n.y, 0.0));
        if (abs(n.y) < 0.5) {
            float along = dot(vWorldPos.xz, vec2(-n.z, n.x));
            float run = valueNoise(vec2(along * 1.3, vWorldPos.y * 0.12 + uTime * 0.5));
            rain *= mix(0.7, 1.15, smoothstep(0.3, 0.75, run));
        }
        wet = max(rain, shore * 0.8);
        albedo *= mix(1.0, 0.6, wet);
    }

    float shadow = 1.0;
    if (uShadowsOn != 0)
        shadow = shadowAt(vWorldPos, n);

    vec3 l = normalize(vWorldPos.y < uWaterLevel ? uWaterLightDir : uLightDir);
    // The big swells' bright spots from the caustic map, where the sun ray
    // through this point crossed the surface. Looked up outside the branch
    // below so the mip level is chosen right.
    float swell = 1.0;
    if (uCausticMapOn != 0) {
        vec2 s = vWorldPos.xz + l.xz / max(l.y, 0.2) * max(uWaterLevel - vWorldPos.y, 0.0);
        vec2 uv = (s - uCausticMapArea.xy) / uCausticMapArea.z + 0.5;
        vec2 inside = smoothstep(vec2(0.0), vec2(0.05), uv) * (1.0 - smoothstep(vec2(0.95), vec2(1.0), uv));
        swell = mix(1.0, texture2D(uCausticMap, uv).b, inside.x * inside.y);
    }
    vec3 sun = uLightColor * shadow;
    vec3 skyAmbient = uSkyAmbient * vExtra.w;
    vec3 groundAmbient = uGroundAmbient * vExtra.w;
    if (vWorldPos.y < uWaterLevel) {
        // Under water the sunlight dims and turns blue with depth and is
        // focused into caustics; the ambient light is scattered blue too.
        float depth = uWaterLevel - vWorldPos.y;
        // The surface bends the light steeper but lets no more of it
        // through: per unit of level ground the sea bed gets what the
        // surface got (less a few percent reflected), so the bent beam is
        // weaker by the ratio of the two slopes.
        sun *= 0.97 * max(normalize(uLightDir).y, 0.0) / max(l.y, 0.05);
        sun *= sunThroughWater(depth, l) *
               caustics(vWorldPos, l, uWaterLevel, uTime, length(cameraPos - vWorldPos), swell);
        vec3 tint = exp(-kWaterAbsorb * depth * 0.8) * vec3(0.55, 0.95, 1.2);
        skyAmbient *= tint;
        groundAmbient = skyAmbient * 0.75; // light bounced off the pale sand
    }

    vec3 v = normalize(cameraPos - vWorldPos);
    vec3 color;
    if (uUnlit != 0) {
        // No usable normal: light it as if it faced the sky, so it still
        // follows the time of day and picks up shadows.
        color = albedo * (skyAmbient + sun * 0.7);
    } else {
        vec3 ambient = mix(groundAmbient, skyAmbient, n.y * 0.5 + 0.5);
        color = albedo * ambient;
        color += blinnPhong(albedo, n, l, v, wet) * sun;
    }
    // Light sources look like ornaments by day and like lamps by night (and
    // in tunnels and under cover at any hour): how much the light they cast
    // shows depends on how dark it is where it falls, the sky light there
    // times the daylight; their own glow is kept stronger, as a lamp that is
    // lit still looks lit in the sun, only less so.
    float dark = 1.0 - uDaylight * vExtra.w;
    float spill = mix(0.1, 1.0, dark);
    float own = mix(0.45, 1.0, dark);
    float warm = max(-vExtra.y, 0.0);
    color += albedo * vec3(1.0, 0.62, 0.32) * warm * 1.6 * spill; // lava, lanterns
    color += albedo * uBlockLightColor * vExtra.z * spill;       // crystals, sea lanterns
    color += albedo * max(vExtra.x, 0.0) * 3.0 * own;

    if (uHeadlightOn != 0) {
        vec3 toLamp = uHeadPos - vWorldPos;
        float d = length(toLamp);
        vec3 l = toLamp / max(d, 0.0001);
        float cone = smoothstep(0.86, 0.965, dot(-l, uHeadDir));
        float falloff = (1.0 - smoothstep(60.0, 110.0, d)) / (1.0 + d * d * 0.006);
        vec3 lamp = uUnlit != 0 ? albedo * 0.7 : blinnPhong(albedo, n, l, v, wet);
        color += lamp * uHeadColor * (cone * falloff);
    }

    float dist = length(cameraPos - vWorldPos) * uFogDensity;
    if (uHdr != 0) {
        // Tone mapping happens in the final post pass. The alpha of solid
        // surfaces carries how open they are to the rain, for the wet ground
        // pass; see-through ones keep their own, which blends them. Plants
        // that sway (grass, flowers) get no film: their blades are no wall.
        color = mix(uFogColor, color, exp(-dist * dist));
        float rainMask = vExtra.y > 0.0 ? 0.002 : max(exposure, 0.002);
        gl_FragColor = vec4(color, uTranslucent != 0 ? base.a : rainMask);
        return;
    }

    // Soft shoulder so bright highlights do not clip to flat white.
    color = color / (1.0 + color * 0.25) * 1.25;
    color = mix(uFogColor, color, exp(-dist * dist));

    gl_FragColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), base.a);
}
)GLSL";

// ---------------------------------------------------------------------
// Vector helpers.
// ---------------------------------------------------------------------
void normalize3(float v[3])
{
    const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f)
    {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

void cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

float dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void lookAt(const float eye[3], const float center[3], const float up[3], float m[16])
{
    float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    normalize3(f);
    float s[3];
    cross3(f, up, s);
    normalize3(s);
    float u[3];
    cross3(s, f, u);

    identity(m);
    m[0] = s[0];
    m[4] = s[1];
    m[8] = s[2];
    m[1] = u[0];
    m[5] = u[1];
    m[9] = u[2];
    m[2] = -f[0];
    m[6] = -f[1];
    m[10] = -f[2];
    m[12] = -dot3(s, eye);
    m[13] = -dot3(u, eye);
    m[14] = dot3(f, eye);
}

void ortho(float l, float r, float b, float t, float n, float f, float m[16])
{
    identity(m);
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
}

float clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

float smoothstep(float e0, float e1, float x)
{
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

void mix3(const float a[3], const float b[3], float t, float out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = a[i] + (b[i] - a[i]) * t;
}

// Sun direction for a time of day (6:00 sunrise in +x, 18:00 sunset in -x).
void sunDirection(float timeOfDay, float dir[3])
{
    float hour = fmodf(timeOfDay, 24.0f);
    if (hour < 0.0f)
        hour += 24.0f;
    const float angle = (hour - 6.0f) / 12.0f * 3.14159265f;
    dir[0] = cosf(angle);
    dir[1] = sinf(angle) * 0.9f;
    dir[2] = 0.4f;
    normalize3(dir);
}

GLuint compileShader(GLenum type, const char *const *parts, int count)
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
        fprintf(stderr, "AdvancedLighting: shader compile failed:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

AdvancedLighting::AdvancedLighting()
{
    invalidate();
}

void AdvancedLighting::invalidate()
{
    m_triedInit = false;
    m_ok = false;
    m_active = false;
    m_shadowMapValid = false;
    m_program = 0;
    m_shadowFbo = 0;
    m_shadowTexture = 0;
    m_shadowSize = kShadowMapSize;
    m_farUsed = m_farValid = false;
    m_cascade = 0;
    m_farFbo = 0;
    m_farTexture = 0;
    m_farSize = kFarShadowMapSize;
    m_previousFbo = 0;
    m_normalOffset = 0.0f;
    m_depthBias = 0.0f;
    m_headOn = false;
    for (int i = 0; i < 3; ++i)
        m_headPos[i] = m_headDir[i] = 0.0f;
    m_waterLightDir[0] = m_waterLightDir[2] = 0.0f;
    m_waterLightDir[1] = 1.0f;
    m_sky = SkyState();
    identity(m_lightView);
    identity(m_lightProj);
    identity(m_lightMatrix);
    identity(m_farView);
    identity(m_farProj);
    identity(m_farMatrix);
}

void AdvancedLighting::releaseFramebuffer()
{
    for (unsigned int *fbo : {&m_shadowFbo, &m_farFbo})
        if (*fbo)
        {
            glDeleteFramebuffers(1, fbo);
            *fbo = 0;
        }
    for (unsigned int *texture : {&m_shadowTexture, &m_farTexture})
        if (*texture)
        {
            glDeleteTextures(1, texture);
            *texture = 0;
        }
}

// A depth texture with hardware comparison, so shadow2D() returns 0 or 1
// (bilinearly filtered between the four nearest texels), lit beyond its
// edge, and a framebuffer drawing into it. False if the framebuffer is not
// complete.
static bool makeShadowTarget(int size, unsigned int &texture, unsigned int &fbo)
{
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const GLfloat border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        fprintf(stderr, "AdvancedLighting: shadow framebuffer incomplete (0x%x)\n", status);
        return false;
    }
    return true;
}

bool AdvancedLighting::init()
{
    if (m_triedInit)
        return m_ok;
    m_triedInit = true;

    // Shaders and framebuffer objects need OpenGL 2.0 and 3.0 (or the extensions).
    if (!glCreateShader || !glGenFramebuffers || !glFramebufferTexture2D)
    {
        fprintf(stderr, "AdvancedLighting: OpenGL 3.0 is not available, using basic lighting\n");
        return false;
    }

    const char *vertexParts[3] = {"#version 120\n", kLandWaterMotionGlsl, kVertexShader};
    const char *fragmentParts[3] = {"#version 120\n", kUnderwaterOpticsGlsl, kFragmentShader};
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexParts, 3);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentParts, 3);
    if (!vs || !fs)
    {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        char log[2048];
        glGetProgramInfoLog(m_program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        fprintf(stderr, "AdvancedLighting: shader link failed:\n%s\n", log);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_locInvView = glGetUniformLocation(m_program, "uInvView");
    m_locLightMatrix = glGetUniformLocation(m_program, "uLightMatrix");
    m_locShadowMap = glGetUniformLocation(m_program, "uShadowMap");
    m_locFarMatrix = glGetUniformLocation(m_program, "uFarMatrix");
    m_locFarShadowMap = glGetUniformLocation(m_program, "uFarShadowMap");
    m_locFarOn = glGetUniformLocation(m_program, "uFarOn");
    m_locFarTexel = glGetUniformLocation(m_program, "uFarTexel");
    m_locFarNormalOffset = glGetUniformLocation(m_program, "uFarNormalOffset");
    m_locFarDepthBias = glGetUniformLocation(m_program, "uFarDepthBias");
    m_locShadowsOn = glGetUniformLocation(m_program, "uShadowsOn");
    m_locShadowTexel = glGetUniformLocation(m_program, "uShadowTexel");
    m_locNormalOffset = glGetUniformLocation(m_program, "uNormalOffset");
    m_locDepthBias = glGetUniformLocation(m_program, "uDepthBias");
    m_locLightDir = glGetUniformLocation(m_program, "uLightDir");
    m_locWaterLightDir = glGetUniformLocation(m_program, "uWaterLightDir");
    m_locCausticMap = glGetUniformLocation(m_program, "uCausticMap");
    m_locCausticMapArea = glGetUniformLocation(m_program, "uCausticMapArea");
    m_locCausticMapOn = glGetUniformLocation(m_program, "uCausticMapOn");
    m_locLightColor = glGetUniformLocation(m_program, "uLightColor");
    m_locSkyAmbient = glGetUniformLocation(m_program, "uSkyAmbient");
    m_locGroundAmbient = glGetUniformLocation(m_program, "uGroundAmbient");
    m_locFogColor = glGetUniformLocation(m_program, "uFogColor");
    m_locFogDensity = glGetUniformLocation(m_program, "uFogDensity");
    m_locHeadlightOn = glGetUniformLocation(m_program, "uHeadlightOn");
    m_locHeadPos = glGetUniformLocation(m_program, "uHeadPos");
    m_locHeadDir = glGetUniformLocation(m_program, "uHeadDir");
    m_locHeadColor = glGetUniformLocation(m_program, "uHeadColor");
    m_locUnlit = glGetUniformLocation(m_program, "uUnlit");
    m_locHdr = glGetUniformLocation(m_program, "uHdr");
    m_locClipY = glGetUniformLocation(m_program, "uClipY");
    m_locTime = glGetUniformLocation(m_program, "uTime");
    m_locView = glGetUniformLocation(m_program, "uView");
    m_locTexture = glGetUniformLocation(m_program, "uTexture");
    m_locWaterLevel = glGetUniformLocation(m_program, "uWaterLevel");
    m_locBlockLightColor = glGetUniformLocation(m_program, "uBlockLightColor");
    m_locWetness = glGetUniformLocation(m_program, "uWetness");
    m_locTranslucent = glGetUniformLocation(m_program, "uTranslucent");
    m_locDaylight = glGetUniformLocation(m_program, "uDaylight");

    // Textured drawing code (glBindTexture on unit 0) keeps working through the
    // shader. Untextured code leaves the default texture object 0 bound, so
    // give that one a single white texel: multiplying by it changes nothing.
    {
        const GLubyte white[4] = {255, 255, 255, 255};
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    // The fine shadow map, and the coarse one round it; without the coarse
    // one (a GPU that cannot make it) the shadows fade out past the fine one.
    if (!makeShadowTarget(m_shadowSize, m_shadowTexture, m_shadowFbo))
    {
        releaseFramebuffer();
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    if (!makeShadowTarget(m_farSize, m_farTexture, m_farFbo))
    {
        if (m_farFbo)
            glDeleteFramebuffers(1, &m_farFbo);
        if (m_farTexture)
            glDeleteTextures(1, &m_farTexture);
        m_farFbo = m_farTexture = 0;
    }

    m_ok = true;
    return true;
}

void AdvancedLighting::skyColor(float timeOfDay, float rgb[3])
{
    float sun[3];
    sunDirection(timeOfDay, sun);
    const float day = smoothstep(-0.14f, 0.22f, sun[1]);
    // the golden hour: strongest with the sun on the horizon, gone by the
    // time it is a third of the way up, and fading out after it has set
    const float dusk =
        (1.0f - smoothstep(0.04f, 0.42f, sun[1])) * smoothstep(-0.16f, -0.02f, sun[1]);

    const float night[3] = {0.035f, 0.05f, 0.12f};
    const float noon[3] = {0.50f, 0.72f, 0.95f};
    const float sunset[3] = {1.0f, 0.50f, 0.22f};
    float base[3];
    mix3(night, noon, day, base);
    mix3(base, sunset, dusk * 0.85f, rgb);
}

void AdvancedLighting::prepare(const LightingFrame &frame)
{
    m_shadowMapValid = false;
    m_farValid = false;
    m_farUsed = false;
    if (!m_ok)
        return;

    float sun[3];
    sunDirection(frame.timeOfDay, sun);
    float *lightDir = m_sky.lightDir;
    float *lightColor = m_sky.lightColor;
    for (int i = 0; i < 3; ++i)
        m_sky.sunDir[i] = sun[i];
    // The moon rises in the east as the sun sets in the west, a little to
    // the other side of the sky, so the two never overlap.
    m_sky.moonDir[0] = -sun[0];
    m_sky.moonDir[1] = -sun[1];
    m_sky.moonDir[2] = -sun[2];
    const float day = smoothstep(-0.04f, 0.16f, sun[1]);
    const float dusk =
        (1.0f - smoothstep(0.04f, 0.42f, sun[1])) * smoothstep(-0.16f, -0.02f, sun[1]);
    // the lamps come on as the sun sets, and earlier under a rain sky
    m_daylight = smoothstep(0.02f, 0.3f, sun[1]) * (1.0f - 0.45f * clamp01(frame.rain));

    // The sun lights the scene by day: deep orange on the horizon, golden
    // a little higher, white by mid morning. The moon (opposite side, cool
    // blue, bright enough to see by) lights it by night. Shadows always come
    // from whichever one is up.
    if (sun[1] > -0.02f)
    {
        const float white[3] = {1.0f, 0.95f, 0.86f};
        const float golden[3] = {1.0f, 0.66f, 0.36f};
        const float ember[3] = {1.0f, 0.40f, 0.14f};
        const float high = smoothstep(0.12f, 0.5f, sun[1]);
        const float low = 1.0f - smoothstep(0.0f, 0.14f, sun[1]);
        float warm[3];
        mix3(golden, ember, low, warm);
        mix3(warm, white, high, lightColor);
        for (int i = 0; i < 3; ++i)
        {
            lightColor[i] *= 1.35f * day;
            lightDir[i] = sun[i];
        }
    }
    else
    {
        const float moon[3] = {0.13f, 0.17f, 0.30f};
        const float up = smoothstep(-0.02f, -0.16f, sun[1]); // fades in as the sun goes down
        for (int i = 0; i < 3; ++i)
        {
            lightColor[i] = moon[i] * up;
            lightDir[i] = m_sky.moonDir[i];
        }
    }
    // Keep the light a little above the horizon so shadows stay finite.
    if (lightDir[1] < 0.12f)
    {
        lightDir[1] = 0.12f;
        normalize3(lightDir);
    }

    // Light from the sky itself: blue by day, warm and rosy in the golden
    // hour, and the moon's pale blue at night.
    const float nightSky[3] = {0.040f, 0.050f, 0.090f};
    const float daySky[3] = {0.26f, 0.31f, 0.40f};
    const float duskSky[3] = {0.30f, 0.20f, 0.20f};
    mix3(nightSky, daySky, day, m_skyAmbient);
    mix3(m_skyAmbient, duskSky, dusk * 0.6f, m_skyAmbient);
    const float nightGround[3] = {0.016f, 0.018f, 0.028f};
    const float dayGround[3] = {0.13f, 0.11f, 0.08f};
    const float duskGround[3] = {0.16f, 0.09f, 0.05f};
    mix3(nightGround, dayGround, day, m_groundAmbient);
    mix3(m_groundAmbient, duskGround, dusk * 0.5f, m_groundAmbient);

    float sky[3];
    skyColor(frame.timeOfDay, sky);
    for (int i = 0; i < 3; ++i)
        m_fogColor[i] = powf(sky[i], 2.2f);

    const float nightZenith[3] = {0.004f, 0.006f, 0.022f};
    const float dayZenith[3] = {0.09f, 0.22f, 0.62f};
    const float duskZenith[3] = {0.05f, 0.05f, 0.22f}; // deep blue turning violet
    mix3(nightZenith, dayZenith, day, m_sky.zenith);
    mix3(m_sky.zenith, duskZenith, dusk * 0.8f, m_sky.zenith);
    for (int i = 0; i < 3; ++i)
        m_sky.horizon[i] = m_fogColor[i];
    m_sky.day = day;
    m_sky.dusk = dusk;

    // Rain: the clouds close in, so the sun dims and the sky, the fog and
    // the light from the sky all turn grey.
    const float rain = clamp01(frame.rain);
    m_sky.overcast = rain;
    if (rain > 0.0f)
    {
        const float grey[3] = {0.16f * day + 0.004f, 0.18f * day + 0.005f, 0.21f * day + 0.008f};
        const float blend = 0.8f * rain;
        for (int i = 0; i < 3; ++i)
        {
            lightColor[i] *= 1.0f - 0.65f * rain;
            m_sky.zenith[i] += (grey[i] * 0.8f - m_sky.zenith[i]) * blend;
            m_sky.horizon[i] += (grey[i] * 1.6f - m_sky.horizon[i]) * blend;
            m_fogColor[i] = m_sky.horizon[i];
            m_skyAmbient[i] += (grey[i] * 1.5f - m_skyAmbient[i]) * blend * 0.6f;
        }
    }

    // Under water the sunlight has been bent towards straight down by the
    // surface. While the camera is under water, the shadow map is drawn
    // along that bent light too, so the shadows on the sea bed, the caustics
    // and the light shafts all slant the same way (the land above, out of
    // sight then, gets slightly wrong shadows).
    lightDirInWater(lightDir, m_waterLightDir);
    float shadowDir[3];
    for (int i = 0; i < 3; ++i)
        shadowDir[i] = frame.cameraUnderwater ? m_waterLightDir[i] : lightDir[i];

    // Orthographic light camera. With a focus it covers the ground round
    // the point the camera looks at; otherwise (the top view) every shadow
    // caster.
    float center[3];
    float radius = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        center[i] = 0.5f * (frame.casterMin[i] + frame.casterMax[i]);
        const float half = 0.5f * (frame.casterMax[i] - frame.casterMin[i]);
        radius += half * half;
    }
    radius = sqrtf(radius) + 2.0f;
    // how far back towards the light casters may stand: the whole box
    float reach = radius * 2.0f;
    const float upY[3] = {0.0f, 1.0f, 0.0f};
    const float upZ[3] = {0.0f, 0.0f, 1.0f};
    const float *up = fabsf(shadowDir[1]) > 0.99f ? upZ : upY;
    bool focused = false; // the fine map covers only the ground round the focus
    if (frame.hasFocus && !frame.orthographic && kShadowReach < radius)
    {
        focused = true;
        radius = kShadowReach;
        for (int i = 0; i < 3; ++i)
            center[i] = frame.focus[i];
        // Move the centre in whole texels across the light's view, so the
        // shadows' edges do not crawl as the camera moves.
        float s[3], u[3];
        const float f[3] = {-shadowDir[0], -shadowDir[1], -shadowDir[2]};
        cross3(f, up, s);
        normalize3(s);
        cross3(s, f, u);
        const float texel = 2.0f * radius / static_cast<float>(m_shadowSize);
        const float cs = dot3(s, center), cu = dot3(u, center);
        const float ds = floorf(cs / texel + 0.5f) * texel - cs;
        const float du = floorf(cu / texel + 0.5f) * texel - cu;
        for (int i = 0; i < 3; ++i)
            center[i] += s[i] * ds + u[i] * du;
    }

    const float eye[3] = {center[0] + shadowDir[0] * reach, center[1] + shadowDir[1] * reach,
                          center[2] + shadowDir[2] * reach};
    lookAt(eye, center, up, m_lightView);

    // Receivers may lie far behind the casters (long evening shadows on the
    // ground), so the far plane is generous.
    const float nearPlane = 1.0f;
    const float farPlane = reach * 2.0f + 400.0f;
    ortho(-radius, radius, -radius, radius, nearPlane, farPlane, m_lightProj);

    float bias[16];
    identity(bias);
    bias[0] = bias[5] = bias[10] = 0.5f;
    bias[12] = bias[13] = bias[14] = 0.5f;
    float viewProj[16];
    multiply(m_lightProj, m_lightView, viewProj);
    multiply(bias, viewProj, m_lightMatrix);

    const float texelWorld = 2.0f * radius / static_cast<float>(m_shadowSize);
    m_normalOffset = texelWorld * 1.5f;
    m_depthBias = 0.15f / (farPlane - nearPlane);

    // The coarse cascade: the same light, over kFarShadowReach round the
    // focus, its centre moved in whole texels too. Only when the fine map
    // covers just part of the casters.
    if (m_farTexture && focused)
    {
        m_farUsed = true;
        const float farRadius = kFarShadowReach;
        float farCenter[3] = {frame.focus[0], frame.focus[1], frame.focus[2]};
        float s[3], u[3];
        const float f[3] = {-shadowDir[0], -shadowDir[1], -shadowDir[2]};
        cross3(f, up, s);
        normalize3(s);
        cross3(s, f, u);
        const float texel = 2.0f * farRadius / static_cast<float>(m_farSize);
        const float cs = dot3(s, farCenter), cu = dot3(u, farCenter);
        const float ds = floorf(cs / texel + 0.5f) * texel - cs;
        const float du = floorf(cu / texel + 0.5f) * texel - cu;
        for (int i = 0; i < 3; ++i)
            farCenter[i] += s[i] * ds + u[i] * du;
        const float farEye[3] = {farCenter[0] + shadowDir[0] * reach,
                                 farCenter[1] + shadowDir[1] * reach,
                                 farCenter[2] + shadowDir[2] * reach};
        lookAt(farEye, farCenter, up, m_farView);
        ortho(-farRadius, farRadius, -farRadius, farRadius, nearPlane, farPlane, m_farProj);
        float farViewProj[16];
        multiply(m_farProj, m_farView, farViewProj);
        multiply(bias, farViewProj, m_farMatrix);
        m_farNormalOffset = texel * 1.5f;
        m_farDepthBias = 0.3f / (farPlane - nearPlane);
    }
}

void AdvancedLighting::beginShadowPass(int cascade)
{
    if (!m_ok)
        return;
    m_cascade = cascade == 1 && m_farUsed ? 1 : 0;
    const bool coarse = m_cascade == 1;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_previousFbo);
    glPushAttrib(GL_VIEWPORT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT |
                 GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(coarse ? m_farProj : m_lightProj);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf(coarse ? m_farView : m_lightView);

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, coarse ? m_farFbo : m_shadowFbo);
    glViewport(0, 0, coarse ? m_farSize : m_shadowSize, coarse ? m_farSize : m_shadowSize);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 4.0f);
}

void AdvancedLighting::endShadowPass()
{
    if (!m_ok)
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_previousFbo));
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    if (m_cascade == 1)
        m_farValid = true;
    else
        m_shadowMapValid = true;
}

void AdvancedLighting::begin(const LightingFrame &frame)
{
    if (!m_ok)
        return;

    float view[16];
    float invView[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    if (!invert(view, invView))
        identity(invView);

    glUseProgram(m_program);
    glUniformMatrix4fv(m_locInvView, 1, GL_FALSE, invView);
    glUniformMatrix4fv(m_locLightMatrix, 1, GL_FALSE, m_lightMatrix);
    glUniform1i(m_locShadowMap, 1);
    glUniform1i(m_locShadowsOn, (frame.shadows && m_shadowMapValid) ? 1 : 0);
    glUniform1f(m_locShadowTexel, 1.0f / static_cast<float>(m_shadowSize));
    glUniform1f(m_locNormalOffset, m_normalOffset);
    glUniform1f(m_locDepthBias, m_depthBias);
    glUniformMatrix4fv(m_locFarMatrix, 1, GL_FALSE, m_farMatrix);
    glUniform1i(m_locFarShadowMap, 2);
    glUniform1i(m_locFarOn, (frame.shadows && m_farValid) ? 1 : 0);
    glUniform1f(m_locFarTexel, 1.0f / static_cast<float>(m_farSize));
    glUniform1f(m_locFarNormalOffset, m_farNormalOffset);
    glUniform1f(m_locFarDepthBias, m_farDepthBias);
    glUniform3fv(m_locLightDir, 1, m_sky.lightDir);
    glUniform3fv(m_locWaterLightDir, 1, m_waterLightDir);
    glUniform3fv(m_locLightColor, 1, m_sky.lightColor);
    glUniformMatrix4fv(m_locView, 1, GL_FALSE, view);
    glUniform1i(m_locHdr, frame.hdr ? 1 : 0);
    glUniform1f(m_locClipY, frame.clipBelowY);
    glUniform1f(m_locTime, frame.time);
    glUniform1i(m_locTexture, 0);
    glUniform1i(m_locCausticMap, 3);
    glUniform3fv(m_locCausticMapArea, 1, frame.causticMapArea);
    glUniform1i(m_locCausticMapOn, frame.causticMap != 0 ? 1 : 0);
    glUniform3fv(m_locSkyAmbient, 1, m_skyAmbient);
    glUniform3fv(m_locGroundAmbient, 1, m_groundAmbient);
    glUniform3fv(m_locFogColor, 1, m_fogColor);
    // Under water the fog is drawn by the post pass instead.
    glUniform1f(m_locFogDensity, (frame.orthographic || frame.cameraUnderwater)
                                     ? 0.0f
                                     : 0.0009f + 0.0016f * clamp01(frame.rain));
    glUniform1f(m_locWetness, clamp01(frame.wetness));
    glUniform1i(m_locTranslucent, 0);
    glUniform1f(m_locDaylight, m_daylight);
    glUniform1f(m_locWaterLevel, frame.waterLevel);
    const float crystalLight[3] = {0.12f, 0.55f, 0.75f};
    glUniform3fv(m_locBlockLightColor, 1, crystalLight);
    glUniform1i(m_locUnlit, 0);

    const bool headlight = frame.headlight && frame.hasTrain;
    m_headOn = headlight;
    glUniform1i(m_locHeadlightOn, headlight ? 1 : 0);
    if (headlight)
    {
        // The lamp sits on the orange nose of the train and tilts slightly down.
        float pos[3];
        float dir[3];
        for (int i = 0; i < 3; ++i)
        {
            pos[i] =
                frame.trainPosition[i] + frame.trainUp[i] * 1.4f + frame.trainForward[i] * 3.0f;
            dir[i] = frame.trainForward[i] - frame.trainUp[i] * 0.3f;
        }
        normalize3(dir);
        const float lampColor[3] = {3.2f, 2.9f, 2.3f};
        for (int i = 0; i < 3; ++i)
        {
            m_headPos[i] = pos[i];
            m_headDir[i] = dir[i];
        }
        glUniform3fv(m_locHeadPos, 1, pos);
        glUniform3fv(m_locHeadDir, 1, dir);
        glUniform3fv(m_locHeadColor, 1, lampColor);
    }

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, frame.causticMap);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_farTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_shadowTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0); // the white default texture
    m_active = true;
}

void AdvancedLighting::end()
{
    if (!m_active)
        return;
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    m_active = false;
}

void AdvancedLighting::setUnlit(bool unlit)
{
    if (m_active)
        glUniform1i(m_locUnlit, unlit ? 1 : 0);
}

void AdvancedLighting::setTranslucent(bool translucent)
{
    if (m_active)
        glUniform1i(m_locTranslucent, translucent ? 1 : 0);
}
