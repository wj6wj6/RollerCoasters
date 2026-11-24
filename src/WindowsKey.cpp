/************************************************************************
     File:        WindowsKey.cpp

     Comment:     See WindowsKey.h.

*************************************************************************/
#include "WindowsKey.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
HHOOK g_hook = nullptr;
HWND g_window = nullptr;
bool g_playing = false;
WindowsKeyGate g_gate;

// Runs on the thread that put the hook in (the loop's), while it takes
// messages: the window and the gate are only touched there.
LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION)
    {
        const KBDLLHOOKSTRUCT *key = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (key->vkCode == VK_LWIN || key->vkCode == VK_RWIN)
        {
            const WindowsKeyGate::Side side =
                key->vkCode == VK_LWIN ? WindowsKeyGate::Side::Left : WindowsKeyGate::Side::Right;
            const bool down = (key->flags & LLKHF_UP) == 0;
            const bool playing = g_playing && g_window && GetForegroundWindow() == g_window;
            if (g_gate.swallow(side, down, playing))
                return 1;
        }
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}
} // namespace

namespace WindowsKeyHook
{
void setWindow(HWND__ *window)
{
    g_window = window;
}

void update(bool playing)
{
    g_playing = playing;
    const bool wanted = playing || g_gate.holding();
    if (wanted && !g_hook)
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    else if (!wanted && g_hook)
    {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
}

void stop()
{
    if (g_hook)
        UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
    g_playing = false;
    g_window = nullptr;
    g_gate = WindowsKeyGate();
}
} // namespace WindowsKeyHook
