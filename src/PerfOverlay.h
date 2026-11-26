/************************************************************************
     File:        PerfOverlay.h

     Comment:     The performance screen (F3, KeyBinds' "Performance
                  Screen"), drawn over the ride as a block game's debug
                  screen: white text on grey strips.

                  - top left: frames drawn in the last second, what one
                    frame costs (the processor or the graphics card,
                    whichever is slower) and the frame rate that would
                    allow, the processor's and the card's times, the
                    window, the camera and the switches that cost the most;
                  - top right: every phase of the frame and every part of
                    the scene with its processor and graphics card time in
                    milliseconds, a bar for its share of the frame;
                  - bottom left: the last frames' costs as a bar graph
                    with lines at 60 and 30 frames a second, green below
                    the first, yellow below the second, red above.

                  Only draws (Ui/Gui.h); the numbers come from PerfStats.
                  Without OpenGL, so tests/PerfStatsCheck.cpp lays it out.

*************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "PerfStats.h"
#include "SceneSettings.h"
#include "Ui/Gui.h"

struct PerfInfo
{
    std::string version;  // "RollerCoasters 1.2"
    std::string renderer; // GL_RENDERER
    std::string glVersion;
    std::vector<std::string> lines; // more lines for the left column (sceneLines())
    bool vsync = true;
    bool gpuTimer = false; // the graphics card's times are measured
    std::string memory;    // PerfStats::memoryLine(), or ""
};

namespace PerfOverlay
{
// The camera and the switches that change the cost of a frame most, as
// lines: "Camera: Train", "Light+: on (shadows, water, bloom, SSAO)", ...
std::vector<std::string> sceneLines(const SceneSettings &s);
// How many points the rails are built from (TrackRenderer):
// "Rails: 1665 samples, adaptive (fixed: 2977)", or "" when none yet.
std::string trackLine(int samples, int uniformSamples, int uniformPerSegment, bool adaptive);
// The left column's text, top to bottom (also tested on its own).
std::vector<std::string> leftLines(const PerfStats::Summary &s, const PerfInfo &info, int pixelW,
                                   int pixelH);
void draw(ui::Gui &gui, const PerfStats &stats, const PerfInfo &info, double now);
} // namespace PerfOverlay
