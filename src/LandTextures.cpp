/************************************************************************
     File:        LandTextures.cpp

     Comment:     See LandTextures.h. Every texture is drawn here, texel
                  by texel; row 0 is the top of the block.

*************************************************************************/

#include "LandTextures.h"

namespace
{

using PixelArt::hash;
using PixelArt::Image;
using PixelArt::set;

const int S = PixelArt::kSize;

// A repeatable value in -range .. +range for a texel.
int shadeOf(int x, int y, unsigned int seed, int range)
{
    return static_cast<int>(hash(x, y, seed) % static_cast<unsigned int>(2 * range + 1)) - range;
}

// Fills the image with one colour, mottled by +-range.
Image mottled(int r, int g, int b, int range, unsigned int seed)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, seed, range);
            set(p, x, y, r + s, g + s, b + s);
        }
    return p;
}

Image makeGrassTop()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const unsigned int h = hash(x, y, 1u);
            const int shade = static_cast<int>(h % 23u) - 11;
            int r = 92 + shade, g = 158 + shade * 2, b = 56 + shade;
            if (h % 11u == 0)
            {
                r -= 18;
                g -= 26;
                b -= 12; // dark blade
            }
            else if (h % 13u == 0)
            {
                r += 16;
                g += 20;
                b += 8; // sunlit blade
            }
            set(p, x, y, r, g, b);
        }
    return p;
}

void dirtTexel(Image &p, int x, int y)
{
    const unsigned int h = hash(x, y, 3u);
    const int shade = static_cast<int>(h % 19u) - 9;
    if (h % 9u == 0)
        set(p, x, y, 104 + shade, 72 + shade, 48 + shade); // dark clod
    else if (h % 17u == 0)
        set(p, x, y, 150 + shade, 140 + shade, 130 + shade); // pebble
    else
        set(p, x, y, 134 + shade, 96 + shade, 66 + shade);
}

Image makeDirt()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
            dirtTexel(p, x, y);
    return p;
}

// Dirt with a ragged fringe along the top: grass or snow.
Image makeFringed(bool snow)
{
    Image p = PixelArt::blank();
    for (int x = 0; x < S; ++x)
    {
        const int fringe = 2 + static_cast<int>(hash(x, 0, 7u) % 3u);
        for (int y = 0; y < S; ++y)
        {
            const int shade = shadeOf(x, y, 3u, 9);
            if (y >= fringe)
                dirtTexel(p, x, y);
            else if (snow)
                set(p, x, y, 240 + shade / 2, 246 + shade / 2, 252);
            else
                set(p, x, y, 92 + shade, 158 + shade * 2, 56 + shade);
        }
    }
    return p;
}

Image makeStone()
{
    Image p = mottled(128, 128, 130, 7, 21u);
    // a few darker cracks and lighter flecks
    for (int i = 0; i < 7; ++i)
    {
        int x = static_cast<int>(hash(i, 0, 22u) % 16u);
        int y = static_cast<int>(hash(i, 1, 22u) % 16u);
        const int length = 2 + static_cast<int>(hash(i, 2, 22u) % 4u);
        for (int k = 0; k < length; ++k)
        {
            set(p, x & 15, y & 15, 96, 96, 100);
            x += 1;
            y += static_cast<int>(hash(i, k, 23u) % 3u) - 1;
        }
    }
    for (int i = 0; i < 10; ++i)
        set(p, static_cast<int>(hash(i, 3, 24u) % 16u), static_cast<int>(hash(i, 4, 24u) % 16u),
            150, 150, 152);
    return p;
}

