// Precision touchpad gestures through Windows Direct Manipulation.
//
// Windows turns a two-finger touchpad gesture into either scroll or pinch
// (Ctrl + wheel) messages, never both at once. Direct Manipulation reports
// the fingers' movement and spread together, so the camera can orbit and
// zoom in the same gesture. Without it, the wheel messages still work.
#pragma once

#include <cstdint>

struct IDirectManipulationManager;
struct IDirectManipulationUpdateManager;
struct IDirectManipulationViewport;
struct IDirectManipulationContent;
class ViewportHandler;

class TouchpadGestures
{
  public:
    // Return false to leave the gesture to the normal wheel messages.
    using Accept = bool (*)(void *user);
    // dx, dy: movement under the cursor in window pixels; scale: spread ratio.
    using Apply = void (*)(void *user, float dx, float dy, float scale);

    TouchpadGestures() = default;
    ~TouchpadGestures();
    TouchpadGestures(const TouchpadGestures &) = delete;
    TouchpadGestures &operator=(const TouchpadGestures &) = delete;

    // window is the HWND of the canvas. Safe to call again for the same window;
    // a different window detaches the old one first.
    bool attach(void *window, Accept accept, Apply apply, void *user);
    void detach();
    void *window() const
    {
        return hwnd;
    }
    std::intptr_t previousWindowProc() const
    {
        return previousProc;
    }

    // Called from the Win32 glue in TouchpadGestures.cpp.
    bool hitTest(unsigned int pointerId);
    void statusChanged(int status);
    void contentUpdated(IDirectManipulationContent *content);

  private:
    static void tick(void *data);
    static void resetLater(void *data);
    void stopTicking();
    void resetTransform();
    bool readTransform(float &scale, float &x, float &y) const;

    void *hwnd = nullptr;
    void *failedWindow = nullptr; // do not retry a window where setup failed
    std::intptr_t previousProc = 0;
    Accept accept = nullptr;
    Apply apply = nullptr;
    void *user = nullptr;
    IDirectManipulationManager *manager = nullptr;
    IDirectManipulationUpdateManager *updates = nullptr;
    IDirectManipulationViewport *viewport = nullptr;
    IDirectManipulationContent *content = nullptr;
    ViewportHandler *handler = nullptr;
    unsigned long handlerCookie = 0;
    bool comInitialized = false;
    bool ticking = false;
    bool resetting = false;
    int status = 0;
    int idleTicks = 0;
    int width = 0;
    int height = 0;
    float anchorX = 0; // the cursor, which a pinch zooms around
    float anchorY = 0;
    float lastScale = 1;
    float lastX = 0;
    float lastY = 0;
};
