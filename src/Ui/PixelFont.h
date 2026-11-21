/************************************************************************
     File:        PixelFont.h

     Comment:     The program's own pixel font for the block-game style
                  interface: printable ASCII (32-126), each glyph up to 5
                  pixels wide and 7 tall above the baseline, with one row
                  below it for the tails of g, j, p, q and y. Glyphs are
                  proportional (an i is narrower than an m) with one blank
                  column between them, and are drawn in this file, not
                  taken from any game.

                  Text is measured and laid out in font pixels; the
                  interface scales those up by its GUI scale (Gui.h).

*************************************************************************/
#pragma once

#include <string>

namespace ui
{
namespace PixelFont
{
constexpr int kCell = 8;        // each glyph's cell in the atlas, kCell x kCell
constexpr int kAscent = 7;      // rows above the baseline
constexpr int kLineHeight = 10; // from one line of text to the next
constexpr int kFirst = 32;      // ' '
constexpr int kLast = 126;      // '~'

// Is pixel (x, y) of character c lit? y = 0 is the top row, 7 the row
// below the baseline. Characters outside 32-126 draw as '?'.
bool pixel(char c, int x, int y);

// How many columns the glyph uses (without the gap after it).
int glyphWidth(char c);

// How far the pen moves after the glyph: its width and one blank column.
int advance(char c);

// The width of a line of text, without the gap after the last glyph.
int textWidth(const std::string &text);
} // namespace PixelFont
} // namespace ui
