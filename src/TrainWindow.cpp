/************************************************************************
     File:        TrainWindow.cpp

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     See TrainWindow.H.

*************************************************************************/

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "TrainWindow.H"
#include "TrainView.H"
#include "AppControl.h"
#include "CallBacks.H"
#include "TexturePack.h"
#include "WindowsKey.h"
#include "Utilities/Platform.H"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace
{
// The keys set in Options > Controls, as a block game's options file.
const char kKeyBindsFile[] = "data/keybinds.txt";
// The window starts this size: the Grand Tour wants a wide view.
const int kStartWidth = 1180;
const int kStartHeight = 720;
// One step of the train's speed setting.
const double kStepSeconds = 1.0 / TrainWindow::kStepsPerSecond;
// After a longer stall (a dragged window, a breakpoint) the train goes on
// from where it was rather than jumping ahead.
const double kLongestStep = 0.25;
// The performance screen (F3) is drawn again at least this often.
const double kPerfRefreshSeconds = 0.25;
// The property on the window that leads its procedure back to the TrainWindow.
const wchar_t kWindowProperty[] = L"RollerCoasters.TrainWindow";
// ... and the one that keeps GLFW's own procedure, which the hook passes on to.
const wchar_t kPreviousProperty[] = L"RollerCoasters.Previous";

TrainWindow *windowOf(GLFWwindow *w)
{
    return static_cast<TrainWindow *>(glfwGetWindowUserPointer(w));
}

void reportGlfwError(int code, const char *description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
}

// Touchpad scrolling and pinching: the view reads the wheel messages itself,
// before GLFW drops their fractions' modifier flags (TrainView::systemWheel).
// While the menu is open the wheel scrolls its lists instead.
LRESULT CALLBACK wheelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    TrainWindow *tw = static_cast<TrainWindow *>(GetPropW(hwnd, kWindowProperty));
    if (tw && (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) && !tw->menu.open() &&
        tw->trainView->systemWheel(message, wParam, lParam))
        return 0;
    const WNDPROC previous = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kPreviousProperty));
    return previous ? CallWindowProcW(previous, hwnd, message, wParam, lParam)
                    : DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

//************************************************************************
//
// * The world, the track and the view; the window comes in open()
//========================================================================
TrainWindow::TrainWindow()
{
    trainView = new TrainView();
    trainView->tw = this;
    trainView->m_pTrack = &m_Track;
    keys.loadFile(kKeyBindsFile); // the defaults until a key is changed
    keys.applyTo(trainView->flyCam);
    listTexturePacks();
    // The program opens on the demo: the Grand Tour seen from the train.
    // P (or the hotbar's last slot) starts the ride.
    grandTourCB(this);
}

TrainWindow::~TrainWindow()
{
    if (window)
    {
        WindowsKeyHook::stop();
        trainView->make_current();
        delete trainView; // also takes the touchpad's hook off the window
        // GLFW's procedure back in place, so it sees the window go
        HWND hwnd = glfwGetWin32Window(window);
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(GetPropW(hwnd, kPreviousProperty)));
        RemovePropW(hwnd, kWindowProperty);
        RemovePropW(hwnd, kPreviousProperty);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
    else
        delete trainView;
}

//************************************************************************
//
// * The window and its OpenGL context
//========================================================================
bool TrainWindow::open(std::string &error)
{
    glfwSetErrorCallback(reportGlfwError);
    if (!glfwInit())
    {
        error = "the window system could not be started (GLFW)";
        return false;
    }
    // The drawing uses the fixed-function pipeline next to shaders, so a
    // compatibility context (GLFW's default); a stencil buffer for the
    // planar shadows.
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    // The start size grows with the display's scaling (150 % makes it half
    // as big again); the interface's GUI scale follows the window.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    window =
        glfwCreateWindow(kStartWidth, kStartHeight, "Train and Roller Coaster", nullptr, nullptr);
    if (!window)
    {
        error = "no OpenGL window could be made (is the graphics driver installed?)";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, buttonCallback);
    glfwSetCursorPosCallback(window, cursorCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetWindowFocusCallback(window, focusCallback);
    glfwSetCursorEnterCallback(window, enterCallback);
    glfwSetCharCallback(window, charCallback);
    glfwSetFramebufferSizeCallback(window, sizeCallback);
    glfwSetWindowRefreshCallback(window, refreshCallback);
    glfwSetDropCallback(window, dropCallback);

    HWND hwnd = glfwGetWin32Window(window);
    // Nothing in the program takes typed text, and a Chinese input method
    // would catch W A S D for its composition window: switch it off here.
    // A null context with 0 detaches the input method (documented), though
    // the SAL annotation says _In_.
#pragma warning(suppress : 6387)
    (void)ImmAssociateContextEx(hwnd, nullptr, 0);
    // GLFW's procedure is stored before the hook goes in, which passes everything on to it.
    SetPropW(hwnd, kPreviousProperty,
             reinterpret_cast<HANDLE>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)));
    SetPropW(hwnd, kWindowProperty, this);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wheelProc));
    // The Windows key does nothing while the world is played (WindowsKey.h).
    WindowsKeyHook::setWindow(hwnd);

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    trainView->attach(window);
    trainView->resize(width, height);
    return true;
}

