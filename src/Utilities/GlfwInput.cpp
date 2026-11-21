/************************************************************************
     File:        GlfwInput.cpp

     Comment:     See GlfwInput.H.

*************************************************************************/

#include "GlfwInput.H"
#include "Platform.H"

#include <cmath>
#include <cstdlib>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

int GlfwInput::keyFromGlfw(int k)
{
    // letters as lower case, like FLTK's event_key()
    if (k >= GLFW_KEY_A && k <= GLFW_KEY_Z)
        return 'a' + (k - GLFW_KEY_A);
    // the other printable keys are their unshifted ASCII character
    if (k >= GLFW_KEY_SPACE && k <= GLFW_KEY_GRAVE_ACCENT)
        return k;
    if (k >= GLFW_KEY_F1 && k <= GLFW_KEY_F25)
        return Key::F + 1 + (k - GLFW_KEY_F1);
    if (k >= GLFW_KEY_KP_0 && k <= GLFW_KEY_KP_9)
        return Key::Keypad + '0' + (k - GLFW_KEY_KP_0);
    switch (k)
    {
    case GLFW_KEY_ESCAPE:
        return Key::Escape;
    case GLFW_KEY_ENTER:
        return Key::Enter;
    case GLFW_KEY_TAB:
        return Key::Tab;
    case GLFW_KEY_BACKSPACE:
        return Key::BackSpace;
    case GLFW_KEY_INSERT:
        return Key::Insert;
    case GLFW_KEY_DELETE:
        return Key::Delete;
    case GLFW_KEY_RIGHT:
        return Key::Right;
    case GLFW_KEY_LEFT:
        return Key::Left;
    case GLFW_KEY_DOWN:
        return Key::Down;
    case GLFW_KEY_UP:
        return Key::Up;
    case GLFW_KEY_PAGE_UP:
        return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN:
        return Key::PageDown;
    case GLFW_KEY_HOME:
        return Key::Home;
    case GLFW_KEY_END:
        return Key::End;
    case GLFW_KEY_KP_DECIMAL:
        return Key::Keypad + '.';
    case GLFW_KEY_KP_DIVIDE:
        return Key::Keypad + '/';
    case GLFW_KEY_KP_MULTIPLY:
        return Key::Keypad + '*';
    case GLFW_KEY_KP_SUBTRACT:
        return Key::Keypad + '-';
    case GLFW_KEY_KP_ADD:
        return Key::Keypad + '+';
    case GLFW_KEY_KP_ENTER:
        return Key::KeypadEnter;
    case GLFW_KEY_KP_EQUAL:
        return Key::Keypad + '=';
    case GLFW_KEY_LEFT_SHIFT:
        return Key::ShiftL;
    case GLFW_KEY_RIGHT_SHIFT:
        return Key::ShiftR;
    case GLFW_KEY_LEFT_CONTROL:
        return Key::ControlL;
    case GLFW_KEY_RIGHT_CONTROL:
        return Key::ControlR;
    case GLFW_KEY_LEFT_ALT:
        return Key::AltL;
    case GLFW_KEY_RIGHT_ALT:
        return Key::AltR;
    default:
        return 0;
    }
}

InputEvent GlfwInput::event(InputType type) const
{
    InputEvent e;
    e.type = type;
    e.x = m_x;
    e.y = m_y;
    e.state = m_state;
    return e;
}

void GlfwInput::setModifiers(int glfwMods)
{
    m_state &= ~(Mod::Shift | Mod::Ctrl | Mod::Alt);
    if (glfwMods & GLFW_MOD_SHIFT)
        m_state |= Mod::Shift;
    if (glfwMods & GLFW_MOD_CONTROL)
        m_state |= Mod::Ctrl;
    if (glfwMods & GLFW_MOD_ALT)
        m_state |= Mod::Alt;
}

InputEvent GlfwInput::key(int glfwKey, int action, int mods)
{
    setModifiers(mods);
    const int k = keyFromGlfw(glfwKey);
    if (!k)
        return InputEvent();
    InputEvent e = event(action == GLFW_RELEASE ? InputType::KeyUp : InputType::KeyDown);
    e.key = k;
    return e;
}

InputEvent GlfwInput::button(int glfwButton, int action, int mods, double seconds)
{
    setModifiers(mods);
    int which = 0, bit = 0;
    switch (glfwButton)
    {
    case GLFW_MOUSE_BUTTON_LEFT:
        which = Mouse::Left;
        bit = Mod::Button1;
        break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        which = Mouse::Middle;
        bit = Mod::Button2;
        break;
    case GLFW_MOUSE_BUTTON_RIGHT:
        which = Mouse::Right;
        bit = Mod::Button3;
        break;
    default:
        return InputEvent();
    }
    if (action == GLFW_RELEASE)
    {
        m_state &= ~bit;
        InputEvent e = event(InputType::Release);
        e.button = which;
        return e;
    }
    // like FLTK: the button being pressed is already in the state
    m_state |= bit;
    const bool forget = platform::takeForgetClick();
    const bool again = !forget && which == m_lastButton && m_lastPress >= 0.0 &&
                       seconds - m_lastPress <= kDoubleClickSeconds &&
                       std::abs(m_x - m_lastX) <= kDoubleClickPixels &&
                       std::abs(m_y - m_lastY) <= kDoubleClickPixels;
    m_clicks = again ? m_clicks + 1 : 0;
    m_lastPress = seconds;
    m_lastButton = which;
    m_lastX = m_x;
    m_lastY = m_y;
    InputEvent e = event(InputType::Push);
    e.button = which;
    e.clicks = m_clicks;
    return e;
}

InputEvent GlfwInput::cursor(double x, double y)
{
    m_x = static_cast<int>(std::floor(x));
    m_y = static_cast<int>(std::floor(y));
    const bool held = (m_state & (Mod::Button1 | Mod::Button2 | Mod::Button3)) != 0;
    return event(held ? InputType::Drag : InputType::Move);
}

InputEvent GlfwInput::scroll(double xOffset, double yOffset)
{
    // GLFW: +y is wheel up, +x is tilting left. InputEvent: +dy towards the
    // user (wheel down), +dx right.
    InputEvent e = event(InputType::Wheel);
    e.dx = static_cast<float>(-xOffset);
    e.dy = static_cast<float>(-yOffset);
    return e;
}

InputEvent GlfwInput::focus(bool focused)
{
    if (!focused)
        m_state = 0; // released keys and buttons are not reported to an unfocused window
    return event(focused ? InputType::Focus : InputType::Unfocus);
}

InputEvent GlfwInput::enter(bool entered)
{
    return event(entered ? InputType::Enter : InputType::Leave);
}
