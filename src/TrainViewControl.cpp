/************************************************************************
     File:        TrainViewControl.cpp

     Comment:     The TrainView side of --doctor and --screenshot
                  (AppControl.h): after each drawn frame, reports what the
                  OpenGL driver and the shader path did, or reads the frame
                  back, saves it and checks that it looks drawn, then
                  closes the window so TrainWindow::run() returns.

                  The frame is read from the back buffer before the window
                  swaps it, so it is exactly what the user would see,
                  including the edit and Fly overlays; the hotbar and the
                  key hints are left out of automated runs.

*************************************************************************/

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include "AppControl.h"
#include "TrainView.H"
#include "TrainWindow.H"

#include "Utilities/Platform.H"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace
{
// A frame that never comes (the window was not mapped, the driver hangs,
// the world never finishes building) still ends the run. Generous: a Debug
// build makes a new world in well over a minute.
const double kGiveUpSeconds = 300.0;

double secondsNow()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string glString(GLenum name)
{
    const GLubyte *text = glGetString(name);
    return text ? reinterpret_cast<const char *>(text) : "";
}

void redrawCB(void *view)
{
    static_cast<TrainView *>(view)->redraw();
}

void closeCB(void *window)
{
    glfwSetWindowShouldClose(static_cast<GLFWwindow *>(window), GLFW_TRUE);
}

void giveUpCB(void *view)
{
    TrainView *trainView = static_cast<TrainView *>(view);
    if (trainView->automationResult >= 0)
        return;
    AppControl::report({{"frame", false,
                         trainView->automationFrames == 0
                             ? "the scene never appeared (still loading, or no frame drawn)"
                             : "no frame was drawn in time"}},
                       stdout);
    trainView->finishAutomation(AppControl::kFail);
}
} // namespace

//************************************************************************
//
// * Start counting frames and make sure the run ends
//========================================================================
void TrainView::startAutomation(const AppControl::Options *options)
{
    automation = options;
    automationFrames = 0;
    automationResult = -1;
    automationStart = secondsNow();
    if (automation)
        platform::addTimeout(kGiveUpSeconds, giveUpCB, this);
}

//************************************************************************
//
// * Record the result and close the main window
//========================================================================
void TrainView::finishAutomation(int result)
{
    automationResult = result;
    platform::removeTimeout(giveUpCB, this);
    platform::removeTimeout(redrawCB, this);
    if (m_window)
        platform::addTimeout(0.0, closeCB, m_window);
}

//************************************************************************
//
// * After a frame: --doctor reports, --screenshot saves the last frame
//========================================================================
void TrainView::finishAutomatedFrame()
{
    if (!automation || automationResult >= 0)
        return;

    // --doctor needs only the OpenGL context, so it does not wait for the world
    if (automation->mode == AppControl::Mode::Doctor)
    {
        std::vector<AppControl::Check> checks;
        checks.push_back(
            {"opengl", glGetString(GL_VERSION) != nullptr,
             glString(GL_VENDOR) + " / " + glString(GL_RENDERER) + " / " + glString(GL_VERSION)});
        const std::string glsl = glString(GL_SHADING_LANGUAGE_VERSION);
        checks.push_back({"glsl", !glsl.empty(), glsl.empty() ? "no shading language" : glsl});
        // Light+ quietly falls back to the fixed pipeline when these fail,
        // which looks like a working program with plainer pictures.
        const bool lit = lighting.init();
        checks.push_back({"shader lighting", lit,
                          lit ? "shadow maps and per-pixel lighting ready"
                              : "Light+ would fall back to the fixed pipeline"});
        const bool post = effects.init();
        checks.push_back({"post effects", post,
                          post ? "frame buffers and effect shaders ready"
                               : "water, SSAO, bloom and rain would be off"});
        finishAutomation(AppControl::report(checks, stdout));
        return;
    }

    // the loading screen redraws itself until the world is built
    if (!frameShowsScene)
        return;
    if (++automationFrames == 1)
        worldSeconds = secondsNow() - automationStart;
    if (automationFrames < automation->frames)
    {
        // the next frame: the scenery and the textures settle over the first few
        platform::addTimeout(0.02, redrawCB, this);
        return;
    }

    const int width = w(), height = h();
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    if (glBindFramebuffer) // absent on OpenGL 1.1 (Windows without a GPU driver)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    // OpenGL's rows start at the bottom; a PNG's at the top
    std::vector<unsigned char> rgb(pixels.size());
    const size_t row = static_cast<size_t>(width) * 3;
    for (int y = 0; y < height; ++y)
        std::copy(pixels.begin() + static_cast<std::ptrdiff_t>(row * static_cast<size_t>(y)),
                  pixels.begin() + static_cast<std::ptrdiff_t>(row * static_cast<size_t>(y + 1)),
                  rgb.begin() +
                      static_cast<std::ptrdiff_t>(row * static_cast<size_t>(height - 1 - y)));

    std::vector<AppControl::Check> checks;
    const AppControl::ImageStats stats = AppControl::measure(rgb, width, height);
    std::string error;
    const bool saved = AppControl::writePng(automation->output, rgb, width, height, error);
    checks.push_back({"screenshot", saved, saved ? automation->output : error});
    char built[64];
    std::snprintf(built, sizeof built, "the scene appeared after %.1f s", worldSeconds);
    checks.push_back({"world", true, built});
    checks.push_back({"frame", AppControl::looksDrawn(stats), AppControl::describe(stats)});
    checks.push_back(
        {"shader lighting", !tw->settings.lighting || lighting.init(),
         tw->settings.lighting ? (lighting.init() ? "on" : "Light+ is on but fell back") : "off"});
    finishAutomation(AppControl::report(checks, stdout));
}