// Rounded stones with dark gaps; moss grows in the gaps when asked.
Image makeCobble(bool mossy)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            // stones on a 4 x 4 grid, every other row shifted
            const int row = y / 4;
            const int sx = (x + (row % 2) * 2) % 16;
            const int col = sx / 4;
            const bool gap = (y % 4 == 3) || (sx % 4 == 3);
            const int tone = shadeOf(col, row, 31u, 16);
            const int s = shadeOf(x, y, 32u, 5);
            if (gap)
            {
                if (mossy && hash(x, y, 33u) % 3u != 0)
                    set(p, x, y, 70 + s, 112 + s, 52 + s);
                else
                    set(p, x, y, 78 + s, 78 + s, 82 + s);
            }
            else if (mossy && hash(x, y, 34u) % 9u == 0)
                set(p, x, y, 84 + s, 128 + s, 60 + s);
            else
                set(p, x, y, 132 + tone + s, 132 + tone + s, 134 + tone + s);
        }
    return p;
}

Image makeGravel()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = shadeOf(x / 2 + (y / 2) * 3, y / 2, 41u, 22);
            const int s = shadeOf(x, y, 42u, 6);
            set(p, x, y, 132 + tone + s, 126 + tone + s, 122 + tone + s);
        }
    return p;
}

Image makeBasalt()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int column = shadeOf(x, 0, 51u, 9);
            const int s = shadeOf(x, y, 52u, 5);
            set(p, x, y, 62 + column + s, 60 + column + s, 68 + column + s);
        }
    return p;
}

// Dark rock with glowing cracks.
Image makeMagma()
{
    Image p = mottled(64, 40, 36, 8, 61u);
    for (int i = 0; i < 6; ++i)
    {
        int x = static_cast<int>(hash(i, 0, 62u) % 16u);
        int y = static_cast<int>(hash(i, 1, 62u) % 16u);
        for (int k = 0; k < 6; ++k)
        {
            set(p, x & 15, y & 15, 255, 150 + static_cast<int>(hash(i, k, 63u) % 70u), 40);
            if (hash(i, k, 64u) % 2u == 0)
                x += 1;
            else
                y += 1;
        }
    }
    return p;
}

Image makeLava()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            // slow swirls of yellow in orange, with a darker crust here and there
            const int wave = ((x + shadeOf(0, y / 3, 71u, 3) + 16) % 8) + ((y + x / 4) % 5);
            const int s = shadeOf(x, y, 72u, 10);
            if (wave < 3)
                set(p, x, y, 255, 214 + s, 96 + s);
            else if (hash(x / 2, y / 2, 73u) % 9u == 0)
                set(p, x, y, 176 + s, 62 + s, 20);
            else
                set(p, x, y, 240 + s, 120 + s * 2, 26);
        }
    return p;
}

// Fired clay of the badlands: flat colour with fine horizontal grain.
Image makeClay(int r, int g, int b, unsigned int seed)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
    {
        const int band = shadeOf(0, y, seed, 6);
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, seed + 1u, 4);
            set(p, x, y, r + band + s, g + band + s, b + band + s);
        }
    }
    return p;
}

Image makeLogSide(int r, int g, int b, bool birch, unsigned int seed)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int column = shadeOf(x, 0, seed, 12);
            const int s = shadeOf(x, y, seed + 1u, 5);
            set(p, x, y, r + column + s, g + column + s, b + column + s);
        }
    if (birch)
        // short dark dashes across the white bark
        for (int i = 0; i < 12; ++i)
        {
            const int x = static_cast<int>(hash(i, 0, seed + 2u) % 14u);
            const int y = static_cast<int>(hash(i, 1, seed + 2u) % 16u);
            const int length = 2 + static_cast<int>(hash(i, 2, seed + 2u) % 3u);
            for (int k = 0; k < length; ++k)
                set(p, x + k, y, 52, 50, 48);
        }
    return p;
}

// The cut end of a log: rings inside a ring of bark.
Image makeLogTop(int barkR, int barkG, int barkB, int woodR, int woodG, int woodB)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int dx = x < 8 ? 7 - x : x - 8;
            const int dy = y < 8 ? 7 - y : y - 8;
            const int ring = dx > dy ? dx : dy;
            const int s = shadeOf(x, y, 81u, 4);
            if (ring == 7)
                set(p, x, y, barkR + s, barkG + s, barkB + s);
            else if (ring % 2 == 1)
                set(p, x, y, woodR - 22 + s, woodG - 22 + s, woodB - 18 + s);
            else
                set(p, x, y, woodR + s, woodG + s, woodB + s);
        }
    return p;
}

