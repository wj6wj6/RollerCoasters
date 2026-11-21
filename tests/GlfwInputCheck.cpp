// Checks that GLFW's callbacks become the InputEvents the cameras and the view were written for
// (Input.H): keys by their FLTK-era numbers (letters lower case, keypad and function keys),
// modifiers and held buttons in the state, the mouse position on every event, Drag only with a
// button down, wheel signs, double clicks within time and distance, and forgetClick().
#include "Utilities/GlfwInput.H"
#include "Utilities/Platform.H"

#include <cstdio>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}
} // namespace

int main()
{
    // Keys
    expect(GlfwInput::keyFromGlfw(GLFW_KEY_W) == 'w' && GlfwInput::keyFromGlfw(GLFW_KEY_A) == 'a',
           "letters are lower case ASCII");
    expect(GlfwInput::keyFromGlfw(GLFW_KEY_1) == '1' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_SPACE) == ' ' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_MINUS) == '-' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_EQUAL) == '=' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_LEFT_BRACKET) == '[' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_RIGHT_BRACKET) == ']',
           "digits, space and punctuation are their unshifted characters");
    expect(GlfwInput::keyFromGlfw(GLFW_KEY_F1) == Key::F + 1 &&
               GlfwInput::keyFromGlfw(GLFW_KEY_F4) == Key::F + 4,
           "function keys are Key::F + n");
    expect(GlfwInput::keyFromGlfw(GLFW_KEY_KP_1) == Key::Keypad + '1' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_KP_7) == Key::Keypad + '7' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_KP_ADD) == Key::Keypad + '+' &&
               GlfwInput::keyFromGlfw(GLFW_KEY_KP_ENTER) == Key::KeypadEnter,
           "keypad keys are Key::Keypad + their character");
    expect(GlfwInput::keyFromGlfw(GLFW_KEY_ESCAPE) == Key::Escape &&
               GlfwInput::keyFromGlfw(GLFW_KEY_HOME) == Key::Home &&
               GlfwInput::keyFromGlfw(GLFW_KEY_DELETE) == Key::Delete &&
               GlfwInput::keyFromGlfw(GLFW_KEY_LEFT_SHIFT) == Key::ShiftL &&
               GlfwInput::keyFromGlfw(GLFW_KEY_RIGHT_CONTROL) == Key::ControlR &&
               GlfwInput::keyFromGlfw(GLFW_KEY_UP) == Key::Up,
           "special keys are the Key:: constants");
    expect(GlfwInput::keyFromGlfw(GLFW_KEY_CAPS_LOCK) == 0, "keys nobody uses are 0");

    GlfwInput input;
    InputEvent e = input.cursor(120.6, 45.2);
    expect(e.type == InputType::Move && e.x == 120 && e.y == 45, "moving with no button is Move");
    e = input.key(GLFW_KEY_D, GLFW_PRESS, GLFW_MOD_SHIFT);
    expect(e.type == InputType::KeyDown && e.key == 'd' && (e.state & Mod::Shift) && e.x == 120 &&
               e.y == 45,
           "a key press carries the modifiers and the mouse position");
    e = input.key(GLFW_KEY_D, GLFW_REPEAT, 0);
    expect(e.type == InputType::KeyDown && !(e.state & Mod::Shift), "a repeat is another KeyDown");
    e = input.key(GLFW_KEY_D, GLFW_RELEASE, 0);
    expect(e.type == InputType::KeyUp && e.key == 'd', "a release is KeyUp");
    e = input.key(GLFW_KEY_CAPS_LOCK, GLFW_PRESS, 0);
    expect(e.type == InputType::None, "unused keys send nothing");

    // Buttons and drags
    e = input.button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, GLFW_MOD_ALT, 10.0);
    expect(e.type == InputType::Push && e.button == Mouse::Right && (e.state & Mod::Button3) &&
               (e.state & Mod::Alt) && e.clicks == 0,
           "a press has its button in the state, like FLTK");
    e = input.cursor(130.0, 50.0);
    expect(e.type == InputType::Drag && (e.state & Mod::Button3),
           "moving with a button down is Drag");
    e = input.button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0, 10.1);
    expect(e.type == InputType::Release && e.button == Mouse::Right && !(e.state & Mod::Button3),
           "a release takes the button out of the state");
    e = input.cursor(131.0, 50.0);
    expect(e.type == InputType::Move, "and moving is Move again");

    // Double clicks
    e = input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0, 20.0);
    input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0, 20.05);
    e = input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0, 20.2);
    expect(e.clicks == 1, "a second press soon after at the same spot is a double click");
    input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0, 20.25);
    e = input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0, 21.5);
    expect(e.clicks == 0, "a press after a pause is a single click");
    input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0, 21.55);
    input.cursor(150.0, 50.0);
    e = input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0, 21.6);
    expect(e.clicks == 0, "a press somewhere else is a single click");
    input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0, 21.65);
    e = input.button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0, 21.7);
    expect(e.clicks == 0, "a press of another button is a single click");
    input.button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0, 21.75);
    platform::forgetClick();
    e = input.button(GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0, 21.8);
    expect(e.clicks == 0, "after forgetClick() the next press is a single click");

    // Wheel: InputEvent's +dy is towards the user (GLFW's -y), +dx right (GLFW's -x)
    e = input.scroll(0.0, 1.0);
    expect(e.type == InputType::Wheel && e.dy == -1.0f && e.dx == 0.0f, "wheel up is -dy");
    e = input.scroll(-0.25, -0.5);
    expect(e.dy == 0.5f && e.dx == 0.25f, "touchpad fractions are kept, tilting right is +dx");

    // Focus
    input.button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, GLFW_MOD_CONTROL, 30.0);
    e = input.focus(false);
    expect(e.type == InputType::Unfocus && e.state == 0,
           "losing focus forgets the held buttons and modifiers");
    expect(input.focus(true).type == InputType::Focus &&
               input.enter(true).type == InputType::Enter &&
               input.enter(false).type == InputType::Leave,
           "focus and the mouse entering and leaving");

    std::printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
