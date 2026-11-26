/************************************************************************
     File:        WaterSurface.cpp

     Comment:     See WaterSurface.h.

*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <math.h>
#include <stdio.h>

#include <cstdlib>

#include "WaterSurface.h"
#include "Folders.h"

using namespace WaterWaves;

// The height of the surface for the mesh's vertices. A vertex stands for a
// whole cell of the mesh, so it reads the height maps at the mip level as
// coarse as the cell (finer waves would only make it jitter) and the wave
// equation's grid averaged over the cell.
const char *const kWaterSurfaceVertexGlsl = R"GLSL(
uniform int uWaveModel;        // 0 Gerstner, 1 height maps, 2 wave equation
uniform sampler3D uHeightMaps; // the sequence: x, z across a frame, y through it
uniform vec4 uLayerA[3];       // each layer's A: u = a.x x + a.y z, v = a.z x + a.w z
uniform vec4 uLayerB[3];       // its B: offset u, v; loops a second; phase
uniform float uLayerAmp[3];
uniform float uHeightMean;     // the still water's level in the pictures
uniform float uHeightTexels;   // texels across a frame
uniform sampler2D uWaveState;  // r: height now, g: a step before
uniform vec4 uWaveGrid;        // corner x, z; 1 / the grid's size; a cell

float heightLayerLod(int i, vec2 p, float t, float cell)
{
    vec4 a = uLayerA[i];
    vec2 uv = vec2(a.x * p.x + a.y * p.y, a.z * p.x + a.w * p.y) + uLayerB[i].xy;
    float w = t * uLayerB[i].z + uLayerB[i].w;
    float texel = 1.0 / (length(a.xy) * uHeightTexels);
    float lod = max(log2(cell / texel), 0.0);
    return uLayerAmp[i] * (texture3DLod(uHeightMaps, vec3(uv, w), lod).r - uHeightMean);
}

float waveStateAt(vec2 p)
{
    vec2 uv = (p - uWaveGrid.xy) * uWaveGrid.z;
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0)
        return 0.0;
    return texture2DLod(uWaveState, uv, 0.0).r;
}

float waveHeightVertex(vec2 p, float t, float cell)
{
    if (uWaveModel == 1)
        return heightLayerLod(0, p, t, cell) + heightLayerLod(1, p, t, cell) +
               heightLayerLod(2, p, t, cell);
    if (uWaveModel == 2) {
        float e = 0.35 * cell;
        return waveStateAt(p) * 0.4 + (waveStateAt(p + vec2(e, 0.0)) + waveStateAt(p - vec2(e, 0.0)) +
                                       waveStateAt(p + vec2(0.0, e)) + waveStateAt(p - vec2(0.0, e))) * 0.15;
    }
    return 0.0;
}
)GLSL";

// The normal per pixel, from the heights a little way along x and z.
const char *const kWaterSurfaceFragmentGlsl = R"GLSL(
uniform int uWaveModel;
uniform sampler3D uHeightMaps;
uniform vec4 uLayerA[3];
uniform vec4 uLayerB[3];
uniform float uLayerAmp[3];
uniform float uHeightMean;
uniform sampler2D uWaveState;
uniform vec4 uWaveGrid;

// H(x, z, t) = sum of a_i (b(A_i (x, z) + B_i, At_i t + Bt_i) - mean)
float heightMapAt(vec2 p, float t)
{
    float h = 0.0;
    for (int i = 0; i < 3; ++i) {
        vec4 a = uLayerA[i];
        vec2 uv = vec2(a.x * p.x + a.y * p.y, a.z * p.x + a.w * p.y) + uLayerB[i].xy;
        h += uLayerAmp[i] * (texture3D(uHeightMaps, vec3(uv, t * uLayerB[i].z + uLayerB[i].w)).r - uHeightMean);
    }
    return h;
}

float waveGridAt(vec2 uv)
{
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0)
        return 0.0;
    return texture2D(uWaveState, uv).r;
}

vec3 waveNormal(vec2 p, float t, out float h)
{
    if (uWaveModel == 1) {
        float e = 0.2;
        h = heightMapAt(p, t);
        float hx = heightMapAt(p + vec2(e, 0.0), t);
        float hz = heightMapAt(p + vec2(0.0, e), t);
        return normalize(vec3(h - hx, e, h - hz));
    }
    vec2 uv = (p - uWaveGrid.xy) * uWaveGrid.z;
    float step = uWaveGrid.w * uWaveGrid.z; // a cell, across the texture
    h = waveGridAt(uv);
    float l = waveGridAt(uv - vec2(step, 0.0));
    float r = waveGridAt(uv + vec2(step, 0.0));
    float d = waveGridAt(uv - vec2(0.0, step));
    float u = waveGridAt(uv + vec2(0.0, step));
    return normalize(vec3(l - r, 2.0 * uWaveGrid.w, d - u));
}
)GLSL";

namespace
{
// The full screen pass over the grid.
const char *kGridVertex = R"GLSL(
#version 120
varying vec2 vUv;
void main()
{
    vUv = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)GLSL";

// One Verlet step of the wave equation (Project 4's
// p(t2) = (1 + a) p(t1) - a p(t0) + accel h^2, a the damping), with the
// five-point Laplacian; the grid's contents move by uShift cells first when
// it followed the camera. Outside the grid the water is still. Round the
// edge a sponge soaks the waves up.
const char *kStepFragment = R"GLSL(
#version 120
varying vec2 vUv;
uniform sampler2D uState;
uniform float uCells;
uniform vec2 uShift;
uniform float uK;
uniform float uDamping;
uniform float uSponge;
uniform int uAdvance;
vec2 stateAt(vec2 uv)
{
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
        return vec2(0.0);
    return texture2D(uState, uv).rg;
}
void main()
{
    float texel = 1.0 / uCells;
    vec2 uv = vUv + uShift * texel;
    vec2 s = stateAt(uv);
    if (uAdvance == 0) {
        gl_FragColor = vec4(s, 0.0, 1.0);
        return;
    }
    float h = s.r;
    float sum = stateAt(uv + vec2(texel, 0.0)).r + stateAt(uv - vec2(texel, 0.0)).r +
                stateAt(uv + vec2(0.0, texel)).r + stateAt(uv - vec2(0.0, texel)).r;
    vec2 cell = floor(vUv * uCells);
    float edge = min(min(cell.x, uCells - 1.0 - cell.x), min(cell.y, uCells - 1.0 - cell.y));
    float damping = uDamping * (0.92 + 0.08 * min(1.0, edge / uSponge));
    float next = h + damping * (h - s.g) + uK * (sum - 4.0 * h);
    gl_FragColor = vec4(next, h, 0.0, 1.0);
}
)GLSL";

// Adds up to eight drops: a cosine bump each, in cells.
const char *kDropFragment = R"GLSL(
#version 120
varying vec2 vUv;
uniform sampler2D uState;
uniform float uCells;
uniform vec4 uDrops[8]; // centre x, z, radius (cells), strength
uniform int uCount;
void main()
{
    vec2 s = texture2D(uState, vUv).rg;
    vec2 cell = vUv * uCells;
    for (int i = 0; i < 8; ++i) {
        if (i >= uCount)
            break;
        float d = length(cell - uDrops[i].xy) / uDrops[i].z;
        if (d < 1.0)
            s.r += uDrops[i].w * (0.5 + 0.5 * cos(3.14159265 * d));
    }
    gl_FragColor = vec4(s, 0.0, 1.0);
}
)GLSL";

GLuint compileShader(GLenum type, const char *source, const char *name)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        char log[2048];
        glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
        fprintf(stderr, "WaterSurface: %s shader failed:\n%s\n", name, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint gridProgram(const char *fragment, const char *name)
{
    const GLuint vs = compileShader(GL_VERTEX_SHADER, kGridVertex, name);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragment, name);
    if (!vs || !fs)
    {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        fprintf(stderr, "WaterSurface: %s link failed\n", name);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

GLint loc(GLuint program, const char *name)
{
    return glGetUniformLocation(program, name);
}

// The atlas in assets/heightmaps/: the course's 200 frames of 512 x 512,
// every second one, at 256 x 256, ten to a row.
const int kAtlasColumns = 10;
const int kAtlasFrames = 100;
} // namespace

WaterSurface::WaterSurface()
{
}

void WaterSurface::invalidate()
{
    m_triedHeights = false;
    m_heightTexture = 0;
    m_triedSimulation = false;
    m_simulationOk = false;
    for (int i = 0; i < 2; ++i)
        m_state[i] = m_fbo[i] = 0;
    m_stepProgram = m_dropProgram = 0;
    m_clear = true;
}

void WaterSurface::setModel(WaveModel model)
{
    if (model == m_model)
        return;
    m_model = model;
    m_clear = true;
    m_drops.clear();
    m_lastTime = -1.0f;
}

void WaterSurface::addDrop(const Drop &drop)
{
    if (m_model != WaveModel::Equation || m_drops.size() > 512)
        return;
    m_drops.push_back(drop);
}

//************************************************************************
//
// * The height maps: the course's sequence beside the program if it is
//   there (HeightMap\000.png ...), else the smaller atlas made from it
//========================================================================
bool WaterSurface::ensureHeightMaps()
{
    if (m_triedHeights)
        return m_heightTexture != 0;
    m_triedHeights = true;
    if (!glTexImage3D)
        return false;

    HeightFrames frames;
    std::string error;
    const std::string sequence = Folders::besideProgram("HeightMap");
    const std::string atlas = Folders::besideProgram("assets") + "/heightmaps/waves.png";
    if (loadSequence(sequence, frames, error))
        m_heightSource = sequence;
    else if (loadAtlas(atlas, kAtlasColumns, kAtlasFrames, frames, error))
        m_heightSource = atlas;
    else
    {
        fprintf(stderr, "WaterSurface: no height maps (%s); using made-up ones\n", error.c_str());
        frames = fallbackFrames();
        m_heightSource = "built-in";
    }
    m_heightMean = frames.mean;
    m_heightSize = frames.size;

    glGenTextures(1, &m_heightTexture);
    glBindTexture(GL_TEXTURE_3D, m_heightTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, frames.size, frames.size, frames.count, 0, GL_RED,
                 GL_UNSIGNED_BYTE, frames.texels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_3D);
    glBindTexture(GL_TEXTURE_3D, 0);
    fprintf(stderr, "WaterSurface: %d height maps of %d x %d from %s\n", frames.count, frames.size,
            frames.size, m_heightSource.c_str());
    return true;
}

//************************************************************************
//
// * The wave equation's two textures and frame buffers, and its programs
//========================================================================
bool WaterSurface::ensureSimulation()
{
    if (m_triedSimulation)
        return m_simulationOk;
    m_triedSimulation = true;
    m_stepProgram = gridProgram(kStepFragment, "wave step");
    m_dropProgram = gridProgram(kDropFragment, "wave drop");
    if (!m_stepProgram || !m_dropProgram)
        return false;
    for (int i = 0; i < 2; ++i)
    {
        glGenTextures(1, &m_state[i]);
        glBindTexture(GL_TEXTURE_2D, m_state[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, kGridCells, kGridCells, 0, GL_RG, GL_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &m_fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_state[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            fprintf(stderr, "WaterSurface: the wave grid's frame buffer is incomplete\n");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_simulationOk = true;
    m_clear = true;
    return true;
}

// Draws the grid into the other texture from the current one, then makes
// that one current.
void WaterSurface::runPass(unsigned int program, int target)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[target]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_state[1 - target]);
    glUniform1i(loc(program, "uState"), 0);
    glUniform1f(loc(program, "uCells"), static_cast<float>(kGridCells));
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    m_current = target;
}

void WaterSurface::simulate(float time, float x, float z)
{
    if (m_model != WaveModel::Equation || !ensureSimulation())
        return;

    // how far the grid moves to stay round (x, z)
    float ox, oz;
    WaveGrid::snap(x, z, kGridCells, ox, oz);
    int shiftX = 0, shiftZ = 0;
    if (m_placed)
    {
        shiftX = static_cast<int>(lroundf((ox - m_originX) / kGridCell));
        shiftZ = static_cast<int>(lroundf((oz - m_originZ) / kGridCell));
    }
    m_placed = true;
    m_originX = ox;
    m_originZ = oz;

    // the steps due: fixed steps, as many as the time since the last frame
    // holds, but no more than a few (then the water slows down)
    int steps = 0;
    if (m_lastTime >= 0.0f)
    {
        m_owed += fmaxf(0.0f, time - m_lastTime);
        steps = static_cast<int>(m_owed / kStepSeconds);
        if (steps > kMostStepsAFrame)
        {
            steps = kMostStepsAFrame;
            m_owed = 0.0f;
        }
        else
            m_owed -= static_cast<float>(steps) * kStepSeconds;
    }
    m_lastTime = time;

    if (!m_clear && shiftX == 0 && shiftZ == 0 && steps == 0 && m_drops.empty())
        return;

    glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glViewport(0, 0, kGridCells, kGridCells);

    if (m_clear || std::abs(shiftX) >= kGridCells || std::abs(shiftZ) >= kGridCells)
    {
        for (int i = 0; i < 2; ++i)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[i]);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        m_clear = false;
        shiftX = shiftZ = 0;
    }

    // the drops, eight at a time
    if (!m_drops.empty())
    {
        glUseProgram(m_dropProgram);
        for (size_t first = 0; first < m_drops.size(); first += 8)
        {
            float drops[8 * 4] = {};
            int count = 0;
            for (size_t i = first; i < m_drops.size() && count < 8; ++i, ++count)
            {
                float cx, cz, radius;
                // the textures still hold the grid as it was before the
                // move below: shiftX cells further along
                dropInCells(m_drops[i], m_originX, m_originZ, cx, cz, radius);
                drops[count * 4 + 0] = cx + static_cast<float>(shiftX);
                drops[count * 4 + 1] = cz + static_cast<float>(shiftZ);
                drops[count * 4 + 2] = fmaxf(radius, 1.0f);
                drops[count * 4 + 3] = m_drops[i].strength;
            }
            glUniform4fv(loc(m_dropProgram, "uDrops"), 8, drops);
            glUniform1i(loc(m_dropProgram, "uCount"), count);
            runPass(m_dropProgram, 1 - m_current);
        }
        m_drops.clear();
    }

    glUseProgram(m_stepProgram);
    glUniform1f(loc(m_stepProgram, "uK"), courantSquared());
    glUniform1f(loc(m_stepProgram, "uDamping"), kDamping);
    glUniform1f(loc(m_stepProgram, "uSponge"), static_cast<float>(kSpongeCells));
    // the move goes with the first step (or on its own when none is due)
    for (int i = 0; i < steps || (i == 0 && (shiftX != 0 || shiftZ != 0)); ++i)
    {
        glUniform2f(loc(m_stepProgram, "uShift"), static_cast<float>(i == 0 ? shiftX : 0),
                    static_cast<float>(i == 0 ? shiftZ : 0));
        glUniform1i(loc(m_stepProgram, "uAdvance"), i < steps ? 1 : 0);
        runPass(m_stepProgram, 1 - m_current);
    }

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPopAttrib();
}

void WaterSurface::bind(unsigned int program, int heightUnit, int wavesUnit)
{
    int model = static_cast<int>(m_model);
    if (m_model == WaveModel::HeightMap && !ensureHeightMaps())
        model = 0;
    if (m_model == WaveModel::Equation && !ensureSimulation())
        model = 0;
    glUniform1i(loc(program, "uWaveModel"), model);

    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + heightUnit));
    glBindTexture(GL_TEXTURE_3D, model == 1 ? m_heightTexture : 0);
    glUniform1i(loc(program, "uHeightMaps"), heightUnit);
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + wavesUnit));
    glBindTexture(GL_TEXTURE_2D, model == 2 ? m_state[m_current] : 0);
    glUniform1i(loc(program, "uWaveState"), wavesUnit);
    glActiveTexture(GL_TEXTURE0);

    float a[3 * 4], b[3 * 4], amp[3];
    for (int i = 0; i < kHeightLayers; ++i)
    {
        const HeightLayer &layer = heightLayer(i);
        layerMatrix(layer, a + i * 4);
        b[i * 4 + 0] = layer.offsetU;
        b[i * 4 + 1] = layer.offsetV;
        b[i * 4 + 2] = 1.0f / layer.loopSeconds;
        b[i * 4 + 3] = layer.phase;
        amp[i] = layer.amplitude;
    }
    glUniform4fv(loc(program, "uLayerA"), 3, a);
    glUniform4fv(loc(program, "uLayerB"), 3, b);
    glUniform1fv(loc(program, "uLayerAmp"), 3, amp);
    glUniform1f(loc(program, "uHeightMean"), m_heightMean);
    glUniform1f(loc(program, "uHeightTexels"), static_cast<float>(m_heightSize));
    glUniform4f(loc(program, "uWaveGrid"), m_originX, m_originZ,
                1.0f / (static_cast<float>(kGridCells) * kGridCell), kGridCell);
}

void WaterSurface::unbind(int heightUnit, int wavesUnit)
{
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + heightUnit));
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + wavesUnit));
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}