// Leaves as on fast graphics: solid, with dark gaps and bright tips.
Image makeLeaves(int r, int g, int b, unsigned int seed)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const unsigned int h = hash(x, y, seed);
            const int s = shadeOf(x, y, seed + 1u, 8);
            if (h % 5u == 0)
                set(p, x, y, r * 5 / 9 + s, g * 5 / 9 + s, b * 5 / 9 + s);
            else if (h % 7u == 0)
                set(p, x, y, r + 28 + s, g + 28 + s, b + 22 + s);
            else
                set(p, x, y, r + s, g + s, b + s);
        }
    return p;
}

Image makePlanks()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int board = y / 4;
            const int tone = shadeOf(board, 0, 91u, 10);
            const int s = shadeOf(x, y, 92u, 5);
            const bool seam = (y % 4 == 3) || ((x + board * 5) % 16 == 0);
            if (seam)
                set(p, x, y, 118 + s, 90 + s, 52 + s);
            else
                set(p, x, y, 176 + tone + s, 140 + tone + s, 86 + tone + s);
        }
    return p;
}

// Rows of rounded shingles.
Image makeRoof()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int row = y / 4;
            const int sx = (x + (row % 2) * 2) % 4;
            const int s = shadeOf(x, y, 101u, 6);
            const int tone = shadeOf((x + (row % 2) * 2) / 4, row, 102u, 12);
            if (y % 4 == 3 || (sx == 3 && y % 4 != 0))
                set(p, x, y, 96 + s, 34 + s, 30 + s);
            else
                set(p, x, y, 164 + tone + s, 62 + tone / 2 + s, 50 + tone / 2 + s);
        }
    return p;
}

// Four lit panes in a wooden frame.
Image makeWindow()
{
    Image p = makePlanks();
    for (int y = 2; y < 14; ++y)
        for (int x = 2; x < 14; ++x)
        {
            const bool bar =
                x == 7 || x == 8 || y == 7 || y == 8 || x == 2 || x == 13 || y == 2 || y == 13;
            if (bar)
                set(p, x, y, 84, 60, 36);
            else
                set(p, x, y, 255, 226 - (x + y) * 2, 130 - (x + y) * 2);
        }
    return p;
}

Image makeDoor()
{
    Image p = makePlanks();
    for (int y = 1; y < S; ++y)
        for (int x = 3; x < 13; ++x)
        {
            const int s = shadeOf(x, y, 111u, 5);
            const bool edge = x == 3 || x == 12 || y == 1;
            const bool pane = y >= 3 && y <= 6 && ((x >= 5 && x <= 6) || (x >= 9 && x <= 10));
            if (edge)
                set(p, x, y, 66, 44, 24);
            else if (pane)
                set(p, x, y, 60, 70, 84);
            else
                set(p, x, y, 124 + s, 86 + s, 46 + s);
        }
    set(p, 10, 10, 236, 200, 80);
    set(p, 10, 11, 236, 200, 80);
    return p;
}

Image makeLantern()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const bool frame = x == 0 || y == 0 || x == 15 || y == 15 || x == 7 || x == 8;
            const int dx = x < 8 ? 7 - x : x - 8;
            const int dy = y < 8 ? 7 - y : y - 8;
            const int d = dx + dy;
            if (frame)
                set(p, x, y, 70, 54, 40);
            else
                set(p, x, y, 255, 250 - d * 6, 190 - d * 12);
        }
    return p;
}

