/************************************************************************
     File:        Gui.cpp

     Comment:     See Gui.h.

*************************************************************************/

#include "Gui.h"
#include "PixelFont.h"

#include <cmath>
#include <cstdio>

int ui::Gui::autoScale(int pixelWidth, int pixelHeight, int maxScale)
{
    int scale = 1;
    while (scale < maxScale && pixelWidth / (scale + 1) >= 320 && pixelHeight / (scale + 1) >= 240)
        ++scale;
    return scale;
}

void ui::Gui::beginFrame(int pixelWidth, int pixelHeight, int scale)
{
    m_pixelWidth = pixelWidth > 0 ? pixelWidth : 1;
    m_pixelHeight = pixelHeight > 0 ? pixelHeight : 1;
    m_scale = scale > 0 ? scale : autoScale(m_pixelWidth, m_pixelHeight);
    m_width = m_pixelWidth / m_scale;
    m_height = m_pixelHeight / m_scale;
    m_quads.clear();
    m_solid.clear();
    m_widget = 0;

    // Hand this frame the presses fed since the last one.
    m_push = m_pushPending;
    m_framePushX = m_pushX;
    m_framePushY = m_pushY;
    m_rightPush = m_rightPushPending;
    m_frameRightX = m_rightPushX;
    m_frameRightY = m_rightPushY;
    m_wheel = m_wheelPending;
    m_pushPending = m_rightPushPending = false;
    m_wheelPending = 0.0f;
    // A drag that ended between frames still lets its slider take the last position.
}

float ui::Gui::unitX(int pixelX) const
{
    return static_cast<float>(pixelX) / static_cast<float>(m_scale);
}

float ui::Gui::unitY(int pixelY) const
{
    return static_cast<float>(pixelY) / static_cast<float>(m_scale);
}

bool ui::Gui::covers(int pixelX, int pixelY) const
{
    for (const Rect &r : m_solid)
        if (r.contains(unitX(pixelX), unitY(pixelY)))
            return true;
    return false;
}

bool ui::Gui::feed(const InputEvent &e)
{
    switch (e.type)
    {
    case InputType::Push:
    case InputType::Release:
    case InputType::Drag:
    case InputType::Move:
    case InputType::Wheel:
        break;
    case InputType::Unfocus:
    case InputType::Hide:
        m_leftDown = false;
        return false;
    default:
        return false;
    }
    m_mouseX = e.x;
    m_mouseY = e.y;
    const bool dragging = m_activeSlider >= 0;
    const bool over = covers(e.x, e.y);
    if (e.type == InputType::Push && e.button == Mouse::Left)
    {
        m_leftDown = true;
        if (over)
        {
            m_pushPending = true;
            m_pushX = e.x;
            m_pushY = e.y;
        }
    }
    else if (e.type == InputType::Push && e.button == Mouse::Right && over)
    {
        m_rightPushPending = true;
        m_rightPushX = e.x;
        m_rightPushY = e.y;
    }
    else if (e.type == InputType::Release && e.button == Mouse::Left)
        m_leftDown = false;
    else if (e.type == InputType::Wheel && over)
        m_wheelPending += e.dy;
    return dragging || over;
}

void ui::Gui::quad(float x, float y, float w, float h, int u, int v, int tw, int th, Color c)
{
    const float s = static_cast<float>(m_scale);
    const float size = static_cast<float>(UiAtlas::kSize);
    Quad q;
    q.x0 = x * s;
    q.y0 = y * s;
    q.x1 = (x + w) * s;
    q.y1 = (y + h) * s;
    q.u0 = static_cast<float>(u) / size;
    q.v0 = static_cast<float>(v) / size;
    q.u1 = static_cast<float>(u + tw) / size;
    q.v1 = static_cast<float>(v + th) / size;
    q.color = c;
    m_quads.push_back(q);
}

