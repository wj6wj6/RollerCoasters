/************************************************************************
     File:        GlfwInput.H

     Comment:     GLFW's keyboard and mouse callbacks turned into InputEvent
                  (Input.H). GLFW only reports what changed, so this keeps
                  what FLTK used to: the mouse position on key events, the
                  modifier keys and buttons held, and double clicks
                  (InputEvent::clicks).

                  GLFW reads keys by where they are on the keyboard, the
                  same with any layout or input method, so W is W for the
                  Fly camera even while a Chinese input method is on.

*************************************************************************/
#pragma once

#include "Input.H"

class GlfwInput
{
  public:
    // Key:: / ASCII value of a GLFW_KEY_*, 0 for keys the program has no use for.
    static int keyFromGlfw(int glfwKey);

    // The callbacks' arguments, as GLFW passes them. seconds: glfwGetTime(),
    // for double clicks. Events nobody uses come back as InputType::None.
    InputEvent key(int glfwKey, int action, int mods);
    InputEvent button(int glfwButton, int action, int mods, double seconds);
    InputEvent cursor(double x, double y);
    InputEvent scroll(double xOffset, double yOffset);
    InputEvent focus(bool focused);
    InputEvent enter(bool entered);
    // A typed character (GLFW's char callback), for the menu's text field.
    InputEvent text(unsigned int codepoint);

    int x() const
    {
        return m_x;
    }
    int y() const
    {
        return m_y;
    }

    // Two presses of the same button this close together count as a double click.
    static constexpr double kDoubleClickSeconds = 0.4;
    static constexpr int kDoubleClickPixels = 4;

  private:
    InputEvent event(InputType type) const;
    void setModifiers(int glfwMods);

    int m_x = 0, m_y = 0;
    int m_state = 0; // Mod:: bits
    // the last press, for double clicks
    double m_lastPress = -1.0;
    int m_lastButton = 0;
    int m_lastX = 0, m_lastY = 0;
    int m_clicks = 0;
};
