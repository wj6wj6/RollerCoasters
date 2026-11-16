#include "TouchpadGestures.H"

#include <windows.h>
#include <directmanipulation.h>
#include <FL/Fl.H>
#include <cstdarg>
#include <cstdio>

namespace
{
const wchar_t *const kOwnerProperty = L"RollerCoasters.TouchpadGestures";
// The content is far larger than the window so one gesture never reaches an
// edge; the transform returns to the middle after every gesture.
const float kContentSize = 40000;
const double kTickSeconds = 1.0 / 60.0;
} // namespace

// Diagnostics on the console: whether touchpad gestures reach Direct
// Manipulation, and what each gesture did.
void touchpadLog(const char *format, ...)
{
    std::va_list arguments;
    va_start(arguments, format);
    std::fputs("[touchpad] ", stdout);
    std::vprintf(format, arguments);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    va_end(arguments);
}

static bool setupFailed(TouchpadGestures &gestures, const char *step, HRESULT result)
{
    touchpadLog("Direct Manipulation setup failed at %s (HRESULT 0x%08lx); using wheel messages",
                step, (unsigned long)result);
    gestures.detach();
    return false;
}

class ViewportHandler final : public IDirectManipulationViewportEventHandler
{
  public:
    explicit ViewportHandler(TouchpadGestures &gestures) : owner(&gestures)
    {
    }
    void disconnect()
    {
        owner = nullptr;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void **object) override
    {
        if (!object)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirectManipulationViewportEventHandler))
        {
            *object = static_cast<IDirectManipulationViewportEventHandler *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++references;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG left = --references;
        if (left == 0)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE OnViewportStatusChanged(IDirectManipulationViewport *,
                                                      DIRECTMANIPULATION_STATUS current,
                                                      DIRECTMANIPULATION_STATUS) override
    {
        if (owner)
            owner->statusChanged((int)current);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnViewportUpdated(IDirectManipulationViewport *) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnContentUpdated(IDirectManipulationViewport *,
                                               IDirectManipulationContent *updated) override
    {
        if (owner && updated)
            owner->contentUpdated(updated);
        return S_OK;
    }

  private:
    ~ViewportHandler() = default;

    TouchpadGestures *owner;
    ULONG references = 1;
};

// Touchpad input starts with DM_POINTERHITTEST; claiming its pointer hands the
// gesture to Direct Manipulation instead of the scroll/pinch wheel messages.
static LRESULT CALLBACK touchpadWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    TouchpadGestures *gestures = static_cast<TouchpadGestures *>(GetPropW(window, kOwnerProperty));
    const WNDPROC previous =
        gestures ? reinterpret_cast<WNDPROC>(gestures->previousWindowProc()) : nullptr;
    if (!previous)
        return DefWindowProcW(window, message, wParam, lParam);
    if (message == DM_POINTERHITTEST && gestures->hitTest(GET_POINTERID_WPARAM(wParam)))
        return 0;
    if (message == WM_NCDESTROY)
    {
        const LRESULT result = CallWindowProcW(previous, window, message, wParam, lParam);
        gestures->detach();
        return result;
    }
    return CallWindowProcW(previous, window, message, wParam, lParam);
}

TouchpadGestures::~TouchpadGestures()
{
    detach();
}

