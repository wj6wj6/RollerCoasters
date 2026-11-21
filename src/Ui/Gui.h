/************************************************************************
     File:        Gui.h

     Comment:     A small immediate-mode interface in the style of a block
                  game: every frame the caller lays out its screen by
                  calling button(), slider() and so on, and each call both
                  draws the widget and says whether it was used. Nothing
                  is kept between frames except the mouse and which slider
                  is being dragged, so screens are plain code.

                  Coordinates are GUI units, y down, from the top left.
                  The GUI scale (pixels per unit) is picked like a block
                  game's "Auto" GUI scale: the largest whole number that
                  still leaves at least 320 x 240 units.

                  Mouse input comes in through feed() between frames (a
                  button fires when it is pressed, like in the game); the
                  next frame acts on it. feed() says whether the interface
                  took the event, so the 3D view does not also get it.

                  The widgets turn into textured quads (quads()), in
                  pixels, that sample UiAtlas; GuiGl.h draws them. Without
                  OpenGL this file can be tested on its own (GuiCheck).

*************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "../Utilities/Input.H"
#include "UiAtlas.h"

namespace ui
{
struct Rect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool contains(float px, float py) const
    {
        return px >= static_cast<float>(x) && py >= static_cast<float>(y) &&
               px < static_cast<float>(x + w) && py < static_cast<float>(y + h);
    }
};

struct Color
{
    unsigned char r = 255, g = 255, b = 255, a = 255;
};

constexpr Color kWhite = {255, 255, 255, 255};
constexpr Color kGrey = {160, 160, 160, 255};   // disabled text, hints
constexpr Color kYellow = {255, 255, 160, 255}; // hovered button text
constexpr Color kDim = {0, 0, 0, 140};          // darkens the world behind a menu

// One textured quad in window pixels; u, v in 0..1 of the atlas.
struct Quad
{
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    Color color;
};

enum class Align
{
    Left,
    Center,
    Right
};

class Gui
{
  public:
    // The largest scale (at most maxScale) that leaves 320 x 240 units.
    static int autoScale(int pixelWidth, int pixelHeight, int maxScale = 4);

    // Start a frame for a window of this size. scale 0 picks autoScale().
    void beginFrame(int pixelWidth, int pixelHeight, int scale = 0);
    int width() const // in GUI units
    {
        return m_width;
    }
    int height() const
    {
        return m_height;
    }
    int scale() const
    {
        return m_scale;
    }

    // Mouse input between frames, in window pixels. True when the interface
    // takes it: over something it drew last frame, or a slider being dragged.
    // Call it after the frame that drew the screen, then draw a new frame.
    bool feed(const InputEvent &e);
    // Is this window pixel over something the interface drew last frame?
    bool covers(int pixelX, int pixelY) const;
    // Has fed input that the next frame still has to act on?
    bool pending() const
    {
        return m_pushPending || m_rightPushPending || m_wheelPending != 0.0f;
    }

    // Drawing only.
    void fill(const Rect &r, Color c);
    void panel(const Rect &r);
    void skin(const Rect &r, UiAtlas::SkinId id, Color tint = kWhite);
    // Text at (x, y), the top of the capitals; align about x.
    void text(int x, int y, const std::string &s, Color c = kWhite, Align align = Align::Left,
              bool shadow = true);

    // Widgets: true when used this frame.
    bool button(const Rect &r, const std::string &label, bool enabled = true);
    // "label: ON" / "label: OFF"; a click flips value.
    bool toggle(const Rect &r, const std::string &label, bool &value);
    // "label: option"; left click picks the next option, right click the one before.
    bool cycle(const Rect &r, const std::string &label, const std::vector<std::string> &options,
               int &index);
    // "label: value" on the knob's track; drag or click to set. decimals: shown digits.
    bool slider(const Rect &r, const std::string &label, float &value, float minimum, float maximum,
                int decimals = 0);
    // A scrolling list of rows; a click selects, the wheel scrolls.
    bool list(const Rect &r, const std::vector<std::string> &items, int &selected, int &scroll);

    const std::vector<Quad> &quads() const
    {
        return m_quads;
    }
    int pixelWidth() const
    {
        return m_pixelWidth;
    }
    int pixelHeight() const
    {
        return m_pixelHeight;
    }

    static constexpr int kRowHeight = 12; // list rows

  private:
    void quad(float x, float y, float w, float h, int u, int v, int tw, int th, Color c);
    void nineSlice(const Rect &r, const UiAtlas::Skin &s, Color c);
    void solid(const Rect &r);
    bool hovered(const Rect &r) const;
    bool takePush(const Rect &r);
    bool takeRightPush(const Rect &r);
    void buttonFace(const Rect &r, const std::string &label, bool enabled, bool hot);
    float unitX(int pixelX) const;
    float unitY(int pixelY) const;

    int m_pixelWidth = 0, m_pixelHeight = 0;
    int m_width = 0, m_height = 0, m_scale = 1;
    std::vector<Quad> m_quads;
    std::vector<Rect> m_solid; // in units: what the last frame drew, for feed()

    // The mouse, in window pixels.
    int m_mouseX = -1, m_mouseY = -1;
    bool m_leftDown = false;
    // Presses fed since the last frame, and the ones this frame still has to hand out.
    bool m_pushPending = false, m_rightPushPending = false;
    int m_pushX = 0, m_pushY = 0, m_rightPushX = 0, m_rightPushY = 0;
    float m_wheelPending = 0.0f;
    bool m_push = false, m_rightPush = false;
    int m_framePushX = 0, m_framePushY = 0, m_frameRightX = 0, m_frameRightY = 0;
    float m_wheel = 0.0f;

    int m_widget = 0;        // counts widgets in this frame, for the dragged slider
    int m_activeSlider = -1; // the slider whose knob is held, or -1
};
} // namespace ui