Image makeCrystal()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int facet = ((x + y) / 4 + (x - y + 16) / 5) % 3;
            const int s = shadeOf(x, y, 121u, 8);
            set(p, x, y, 110 + facet * 40 + s, 222 + facet * 12 + s, 244);
        }
    return p;
}

// Stone with a few nuggets of the given colour.
Image makeOre(int r, int g, int b, unsigned int seed)
{
    Image p = makeStone();
    for (int i = 0; i < 6; ++i)
    {
        const int x = 1 + static_cast<int>(hash(i, 0, seed) % 13u);
        const int y = 1 + static_cast<int>(hash(i, 1, seed) % 13u);
        set(p, x, y, r, g, b);
        set(p, x + 1, y, r, g, b);
        set(p, x, y + 1, r * 3 / 4, g * 3 / 4, b * 3 / 4);
        if (hash(i, 2, seed) % 2u == 0)
            set(p, x + 1, y + 1, r + 30, g + 30, b + 30);
    }
    return p;
}

Image makeCactusSide()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 131u, 5);
            if (x % 4 == 1)
                set(p, x, y, 40 + s, 100 + s, 36 + s); // rib
            else
                set(p, x, y, 78 + s, 150 + s, 60 + s);
            if (x % 4 == 1 && y % 5 == 2)
                set(p, x, y, 226, 226, 196); // spine
        }
    return p;
}

Image makeMushroomCap()
{
    Image p = mottled(206, 44, 40, 6, 141u);
    const int spots[5][2] = {{2, 2}, {10, 1}, {6, 7}, {12, 10}, {2, 11}};
    for (const int *spot : spots)
        for (int dy = 0; dy < 3; ++dy)
            for (int dx = 0; dx < 3; ++dx)
                set(p, spot[0] + dx, spot[1] + dy, 244, 238, 226);
    return p;
}

Image makeHay(bool top)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(top ? x : 0, top ? 0 : y, 151u, 14) + shadeOf(x, y, 152u, 5);
            if (!top && (x == 4 || x == 11))
                set(p, x, y, 150, 60, 44); // binding
            else
                set(p, x, y, 214 + s, 178 + s, 60 + s);
        }
    return p;
}

Image makeFarmland()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 161u, 6);
            const int furrow = (x % 4 == 0) ? -22 : 0;
            set(p, x, y, 98 + furrow + s, 66 + furrow + s, 40 + furrow + s);
        }
    return p;
}

// Still water as in a block game: deep blue with a violet cast, a few
// lighter ripples drifting across, see-through (alpha about 0.6; the shader
// cuts out texels below 0.5).
Image makeWater()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 171u, 5);
            // ripples: short streaks that wander along the rows
            const int wobble = static_cast<int>(hash(0, y / 2, 173u) % 7u);
            const bool ripple =
                ((x + wobble + (y / 3) * 5) % 9) < 2 && hash(x / 3, y, 174u) % 3u != 0;
            const bool dark = hash(x, 0, 175u) % 7u == 0 && y % 5 != 0;
            if (ripple)
                set(p, x, y, 104 + s, 126 + s, 244, 158);
            else if (dark)
                set(p, x, y, 60 + s, 76 + s, 200, 158);
            else
                set(p, x, y, 74 + s, 94 + s, 226, 150);
        }
    return p;
}