void ui::Gui::nineSlice(const Rect &r, const UiAtlas::Skin &s, Color c)
{
    const int b = s.border;
    if (b == 0)
    {
        quad(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w),
             static_cast<float>(r.h), s.x, s.y, s.w, s.h, c);
        return;
    }
    // Columns and rows: border, stretched middle, border (narrower when the
    // rectangle is smaller than two borders).
    const int bw = r.w < 2 * b ? r.w / 2 : b;
    const int bh = r.h < 2 * b ? r.h / 2 : b;
    const int xs[3] = {r.x, r.x + bw, r.x + r.w - bw};
    const int ws[3] = {bw, r.w - 2 * bw, bw};
    const int ys[3] = {r.y, r.y + bh, r.y + r.h - bh};
    const int hs[3] = {bh, r.h - 2 * bh, bh};
    const int us[3] = {s.x, s.x + b, s.x + s.w - b};
    const int tws[3] = {b, s.w - 2 * b, b};
    const int vs[3] = {s.y, s.y + b, s.y + s.h - b};
    const int ths[3] = {b, s.h - 2 * b, b};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            if (ws[i] > 0 && hs[j] > 0)
                quad(static_cast<float>(xs[i]), static_cast<float>(ys[j]),
                     static_cast<float>(ws[i]), static_cast<float>(hs[j]), us[i], vs[j], tws[i],
                     ths[j], c);
}

void ui::Gui::solid(const Rect &r)
{
    m_solid.push_back(r);
}

bool ui::Gui::hovered(const Rect &r) const
{
    return m_mouseX >= 0 && r.contains(unitX(m_mouseX), unitY(m_mouseY));
}

bool ui::Gui::takePush(const Rect &r)
{
    if (!m_push || !r.contains(unitX(m_framePushX), unitY(m_framePushY)))
        return false;
    m_push = false; // one press uses one widget
    return true;
}

bool ui::Gui::takeRightPush(const Rect &r)
{
    if (!m_rightPush || !r.contains(unitX(m_frameRightX), unitY(m_frameRightY)))
        return false;
    m_rightPush = false;
    return true;
}

void ui::Gui::fill(const Rect &r, Color c)
{
    skin(r, UiAtlas::WHITE, c);
}

void ui::Gui::skin(const Rect &r, UiAtlas::SkinId id, Color tint)
{
    nineSlice(r, UiAtlas::skin(id), tint);
}

void ui::Gui::icon(int x, int y, UiAtlas::IconId id, Color tint)
{
    int u = 0, v = 0;
    UiAtlas::iconCell(id, u, v);
    const float size = static_cast<float>(UiAtlas::kIconSize);
    quad(static_cast<float>(x), static_cast<float>(y), size, size, u, v, UiAtlas::kIconSize,
         UiAtlas::kIconSize, tint);
}

void ui::Gui::panel(const Rect &r)
{
    skin(r, UiAtlas::PANEL);
    solid(r);
}

void ui::Gui::text(int x, int y, const std::string &s, Color c, Align align, bool shadow)
{
    int pen = x;
    if (align == Align::Center)
        pen = x - PixelFont::textWidth(s) / 2;
    else if (align == Align::Right)
        pen = x - PixelFont::textWidth(s);
    const Color shade = {static_cast<unsigned char>(c.r / 4), static_cast<unsigned char>(c.g / 4),
                         static_cast<unsigned char>(c.b / 4), c.a};
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass)
    {
        int at = pen;
        const int offset = pass == 0 ? 1 : 0;
        for (char ch : s)
        {
            const int width = PixelFont::glyphWidth(ch);
            if (ch != ' ')
            {
                int u = 0, v = 0;
                UiAtlas::fontCell(ch, u, v);
                quad(static_cast<float>(at + offset), static_cast<float>(y + offset),
                     static_cast<float>(width), static_cast<float>(PixelFont::kCell), u, v, width,
                     PixelFont::kCell, pass == 0 ? shade : c);
            }
            at += width + 1;
        }
    }
}

void ui::Gui::buttonFace(const Rect &r, const std::string &label, bool enabled, bool hot)
{
    skin(r, !enabled ? UiAtlas::BUTTON_DISABLED : (hot ? UiAtlas::BUTTON_HOVER : UiAtlas::BUTTON));
    const Color c = !enabled ? kGrey : (hot ? kYellow : kWhite);
    text(r.x + r.w / 2, r.y + (r.h - PixelFont::kAscent) / 2, label, c, Align::Center);
}

bool ui::Gui::button(const Rect &r, const std::string &label, bool enabled)
{
    ++m_widget;
    solid(r);
    const bool used = enabled && takePush(r);
    buttonFace(r, label, enabled, enabled && hovered(r));
    return used;
}

bool ui::Gui::toggle(const Rect &r, const std::string &label, bool &value)
{
    const bool used = button(r, label + (value ? ": ON" : ": OFF"));
    if (used)
        value = !value;
    return used;
}

