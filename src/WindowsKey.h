/************************************************************************
     File:        WindowsKey.h

     Comment:     The Windows key while playing. Its press opens Start and
                  takes the focus from the game (a fullscreen game used to
                  go down to the taskbar), so, as many games do, it does
                  nothing while the game is in front and being played: in
                  the world with no menu open. In the menus, on the title
                  and with another window in front it works as usual.

                  WindowsKeyGate decides each press and release; the hook
                  (WindowsKeyHook, Windows only) feeds it the low-level
                  keyboard events.

*************************************************************************/
#pragma once

// Which press and release of the left and right Windows keys to keep from
// the system. A release is kept exactly when its press was, so the system
// never sees a key go up that it did not see go down (or the reverse, which
// would leave it thinking the key is held).
class WindowsKeyGate
{
  public:
    enum class Side
    {
        Left,
        Right
    };

    // True when the event is to be swallowed. `playing`: the game window
    // is in front and nothing but the world is shown.
    bool swallow(Side side, bool down, bool playing)
    {
        bool &held = side == Side::Left ? m_left : m_right;
        if (!down)
        {
            const bool kept = held;
            held = false;
            return kept;
        }
        // key repeat of a kept press stays kept, even if a menu opened meanwhile
        if (held || playing)
        {
            held = true;
            return true;
        }
        return false;
    }

    // A kept press waits for its release: the hook must stay until then.
    bool holding() const
    {
        return m_left || m_right;
    }

  private:
    bool m_left = false;
    bool m_right = false;
};

#ifdef _WIN32
struct HWND__;

// The low-level keyboard hook around the gate. update() is called by the
// loop with whether the game is being played; the hook is in the system
// only while that is so (or a kept press waits for its release), because
// it sees every key pressed anywhere and a busy loop would slow typing in
// other programs.
namespace WindowsKeyHook
{
// The game window: a press counts as playing only while it is in front.
void setWindow(HWND__ *window);
void update(bool playing);
// Takes the hook out (the window closes).
void stop();
} // namespace WindowsKeyHook
#endif
