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
#include <stdio.h>

#include "AdvancedLighting.h"

namespace
{

const int kShadowMapSize = 2048;

const char* kVertexShader = R"GLSL(
#version 120
// World space is recovered from the fixed function modelview matrix, so the
// existing glBegin/glEnd drawing code works unchanged.
uniform mat4 uInvView;
varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec4 vColor;

void main()
{
    vec4 eyePos = gl_ModelViewMatrix * gl_Vertex;
    vWorldPos = (uInvView * eyePos).xyz;
    mat3 invViewRot = mat3(uInvView[0].xyz, uInvView[1].xyz, uInvView[2].xyz);
    vWorldNormal = invViewRot * (gl_NormalMatrix * gl_Normal);
    vColor = gl_Color;
    gl_Position = gl_ProjectionMatrix * eyePos;
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#version 120
uniform mat4 uInvView;
uniform mat4 uLightMatrix;     // world -> shadow map texture coordinates
uniform sampler2DShadow uShadowMap;
uniform int uShadowsOn;
uniform float uShadowTexel;    // 1 / shadow map size
uniform float uNormalOffset;   // world units, pushes the lookup off the surface
uniform float uDepthBias;      // shadow map depth units

uniform vec3 uLightDir;        // towards the sun or moon
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

varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec4 vColor;

// Percentage closer filtering over a 12 tap spiral. Each tap is itself a
// hardware 2x2 comparison, so the edge comes out soft.
float shadowAt(vec3 p, vec3 n)
{
    vec4 sc = uLightMatrix * vec4(p + n * uNormalOffset, 1.0);
    vec3 c = sc.xyz / sc.w;
    if (c.z >= 1.0)
        return 1.0;
    c.z -= uDepthBias;
    float lit = 0.0;
    for (int i = 0; i < 12; ++i) {
        float a = float(i) * 2.39996;
        float r = sqrt((float(i) + 0.5) / 12.0) * 1.8;
        vec2 o = vec2(cos(a), sin(a)) * (r * uShadowTexel);
        lit += shadow2D(uShadowMap, vec3(c.xy + o, c.z)).r;
    }
    return lit / 12.0;
}

vec3 blinnPhong(vec3 albedo, vec3 n, vec3 l, vec3 v)
{
    float ndl = max(dot(n, l), 0.0);
    vec3 h = normalize(l + v);
    float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), 48.0) * 0.3 : 0.0;
    return albedo * ndl + vec3(spec);
}

void main()
{
    vec3 n = normalize(vWorldNormal);
    vec3 cameraPos = uInvView[3].xyz;
    vec3 albedo = pow(vColor.rgb, vec3(2.2));

    float shadow = 1.0;
    if (uShadowsOn != 0)
        shadow = shadowAt(vWorldPos, n);

    vec3 v = normalize(cameraPos - vWorldPos);
    vec3 color;
    if (uUnlit != 0) {
        // No usable normal: light it as if it faced the sky, so it still
        // follows the time of day and picks up shadows.
        color = albedo * (uSkyAmbient + uLightColor * (0.7 * shadow));
    } else {
        vec3 ambient = mix(uGroundAmbient, uSkyAmbient, n.y * 0.5 + 0.5);
        color = albedo * ambient;
        color += blinnPhong(albedo, n, normalize(uLightDir), v) * uLightColor * shadow;
    }

    if (uHeadlightOn != 0) {
        vec3 toLamp = uHeadPos - vWorldPos;
        float d = length(toLamp);
        vec3 l = toLamp / max(d, 0.0001);
        float cone = smoothstep(0.86, 0.965, dot(-l, uHeadDir));
        float falloff = (1.0 - smoothstep(60.0, 110.0, d)) / (1.0 + d * d * 0.006);
        vec3 lamp = uUnlit != 0 ? albedo * 0.7 : blinnPhong(albedo, n, l, v);
        color += lamp * uHeadColor * (cone * falloff);
    }

    // Soft shoulder so bright highlights do not clip to flat white.
    color = color / (1.0 + color * 0.25) * 1.25;

    float dist = length(cameraPos - vWorldPos) * uFogDensity;
    color = mix(uFogColor, color, exp(-dist * dist));

    gl_FragColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), vColor.a);
}
)GLSL";