bool TouchpadGestures::attach(void *window, Accept acceptFn, Apply applyFn, void *userData)
{
    if (!window || window == failedWindow)
        return false;
    if (window == hwnd)
        return true;
    detach();
    failedWindow = window;
    HWND target = static_cast<HWND>(window);
    accept = acceptFn;
    apply = applyFn;
    user = userData;

    comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    // Without Direct Manipulation the wheel messages keep working as before.
    HRESULT result = CoCreateInstance(__uuidof(DirectManipulationManager), nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager));
    if (FAILED(result))
        return setupFailed(*this, "CoCreateInstance", result);
    result = manager->GetUpdateManager(IID_PPV_ARGS(&updates));
    if (FAILED(result))
        return setupFailed(*this, "GetUpdateManager", result);
    result = manager->CreateViewport(nullptr, target, IID_PPV_ARGS(&viewport));
    if (FAILED(result))
        return setupFailed(*this, "CreateViewport", result);
    // Pan and pinch together; only the pan keeps moving after the fingers lift.
    const DIRECTMANIPULATION_CONFIGURATION configuration =
        static_cast<DIRECTMANIPULATION_CONFIGURATION>(
            DIRECTMANIPULATION_CONFIGURATION_INTERACTION |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA |
            DIRECTMANIPULATION_CONFIGURATION_SCALING);
    handler = new ViewportHandler(*this);
    DWORD cookie = 0;
    result = viewport->ActivateConfiguration(configuration);
    if (FAILED(result))
        return setupFailed(*this, "ActivateConfiguration", result);
    result = viewport->SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE);
    if (FAILED(result))
        return setupFailed(*this, "SetViewportOptions", result);
    result = viewport->AddEventHandler(target, handler, &cookie);
    if (FAILED(result))
        return setupFailed(*this, "AddEventHandler", result);
    result = viewport->GetPrimaryContent(IID_PPV_ARGS(&content));
    if (FAILED(result))
        return setupFailed(*this, "GetPrimaryContent", result);
    handlerCookie = cookie;
    IDirectManipulationPrimaryContent *primary = nullptr;
    if (SUCCEEDED(viewport->GetPrimaryContent(IID_PPV_ARGS(&primary))) && primary)
    {
        primary->SetZoomBoundaries(.1f, 10.0f);
        primary->Release();
    }
    const RECT contentRect = {0, 0, (LONG)kContentSize, (LONG)kContentSize};
    content->SetContentRect(&contentRect);

    SetPropW(target, kOwnerProperty, this);
    previousProc =
        SetWindowLongPtrW(target, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&touchpadWindowProc));
    hwnd = window;
    result = manager->Activate(target);
    if (FAILED(result))
        return setupFailed(*this, "Activate", result);
    result = viewport->Enable();
    if (FAILED(result))
        return setupFailed(*this, "Enable", result);
    resetLater(this);
    failedWindow = nullptr;
    touchpadLog("Direct Manipulation ready; two-finger gestures should start with a hit test");
    return true;
}

void TouchpadGestures::detach()
{
    stopTicking();
    Fl::remove_timeout(resetLater, this);
    HWND target = static_cast<HWND>(hwnd);
    if (target)
    {
        if (previousProc && GetWindowLongPtrW(target, GWLP_WNDPROC) ==
                                reinterpret_cast<LONG_PTR>(&touchpadWindowProc))
            SetWindowLongPtrW(target, GWLP_WNDPROC, previousProc);
        RemovePropW(target, kOwnerProperty);
    }
    if (viewport)
    {
        viewport->Stop();
        if (handlerCookie)
            viewport->RemoveEventHandler(handlerCookie);
        viewport->Abandon();
    }
    if (manager && target)
        manager->Deactivate(target);
    if (handler)
    {
        handler->disconnect();
        handler->Release();
    }
    if (content)
        content->Release();
    if (viewport)
        viewport->Release();
    if (updates)
        updates->Release();
    if (manager)
        manager->Release();
    if (comInitialized)
        CoUninitialize();
    handler = nullptr;
    content = nullptr;
    viewport = nullptr;
    updates = nullptr;
    manager = nullptr;
    comInitialized = false;
    handlerCookie = 0;
    previousProc = 0;
    hwnd = nullptr;
    resetting = false;
    status = 0;
}

bool TouchpadGestures::readTransform(float &scale, float &x, float &y) const
{
    float matrix[6] = {1, 0, 0, 1, 0, 0};
    if (!content || FAILED(content->GetContentTransform(matrix, 6)) || matrix[0] <= 0)
        return false;
    scale = matrix[0];
    x = matrix[4];
    y = matrix[5];
    return true;
}

bool TouchpadGestures::hitTest(unsigned int pointerId)
{
    POINTER_INPUT_TYPE type = PT_POINTER;
    const BOOL known = GetPointerType(pointerId, &type);
    touchpadLog("hit test: pointer %u, type %lu%s", pointerId, (unsigned long)type,
                known ? "" : " (unknown)");
    if (!viewport || !known || type != PT_TOUCHPAD)
        return false;
    if (accept && !accept(user))
    {
        touchpadLog("not World view; leaving the gesture to the wheel messages");
        return false;
    }
    HWND target = static_cast<HWND>(hwnd);
    // A gesture that starts right after the last one must not race its reset.
    if (Fl::has_timeout(resetLater, this))
    {
        Fl::remove_timeout(resetLater, this);
        resetLater(this);
    }
    // A pinch zooms around the cursor, which stays still during the gesture.
    POINT cursor = {0, 0};
    GetCursorPos(&cursor);
    ScreenToClient(target, &cursor);
    anchorX = (float)cursor.x;
    anchorY = (float)cursor.y;
    const HRESULT contact = viewport->SetContact(pointerId);
    if (FAILED(contact))
    {
        touchpadLog("SetContact failed (HRESULT 0x%08lx)", (unsigned long)contact);
        return false;
    }
    gestureUpdates = 0;
    gestureMoveX = gestureMoveY = 0;
    gestureZoom = 1;
    resetting = false;
    if (!readTransform(lastScale, lastX, lastY))
    {
        lastScale = 1;
        lastX = lastY = 0;
    }
    idleTicks = 0;
    if (!ticking)
    {
        ticking = true;
        Fl::add_timeout(kTickSeconds, tick, this);
    }
    return true;
}