void stroke(Image &p, int x0, int y0, int x1, int y1, int r, int g, int b)
{
    const int dx = x1 - x0, dy = y1 - y0;
    const int steps =
        (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    for (int i = 0; i <= steps; ++i)
    {
        const int x = x0 + (steps == 0 ? 0 : dx * i / steps);
        const int y = y0 + (steps == 0 ? 0 : dy * i / steps);
        set(p, x, y, r, g, b);
    }
}

// Blades growing up from the bottom row.
Image makeBlades(int r, int g, int b, int minHeight, unsigned int seed)
{
    Image p = PixelArt::blank();
    for (int x = 1; x < 15; ++x)
    {
        if (hash(x, 0, seed) % 4u == 0)
            continue;
        const int height = minHeight + static_cast<int>(hash(x, 1, seed) % 6u);
        const int lean = static_cast<int>(hash(x, 2, seed) % 3u) - 1;
        const int s = shadeOf(x, 3, seed, 14);
        for (int k = 0; k < height; ++k)
            set(p, x + (k > height / 2 ? lean : 0), 15 - k, r + s + k * 2, g + s + k * 3, b + s);
    }
    return p;
}

// A flower: a green stalk with two leaves and a head of the given colours.
Image makeFlower(int r, int g, int b, int centreR, int centreG, int centreB, bool tulip)
{
    Image p = PixelArt::blank();
    stroke(p, 8, 15, 8, 7, 60, 132, 48);
    stroke(p, 7, 12, 5, 10, 70, 146, 54);
    stroke(p, 9, 13, 11, 11, 70, 146, 54);
    if (tulip)
    {
        for (int y = 2; y <= 6; ++y)
            for (int x = 6; x <= 10; ++x)
                if (!(y == 2 && (x == 7 || x == 9)))
                    set(p, x, y, r - (x == 6 || x == 10 ? 30 : 0), g - (x == 6 || x == 10 ? 20 : 0),
                        b - (x == 6 || x == 10 ? 20 : 0));
    }
    else
    {
        for (int y = 2; y <= 7; ++y)
            for (int x = 5; x <= 11; ++x)
            {
                const int dx = x - 8, dy = y - 4;
                const int d = dx * dx + dy * dy;
                if (d <= 2)
                    set(p, x, y, centreR, centreG, centreB);
                else if (d <= 10 && !((dx == 3 || dx == -3) && (dy == 3 || dy == -2)))
                    set(p, x, y, r, g, b);
            }
    }
    return p;
}

Image makeDeadBush()
{
    Image p = PixelArt::blank();
    stroke(p, 8, 15, 8, 9, 120, 84, 44);
    stroke(p, 8, 11, 3, 5, 132, 94, 50);
    stroke(p, 8, 10, 12, 4, 132, 94, 50);
    stroke(p, 8, 9, 7, 3, 120, 84, 44);
    stroke(p, 5, 7, 2, 8, 140, 100, 56);
    stroke(p, 11, 6, 14, 7, 140, 100, 56);
    return p;
}

Image makeWheat()
{
    Image p = PixelArt::blank();
    for (int x = 1; x < 15; x += 2)
    {
        const int height = 10 + static_cast<int>(hash(x, 0, 181u) % 4u);
        for (int k = 0; k < height; ++k)
        {
            const bool ear = k >= height - 4;
            set(p, x, 15 - k, ear ? 226 : 150, ear ? 186 : 160, ear ? 62 : 60);
            if (ear && k % 2 == 0)
                set(p, x + 1, 15 - k, 204, 160, 50);
        }
    }
    return p;
}

Image makeFern()
{
    Image p = PixelArt::blank();
    for (int i = 0; i < 5; ++i)
    {
        const int tipX = 1 + i * 3 + static_cast<int>(hash(i, 0, 191u) % 2u);
        const int tipY = 3 + static_cast<int>(hash(i, 1, 191u) % 5u);
        stroke(p, 8, 15, tipX, tipY, 48, 112, 58);
        stroke(p, 8, 14, tipX + (i < 2 ? 1 : -1), tipY + 2, 64, 136, 70);
    }
    return p;
}

} // namespace

