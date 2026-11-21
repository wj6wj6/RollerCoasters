// Helpers for the camera checks: a window-less Viewport and InputEvent builders, so the
// checks drive the real cameras through the same handle() and timer path as the program.
#pragma once

#include "Input.H"

class TestViewport final : public Viewport
{
  public:
    TestViewport(int w, int h) : width(w), height(h)
    {
    }
    int viewWidth() const override
    {
        return width;
    }
    int viewHeight() const override
    {
        return height;
    }
    void requestRedraw() override
    {
        ++redraws;
    }

    int width;
    int height;
    int redraws = 0;
};

inline InputEvent keyEvent(InputType type, int key, int state = 0)
{
    InputEvent e;
    e.type = type;
    e.key = key;
    e.state = state;
    return e;
}

inline InputEvent mouseEvent(InputType type, int x, int y, int button = 0, int state = 0)
{
    InputEvent e;
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = button;
    e.state = state;
    return e;
}

inline InputEvent wheelEvent(float dx, float dy, int state = 0)
{
    InputEvent e;
    e.type = InputType::Wheel;
    e.dx = dx;
    e.dy = dy;
    e.state = state;
    return e;
}

inline InputEvent simpleEvent(InputType type)
{
    InputEvent e;
    e.type = type;
    return e;
}
