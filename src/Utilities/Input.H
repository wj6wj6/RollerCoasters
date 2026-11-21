/************************************************************************
     File:        Input.H

     Comment:     Keyboard and mouse input in the program's own terms, so
                  the cameras and the view's event handling do not depend
                  on a window toolkit. The window layer (PlatformFltk.cpp
                  for now) turns its events into InputEvent.

                  The key and modifier values are the same numbers FLTK
                  uses, so key tables (EditKeys, FlyCam) read the same and
                  PlatformFltk.cpp checks that they stay equal. Letters are
                  lower case ASCII ('w'), other printable keys their ASCII
                  character, special keys the Key:: constants below.

*************************************************************************/
#pragma once

namespace Key
{
constexpr int BackSpace = 0xff08;
constexpr int Tab = 0xff09;
constexpr int Enter = 0xff0d;
constexpr int Escape = 0xff1b;
constexpr int Home = 0xff50;
constexpr int Left = 0xff51;
constexpr int Up = 0xff52;
constexpr int Right = 0xff53;
constexpr int Down = 0xff54;
constexpr int PageUp = 0xff55;
constexpr int PageDown = 0xff56;
constexpr int End = 0xff57;
constexpr int Insert = 0xff63;
// Keypad keys are Keypad + their character: Keypad + '1', Keypad + '+'.
constexpr int Keypad = 0xff80;
constexpr int KeypadEnter = 0xff8d;
// Function keys are F + n: F + 4 is F4.
constexpr int F = 0xffbd;
constexpr int ShiftL = 0xffe1;
constexpr int ShiftR = 0xffe2;
constexpr int ControlL = 0xffe3;
constexpr int ControlR = 0xffe4;
constexpr int AltL = 0xffe9;
constexpr int AltR = 0xffea;
constexpr int Delete = 0xffff;
} // namespace Key

// Bits of InputEvent::state: the modifier keys held and mouse buttons down.
namespace Mod
{
constexpr int Shift = 0x00010000;
constexpr int Ctrl = 0x00040000;
constexpr int Alt = 0x00080000;
constexpr int Button1 = 0x01000000; // left mouse button down
constexpr int Button2 = 0x02000000; // middle
constexpr int Button3 = 0x04000000; // right
} // namespace Mod

namespace Mouse
{
constexpr int Left = 1;
constexpr int Middle = 2;
constexpr int Right = 3;
} // namespace Mouse

enum class InputType
{
    None,
    Push,    // a mouse button went down: button, x, y, clicks
    Release, // a mouse button came up: button, x, y
    Drag,    // the mouse moved with a button down: x, y
    Move,    // the mouse moved with no button down: x, y
    Wheel,   // dx, dy in wheel notches: +dx right, +dy down (towards the user)
    KeyDown, // key (also repeats while held)
    KeyUp,   // key
    Focus,   // the view got the keyboard
    Unfocus, // the view lost the keyboard: held keys never send KeyUp
    Enter,   // the mouse came into the view
    Leave,   // the mouse left the view
    Hide     // the view was hidden
};

struct InputEvent
{
    InputType type = InputType::None;
    // Mouse position in window pixels from the top left, also on key events.
    int x = 0;
    int y = 0;
    int button = 0; // Mouse::Left, Middle or Right on Push and Release
    int key = 0;    // on KeyDown and KeyUp
    int state = 0;  // Mod:: bits
    float dx = 0;   // on Wheel
    float dy = 0;
    int clicks = 0; // on Push: 0 for a single click, 1 or more for a double click
};

// A window the cameras draw into: its size and a way to ask for a redraw.
class Viewport
{
  public:
    virtual int viewWidth() const = 0;
    virtual int viewHeight() const = 0;
    virtual void requestRedraw() = 0;

  protected:
    ~Viewport() = default;
};
