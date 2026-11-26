/************************************************************************
     File:        WaterSurface.h

     Comment:     The graphics card's side of WaterWaves.h: the height map
                  sequence as a 3D texture, and the wave equation solved
                  in a pair of float textures that take turns (ping-pong
                  frame buffers), one step a draw call over the grid.
                  PostEffects asks it to run the steps due before it draws
                  the sea, and to hand its textures and numbers to the
                  sea's shaders, which displace the mesh's vertices by the
                  surface's height and shade it with the normal worked
                  out from the same height per pixel.

*************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "WaterWaves.h"

// GLSL for the sea's shaders, pasted in front of them. The vertex part has
// float waveHeightVertex(vec2 p, float t, float cell): the height of the
// surface at p for a mesh whose cells are `cell` wide. The fragment part
// has vec3 waveNormal(vec2 p, float t, out float h): the normal there, and
// the height. Both use uWaveModel (1 height maps, 2 wave equation).
extern const char *const kWaterSurfaceVertexGlsl;
extern const char *const kWaterSurfaceFragmentGlsl;

class WaterSurface
{
  public:
    WaterSurface();

    // forgets the GL names (a new context has none of them)
    void invalidate();

    WaveModel model() const
    {
        return m_model;
    }
    // Switching to Wave Equation starts from still water.
    void setModel(WaveModel model);

    // Disturbances for the next step (the train, the rain, a stone); ones
    // outside the grid are left out.
    void addDrop(const WaterWaves::Drop &drop);

    // Runs the steps due by `time` (seconds) with the grid round (x, z).
    // Leaves no program or frame buffer bound: the caller binds its own.
    void simulate(float time, float x, float z);

    // Sets the surface's uniforms on `program` (in use) and binds its
    // textures to units `heightUnit` and `wavesUnit`.
    void bind(unsigned int program, int heightUnit, int wavesUnit);
    void unbind(int heightUnit, int wavesUnit);

    // Where the height map frames came from, for the menu and the logs.
    const std::string &heightSource() const
    {
        return m_heightSource;
    }

  private:
    bool ensureHeightMaps();
    bool ensureSimulation();
    void runPass(unsigned int program, int target);

    WaveModel m_model = WaveModel::Gerstner;
    std::vector<WaterWaves::Drop> m_drops;

    // the height map sequence
    bool m_triedHeights = false;
    unsigned int m_heightTexture = 0;
    float m_heightMean = 0.5f;
    int m_heightSize = 1;
    std::string m_heightSource;

    // the wave equation
    bool m_triedSimulation = false;
    bool m_simulationOk = false;
    unsigned int m_state[2] = {0, 0};
    unsigned int m_fbo[2] = {0, 0};
    int m_current = 0;
    unsigned int m_stepProgram = 0;
    unsigned int m_dropProgram = 0;
    bool m_placed = false;
    bool m_clear = true;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    float m_lastTime = -1.0f;
    float m_owed = 0.0f; // seconds not yet stepped
};
