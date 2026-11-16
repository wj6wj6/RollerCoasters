#include "TouchpadGestures.H"
#include "Utilities/TwoFingerGesture.H"

#include <windows.h>
#include <directmanipulation.h>
#include <hidsdi.h>
#include <FL/Fl.H>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace
{
const wchar_t *const kOwnerProperty = L"RollerCoasters.TouchpadGestures";
// The content is far larger than the window so one gesture never reaches an
// edge; the transform returns to the middle after every gesture.
const float kContentSize = 40000;
const double kTickSeconds = 1.0 / 60.0;
} // namespace

// Without Direct Manipulation the wheel messages keep working as before.
static bool setupFailed(TouchpadGestures &gestures)
{
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

struct RawTouchpad
{
    struct Contact
    {
        ULONG id;
        float x, y; // physical units
    };
    // Report layout of the current device.
    HANDLE device = nullptr;
    bool layoutOk = false;
    std::vector<BYTE> preparsed;
    std::vector<USHORT> fingerLinks;
    USHORT countLink = 0;
    float unitX = 1, unitY = 1; // physical units per logical unit
    float padWidth = 1;         // physical width of the pad
    std::vector<BYTE> buffer;
    // The frame being collected; hybrid touchpads split one over reports.
    ULONG expected = 0;
    ULONG received = 0;
    bool button = false;
    std::vector<Contact> contacts;
    // The two-finger gesture in progress.
    bool works = false; // a raw gesture has moved the camera
    bool tracking = false;
    ULONG idA = 0, idB = 0;
    TwoFingerGesture filter; // jitter, thresholds and which motion dominates
    std::chrono::steady_clock::time_point lastFrame;
    float velocityX = 0, velocityY = 0; // window pixels per second
};

static float physicalPerLogical(const HIDP_VALUE_CAPS &value, float &physicalSize)
{
    float logical = (float)value.LogicalMax - (float)value.LogicalMin;
    if (logical <= 0 && value.BitSize > 0 && value.BitSize < 32)
        logical = (float)((1UL << value.BitSize) - 1);
    const float physical = (float)value.PhysicalMax - (float)value.PhysicalMin;
    if (logical <= 0)
        logical = 1;
    if (physical > 0)
    {
        physicalSize = physical;
        return physical / logical;
    }
    physicalSize = logical;
    return 1;
}

static bool loadLayout(RawTouchpad &raw, HANDLE device)
{
    raw.device = device;
    raw.layoutOk = false;
    raw.fingerLinks.clear();
    raw.expected = raw.received = 0;
    raw.contacts.clear();
    UINT size = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &size) != 0 || size == 0)
        return false;
    raw.preparsed.assign(size, 0);
    if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, raw.preparsed.data(), &size) == (UINT)-1)
        return false;
    PHIDP_PREPARSED_DATA data = reinterpret_cast<PHIDP_PREPARSED_DATA>(raw.preparsed.data());
    HIDP_CAPS caps = {};
    if (HidP_GetCaps(data, &caps) != HIDP_STATUS_SUCCESS || caps.NumberInputValueCaps == 0)
        return false;
    USHORT count = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> values(count);
    if (HidP_GetValueCaps(HidP_Input, values.data(), &count, data) != HIDP_STATUS_SUCCESS)
        return false;
    bool hasCount = false;
    for (USHORT i = 0; i < count && i < values.size(); ++i)
    {
        const HIDP_VALUE_CAPS &value = values[i];
        const USAGE usage = value.IsRange ? value.Range.UsageMin : value.NotRange.Usage;
        if (value.UsagePage == 0x0D && usage == 0x54) // contact count
        {
            raw.countLink = value.LinkCollection;
            hasCount = true;
        }
        else if (value.UsagePage == 0x01 && usage == 0x30) // X of one finger
        {
            raw.fingerLinks.push_back(value.LinkCollection);
            raw.unitX = physicalPerLogical(value, raw.padWidth);
        }
        else if (value.UsagePage == 0x01 && usage == 0x31) // Y of one finger
        {
            float height = 0;
            raw.unitY = physicalPerLogical(value, height);
        }
    }
    raw.layoutOk = hasCount && !raw.fingerLinks.empty() && raw.padWidth > 0;
    return raw.layoutOk;
}

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
    // WM_INPUT must still reach DefWindowProc, which frees the input.
    if (message == WM_INPUT)
        gestures->rawInput(reinterpret_cast<void *>(lParam));
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
        return setupFailed(*this);
    result = manager->GetUpdateManager(IID_PPV_ARGS(&updates));
    if (FAILED(result))
        return setupFailed(*this);
    result = manager->CreateViewport(nullptr, target, IID_PPV_ARGS(&viewport));
    if (FAILED(result))
        return setupFailed(*this);
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
        return setupFailed(*this);
    result = viewport->SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE);
    if (FAILED(result))
        return setupFailed(*this);
    result = viewport->AddEventHandler(target, handler, &cookie);
    if (FAILED(result))
        return setupFailed(*this);
    result = viewport->GetPrimaryContent(IID_PPV_ARGS(&content));
    if (FAILED(result))
        return setupFailed(*this);
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
        return setupFailed(*this);
    result = viewport->Enable();
    if (FAILED(result))
        return setupFailed(*this);
    resetLater(this);
    failedWindow = nullptr;

    // Raw precision touchpad reports (digitizer page, touch pad usage).
    RAWINPUTDEVICE device = {0x0D, 0x05, 0, target};
    // Without it, Direct Manipulation's pan-or-pinch output is used instead.
    if (RegisterRawInputDevices(&device, 1, sizeof(device)))
        raw = new RawTouchpad();
    return true;
}

