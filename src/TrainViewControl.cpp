/************************************************************************
     File:        TrainViewControl.cpp

     Comment:     The TrainView side of --doctor and --screenshot
                  (AppControl.h): after each drawn frame, reports what the
                  OpenGL driver and the shader path did, or reads the frame
                  back, saves it and checks that it looks drawn, then
                  closes the window so Fl::run() returns.

                  The frame is read from the back buffer before FLTK swaps
                  it, so it is exactly what the user would see, including
                  the edit and Fly overlays.

*************************************************************************/

#include <algorithm>
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

#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/Fl.H>
#pragma warning(pop)

namespace
{
// A frame that never comes (the window was not mapped, the driver hangs)
// still ends the run.
const double kGiveUpSeconds = 120.0;

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
    static_cast<Fl_Window *>(window)->hide();
}

void giveUpCB(void *view)
{
    TrainView *trainView = static_cast<TrainView *>(view);
    if (trainView->automationResult >= 0)
        return;
    AppControl::report({{"frame", false, "no frame was drawn in time"}}, stdout);
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
    if (automation)
        Fl::add_timeout(kGiveUpSeconds, giveUpCB, this);
}

//************************************************************************
//
// * Record the result and close the main window
//========================================================================
void TrainView::finishAutomation(int result)
{
    automationResult = result;
    Fl::remove_timeout(giveUpCB, this);
    Fl::remove_timeout(redrawCB, this);
    Fl::add_timeout(0.0, closeCB, window());
}

//************************************************************************
//
// * After a frame: --doctor reports, --screenshot saves the last frame
//========================================================================
void TrainView::finishAutomatedFrame()
{
    if (!automation || automationResult >= 0)
        return;
    ++automationFrames;

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

    if (automationFrames < automation->frames)
    {
        // the next frame: the scenery and the textures settle over the first few
        Fl::add_timeout(0.02, redrawCB, this);
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
    checks.push_back({"frame", AppControl::looksDrawn(stats), AppControl::describe(stats)});
    checks.push_back({"shader lighting", !tw->lightingButton->value() || lighting.init(),
                      tw->lightingButton->value()
                          ? (lighting.init() ? "on" : "Light+ is on but fell back")
                          : "off"});
    finishAutomation(AppControl::report(checks, stdout));
}