// ---------------------------------------------------------------------
// Small column-major 4x4 helpers (same layout as glLoadMatrixf).
// ---------------------------------------------------------------------
void identity(float m[16])
{
    for (int i = 0; i < 16; ++i)
        m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

void multiply(const float a[16], const float b[16], float out[16])
{
    float r[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = sum;
        }
    for (int i = 0; i < 16; ++i)
        out[i] = r[i];
}

bool invert(const float m[16], float out[16])
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(det) < 1e-12f)
        return false;
    const float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i)
        out[i] = inv[i] * invDet;
    return true;
}

void normalize3(float v[3])
{
    const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 1e-6f) {
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

GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
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
    m_previousFbo = 0;
    m_normalOffset = 0.0f;
    m_depthBias = 0.0f;
    identity(m_lightView);
    identity(m_lightProj);
    identity(m_lightMatrix);
}

void AdvancedLighting::releaseFramebuffer()
{
    if (m_shadowFbo)
        glDeleteFramebuffers(1, &m_shadowFbo);
    if (m_shadowTexture)
        glDeleteTextures(1, &m_shadowTexture);
    m_shadowFbo = 0;
    m_shadowTexture = 0;
}

bool AdvancedLighting::init()
{
    if (m_triedInit)
        return m_ok;
    m_triedInit = true;

    // Shaders and framebuffer objects need OpenGL 2.0 and 3.0 (or the extensions).
    if (!glCreateShader || !glGenFramebuffers || !glFramebufferTexture2D) {
        fprintf(stderr, "AdvancedLighting: OpenGL 3.0 is not available, using basic lighting\n");
        return false;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
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
    if (linked != GL_TRUE) {
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
    m_locShadowsOn = glGetUniformLocation(m_program, "uShadowsOn");
    m_locShadowTexel = glGetUniformLocation(m_program, "uShadowTexel");
    m_locNormalOffset = glGetUniformLocation(m_program, "uNormalOffset");
    m_locDepthBias = glGetUniformLocation(m_program, "uDepthBias");
    m_locLightDir = glGetUniformLocation(m_program, "uLightDir");
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

    // Depth texture with hardware comparison, so shadow2D() returns 0 or 1
    // (bilinearly filtered between the four nearest texels).
    glGenTextures(1, &m_shadowTexture);
    glBindTexture(GL_TEXTURE_2D, m_shadowTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, m_shadowSize, m_shadowSize, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
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
    glGenFramebuffers(1, &m_shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadowTexture,
                           0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "AdvancedLighting: shadow framebuffer incomplete (0x%x)\n", status);
        releaseFramebuffer();
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_ok = true;
    return true;
}

void AdvancedLighting::skyColor(float timeOfDay, float rgb[3])
{
    float sun[3];
    sunDirection(timeOfDay, sun);
    const float day = smoothstep(-0.12f, 0.25f, sun[1]);
    const float dusk = (1.0f - smoothstep(0.05f, 0.45f, sun[1])) * day;

    const float night[3] = {0.02f, 0.03f, 0.09f};
    const float noon[3] = {0.50f, 0.72f, 0.95f};
    const float sunset[3] = {1.0f, 0.60f, 0.35f};
    float base[3];
    mix3(night, noon, day, base);
    mix3(base, sunset, dusk * 0.7f, rgb);
}

void AdvancedLighting::computeLight(const LightingFrame& frame)
{
    float sun[3];
    sunDirection(frame.timeOfDay, sun);
    const float day = smoothstep(-0.02f, 0.25f, sun[1]);
    const float dusk = 1.0f - smoothstep(0.05f, 0.5f, sun[1]);

    // The sun lights the scene by day and the moon (opposite side, dim and
    // blue) by night. Shadows always come from whichever one is up.
    if (sun[1] > 0.0f) {
        const float white[3] = {1.0f, 0.95f, 0.86f};
        const float orange[3] = {1.0f, 0.55f, 0.28f};
        mix3(white, orange, dusk, m_lightColor);
        for (int i = 0; i < 3; ++i) {
            m_lightColor[i] *= 1.3f * day;
            m_lightDir[i] = sun[i];
        }
    } else {
        const float moon[3] = {0.025f, 0.035f, 0.07f};
        for (int i = 0; i < 3; ++i)
            m_lightColor[i] = moon[i] * (1.0f - day);
        m_lightDir[0] = -sun[0];
        m_lightDir[1] = -sun[1];
        m_lightDir[2] = sun[2];
    }
    // Keep the light a little above the horizon so shadows stay finite.
    if (m_lightDir[1] < 0.12f) {
        m_lightDir[1] = 0.12f;
        normalize3(m_lightDir);
    }

    const float nightSky[3] = {0.010f, 0.012f, 0.025f};
    const float daySky[3] = {0.26f, 0.31f, 0.40f};
    mix3(nightSky, daySky, day, m_skyAmbient);
    m_skyAmbient[0] += 0.06f * dusk * day;
    const float nightGround[3] = {0.01f, 0.01f, 0.015f};
    const float dayGround[3] = {0.13f, 0.11f, 0.08f};
    mix3(nightGround, dayGround, day, m_groundAmbient);

    float sky[3];
    skyColor(frame.timeOfDay, sky);
    for (int i = 0; i < 3; ++i)
        m_fogColor[i] = powf(sky[i], 2.2f);

    // Orthographic light camera that encloses every shadow caster.
    float center[3];
    float radius = 0.0f;
    for (int i = 0; i < 3; ++i) {
        center[i] = 0.5f * (frame.casterMin[i] + frame.casterMax[i]);
        const float half = 0.5f * (frame.casterMax[i] - frame.casterMin[i]);
        radius += half * half;
    }
    radius = sqrtf(radius) + 2.0f;

    const float eye[3] = {center[0] + m_lightDir[0] * radius * 2.0f,
                          center[1] + m_lightDir[1] * radius * 2.0f,
                          center[2] + m_lightDir[2] * radius * 2.0f};
    const float upY[3] = {0.0f, 1.0f, 0.0f};
    const float upZ[3] = {0.0f, 0.0f, 1.0f};
    lookAt(eye, center, fabsf(m_lightDir[1]) > 0.99f ? upZ : upY, m_lightView);

    // Receivers may lie far behind the casters (long evening shadows on the
    // ground), so the far plane is generous.
    const float nearPlane = radius * 0.5f;
    const float farPlane = radius * 3.0f + 400.0f;
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
}

void AdvancedLighting::beginShadowPass(const LightingFrame& frame)
{
    if (!m_ok)
        return;
    computeLight(frame);

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_previousFbo);
    glPushAttrib(GL_VIEWPORT_BIT | GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_POLYGON_BIT |
                 GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(m_lightProj);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadMatrixf(m_lightView);

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glViewport(0, 0, m_shadowSize, m_shadowSize);
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
    m_shadowMapValid = true;
}

void AdvancedLighting::begin(const LightingFrame& frame)
{
    if (!m_ok)
        return;
    if (!m_shadowMapValid)
        computeLight(frame);

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
    glUniform3fv(m_locLightDir, 1, m_lightDir);
    glUniform3fv(m_locLightColor, 1, m_lightColor);
    glUniform3fv(m_locSkyAmbient, 1, m_skyAmbient);
    glUniform3fv(m_locGroundAmbient, 1, m_groundAmbient);
    glUniform3fv(m_locFogColor, 1, m_fogColor);
    glUniform1f(m_locFogDensity, frame.orthographic ? 0.0f : 0.002f);
    glUniform1i(m_locUnlit, 0);

    const bool headlight = frame.headlight && frame.hasTrain;
    glUniform1i(m_locHeadlightOn, headlight ? 1 : 0);
    if (headlight) {
        // The lamp sits on the orange nose of the train and tilts slightly down.
        float pos[3];
        float dir[3];
        for (int i = 0; i < 3; ++i) {
            pos[i] = frame.trainPosition[i] + frame.trainUp[i] * 1.4f +
                     frame.trainForward[i] * 3.0f;
            dir[i] = frame.trainForward[i] - frame.trainUp[i] * 0.3f;
        }
        normalize3(dir);
        const float lampColor[3] = {3.2f, 2.9f, 2.3f};
        glUniform3fv(m_locHeadPos, 1, pos);
        glUniform3fv(m_locHeadDir, 1, dir);
        glUniform3fv(m_locHeadColor, 1, lampColor);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_shadowTexture);
    glActiveTexture(GL_TEXTURE0);
    m_active = true;
}

void AdvancedLighting::end()
{
    if (!m_active)
        return;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    m_active = false;
    m_shadowMapValid = false;
}

void AdvancedLighting::setUnlit(bool unlit)
{
    if (m_active)
        glUniform1i(m_locUnlit, unlit ? 1 : 0);
}