void TouchpadGestures::detach()
{
    stopTicking();
    Fl::remove_timeout(resetLater, this);
    Fl::remove_timeout(coast, this);
    if (raw)
    {
        RAWINPUTDEVICE device = {0x0D, 0x05, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
        delete raw;
        raw = nullptr;
    }
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
    if (!viewport || !GetPointerType(pointerId, &type) || type != PT_TOUCHPAD ||
        (accept && !accept(user)))
        return false;
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
    if (FAILED(viewport->SetContact(pointerId)))
        return false;
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
    // Raw reports already move the camera; this gesture only keeps Windows
    // from also sending wheel messages.
    if (raw && raw->works)
        return;
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

void TouchpadGestures::rawInput(void *rawInputHandle)
{
    if (!raw)
        return;
    HRAWINPUT handle = static_cast<HRAWINPUT>(rawInputHandle);
    UINT size = 0;
    if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
        size < sizeof(RAWINPUTHEADER))
        return;
    raw->buffer.assign(size, 0);
    if (GetRawInputData(handle, RID_INPUT, raw->buffer.data(), &size, sizeof(RAWINPUTHEADER)) !=
        size)
        return;
    const RAWINPUT *input = reinterpret_cast<const RAWINPUT *>(raw->buffer.data());
    if (input->header.dwType != RIM_TYPEHID)
        return;
    if (input->header.hDevice != raw->device)
        loadLayout(*raw, input->header.hDevice);
    if (!raw->layoutOk)
        return;
    const RAWHID &hid = input->data.hid;
    const size_t available = size - offsetof(RAWINPUT, data.hid.bRawData);
    if ((size_t)hid.dwSizeHid * hid.dwCount > available)
        return;
    for (DWORD i = 0; i < hid.dwCount; ++i)
        readReport(reinterpret_cast<char *>(raw->buffer.data() +
                                            offsetof(RAWINPUT, data.hid.bRawData) +
                                            (size_t)i * hid.dwSizeHid),
                   hid.dwSizeHid);
}

void TouchpadGestures::readReport(char *report, unsigned long length)
{
    PHIDP_PREPARSED_DATA data = reinterpret_cast<PHIDP_PREPARSED_DATA>(raw->preparsed.data());
    ULONG count = 0;
    // A nonzero contact count starts a frame; hybrid touchpads send the rest
    // of its fingers in following reports with a count of zero.
    if (HidP_GetUsageValue(HidP_Input, 0x0D, raw->countLink, 0x54, &count, data, report, length) ==
            HIDP_STATUS_SUCCESS &&
        count > 0)
    {
        raw->expected = count;
        raw->received = 0;
        raw->contacts.clear();
        USAGE buttons[8] = {};
        ULONG pressed = 8;
        raw->button = HidP_GetUsages(HidP_Input, 0x09, 0, buttons, &pressed, data, report,
                                     length) == HIDP_STATUS_SUCCESS &&
                      pressed > 0;
    }
    if (raw->expected == 0)
        return;
    for (USHORT link : raw->fingerLinks)
    {
        if (raw->received >= raw->expected)
            break;
        ULONG id = 0, x = 0, y = 0;
        if (HidP_GetUsageValue(HidP_Input, 0x0D, link, 0x51, &id, data, report, length) !=
                HIDP_STATUS_SUCCESS ||
            HidP_GetUsageValue(HidP_Input, 0x01, link, 0x30, &x, data, report, length) !=
                HIDP_STATUS_SUCCESS ||
            HidP_GetUsageValue(HidP_Input, 0x01, link, 0x31, &y, data, report, length) !=
                HIDP_STATUS_SUCCESS)
            continue;
        ++raw->received;
        USAGE usages[16] = {};
        ULONG active = 16;
        bool touching = false;
        if (HidP_GetUsages(HidP_Input, 0x0D, link, usages, &active, data, report, length) ==
            HIDP_STATUS_SUCCESS)
            for (ULONG i = 0; i < active && i < 16; ++i)
                touching = touching || usages[i] == 0x42; // tip switch
        if (touching)
            raw->contacts.push_back({id, (float)x * raw->unitX, (float)y * raw->unitY});
    }
    if (raw->received >= raw->expected)
    {
        raw->expected = 0;
        rawFrame();
    }
}

bool TouchpadGestures::rawStartAllowed() const
{
    if (accept && !accept(user))
        return false;
    HWND target = static_cast<HWND>(hwnd);
    POINT cursor = {0, 0};
    RECT client = {0, 0, 0, 0};
    return GetCursorPos(&cursor) && ScreenToClient(target, &cursor) &&
           GetClientRect(target, &client) && PtInRect(&client, cursor);
}

void TouchpadGestures::rawFrame()
{
    // Exactly two resting fingers and no click: anything else is not ours.
    if (raw->contacts.size() != 2 || raw->button)
    {
        if (raw->tracking)
            endRawGesture();
        return;
    }
    RawTouchpad::Contact a = raw->contacts[0];
    RawTouchpad::Contact b = raw->contacts[1];
    if (b.id < a.id)
        std::swap(a, b);
    // In touchpad widths, the same scale on both axes.
    const float centerX = (a.x + b.x) * .5f / raw->padWidth;
    const float centerY = (a.y + b.y) * .5f / raw->padWidth;
    const float spread = std::hypot(a.x - b.x, a.y - b.y) / raw->padWidth;
    const auto now = std::chrono::steady_clock::now();
    if (!raw->tracking || a.id != raw->idA || b.id != raw->idB)
    {
        if (raw->tracking)
            endRawGesture();
        if (!rawStartAllowed())
            return;
        Fl::remove_timeout(coast, this);
        raw->tracking = true;
        raw->idA = a.id;
        raw->idB = b.id;
        raw->filter.start(centerX, centerY, spread);
        raw->lastFrame = now;
        raw->velocityX = raw->velocityY = 0;
        return;
    }
    const float seconds = std::chrono::duration<float>(now - raw->lastFrame).count();
    raw->lastFrame = now;
    float moveX = 0, moveY = 0, ratio = 1;
    raw->filter.update(centerX, centerY, spread, seconds, moveX, moveY, ratio);
    // A full pad-width swipe turns like dragging across the whole canvas.
    RECT client = {0, 0, 0, 0};
    GetClientRect(static_cast<HWND>(hwnd), &client);
    const float dx = moveX * (float)client.right;
    const float dy = moveY * (float)client.bottom;
    if (seconds > .002f)
    {
        raw->velocityX = raw->velocityX * .6f + dx / seconds * .4f;
        raw->velocityY = raw->velocityY * .6f + dy / seconds * .4f;
    }
    raw->works = true;
    if (apply && (dx != 0 || dy != 0 || ratio != 1))
        apply(user, dx, dy, ratio);
}

void TouchpadGestures::endRawGesture()
{
    raw->tracking = false;
    // Keep turning briefly after a flick, like the system's scroll inertia.
    const float idle =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - raw->lastFrame).count();
    if (inertia && idle < .05f && std::hypot(raw->velocityX, raw->velocityY) > 150)
    {
        raw->lastFrame = std::chrono::steady_clock::now();
        Fl::add_timeout(kTickSeconds, coast, this);
    }
}

void TouchpadGestures::coast(void *data)
{
    TouchpadGestures *gestures = static_cast<TouchpadGestures *>(data);
    RawTouchpad *raw = gestures->raw;
    if (!raw || raw->tracking)
        return;
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - raw->lastFrame).count();
    raw->lastFrame = now;
    if (gestures->apply)
        gestures->apply(gestures->user, raw->velocityX * seconds, raw->velocityY * seconds, 1);
    const float decay = std::exp(-seconds / .25f);
    raw->velocityX *= decay;
    raw->velocityY *= decay;
    if (std::hypot(raw->velocityX, raw->velocityY) > 20)
        Fl::repeat_timeout(kTickSeconds, coast, data);
}