bool ui::Gui::cycle(const Rect &r, const std::string &label,
                    const std::vector<std::string> &options, int &index)
{
    const int count = static_cast<int>(options.size());
    if (count == 0)
        return button(r, label, false);
    if (index < 0 || index >= count)
        index = 0;
    ++m_widget;
    solid(r);
    int step = 0;
    if (takePush(r))
        step = 1;
    else if (takeRightPush(r))
        step = -1;
    index = (index + step + count) % count;
    buttonFace(r, label + ": " + options[static_cast<size_t>(index)], true, hovered(r));
    return step != 0;
}

bool ui::Gui::slider(const Rect &r, const std::string &label, float &value, float minimum,
                     float maximum, int decimals)
{
    const int id = ++m_widget;
    solid(r);
    const int knob = 8;
    const float before = value;
    auto setFrom = [&](int pixelX) {
        const float t = (unitX(pixelX) - static_cast<float>(r.x) - knob * 0.5f) /
                        static_cast<float>(r.w - knob);
        const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        value = minimum + (maximum - minimum) * clamped;
    };
    if (takePush(r))
    {
        m_activeSlider = id;
        setFrom(m_framePushX);
    }
    if (m_activeSlider == id)
    {
        if (m_mouseX >= 0)
            setFrom(m_mouseX);
        if (!m_leftDown)
            m_activeSlider = -1;
    }
    if (maximum > minimum)
        value = value < minimum ? minimum : (value > maximum ? maximum : value);

    skin(r, UiAtlas::SLIDER_TRACK);
    const float t = maximum > minimum ? (value - minimum) / (maximum - minimum) : 0.0f;
    const int knobX = r.x + static_cast<int>(std::lround(t * static_cast<float>(r.w - knob)));
    const bool hot = hovered(r) || m_activeSlider == id;
    skin({knobX, r.y, knob, r.h}, hot ? UiAtlas::BUTTON_HOVER : UiAtlas::BUTTON);
    std::string shown = label;
    if (decimals >= 0)
    {
        char number[32];
        std::snprintf(number, sizeof(number), "%.*f", decimals, static_cast<double>(value));
        shown += std::string(": ") + number;
    }
    text(r.x + r.w / 2, r.y + (r.h - PixelFont::kAscent) / 2, shown, hot ? kYellow : kWhite,
         Align::Center);
    return value != before;
}

bool ui::Gui::area(const Rect &r)
{
    ++m_widget;
    solid(r);
    return takePush(r);
}

bool ui::Gui::list(const Rect &r, const std::vector<std::string> &items, int &selected, int &scroll)
{
    ++m_widget;
    panel(r);
    const int count = static_cast<int>(items.size());
    const int rows = (r.h - 4) / kRowHeight > 0 ? (r.h - 4) / kRowHeight : 1;
    if (m_wheel != 0.0f && hovered(r))
    {
        scroll += m_wheel > 0.0f ? 1 : -1;
        m_wheel = 0.0f;
    }
    const int most = count > rows ? count - rows : 0;
    scroll = scroll < 0 ? 0 : (scroll > most ? most : scroll);

    const int before = selected;
    const bool bar = count > rows;
    const int rowWidth = r.w - 4 - (bar ? 4 : 0);
    for (int i = 0; i < rows && scroll + i < count; ++i)
    {
        const int item = scroll + i;
        const Rect row = {r.x + 2, r.y + 2 + i * kRowHeight, rowWidth, kRowHeight};
        if (takePush(row))
            selected = item;
        if (item == selected)
            fill(row, {255, 255, 255, 70});
        else if (hovered(row))
            fill(row, {255, 255, 255, 30});
        text(row.x + 3, row.y + (kRowHeight - PixelFont::kAscent) / 2,
             items[static_cast<size_t>(item)], item == selected ? kYellow : kWhite);
    }
    if (bar)
    {
        // A thin scroll bar showing which part of the list is in view.
        const int trackH = r.h - 4;
        const int thumbH = trackH * rows / count > 4 ? trackH * rows / count : 4;
        const int thumbY = r.y + 2 + (trackH - thumbH) * scroll / most;
        fill({r.x + r.w - 5, r.y + 2, 3, trackH}, {0, 0, 0, 120});
        fill({r.x + r.w - 5, thumbY, 3, thumbH}, {200, 200, 210, 255});
    }
    return selected != before;
}
