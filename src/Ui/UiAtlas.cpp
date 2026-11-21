/************************************************************************
     File:        UiAtlas.cpp

     Comment:     See UiAtlas.h. Layout: the font's 95 cells fill the top
                  left (16 per row, 6 rows of 8 texels); the skins sit in a
                  row of 16 x 16 tiles below it, starting at y = 64.

*************************************************************************/

#include "UiAtlas.h"
#include "PixelFont.h"

namespace
{
using ui::UiAtlas::kSize;

struct Rgba
{
    int r, g, b, a;
};

void put(std::vector<unsigned char> &image, int x, int y, Rgba c)
{
    if (x < 0 || y < 0 || x >= kSize || y >= kSize)
        return;
    unsigned char *p = &image[(static_cast<size_t>(y) * kSize + static_cast<size_t>(x)) * 4];
    p[0] = static_cast<unsigned char>(c.r);
    p[1] = static_cast<unsigned char>(c.g);
    p[2] = static_cast<unsigned char>(c.b);
    p[3] = static_cast<unsigned char>(c.a);
}

void fillRect(std::vector<unsigned char> &image, int x, int y, int w, int h, Rgba c)
{
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i)
            put(image, x + i, y + j, c);
}

// A bevelled box: outline, fill, a light top-left edge and a darker
// bottom-right edge two texels deep, like a raised stone slab.
void bevel(std::vector<unsigned char> &image, const ui::UiAtlas::Skin &s, Rgba outline, Rgba fill,
           Rgba light, Rgba dark)
{
    fillRect(image, s.x, s.y, s.w, s.h, outline);
    fillRect(image, s.x + 1, s.y + 1, s.w - 2, s.h - 2, fill);
    fillRect(image, s.x + 1, s.y + 1, s.w - 2, 1, light);      // top
    fillRect(image, s.x + 1, s.y + 1, 1, s.h - 2, light);      // left
    fillRect(image, s.x + 1, s.y + s.h - 3, s.w - 2, 2, dark); // bottom
    fillRect(image, s.x + s.w - 2, s.y + 2, 1, s.h - 3, dark); // right
    put(image, s.x + 1, s.y + s.h - 3, light);                 // keep the left edge whole
}

const ui::UiAtlas::Skin kSkins[ui::UiAtlas::SKIN_COUNT] = {
    {0, 64, 16, 16, 3},   // BUTTON
    {16, 64, 16, 16, 3},  // BUTTON_HOVER
    {32, 64, 16, 16, 3},  // BUTTON_DISABLED
    {48, 64, 16, 16, 2},  // SLIDER_TRACK
    {64, 64, 16, 16, 2},  // PANEL
    {80, 64, 4, 4, 0},    // WHITE
    {96, 64, 22, 22, 3},  // SLOT
    {120, 64, 24, 24, 4}, // SLOT_SELECTED
};
} // namespace

void ui::UiAtlas::fontCell(char c, int &x, int &y)
{
    int code = static_cast<unsigned char>(c);
    if (code < PixelFont::kFirst || code > PixelFont::kLast)
        code = '?';
    const int index = code - PixelFont::kFirst;
    x = (index % 16) * PixelFont::kCell;
    y = (index / 16) * PixelFont::kCell;
}

const ui::UiAtlas::Skin &ui::UiAtlas::skin(SkinId id)
{
    return kSkins[id];
}

std::vector<unsigned char> ui::UiAtlas::build()
{
    std::vector<unsigned char> image(static_cast<size_t>(kSize) * kSize * 4, 0);

    // The font in white, so a tint colours it.
    for (int code = PixelFont::kFirst; code <= PixelFont::kLast; ++code)
    {
        const char c = static_cast<char>(code);
        int cx = 0, cy = 0;
        fontCell(c, cx, cy);
        for (int y = 0; y < PixelFont::kCell; ++y)
            for (int x = 0; x < PixelFont::kCell; ++x)
                if (PixelFont::pixel(c, x, y))
                    put(image, cx + x, cy + y, {255, 255, 255, 255});
    }

    const Rgba black = {16, 16, 20, 255};
    bevel(image, skin(BUTTON), black, {112, 114, 124, 255}, {168, 170, 182, 255},
          {72, 73, 82, 255});
    // Hovered: a cool blue slab with a white outline, so it stands out.
    bevel(image, skin(BUTTON_HOVER), {236, 240, 255, 255}, {96, 112, 168, 255},
          {150, 166, 222, 255}, {58, 68, 116, 255});
    // Disabled: flat and dark, no bevel.
    const Skin &off = skin(BUTTON_DISABLED);
    fillRect(image, off.x, off.y, off.w, off.h, black);
    fillRect(image, off.x + 1, off.y + 1, off.w - 2, off.h - 2, {52, 52, 56, 255});

    // The slider groove: sunken, so the light edge is at the bottom right.
    const Skin &track = skin(SLIDER_TRACK);
    fillRect(image, track.x, track.y, track.w, track.h, black);
    fillRect(image, track.x + 1, track.y + 1, track.w - 2, track.h - 2, {40, 40, 46, 255});
    fillRect(image, track.x + 1, track.y + track.h - 2, track.w - 2, 1, {84, 84, 94, 255});
    fillRect(image, track.x + track.w - 2, track.y + 1, 1, track.h - 2, {84, 84, 94, 255});

    // The panel: a see-through dark sheet with a thin light frame.
    const Skin &panel = skin(PANEL);
    fillRect(image, panel.x, panel.y, panel.w, panel.h, {150, 152, 164, 230});
    fillRect(image, panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2, {10, 10, 14, 190});

    const Skin &white = skin(WHITE);
    fillRect(image, white.x, white.y, white.w, white.h, {255, 255, 255, 255});

    // Hotbar slots: a dark see-through square in a grey frame; the chosen
    // one has a thick bright frame drawn around the slot.
    const Skin &slot = skin(SLOT);
    fillRect(image, slot.x, slot.y, slot.w, slot.h, {120, 122, 132, 255});
    fillRect(image, slot.x + 1, slot.y + 1, slot.w - 2, slot.h - 2, {40, 40, 46, 255});
    fillRect(image, slot.x + 2, slot.y + 2, slot.w - 4, slot.h - 4, {0, 0, 0, 110});
    const Skin &chosen = skin(SLOT_SELECTED);
    fillRect(image, chosen.x, chosen.y, chosen.w, chosen.h, black);
    fillRect(image, chosen.x + 1, chosen.y + 1, chosen.w - 2, chosen.h - 2, {250, 250, 250, 255});
    fillRect(image, chosen.x + 3, chosen.y + 3, chosen.w - 6, chosen.h - 6, black);
    fillRect(image, chosen.x + 4, chosen.y + 4, chosen.w - 8, chosen.h - 8, {0, 0, 0, 0});
    return image;
}
