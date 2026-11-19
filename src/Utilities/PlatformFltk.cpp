/************************************************************************
     File:        PlatformFltk.cpp

     Comment:     Platform.H and PlatformFltk.H on top of FLTK.

*************************************************************************/

#include "Platform.H"
#include "PlatformFltk.H"

#pragma warning(push)
#pragma warning(disable : 4311) // convert void* to long
#pragma warning(disable : 4312) // convert long to void*
#include <FL/Fl.H>
#pragma warning(pop)

// Input.H uses FLTK's numbers, so key tables written either way agree.
static_assert(Key::BackSpace == FL_BackSpace && Key::Tab == FL_Tab && Key::Enter == FL_Enter &&
                  Key::Escape == FL_Escape && Key::Home == FL_Home && Key::Left == FL_Left &&
                  Key::Up == FL_Up && Key::Right == FL_Right && Key::Down == FL_Down &&
                  Key::PageUp == FL_Page_Up && Key::PageDown == FL_Page_Down &&
                  Key::End == FL_End && Key::Insert == FL_Insert && Key::Keypad == FL_KP &&
                  Key::KeypadEnter == FL_KP_Enter && Key::F == FL_F && Key::ShiftL == FL_Shift_L &&
                  Key::ShiftR == FL_Shift_R && Key::ControlL == FL_Control_L &&
                  Key::ControlR == FL_Control_R && Key::AltL == FL_Alt_L && Key::AltR == FL_Alt_R &&
                  Key::Delete == FL_Delete,
              "Key:: values must match FLTK's");
static_assert(Mod::Shift == FL_SHIFT && Mod::Ctrl == FL_CTRL && Mod::Alt == FL_ALT &&
                  Mod::Button1 == FL_BUTTON1 && Mod::Button2 == FL_BUTTON2 &&
                  Mod::Button3 == FL_BUTTON3,
              "Mod:: values must match FLTK's");
static_assert(Mouse::Left == FL_LEFT_MOUSE && Mouse::Middle == FL_MIDDLE_MOUSE &&
                  Mouse::Right == FL_RIGHT_MOUSE,
              "Mouse:: values must match FLTK's");

void platform::addTimeout(double seconds, TimerCallback callback, void *data)
{
    Fl::add_timeout(seconds, callback, data);
}

void platform::repeatTimeout(double seconds, TimerCallback callback, void *data)
{
    Fl::repeat_timeout(seconds, callback, data);
}

void platform::removeTimeout(TimerCallback callback, void *data)
{
    Fl::remove_timeout(callback, data);
}

bool platform::hasTimeout(TimerCallback callback, void *data)
{
    return Fl::has_timeout(callback, data) != 0;
}

void platform::processPending()
{
    Fl::check();
}

void platform::wait(double seconds)
{
    Fl::wait(seconds);
}

void platform::forgetClick()
{
    Fl::event_is_click(0);
}

InputEvent platform::fltkEvent(int flEvent)
{
    InputEvent e;
    switch (flEvent)
    {
    case FL_PUSH:
        e.type = InputType::Push;
        break;
    case FL_RELEASE:
        e.type = InputType::Release;
        break;
    case FL_DRAG:
        e.type = InputType::Drag;
        break;
    case FL_MOVE:
        e.type = InputType::Move;
        break;
    case FL_MOUSEWHEEL:
        e.type = InputType::Wheel;
        break;
    case FL_KEYDOWN: // also FL_KEYBOARD
        e.type = InputType::KeyDown;
        break;
    case FL_KEYUP:
        e.type = InputType::KeyUp;
        break;
    case FL_FOCUS:
        e.type = InputType::Focus;
        break;
    case FL_UNFOCUS:
        e.type = InputType::Unfocus;
        break;
    case FL_ENTER:
        e.type = InputType::Enter;
        break;
    case FL_LEAVE:
        e.type = InputType::Leave;
        break;
    case FL_HIDE:
        e.type = InputType::Hide;
        break;
    default:
        return e;
    }
    // FLTK keeps the last value of each field for every event, and the old
    // handlers read them that way (the mouse position on a key press), so copy all.
    e.x = Fl::event_x();
    e.y = Fl::event_y();
    e.button = Fl::event_button();
    e.key = Fl::event_key();
    e.state = Fl::event_state();
    e.dx = static_cast<float>(Fl::event_dx());
    e.dy = static_cast<float>(Fl::event_dy());
    e.clicks = Fl::event_clicks();
    return e;
}