void TouchpadGestures::statusChanged(int current)
{
    if (!resetting)
        touchpadLog("status %d -> %d", status, current);
    if (current == DIRECTMANIPULATION_READY && !resetting && gestureUpdates > 0)
        touchpadLog("gesture: %d updates, moved %.0f, %.0f px, zoom x%.2f", gestureUpdates,
                    gestureMoveX, gestureMoveY, gestureZoom);
    status = current;
    if (current == DIRECTMANIPULATION_READY && !resetting)
    {
        stopTicking();
        // This runs inside Update(); reset once it has returned.
        resetting = true;
        Fl::add_timeout(0, resetLater, this);
    }
}

void TouchpadGestures::contentUpdated(IDirectManipulationContent *updated)
{
    float matrix[6] = {1, 0, 0, 1, 0, 0};
    if (FAILED(updated->GetContentTransform(matrix, 6)) || matrix[0] <= 0)
        return;
    const float scale = matrix[0];
    const float x = matrix[4];
    const float y = matrix[5];
    // Only a running gesture or its inertia moves the camera; anything else,
    // such as the reset between gestures, just becomes the new reference.
    if (resetting || !ticking ||
        (status != DIRECTMANIPULATION_RUNNING && status != DIRECTMANIPULATION_INERTIA))
    {
        lastScale = scale;
        lastX = x;
        lastY = y;
        return;
    }
    // Follow the content point under the cursor: a pure pinch keeps it still,
    // so only the fingers' shared movement turns the camera.
    const float pointX = (anchorX - lastX) / lastScale;
    const float pointY = (anchorY - lastY) / lastScale;
    const float dx = scale * pointX + x - anchorX;
    const float dy = scale * pointY + y - anchorY;
    const float ratio = scale / lastScale;
    lastScale = scale;
    lastX = x;
    lastY = y;
    ++gestureUpdates;
    gestureMoveX += dx;
    gestureMoveY += dy;
    gestureZoom *= ratio;
    if (apply && (dx != 0 || dy != 0 || ratio != 1))
        apply(user, dx, dy, ratio);
}

void TouchpadGestures::tick(void *data)
{
    TouchpadGestures *gestures = static_cast<TouchpadGestures *>(data);
    if (gestures->updates)
        gestures->updates->Update(nullptr);
    if (!gestures->ticking)
        return;
    // Stop polling if Direct Manipulation never starts or ends a gesture.
    if (gestures->status == DIRECTMANIPULATION_RUNNING ||
        gestures->status == DIRECTMANIPULATION_INERTIA)
        gestures->idleTicks = 0;
    else if (++gestures->idleTicks > 60)
    {
        gestures->stopTicking();
        return;
    }
    Fl::repeat_timeout(kTickSeconds, tick, data);
}

void TouchpadGestures::stopTicking()
{
    if (ticking)
        Fl::remove_timeout(tick, this);
    ticking = false;
}

void TouchpadGestures::resetLater(void *data)
{
    TouchpadGestures *gestures = static_cast<TouchpadGestures *>(data);
    if (!gestures->viewport || !gestures->updates)
        return;
    // Match the viewport to the canvas so its coordinates are client pixels.
    RECT client = {0, 0, 0, 0};
    GetClientRect(static_cast<HWND>(gestures->hwnd), &client);
    if (client.right != gestures->width || client.bottom != gestures->height)
    {
        gestures->width = client.right;
        gestures->height = client.bottom;
        gestures->viewport->SetViewportRect(&client);
    }
    gestures->resetTransform();
    // Deliver the reset now so it cannot land inside the next gesture.
    gestures->updates->Update(nullptr);
}

void TouchpadGestures::resetTransform()
{
    resetting = true;
    if (!viewport || width <= 0 || height <= 0)
        return;
    const float left = (kContentSize - (float)width) * .5f;
    const float top = (kContentSize - (float)height) * .5f;
    viewport->ZoomToRect(left, top, left + (float)width, top + (float)height, FALSE);
}
