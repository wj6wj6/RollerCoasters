/************************************************************************
     File:        UiAtlas.h

     Comment:     One RGBA texture with everything the interface draws:
                  the pixel font (PixelFont.h), the widget skins (buttons,
                  slider, panels, hotbar slots) and the hotbar's icons, all
                  drawn in code in the program's own slate-grey style. Gui.h turns
                  widgets into quads that sample this atlas; GuiGl.h
                  uploads it and draws them.

                  Skins are drawn nine-slice: the corners keep their size,
                  the edges and the middle stretch.

*************************************************************************/
#pragma once

#include <vector>

namespace ui
{
namespace UiAtlas
{
constexpr int kSize = 256; // the atlas is kSize x kSize texels

// The font: character c's cell is at fontCell(c), PixelFont::kCell square.
void fontCell(char c, int &x, int &y);

struct Skin
{
    int x, y;   // top left texel in the atlas
    int w, h;   // size in texels
    int border; // the nine-slice border, in texels
};

enum SkinId
{
    BUTTON,          // a raised slate button
    BUTTON_HOVER,    // ... with the mouse over it
    BUTTON_DISABLED, // ... that cannot be pressed
    SLIDER_TRACK,    // the sunken groove of a slider
    PANEL,           // a dark, see-through window behind a group of widgets
    WHITE,           // plain white, tinted for flat colour (bars, rows, dimming)
    SLOT,            // a hotbar slot
    SLOT_SELECTED,   // ... the chosen one
    SKIN_COUNT
};

const Skin &skin(SkinId id);

// The hotbar's icons, kIconSize square each.
enum IconId
{
    ICON_WORLD,  // a globe: the World camera
    ICON_TRAIN,  // a minecart: ride the train
    ICON_CHASE,  // an eye behind a cart: the Chase camera
    ICON_TOP,    // a map: the Top view
    ICON_FLY,    // a feather: the Fly camera
    ICON_EDIT,   // a pencil: show and edit the control points
    ICON_ADD,    // a plus: add a point
    ICON_DELETE, // a cross: delete a point
    ICON_RUN,    // play: start the train
    ICON_STOP,   // pause: stop it
    ICON_COUNT
};
constexpr int kIconSize = 16;
void iconCell(IconId id, int &x, int &y);

// The atlas as RGBA, row 0 at the top, kSize x kSize.
std::vector<unsigned char> build();
} // namespace UiAtlas
} // namespace ui