void TrainWindow::close()
{
    if (window)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

//************************************************************************
//
// * The loop: events, timers, the train, and a frame when one is asked for
//========================================================================
void TrainWindow::pumpEvents(double seconds)
{
    if (seconds > 0.0)
        glfwWaitEventsTimeout(seconds);
    else
        glfwPollEvents();
}

void TrainWindow::run()
{
    platform::setEventPump(pumpEvents);
    glfwShowWindow(window);
    glfwFocusWindow(window);
    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        // sleep until something is due: a timer, a frame already asked for;
        // while the train runs or the water, clouds and plants move (Light+),
        // a frame every time the screen can show one (vsync sets the pace)
        double wait = 0.25;
        const double timer = platform::secondsToNextTimeout();
        if (timer >= 0.0 && timer < wait)
            wait = timer;
        // (not while minimised: nothing shows, and the swap may not wait)
        const bool shown = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == 0;
        if ((settings.run || settings.lighting) && shown)
            wait = 0.0;
        // the performance screen's numbers change even when nothing moves
        const bool perfShown = menu.perfShown && !automated;
        if (perfShown)
        {
            const double refresh = m_lastDraw + kPerfRefreshSeconds - now;
            if (refresh < wait)
                wait = refresh > 0.0 ? refresh : 0.0;
        }
        if (trainView->needsDraw() || m_fullscreenAsked >= 0)
            wait = 0.0;
        pumpEvents(wait);
        {
            PerfScope update(PerfStage::Update);
            platform::runDueTimeouts();
        }
        switchFullscreen();
        WindowsKeyHook::update(locksWindowsKey());

        now = glfwGetTime();
        if (settings.run)
        {
            PerfScope update(PerfStage::Update);
            // as far as the time since the last frame takes it
            if (!m_running)
                m_lastAdvance = now;
            const double seconds = std::min(now - m_lastAdvance, kLongestStep);
            m_lastAdvance = now;
            if (seconds > 0.0)
                advanceTrain(static_cast<float>(seconds / kStepSeconds));
            damageMe();
        }
        m_running = settings.run;
        if (perfShown && now - m_lastDraw >= kPerfRefreshSeconds)
            trainView->damage(1);
        // keep the water, clouds and plants moving even when the train is stopped
        if (settings.lighting && shown)
            trainView->damage(1);
        if (trainView->needsDraw())
            drawNow();
    }
    platform::setEventPump(nullptr);
}

void TrainWindow::drawNow()
{
    trainView->make_current();
    // Time the frame for the performance screen (F3) or --benchmark only:
    // switched off, the stages cost nothing.
    PerfStats &perf = trainView->perf;
    const bool benchmark =
        trainView->automation && trainView->automation->mode == AppControl::Mode::Benchmark;
    perf.setEnabled(benchmark || (menu.perfShown && !automated));
    perf.beginFrame(PerfStats::clock());
    trainView->draw();
    {
        // the driver may wait here for the screen's next refresh (vsync)
        PerfScope swap(PerfStage::Swap);
        glfwSwapBuffers(window);
    }
    perf.endFrame(PerfStats::clock());
    m_lastDraw = glfwGetTime();
}

//************************************************************************
//
// * Fullscreen (F11, or Lighting's Fullscreen)
//========================================================================
bool TrainWindow::fullscreen() const
{
    if (m_fullscreenAsked >= 0)
        return m_fullscreenAsked == 1;
    return m_fullscreen;
}

