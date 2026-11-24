/************************************************************************
     File:        WindowsKey.cpp

     Comment:     See WindowsKey.h.

*************************************************************************/
#include "WindowsKey.h"

#include <atomic>
#include <future>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
// Set by the loop, read by the hook's thread.
std::atomic<HWND> g_window{nullptr};
std::atomic<bool> g_playing{false};
// The hook's own thread; the gate is only touched there.
std::thread g_thread;
DWORD g_threadId = 0;
WindowsKeyGate g_gate;

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
            const HWND window = g_window.load();
            const bool playing = g_playing.load() && window && GetForegroundWindow() == window;
            if (g_gate.swallow(side, down, playing))
                return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// A low-level hook is called on the thread that put it in, while that thread
// waits for messages. The game's loop is often busy (a frame, a stall while a
// world builds), and Windows passes a key on, and after a few such waits
// quietly takes the hook out for good, when its thread does not answer in
// time: so the hook gets a thread that does nothing else.
void hookThread(std::promise<DWORD> started)
{
    MSG msg;
    // makes this thread's message queue, so WM_QUIT can be posted to it
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    const HHOOK hook =
        SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    started.set_value(hook ? GetCurrentThreadId() : 0);
    if (!hook)
        return;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(hook);
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
    // Started the first time the game is played with the key locked, then
    // kept: on its own thread it answers at once whatever the loop does.
    if (playing && !g_thread.joinable())
    {
        std::promise<DWORD> started;
        std::future<DWORD> id = started.get_future();
        g_thread = std::thread(hookThread, std::move(started));
        g_threadId = id.get();
        if (g_threadId == 0) // no hook: the key stays as it is
        {
            g_thread.join();
            g_thread = std::thread();
        }
    }
}

void stop()
{
    g_playing = false;
    g_window = nullptr;
    if (g_thread.joinable())
    {
        PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
        g_thread.join();
    }
    g_thread = std::thread();
    g_threadId = 0;
}
} // namespace WindowsKeyHook
