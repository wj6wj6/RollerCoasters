// Precision touchpad gestures that orbit and zoom at the same time.
//
// Windows decides per gesture whether two fingers scroll or pinch: the wheel
// messages and even Direct Manipulation report one or the other, never both.
// So the finger positions are read from the touchpad's raw HID reports, and
// the fingers' shared movement and their spread are applied together.
// Direct Manipulation still claims the gesture so Windows sends no wheel
// messages for it, and its output is the fallback when raw reports are
// unusable. Without either, the wheel messages still work.
#pragma once

#include <cstdint>

struct IDirectManipulationManager;
struct IDirectManipulationUpdateManager;
struct IDirectManipulationViewport;
struct IDirectManipulationContent;
class ViewportHandler;
struct RawTouchpad;

// Prints one "[touchpad] ..." line to the console.
void touchpadLog(const char *format, ...);

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
    void rawInput(void *rawInputHandle);

  private:
    static void tick(void *data);
    static void resetLater(void *data);
    void stopTicking();
    void resetTransform();
    bool readTransform(float &scale, float &x, float &y) const;
    void readReport(char *report, unsigned long length);
    void rawFrame();
    void endRawGesture();
    bool rawStartAllowed() const;
    static void coast(void *data);

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
    RawTouchpad *raw = nullptr;
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
    int gestureUpdates = 0; // diagnostics for the console log
    float gestureMoveX = 0;
    float gestureMoveY = 0;
    float gestureZoom = 1;
};