void TrainWindow::setFullscreen(bool on)
{
    // Not now: the switch resizes the window, and Windows sends the new
    // size and asks for a frame at once, which must not happen in the
    // middle of a frame (the menu's button) or of a key's callback.
    m_fullscreenAsked = on ? 1 : 0;
    trainView->damage(1);
}

void TrainWindow::switchFullscreen()
{
    const int asked = m_fullscreenAsked;
    m_fullscreenAsked = -1;
    if (asked < 0 || !window)
        return;
    const bool on = asked == 1;
    if (on == m_fullscreen)
        return;
    HWND hwnd = glfwGetWin32Window(window);
    if (on)
    {
        // the monitor holding most of the window
        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            return;
        // A maximized window is put back to its own size first, so that
        // size is the one to come back to.
        m_windowedMaximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
        if (m_windowedMaximized)
            glfwRestoreWindow(window);
        glfwGetWindowPos(window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(window, &m_windowedWidth, &m_windowedHeight);
        // Borderless, over the whole screen at its own resolution, as an
        // ordinary window (not glfwSetWindowMonitor): Start, Alt+Tab and
        // the other screen just cover it, without minimizing it or the
        // screen going black while the driver leaves the game's own output.
        // One pixel taller than the screen, or the driver takes a window
        // filling it exactly for an exclusive one all the same. In front it
        // still covers the taskbar, which Windows lowers for such a window.
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        const RECT &r = monitor.rcMonitor;
        SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top + 1,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_fullscreen = true;
    }
    else
    {
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowSize(window, m_windowedWidth, m_windowedHeight);
        glfwSetWindowPos(window, m_windowedX, m_windowedY);
        m_fullscreen = false;
        if (m_windowedMaximized)
            glfwMaximizeWindow(window);
    }
    glfwSwapInterval(1); // some drivers forget it with the new mode
    // The framebuffer size callback has resized the view; ask once more
    // in case the size came back the same.
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    resized(width, height);
}

bool TrainWindow::locksWindowsKey() const
{
    // not on the loading screen either, as the menus: there is no game yet
    return keys.lockWindowsKey && !automated && window &&
           glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE && trainView->frameShowsScene &&
           !menu.open();
}

void TrainWindow::drawInterface()
{
    ui::Gui &gui = trainView->m_gui;
    PerfScope phase(PerfStage::Interface);
    gui.beginFrame(trainView->w(), trainView->h());
    // A check's pictures show only the scene, unless --menu opened a screen
    // (main.cpp) to see it; that screen takes no input.
    if (!automated || menu.screen() != GameMenu::Screen::None)
        menu.draw(*this, gui, glfwGetTime());
    trainView->m_guiRenderer.draw(gui);
}

//************************************************************************
//
// * Input: the interface first, then the view
//========================================================================
void TrainWindow::input(const InputEvent &e)
{
    if (e.type == InputType::None)
        return;
    // While the loading screen shows there is no interface to take anything;
    // the title and the screens before Play show over the dirt and take
    // everything.
    const bool interface = trainView->frameShowsScene || !menu.inWorld();
    if (!automated && interface && menu.input(*this, e))
        return;
    trainView->handleInput(e);
}

void TrainWindow::resized(int width, int height)
{
    trainView->resize(width, height);
}

void TrainWindow::keysChanged()
{
    keys.applyTo(trainView->flyCam);
    if (!automated && !keys.saveFile(kKeyBindsFile))
        std::fprintf(stderr, "Could not save the keys in %s\n", kKeyBindsFile);
}

void TrainWindow::keyCallback(GLFWwindow *w, int key, int, int action, int mods)
{
    TrainWindow *tw = windowOf(w);
    const InputEvent e = tw->m_input.key(key, action, mods);
    // Fullscreen (F11) switches anywhere, also on the title and in the menu,
    // once a press: holding it down does not flip it back and forth. While
    // Controls waits for a new key, the key goes there instead.
    if (e.type == InputType::KeyDown && !tw->menu.pickingKey() &&
        tw->keys.matches(KeyBinds::Action::Fullscreen, e))
    {
        if (action == GLFW_PRESS && !tw->automated)
            tw->setFullscreen(!tw->fullscreen());
        return;
    }
    tw->input(e);
}

void TrainWindow::buttonCallback(GLFWwindow *w, int button, int action, int mods)
{
    TrainWindow *tw = windowOf(w);
    tw->input(tw->m_input.button(button, action, mods, glfwGetTime()));
}

void TrainWindow::cursorCallback(GLFWwindow *w, double x, double y)
{
    TrainWindow *tw = windowOf(w);
    tw->input(tw->m_input.cursor(x, y));
}

void TrainWindow::scrollCallback(GLFWwindow *w, double dx, double dy)
{
    TrainWindow *tw = windowOf(w);
    tw->input(tw->m_input.scroll(dx, dy));
}

void TrainWindow::focusCallback(GLFWwindow *w, int focused)
{
    TrainWindow *tw = windowOf(w);
    tw->input(tw->m_input.focus(focused == GLFW_TRUE));
}

void TrainWindow::enterCallback(GLFWwindow *w, int entered)
{
    TrainWindow *tw = windowOf(w);
    tw->input(tw->m_input.enter(entered == GLFW_TRUE));
}

void TrainWindow::charCallback(GLFWwindow *w, unsigned int codepoint)
{
    TrainWindow *tw = windowOf(w);
    tw->input(tw->m_input.text(codepoint));
}

void TrainWindow::sizeCallback(GLFWwindow *w, int width, int height)
{
    windowOf(w)->resized(width, height);
}

// Windows stops the loop while the window is dragged or resized; draw from here then.
void TrainWindow::refreshCallback(GLFWwindow *w)
{
    windowOf(w)->drawNow();
}

// Files dragged from the file manager onto the window (UTF-8 paths): the
// menu adds tracks and texture packs, as a block game adds dropped packs.
void TrainWindow::dropCallback(GLFWwindow *w, int count, const char **paths)
{
    TrainWindow *tw = windowOf(w);
    if (tw->automated || !tw->trainView->frameShowsScene)
        return; // no interface yet to show what was added
    tw->menu.dropped(*tw, std::vector<std::string>(paths, paths + count));
}

//************************************************************************
//
// * Changes the menu, the hotbar and the view make
//========================================================================
void TrainWindow::damageMe()
{
    if (trainView->selectedCube >= ((int)m_Track.points.size()))
        trainView->selectedCube = 0;
    trainView->damage(1);
}

void TrainWindow::setCamera(CameraView camera)
{
    settings.camera = camera;
    trainView->cameraChanged();
    damageMe();
}

void TrainWindow::nudgeSelected(int axis, float units)
{
    const int selected = trainView->selectedCube;
    if (selected < 0 || selected >= static_cast<int>(m_Track.points.size()))
        return;
    recordHistory();
    Pnt3f &pos = m_Track.points[static_cast<size_t>(selected)].pos;
    float *coordinate = axis == 0 ? &pos.x : (axis == 1 ? &pos.y : &pos.z);
    *coordinate += units;
    damageMe();
}

//************************************************************************
//
// * Undo and redo (TrackHistory.h)
//========================================================================
void TrainWindow::recordHistory()
{
    m_history.record(m_Track, trainView->selectedCube);
}

void TrainWindow::pushHistory(const TrackHistory::Snapshot &before)
{
    m_history.push(before);
}

void TrainWindow::undoEdit()
{
    trainView->finishEdits(); // a move in progress becomes a step of its own first
    if (m_history.undo(m_Track, trainView->selectedCube))
        damageMe();
}

void TrainWindow::redoEdit()
{
    trainView->finishEdits();
    if (m_history.redo(m_Track, trainView->selectedCube))
        damageMe();
}

static float wrapTrackParameter(float u, float period)
{
    return u - floorf(u / period) * period;
}

static float advanceByArcLength(const CTrack &track, float u, float distance,
                                TrackSplineType splineType)
{
    const float period = static_cast<float>(track.points.size());
    const float direction = distance < 0.0f ? -1.0f : 1.0f;
    float remaining = fabsf(distance);
    const float parameterStep = 0.005f;
    u = wrapTrackParameter(u, period);

    for (int i = 0; i < 20000 && remaining > 0.0001f; ++i)
    {
        const float nextU = wrapTrackParameter(u + direction * parameterStep, period);
        const Pnt3f current = track.evaluatePosition(u, splineType);
        const Pnt3f next = track.evaluatePosition(nextU, splineType);
        const float dx = next.x - current.x;
        const float dy = next.y - current.y;
        const float dz = next.z - current.z;
        const float segmentLength = sqrtf(dx * dx + dy * dy + dz * dz);

        if (segmentLength <= 0.000001f)
        {
            u = nextU;
            continue;
        }

        if (segmentLength >= remaining)
        {
            u = wrapTrackParameter(u + direction * parameterStep * (remaining / segmentLength),
                                   period);
            remaining = 0.0f;
        }
        else
        {
            remaining -= segmentLength;
            u = nextU;
        }
    }

    return u;
}

//************************************************************************
//
// * This will get called (approximately) 30 times per second
//   if the run button is pressed
//========================================================================
void TrainWindow::advanceTrain(float dir)
//========================================================================
{
    const int pointCount = static_cast<int>(m_Track.points.size());
    if (pointCount < 4)
        return;

    const TrackSplineType splineType = static_cast<TrackSplineType>(settings.spline);
    const float step = dir * settings.speed;
    if (settings.arcLength)
        m_Track.trainU = advanceByArcLength(m_Track, m_Track.trainU, step, splineType);
    else
        m_Track.trainU =
            wrapTrackParameter(m_Track.trainU + step * 0.01f, static_cast<float>(pointCount));
}

//************************************************************************
//
// * Set the scene up from the command line (AppControl.h)
//========================================================================
bool TrainWindow::applyOptions(const AppControl::Options &options, std::string &error)
//========================================================================
{
    if (!options.track.empty())
    {
        if (!AppControl::checkTrackFile(options.track, error) ||
            !m_Track.readPoints(options.track.c_str(), error))
        {
            error = "track: " + error;
            return false;
        }
        trainView->selectedCube = -1;
    }
    if (!options.texturePack.empty())
    {
        auto pack = std::make_shared<TexturePack::Pack>();
        if (!pack->open(options.texturePack))
        {
            error = "texture pack: no usable textures in " + options.texturePack;
            return false;
        }
        texturePackPath = options.texturePack;
        trainView->setTexturePack(pack);
        listTexturePacks(texturePackPath);
    }

    auto set = [](bool &setting, AppControl::Switch value) {
        if (value != AppControl::Switch::Keep)
            setting = value == AppControl::Switch::On;
    };
    set(settings.scenery, options.scenery);
    set(settings.lighting, options.light);
    set(settings.water, options.water);
    set(settings.rain, options.rain);
    set(settings.shadows, options.shadows);
    set(settings.adaptiveTrack, options.adaptive);
    set(settings.showSamples, options.samples);
    if (options.waves >= 0)
        settings.waves = options.waves;
    if (options.time >= 0)
        settings.timeOfDay = options.time;
    if (options.trainU >= 0)
        m_Track.trainU =
            wrapTrackParameter(options.trainU, static_cast<float>(m_Track.points.size()));
    if (options.width > 0 && window)
        glfwSetWindowSize(window, options.width, options.height);
    // --benchmark: the train rides and frames come as fast as they can,
    // not held to the screen's refresh
    if (options.mode == AppControl::Mode::Benchmark)
    {
        settings.run = true;
        if (window)
            glfwSwapInterval(0);
    }

    bool cameraSet = true;
    switch (options.camera)
    {
    case AppControl::Camera::World:
        settings.camera = CameraView::World;
        break;
    case AppControl::Camera::Train:
        settings.camera = CameraView::Train;
        break;
    case AppControl::Camera::Chase:
        settings.camera = CameraView::Chase;
        break;
    case AppControl::Camera::Top:
        settings.camera = CameraView::Top;
        break;
    case AppControl::Camera::Fly:
        settings.camera = CameraView::Fly;
        break;
    case AppControl::Camera::Keep:
        cameraSet = false;
        break;
    }
    if (cameraSet)
        setCamera(settings.camera);
    damageMe();
    return true;
}

//************************************************************************
//
// * The texture packs to choose from
//========================================================================
void TrainWindow::listTexturePacks(const std::string &select)
{
    texturePackNames = {"Built-in"};
    texturePackPaths = {std::string()};
    bool listed = select.empty();
    for (const TexturePack::Found &pack : TexturePack::findAll())
    {
        texturePackNames.push_back(pack.name);
        texturePackPaths.push_back(pack.path);
        listed = listed || pack.path == select;
    }
    if (!listed)
    {
        // opened from elsewhere: listed by its own name
        TexturePack::Pack pack;
        texturePackNames.push_back(pack.open(select) ? pack.name() : select);
        texturePackPaths.push_back(select);
    }
}