namespace LandTextures
{

Image make(int texture)
{
    switch (texture)
    {
    case GRASS_TOP:
        return makeGrassTop();
    case GRASS_SIDE:
        return makeFringed(false);
    case DIRT:
        return makeDirt();
    case STONE:
        return makeStone();
    case COBBLE:
        return makeCobble(false);
    case MOSSY:
        return makeCobble(true);
    case SAND:
        return mottled(222, 208, 162, 7, 201u);
    case GRAVEL:
        return makeGravel();
    case SNOW:
        return mottled(240, 246, 252, 4, 202u);
    case SNOW_SIDE:
        return makeFringed(true);
    case BASALT:
        return makeBasalt();
    case MAGMA:
        return makeMagma();
    case LAVA:
        return makeLava();
    case CLAY_RED:
        return makeClay(158, 72, 52, 211u);
    case CLAY_ORANGE:
        return makeClay(190, 104, 46, 213u);
    case CLAY_YELLOW:
        return makeClay(206, 156, 70, 215u);
    case CLAY_WHITE:
        return makeClay(222, 196, 174, 217u);
    case CLAY_BROWN:
        return makeClay(112, 70, 50, 219u);
    case CLAY_GREY:
        return makeClay(148, 116, 108, 221u);
    case RED_SAND:
        return mottled(196, 108, 44, 8, 203u);
    case OAK_SIDE:
        return makeLogSide(108, 84, 52, false, 231u);
    case OAK_TOP:
        return makeLogTop(108, 84, 52, 184, 148, 92);
    case BIRCH_SIDE:
        return makeLogSide(222, 222, 214, true, 235u);
    case BIRCH_TOP:
        return makeLogTop(222, 222, 214, 200, 178, 122);
    case SPRUCE_SIDE:
        return makeLogSide(64, 42, 24, false, 239u);
    case SPRUCE_TOP:
        return makeLogTop(64, 42, 24, 132, 100, 62);
    case LEAVES_OAK:
        return makeLeaves(58, 126, 44, 241u);
    case LEAVES_BIRCH:
        return makeLeaves(112, 160, 72, 243u);
    case LEAVES_SPRUCE:
        return makeLeaves(40, 88, 60, 245u);
    case LEAVES_CHERRY:
        return makeLeaves(240, 164, 200, 247u);
    case LEAVES_AUTUMN:
        return makeLeaves(222, 126, 40, 249u);
    case PLANKS:
        return makePlanks();
    case ROOF:
        return makeRoof();
    case WINDOW:
        return makeWindow();
    case DOOR:
        return makeDoor();
    case LANTERN:
        return makeLantern();
    case CRYSTAL:
        return makeCrystal();
    case ORE_COAL:
        return makeOre(40, 40, 44, 251u);
    case ORE_IRON:
        return makeOre(214, 170, 136, 253u);
    case ORE_GOLD:
        return makeOre(250, 214, 70, 255u);
    case ORE_DIAMOND:
        return makeOre(96, 232, 226, 257u);
    case CACTUS_SIDE:
        return makeCactusSide();
    case CACTUS_TOP:
        return mottled(84, 156, 64, 8, 204u);
    case MUSHROOM_CAP:
        return makeMushroomCap();
    case MUSHROOM_STEM:
        return mottled(226, 220, 204, 6, 205u);
    case HAY_SIDE:
        return makeHay(false);
    case HAY_TOP:
        return makeHay(true);
    case FARMLAND:
        return makeFarmland();
    case WATER:
        return makeWater();
    case TALL_GRASS:
        return makeBlades(84, 150, 52, 5, 261u);
    case FERN:
        return makeFern();
    case POPPY:
        return makeFlower(222, 44, 40, 40, 30, 30, false);
    case DANDELION:
        return makeFlower(250, 214, 50, 240, 170, 30, false);
    case CORNFLOWER:
        return makeFlower(84, 120, 236, 240, 230, 150, false);
    case TULIP:
        return makeFlower(246, 150, 190, 0, 0, 0, true);
    case DEAD_BUSH:
        return makeDeadBush();
    case WHEAT:
        return makeWheat();
    default:
        return PixelArt::blank();
    }
}

bool repeatsVertically(int texture)
{
    // tops tile both ways; sides clamp so their top row stays on top,
    // except the ones stretched over several blocks
    return texture == GRASS_TOP || texture == SAND || texture == OAK_SIDE;
}

} // namespace LandTextures
