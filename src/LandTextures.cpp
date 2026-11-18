/************************************************************************
     File:        LandTextures.cpp

     Comment:     See LandTextures.h. Every texture is drawn here, texel
                  by texel, at 32 x 32; row 0 is the top of the block.

                  The look is the classic block-game one drawn at double
                  resolution: clear shapes (stones, pebbles, boards, bark
                  grooves, leaf clusters) with a dark outline and a light
                  top-left edge, shaded in a few flat steps rather than
                  smooth gradients, so the pixels stay crisp. All of it is
                  original and generated here; no image files are used.

*************************************************************************/

#include "LandTextures.h"

#include <math.h>
#include <stdlib.h>

namespace
{

using PixelArt::hash;
using PixelArt::Image;
using PixelArt::set;

const int S = PixelArt::kFineSize;

// A repeatable value in -range .. +range for a texel.
int shadeOf(int x, int y, unsigned int seed, int range)
{
    return static_cast<int>(hash(x, y, seed) % static_cast<unsigned int>(2 * range + 1)) - range;
}

// A repeatable value in 0 .. 1.
float unit(int x, int y, unsigned int seed)
{
    return static_cast<float>(hash(x, y, seed) % 1024u) / 1023.0f;
}

// Rounds v to a multiple of step: flat steps of shade, as pixel art has.
int stepped(float v, int step)
{
    return static_cast<int>(floorf(v / static_cast<float>(step) + 0.5f)) * step;
}

// Smooth noise in 0 .. 1 that tiles across the texture: `cells` lattice
// cells across it in x, `cellsY` in y.
float noise(int x, int y, int cells, int cellsY, unsigned int seed)
{
    const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(cells) / S;
    const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(cellsY) / S;
    const int ix = static_cast<int>(floorf(fx)), iy = static_cast<int>(floorf(fy));
    float tx = fx - static_cast<float>(ix), ty = fy - static_cast<float>(iy);
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    auto at = [&](int cx, int cy) {
        return unit(((cx % cells) + cells) % cells, ((cy % cellsY) + cellsY) % cellsY, seed);
    };
    const float a = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * tx;
    const float b = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * tx;
    return a + (b - a) * ty;
}

// Three octaves of noise, 0 .. 1.
float fbm(int x, int y, int cells, unsigned int seed)
{
    return (noise(x, y, cells, cells, seed) * 4.0f +
            noise(x, y, cells * 2, cells * 2, seed + 7u) * 2.0f +
            noise(x, y, cells * 4, cells * 4, seed + 13u)) /
           7.0f;
}

// Irregular cells (stones, pebbles, leaf clusters) that tile across the
// texture: one point in each square of an n x n grid, moved about inside it.
struct Cells
{
    int n;
    float px[64], py[64];

    Cells(int count, unsigned int seed, float jitter) : n(count)
    {
        const float cell = static_cast<float>(S) / static_cast<float>(n);
        for (int i = 0; i < n * n; ++i)
        {
            const float jx = (unit(i, 0, seed) - 0.5f) * jitter;
            const float jy = (unit(i, 1, seed) - 0.5f) * jitter;
            px[i] = (static_cast<float>(i % n) + 0.5f + jx) * cell;
            py[i] = (static_cast<float>(i / n) + 0.5f + jy) * cell;
        }
    }

    // The nearest cell, the distance to its centre (d1), the gap to the
    // next nearest (0 on a border) and the offset from the centre.
    void find(int x, int y, int &id, float &d1, float &gap, float &ox, float &oy) const
    {
        float best = 1.0e9f, second = 1.0e9f;
        id = 0;
        ox = oy = 0.0f;
        const float fx = static_cast<float>(x) + 0.5f, fy = static_cast<float>(y) + 0.5f;
        for (int i = 0; i < n * n; ++i)
            for (int wy = -1; wy <= 1; ++wy)
                for (int wx = -1; wx <= 1; ++wx)
                {
                    const float dx = fx - px[i] - static_cast<float>(wx * S);
                    const float dy = fy - py[i] - static_cast<float>(wy * S);
                    const float d = sqrtf(dx * dx + dy * dy);
                    if (d < best)
                    {
                        second = best;
                        best = d;
                        id = i;
                        ox = dx;
                        oy = dy;
                    }
                    else if (d < second)
                        second = d;
                }
        d1 = best;
        gap = second - best;
    }
};

// Rounded lumps (cobbles, pebbles): a dark rim on the borders, a light
// edge on the top-left of every lump and a shadow on its bottom-right.
// Calls paint(x, y, tone, id) with tone in about -40 .. +30.
template <typename Paint> void lumps(const Cells &cells, float rim, unsigned int seed, Paint paint)
{
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            int id;
            float d1, gap, ox, oy;
            cells.find(x, y, id, d1, gap, ox, oy);
            int tone;
            if (gap < rim)
                tone = -40; // the gap between two lumps
            else
            {
                tone = shadeOf(id, 0, seed, 14);
                const float edge = gap < rim + 1.6f ? 1.0f : 0.0f;
                const float light = -(ox + oy) / (d1 + 0.001f); // towards the top-left
                if (edge > 0.0f && light > 0.35f)
                    tone += 22;
                else if (edge > 0.0f && light < -0.35f)
                    tone -= 18;
                tone += stepped(static_cast<float>(shadeOf(x, y, seed + 1u, 6)), 3);
            }
            paint(x, y, tone, id);
        }
}

Image makeGrassTop()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int patch = stepped((fbm(x, y, 4, 1u) - 0.5f) * 40.0f, 6);
            const unsigned int h = hash(x / 1, y / 2, 2u); // blades are two texels tall
            int r = 104 + patch, g = 166 + patch * 3 / 2, b = 60 + patch / 2;
            if (h % 7u == 0)
            {
                r -= 22;
                g -= 30;
                b -= 14; // a blade in shadow
            }
            else if (h % 9u == 0)
            {
                r += 16;
                g += 22;
                b += 8; // a blade catching the light
            }
            const int s = shadeOf(x, y, 3u, 3);
            set(p, x, y, r + s, g + s, b + s);
        }
    return p;
}

void dirtTexel(Image &p, int x, int y)
{
    const int patch = stepped((fbm(x, y, 4, 5u) - 0.5f) * 30.0f, 5);
    const unsigned int h = hash(x / 2, y, 6u);
    const int s = shadeOf(x, y, 7u, 4);
    int r = 136 + patch, g = 98 + patch, b = 68 + patch * 3 / 4;
    if (h % 7u == 0)
    {
        r -= 30;
        g -= 24;
        b -= 18; // a dark clod
    }
    else if (hash(x, y, 8u) % 19u == 0)
    {
        r += 26;
        g += 30;
        b += 34; // a grey pebble
    }
    else if (h % 11u == 0)
    {
        r += 14;
        g += 10;
        b += 6;
    }
    set(p, x, y, r + s, g + s, b + s);
}

Image makeDirt()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
            dirtTexel(p, x, y);
    return p;
}

// Dirt with a ragged fringe along the top that hangs down in a few places:
// grass or snow. The dirt just under the fringe is in its shadow.
Image makeFringed(bool snow)
{
    Image p = makeDirt();
    for (int x = 0; x < S; ++x)
    {
        int fringe = 5 + static_cast<int>(noise(x, 0, 8, 1, 9u) * 4.0f);
        if (hash(x / 2, 0, 10u) % 5u == 0)
            fringe += 3 + static_cast<int>(hash(x / 2, 1, 10u) % 4u); // a drip
        for (int y = 0; y < fringe; ++y)
        {
            const int s = shadeOf(x, y, 11u, 4);
            const bool lower = y >= fringe - 2;
            if (snow)
                set(p, x, y, (lower ? 214 : 238) + s, (lower ? 226 : 246) + s,
                    (lower ? 238 : 252) + s);
            else
            {
                const int patch = hash(x, y / 2, 2u) % 7u == 0 ? -24 : 0;
                set(p, x, y, (lower ? 84 : 104) + patch + s, (lower ? 136 : 166) + patch + s,
                    (lower ? 48 : 60) + patch / 2 + s);
            }
        }
        // the shadow it casts on the dirt
        const unsigned char *c = &p[static_cast<size_t>((fringe * S + x) * 4)];
        set(p, x, fringe, c[0] * 3 / 4, c[1] * 3 / 4, c[2] * 3 / 4);
    }
    return p;
}

// Stone: blotches of lighter and darker grey in a few flat steps, with
// small dark pits and pale flecks.
Image makeStone(int r = 126, int g = 126, int b = 128, unsigned int seed = 21u)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float n = fbm(x, y, 4, seed) * 0.75f + noise(x, y, 16, 16, seed + 4u) * 0.25f;
            int v = stepped((n - 0.5f) * 64.0f, 9) + shadeOf(x, y, seed + 2u, 3);
            const unsigned int h = hash(x / 2, y, seed + 1u);
            if (h % 23u == 0)
                v -= 24; // a pit
            else if (h % 29u == 0)
                v += 18; // a fleck
            set(p, x, y, r + v, g + v, b + v);
        }
    return p;
}

// Cobblestone: rounded stones of different greys with dark gaps; moss
// creeps out of the gaps when asked.
Image makeCobble(bool mossy)
{
    Image p = PixelArt::blank(S);
    const Cells cells(4, 31u, 0.9f);
    lumps(cells, 1.1f, 32u, [&](int x, int y, int tone, int) {
        const float moss = fbm(x, y, 4, 33u);
        if (mossy && moss > 0.5f && (tone == -40 || moss > 0.62f))
        {
            const int s = stepped(static_cast<float>(shadeOf(x, y, 34u, 12)), 6);
            set(p, x, y, 72 + s, 118 + s, 50 + s / 2);
        }
        else if (tone == -40)
            set(p, x, y, 66, 66, 70);
        else
            set(p, x, y, 132 + tone, 132 + tone, 134 + tone);
    });
    return p;
}

// Gravel: a heap of small pebbles in greys and browns.
Image makeGravel()
{
    Image p = PixelArt::blank(S);
    const Cells cells(6, 41u, 1.0f);
    lumps(cells, 0.8f, 42u, [&](int x, int y, int tone, int id) {
        if (tone == -40)
            set(p, x, y, 82, 76, 74);
        else
        {
            const int warm = static_cast<int>(hash(id, 3, 43u) % 3u) * 6; // some brownish
            set(p, x, y, 136 + tone + warm, 130 + tone, 126 + tone - warm / 2);
        }
    });
    return p;
}

// Fine sand: even grains, a few darker and lighter.
Image makeSand(int r, int g, int b, unsigned int seed)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int patch = stepped((fbm(x, y, 4, seed) - 0.5f) * 20.0f, 4);
            const unsigned int h = hash(x, y, seed + 1u);
            int s = shadeOf(x, y, seed + 2u, 4) + patch;
            if (h % 9u == 0)
                s -= 16;
            else if (h % 13u == 0)
                s += 12;
            set(p, x, y, r + s, g + s, b + s * 3 / 4);
        }
    return p;
}

Image makeSnow()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int patch = stepped((fbm(x, y, 4, 202u) - 0.5f) * 16.0f, 4);
            const int s = shadeOf(x, y, 203u, 2);
            set(p, x, y, 238 + patch + s, 245 + patch + s, 252);
        }
    return p;
}

Image makeBasalt()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            // columns of dark rock, each a little different, with a groove between
            const int column = x / 6;
            const int tone = shadeOf(column, 0, 51u, 10);
            const int streak = stepped((noise(x, y, 16, 2, 52u) - 0.5f) * 20.0f, 5);
            const int s = shadeOf(x, y, 53u, 3);
            if (x % 6 == 5)
                set(p, x, y, 40 + s, 38 + s, 46 + s);
            else
                set(p, x, y, 66 + tone + streak + s, 64 + tone + streak + s,
                    72 + tone + streak + s);
        }
    return p;
}

// Magma: dark red-brown lumps of rock in a net of glowing cracks.
Image makeMagma()
{
    Image p = PixelArt::blank(S);
    const Cells cells(4, 61u, 0.8f);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            int id;
            float d1, gap, ox, oy;
            cells.find(x, y, id, d1, gap, ox, oy);
            const int s = shadeOf(x, y, 62u, 6);
            if (gap < 0.9f)
                set(p, x, y, 255, 200 + s, 80); // the crack's hot middle
            else if (gap < 2.2f)
                set(p, x, y, 232 + s, 104 + s, 26); // its glowing edge
            else
            {
                const int tone = shadeOf(id, 0, 63u, 10) + stepped(static_cast<float>(s), 3);
                set(p, x, y, 94 + tone, 36 + tone / 2, 26 + tone / 2); // the rock
            }
        }
    return p;
}

Image makeLava()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            // slow swirls of yellow in orange, with a darker crust here and there
            const float n = fbm(x, y, 3, 71u);
            const float swirl = sinf((static_cast<float>(x) * 0.2f + n * 9.0f));
            const int s = shadeOf(x, y, 72u, 6);
            if (swirl > 0.55f)
                set(p, x, y, 255, 220 + s, 110 + s);
            else if (n < 0.3f)
                set(p, x, y, 180 + s, 64 + s, 20);
            else
                set(p, x, y, 244 + s, 128 + stepped(swirl * 30.0f, 10), 28);
        }
    return p;
}

// Fired clay of the badlands: flat colour with a fine horizontal grain.
Image makeClay(int r, int g, int b, unsigned int seed)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int grain = stepped((noise(x, y, 4, 16, seed) - 0.5f) * 14.0f, 3);
            const int s = shadeOf(x, y, seed + 1u, 3);
            set(p, x, y, r + grain + s, g + grain + s, b + grain + s);
        }
    return p;
}

// Bark running up the log: grooves of darker wood between ridges.
Image makeLogSide(int r, int g, int b, unsigned int seed)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float n =
                noise(x, y, 16, 2, seed) * 0.65f + noise(x, y, 32, 6, seed + 1u) * 0.35f;
            const int s = shadeOf(x, y, seed + 2u, 3);
            if (n < 0.32f)
                set(p, x, y, r * 3 / 5 + s, g * 3 / 5 + s, b * 3 / 5 + s); // a groove
            else if (n > 0.7f)
                set(p, x, y, r + 18 + s, g + 16 + s, b + 10 + s); // a lit ridge
            else
                set(p, x, y, r + stepped((n - 0.5f) * 30.0f, 6) + s,
                    g + stepped((n - 0.5f) * 26.0f, 6) + s, b + stepped((n - 0.5f) * 18.0f, 6) + s);
        }
    return p;
}

// Birch: white bark with black dashes across it.
Image makeBirchSide()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = stepped((noise(x, y, 8, 4, 235u) - 0.5f) * 16.0f, 4);
            const int s = shadeOf(x, y, 236u, 3);
            set(p, x, y, 220 + tone + s, 220 + tone + s, 212 + tone + s);
        }
    for (int i = 0; i < 14; ++i)
    {
        const int x = static_cast<int>(hash(i, 0, 237u) % 30u);
        const int y = static_cast<int>(hash(i, 1, 237u) % 32u);
        const int length = 3 + static_cast<int>(hash(i, 2, 237u) % 6u);
        const bool thick = hash(i, 3, 237u) % 3u == 0;
        for (int k = 0; k < length; ++k)
        {
            set(p, (x + k) % S, y, 46, 44, 42);
            if (thick && k > 0 && k + 1 < length)
                set(p, (x + k) % S, (y + 1) % S, 70, 68, 64);
        }
    }
    return p;
}

// The cut end of a log: round growth rings inside a band of bark.
Image makeLogTop(int barkR, int barkG, int barkB, int woodR, int woodG, int woodB)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float dx = static_cast<float>(x) - 15.5f, dy = static_cast<float>(y) - 15.5f;
            // a rounded square: rings follow the log's outline near the edge
            const float d = powf(powf(fabsf(dx), 3.0f) + powf(fabsf(dy), 3.0f), 1.0f / 3.0f);
            const int s = shadeOf(x, y, 81u, 3);
            if (d > 13.6f)
                set(p, x, y, barkR + s + shadeOf(x, y, 82u, 8), barkG + s, barkB + s);
            else if (static_cast<int>(d + noise(x, y, 6, 6, 83u) * 1.2f) % 3 == 0)
                set(p, x, y, woodR - 28 + s, woodG - 28 + s, woodB - 22 + s); // a ring
            else
                set(p, x, y, woodR + s, woodG + s, woodB + s);
        }
    return p;
}

// Leaves: clusters of leaves, each a little lighter on top, with dark
// gaps of shade between them (drawn solid, as on fast graphics).
Image makeLeaves(int r, int g, int b, unsigned int seed)
{
    Image p = PixelArt::blank(S);
    const Cells cells(6, seed, 1.0f);
    lumps(cells, 0.7f, seed + 1u, [&](int x, int y, int tone, int) {
        if (tone == -40)
            set(p, x, y, r * 2 / 5, g * 2 / 5, b * 2 / 5);
        else
        {
            const int t = tone * 3 / 4 + (hash(x, y, seed + 2u) % 6u == 0 ? -26 : 0);
            set(p, x, y, r + t, g + t, b + t * 3 / 4);
        }
    });
    return p;
}

// Planks (oak unless told): four boards with the grain running along them, a dark seam
// under each and its ends at different places.
Image makePlanks(int r = 168, int g = 134, int b = 84, bool nails = false)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int board = y / 8;
            const int end = static_cast<int>(hash(board, 0, 91u) % 32u);
            const int tone = shadeOf(board, 1, 91u, 8);
            const int grain = stepped(
                (noise(x, y, 4, 16, 92u + static_cast<unsigned int>(board)) - 0.5f) * 30.0f, 6);
            const int s = shadeOf(x, y, 93u, 3);
            if (y % 8 == 7 || x == end)
                set(p, x, y, r * 5 / 8 + s, g * 5 / 8 + s, b * 5 / 8 + s); // seam
            else if (nails && y % 8 == 3 && (x == (end + 3) % S || x == (end + 28) % S))
                set(p, x, y, 70, 72, 78); // a nail
            else if (y % 8 == 0)
                set(p, x, y, r + 22 + tone + s, g + 20 + tone + s,
                    b + 14 + tone + s); // the lit edge
            else
                set(p, x, y, r + tone + grain + s, g + tone + grain + s,
                    b + tone + grain * 2 / 3 + s);
        }
    return p;
}

// Rows of shingles, each lighter at its lower edge, dark gaps between.
Image makeRoof()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int row = y / 8;
            const int sx = (x + (row % 2) * 4) % 8;
            const int tile = (x + (row % 2) * 4) / 8;
            const int tone = shadeOf(tile, row, 102u, 10);
            const int s = shadeOf(x, y, 101u, 3);
            const int down = (y % 8) * 3; // lighter towards the lower edge
            if (y % 8 == 7 || sx == 7)
                set(p, x, y, 84 + s, 30 + s, 28 + s);
            else
                set(p, x, y, 146 + tone + down + s, 54 + tone / 2 + down / 2 + s,
                    44 + tone / 2 + down / 3 + s);
        }
    return p;
}

// Four lit panes in a wooden frame.
Image makeWindow()
{
    Image p = makePlanks();
    for (int y = 4; y < 28; ++y)
        for (int x = 4; x < 28; ++x)
        {
            const bool bar =
                (x >= 15 && x <= 16) || (y >= 15 && y <= 16) || x < 6 || x > 25 || y < 6 || y > 25;
            if (bar)
                set(p, x, y, x < 6 || y < 6 ? 96 : 72, x < 6 || y < 6 ? 70 : 50,
                    x < 6 || y < 6 ? 42 : 30);
            else
            {
                // warm light, brighter in the middle of each pane
                const int cx = x < 16 ? 10 : 21, cy = y < 16 ? 10 : 21;
                const int d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                set(p, x, y, 255, 236 - d, 150 - d * 2);
            }
        }
    return p;
}

Image makeDoor()
{
    Image p = makePlanks();
    for (int y = 2; y < S; ++y)
        for (int x = 6; x < 26; ++x)
        {
            const int s = shadeOf(x, y, 111u, 4);
            const bool edge = x <= 7 || x >= 24 || y <= 3;
            const bool pane = y >= 6 && y <= 13 && ((x >= 10 && x <= 14) || (x >= 17 && x <= 21));
            const bool plank = x == 15 || x == 16;
            if (edge)
                set(p, x, y, 70, 48, 26);
            else if (pane)
                set(p, x, y, 58 + (x + y) % 3 * 6, 68, 86);
            else if (plank)
                set(p, x, y, 100, 68, 36);
            else
                set(p, x, y, 128 + s, 88 + s, 48 + s);
        }
    for (int y = 19; y <= 22; ++y)
    {
        set(p, 20, y, 236, 200, 80);
        set(p, 21, y, 180, 148, 56);
    }
    return p;
}

Image makeLantern()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const bool frame = x <= 1 || y <= 1 || x >= 30 || y >= 30 || x == 15 || x == 16;
            const float dx = static_cast<float>(x) - 15.5f, dy = static_cast<float>(y) - 15.5f;
            const int d = static_cast<int>(sqrtf(dx * dx + dy * dy));
            if (frame)
                set(p, x, y, x <= 1 || y <= 1 ? 92 : 64, x <= 1 || y <= 1 ? 70 : 48, 36);
            else
                set(p, x, y, 255, 252 - stepped(static_cast<float>(d * 5), 10),
                    196 - stepped(static_cast<float>(d * 9), 12));
        }
    return p;
}

Image makeCrystal()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int facet = ((x + y) / 8 + (x - y + 32) / 10) % 3;
            const int s = shadeOf(x, y, 121u, 6);
            const bool glint = (x + y) % 11 == 0 && facet == 2;
            set(p, x, y, (glint ? 220 : 110 + facet * 40) + s, 222 + facet * 12 + s, 244);
        }
    return p;
}

// Stone with clusters of the ore in it: each nugget has a dark rim and a
// light glint on its top-left.
Image makeOre(int r, int g, int b, unsigned int seed)
{
    Image p = makeStone();
    for (int i = 0; i < 5; ++i)
    {
        const int cx = 3 + static_cast<int>(hash(i, 0, seed) % 26u);
        const int cy = 3 + static_cast<int>(hash(i, 1, seed) % 26u);
        for (int k = 0; k < 3; ++k)
        {
            const int x = cx + static_cast<int>(hash(i, k + 2, seed) % 5u) - 2;
            const int y = cy + static_cast<int>(hash(i, k + 7, seed) % 5u) - 2;
            // a 3 x 3 nugget: rim, body, glint
            for (int dy = -1; dy <= 2; ++dy)
                for (int dx = -1; dx <= 2; ++dx)
                {
                    const bool rim = dx == -1 || dy == -1 || dx == 2 || dy == 2;
                    if (rim && (dx == -1 || dx == 2) && (dy == -1 || dy == 2))
                        continue; // round the corners
                    if (rim)
                        set(p, x + dx, y + dy, r * 2 / 5, g * 2 / 5, b * 2 / 5);
                    else if (dx == 0 && dy == 0)
                        set(p, x + dx, y + dy, r + (255 - r) / 2, g + (255 - g) / 2,
                            b + (255 - b) / 2);
                    else
                        set(p, x + dx, y + dy, r - (dx + dy) * 12, g - (dx + dy) * 12,
                            b - (dx + dy) * 12);
                }
        }
    }
    return p;
}

Image makeCactusSide()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 131u, 4);
            const int rib = x % 8;
            if (rib == 2)
                set(p, x, y, 38 + s, 96 + s, 34 + s); // groove
            else if (rib == 3 || rib == 1)
                set(p, x, y, 64 + s, 132 + s, 52 + s);
            else
                set(p, x, y, 84 + s + (rib == 5 ? 12 : 0), 156 + s + (rib == 5 ? 14 : 0), 64 + s);
            if (rib == 3 && y % 9 == 4)
            {
                set(p, x, y, 236, 236, 206); // spine
                set(p, x + 1, y, 200, 200, 170);
            }
        }
    return p;
}

Image makeCactusTop()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int dx = x < 16 ? 15 - x : x - 16, dy = y < 16 ? 15 - y : y - 16;
            const int ring = dx > dy ? dx : dy;
            const int s = shadeOf(x, y, 204u, 4);
            if (ring % 5 == 4)
                set(p, x, y, 52 + s, 116 + s, 44 + s);
            else
                set(p, x, y, 88 + s, 160 + s, 68 + s);
        }
    return p;
}

Image makeMushroomCap()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = stepped((fbm(x, y, 4, 141u) - 0.5f) * 24.0f, 6);
            set(p, x, y, 200 + tone, 40 + tone / 2, 36 + tone / 2);
        }
    const int spots[6][3] = {{4, 4, 3},   {20, 3, 3}, {12, 14, 4},
                             {25, 20, 3}, {4, 23, 3}, {16, 27, 2}};
    for (const int *spot : spots)
        for (int dy = -spot[2]; dy <= spot[2]; ++dy)
            for (int dx = -spot[2]; dx <= spot[2]; ++dx)
            {
                const int d = dx * dx + dy * dy;
                if (d > spot[2] * spot[2] + 1)
                    continue;
                const bool shade = dx + dy > spot[2];
                set(p, spot[0] + dx, spot[1] + dy, shade ? 214 : 246, shade ? 206 : 240,
                    shade ? 192 : 228);
            }
    return p;
}

Image makeStem()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = stepped((noise(x, y, 8, 2, 205u) - 0.5f) * 20.0f, 5);
            const int s = shadeOf(x, y, 206u, 3);
            set(p, x, y, 226 + tone + s, 220 + tone + s, 204 + tone + s);
        }
    return p;
}

Image makeHay(bool top)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            // straws: along y on the sides, a jumble on the top
            const float n = top ? fbm(x, y, 8, 151u) : noise(x, y, 16, 2, 151u);
            const int straw = stepped((n - 0.5f) * 50.0f, 8);
            const int s = shadeOf(x, y, 152u, 4);
            if (!top && (x / 2 == 4 || x / 2 == 11))
                set(p, x, y, 150 + s, 58 + s, 42 + s); // binding
            else if (n < 0.3f)
                set(p, x, y, 170 + s, 132 + s, 40 + s);
            else
                set(p, x, y, 216 + straw + s, 180 + straw + s, 62 + straw / 2 + s);
        }
    return p;
}

Image makeFarmland()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 161u, 5) + stepped((fbm(x, y, 4, 162u) - 0.5f) * 16.0f, 4);
            const int furrow = (x % 8 == 0 || x % 8 == 7) ? -26 : (x % 8 == 1 ? 10 : 0);
            set(p, x, y, 96 + furrow + s, 64 + furrow + s, 40 + furrow + s);
        }
    return p;
}

// Still water as in a block game: an even light blue, smooth rather than
// grainy, with a few faint streaks along the rows, a little lighter or a
// little darker; see-through (alpha about 0.6; the shader cuts out texels
// below 0.5). sea: the same in the turquoise of the sea, for water that runs
// into it.
Image makeWater(bool sea)
{
    const int base[3] = {sea ? 92 : 104, sea ? 168 : 160, sea ? 200 : 206};
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
            set(p, x, y, base[0], base[1], base[2], 152);
    // streaks: short runs along a row, a few lighter, a few darker
    for (int i = 0; i < 16; ++i)
    {
        const int x0 = static_cast<int>(hash(i, 0, 171u) % 32u);
        const int y = static_cast<int>(hash(i, 1, 171u) % 32u);
        const int length = 4 + static_cast<int>(hash(i, 2, 171u) % 7u);
        const int shift = i % 3 == 0 ? -12 : 10;
        for (int k = 0; k < length; ++k)
            set(p, (x0 + k) % S, y, base[0] + shift, base[1] + shift, base[2] + shift, 156);
    }
    return p;
}

// A line two texels wide (a stroke on the 16 grid, drawn on the 32 one).
void stroke(Image &p, int x0, int y0, int x1, int y1, int r, int g, int b)
{
    const int dx = x1 - x0, dy = y1 - y0;
    const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const int steps = (adx > ady ? adx : ady) * 2;
    for (int i = 0; i <= steps; ++i)
    {
        const int x = x0 * 2 + (steps == 0 ? 0 : dx * 2 * i / steps);
        const int y = y0 * 2 + (steps == 0 ? 0 : dy * 2 * i / steps);
        const int s = shadeOf(x, y, 175u, 6);
        set(p, x, y, r + s, g + s, b + s);
        set(p, x + 1, y, r - 14 + s, g - 14 + s, b - 10 + s); // its shaded side
        set(p, x, y + 1, r + s, g + s, b + s);
        set(p, x + 1, y + 1, r - 14 + s, g - 14 + s, b - 10 + s);
    }
}

// Blades growing up from the bottom row, each two texels wide, darker at
// the root and with a light edge.
Image makeBlades(int r, int g, int b, int minHeight, unsigned int seed)
{
    Image p = PixelArt::blank(S);
    for (int x = 2; x < 30; x += 2)
    {
        if (hash(x, 0, seed) % 4u == 0)
            continue;
        const int height = minHeight * 2 + static_cast<int>(hash(x, 1, seed) % 12u);
        const int lean = static_cast<int>(hash(x, 2, seed) % 3u) - 1;
        const int s = shadeOf(x, 3, seed, 12);
        for (int k = 0; k < height; ++k)
        {
            const int bx = x + (k > height / 2 ? lean : 0) + (k > height * 4 / 5 ? lean : 0);
            const int lift = k * 40 / height; // lighter towards the tip
            set(p, bx, 31 - k, r - 20 + lift + s, g - 26 + lift + s, b - 10 + lift / 2 + s);
            if (k < height - 2)
                set(p, bx + 1, 31 - k, r - 34 + lift + s, g - 40 + lift + s, b - 16 + lift / 2 + s);
        }
    }
    return p;
}

// A flower: a green stalk with two leaves and a head of the given colours.
Image makeFlower(int r, int g, int b, int centreR, int centreG, int centreB, bool tulip)
{
    Image p = PixelArt::blank(S);
    stroke(p, 8, 15, 8, 7, 60, 132, 48);
    stroke(p, 7, 12, 5, 10, 70, 146, 54);
    stroke(p, 9, 13, 11, 11, 70, 146, 54);
    for (int y = 2; y <= 16; ++y)
        for (int x = 8; x <= 24; ++x)
        {
            const float dx = static_cast<float>(x) - 16.0f, dy = static_cast<float>(y) - 9.0f;
            const float d = sqrtf(dx * dx + dy * dy);
            const int s = shadeOf(x, y, 176u, 8);
            const int shade = dx + dy > 2.0f ? -34 : (dx + dy < -4.0f ? 22 : 0);
            if (tulip)
            {
                if (y >= 4 && y <= 14 && x >= 12 && x <= 20 &&
                    !(y < 7 && (x == 14 || x == 18 || x == 15 || x == 17)))
                    set(p, x, y, r + shade + s, g + shade + s, b + shade + s);
            }
            else if (d <= 2.6f)
                set(p, x, y, centreR + s, centreG + s, centreB + s);
            else if (d <= 6.4f && fabsf(atan2f(dy, dx) * 5.0f / 3.14159f -
                                        floorf(atan2f(dy, dx) * 5.0f / 3.14159f) - 0.5f) <
                                      0.42f + (6.4f - d) * 0.06f)
                set(p, x, y, r + shade + s, g + shade + s, b + shade + s); // five petals
        }
    return p;
}

Image makeDeadBush()
{
    Image p = PixelArt::blank(S);
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
    Image p = PixelArt::blank(S);
    for (int x = 2; x < 30; x += 4)
    {
        const int height = 20 + static_cast<int>(hash(x, 0, 181u) % 8u);
        for (int k = 0; k < height; ++k)
        {
            const bool ear = k >= height - 9;
            const int s = shadeOf(x, k, 182u, 8);
            set(p, x, 31 - k, (ear ? 228 : 150) + s, (ear ? 188 : 162) + s, (ear ? 64 : 60) + s);
            if (ear && k % 3 != 2)
            {
                set(p, x - 1, 31 - k, 206 + s, 162 + s, 50);
                set(p, x + 1, 31 - k, 190 + s, 146 + s, 44);
            }
        }
    }
    return p;
}

Image makeFern()
{
    Image p = PixelArt::blank(S);
    for (int i = 0; i < 5; ++i)
    {
        const int tipX = 1 + i * 3 + static_cast<int>(hash(i, 0, 191u) % 2u);
        const int tipY = 3 + static_cast<int>(hash(i, 1, 191u) % 5u);
        stroke(p, 8, 15, tipX, tipY, 48, 112, 58);
        // leaflets along the frond
        for (int k = 1; k < 5; ++k)
        {
            const int x = (8 * 2 * (5 - k) + tipX * 2 * k) / 5;
            const int y = (15 * 2 * (5 - k) + tipY * 2 * k) / 5;
            set(p, x - 2, y - 1, 70, 146, 72);
            set(p, x + 2, y - 1, 70, 146, 72);
        }
    }
    return p;
}

// Fallen petals: small pink flakes on the lowest rows of the cut-out, so
// the plant reads as a carpet of blossom on the grass.
Image makePetals()
{
    Image p = PixelArt::blank(S);
    for (int i = 0; i < 26; ++i)
    {
        const int x = static_cast<int>(hash(i, 0, 271u) % 30u);
        const int y = 22 + static_cast<int>(hash(i, 1, 271u) % 9u);
        const int s = shadeOf(i, 2, 271u, 14);
        set(p, x, y, 246 + s / 2, 172 + s, 206 + s);
        set(p, x + 1, y, 236 + s / 2, 156 + s, 196 + s);
        if (hash(i, 3, 271u) % 2u == 0)
            set(p, x, y + 1, 228 + s / 2, 146 + s, 186 + s);
    }
    return p;
}

// Packed ice: pale blue with lighter cracks and frosty specks.
Image makeIce()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = stepped((fbm(x, y, 4, 281u) - 0.5f) * 30.0f, 6);
            const unsigned int h = hash(x, y, 282u);
            int r = 162 + tone, g = 196 + tone, b = 238 + tone / 2;
            if (h % 17u == 0)
            {
                r += 30;
                g += 26;
                b += 12; // frost
            }
            set(p, x, y, r, g, b);
        }
    const Cells cracks(3, 283u, 0.9f);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            int id;
            float d1, gap, ox, oy;
            cracks.find(x, y, id, d1, gap, ox, oy);
            if (gap < 0.7f)
                set(p, x, y, 214, 234, 252);
        }
    return p;
}

// Pumpkin skin: orange ribs running down, darker in the grooves.
void pumpkinSkin(Image &p, bool top)
{
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int rib = top ? ((abs(x - 16) + abs(y - 16)) % 6) : (x % 6);
            const int s = shadeOf(x, y, 291u, 5);
            const int groove = rib == 0 ? -40 : (rib == 3 ? 14 : 0);
            set(p, x, y, 222 + groove + s, 118 + groove / 2 + s, 22 + s);
        }
}

// A carved pumpkin lantern: triangle eyes and a jagged grin, lit from
// inside in warm yellow.
Image makePumpkinFace()
{
    Image p = PixelArt::blank(S);
    pumpkinSkin(p, false);
    auto glow = [&](int x, int y) {
        const int s = shadeOf(x, y, 293u, 10);
        set(p, x, y, 255, 214 + s, 96 + s);
    };
    for (int y = 0; y < 6; ++y) // the eyes, points up
        for (int x = -y; x <= y; ++x)
        {
            glow(9 + x, 8 + y);
            glow(22 + x, 8 + y);
        }
    for (int x = 6; x <= 25; ++x) // the grin, with teeth
        for (int y = 19; y <= 23; ++y)
        {
            const bool tooth = (x % 5 == 0 && y == 19) || (x % 5 == 3 && y == 23);
            if (!tooth && !(y == 23 && (x < 8 || x > 23)))
                glow(x, y);
        }
    return p;
}

Image makePumpkinTop()
{
    Image p = PixelArt::blank(S);
    pumpkinSkin(p, true);
    for (int y = 13; y <= 18; ++y)
        for (int x = 14; x <= 17; ++x)
            set(p, x, y, 84 + shadeOf(x, y, 295u, 8), 112, 40); // the stem
    return p;
}

// A bamboo stalk the full height of the cell, so stalks stack into tall
// canes: light on its left edge, shaded on its right, with a ring at each
// node and now and then a small leaf.
Image makeBamboo(bool top)
{
    Image p = PixelArt::blank(S);
    const int from = top ? 16 : 0;
    for (int y = from; y < S; ++y)
        for (int x = 13; x <= 18; ++x)
        {
            const int s = shadeOf(x, y, 301u, 4);
            const bool node = y % 16 == 7 || y % 16 == 8;
            const int edge = x == 13 ? 26 : (x >= 17 ? -24 : 0);
            if (node)
                set(p, x, y, 150 + edge / 2 + s, 186 + s, 92 + s);
            else
                set(p, x, y, 104 + edge + s, 162 + edge + s, 58 + edge / 2 + s);
        }
    // leaves: a spray of blades at the top, a single one lower down
    auto blade = [&](int x0, int y0, int dx, int dy, int length) {
        for (int k = 0; k < length; ++k)
        {
            const int s = shadeOf(x0 + dx * k, y0, 303u, 10);
            set(p, x0 + dx * k / 2, y0 + dy * k / 3, 70 + s, 148 + s, 50 + s);
            set(p, x0 + dx * k / 2, y0 + dy * k / 3 + 1, 52 + s, 118 + s, 40 + s);
        }
    };
    if (top)
        for (int i = 0; i < 6; ++i)
            blade(15, 16 - i * 2, i % 2 == 0 ? 1 : -1, -1, 14 + i % 3 * 3);
    else
        blade(18, 9, 1, 1, 10);
    return p;
}

// Vines hanging in strands of small leaves, longer and shorter.
Image makeVines()
{
    Image p = PixelArt::blank(S);
    for (int k = 0; k < 6; ++k)
    {
        const int x = 2 + k * 5 + static_cast<int>(hash(k, 0, 311u) % 3u);
        const int length = 18 + static_cast<int>(hash(k, 1, 311u) % 15u);
        for (int y = 0; y < length && y < S; ++y)
        {
            const int s = shadeOf(x, y, 313u, 12);
            const int wobble = (y / 4 + k) % 2;
            set(p, x + wobble, y, 64 + s, 128 + s, 44 + s);
            if (y % 3 == 0)
                set(p, x + wobble + (k % 2 == 0 ? 1 : -1), y, 92 + s, 160 + s, 60 + s);
        }
    }
    return p;
}

// Mud: dark wet earth with clumps and a few glossy specks.
Image makeMud()
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = stepped((fbm(x, y, 4, 321u) - 0.5f) * 26.0f, 5);
            const unsigned int h = hash(x / 2, y, 322u);
            int r = 82 + tone, g = 64 + tone, b = 56 + tone;
            if (h % 13u == 0)
            {
                r += 34;
                g += 32;
                b += 34; // a wet glint
            }
            else if (h % 7u == 0)
            {
                r -= 16;
                g -= 14;
                b -= 12;
            }
            set(p, x, y, r, g, b);
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
        return makeSand(220, 208, 160, 201u);
    case GRAVEL:
        return makeGravel();
    case SNOW:
        return makeSnow();
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
        return makeSand(196, 108, 44, 203u);
    case OAK_SIDE:
        return makeLogSide(110, 86, 54, 231u);
    case OAK_TOP:
        return makeLogTop(104, 80, 50, 184, 148, 92);
    case BIRCH_SIDE:
        return makeBirchSide();
    case BIRCH_TOP:
        return makeLogTop(222, 222, 214, 200, 178, 122);
    case SPRUCE_SIDE:
        return makeLogSide(70, 46, 26, 239u);
    case SPRUCE_TOP:
        return makeLogTop(62, 40, 22, 132, 100, 62);
    case LEAVES_OAK:
        return makeLeaves(64, 134, 46, 241u);
    case LEAVES_BIRCH:
        return makeLeaves(116, 164, 74, 243u);
    case LEAVES_SPRUCE:
        return makeLeaves(44, 94, 62, 245u);
    case LEAVES_CHERRY:
        return makeLeaves(240, 166, 202, 247u);
    case LEAVES_AUTUMN:
        return makeLeaves(224, 128, 42, 249u);
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
        return makeOre(52, 52, 56, 251u);
    case ORE_IRON:
        return makeOre(216, 172, 138, 253u);
    case ORE_GOLD:
        return makeOre(250, 214, 70, 255u);
    case ORE_DIAMOND:
        return makeOre(96, 232, 226, 257u);
    case CACTUS_SIDE:
        return makeCactusSide();
    case CACTUS_TOP:
        return makeCactusTop();
    case MUSHROOM_CAP:
        return makeMushroomCap();
    case MUSHROOM_STEM:
        return makeStem();
    case HAY_SIDE:
        return makeHay(false);
    case HAY_TOP:
        return makeHay(true);
    case FARMLAND:
        return makeFarmland();
    case WATER:
        return makeWater(false);
    case SEA_WATER:
        return makeWater(true);
    case TALL_GRASS:
        return makeBlades(92, 158, 56, 5, 261u);
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
    case PETALS:
        return makePetals();
    case OXEYE:
        return makeFlower(246, 246, 236, 248, 206, 44, false);
    case ALLIUM:
        return makeFlower(182, 112, 226, 214, 156, 246, false);
    case TULIP_ORANGE:
        return makeFlower(246, 140, 46, 0, 0, 0, true);
    case ICE:
        return makeIce();
    case PUMPKIN_FACE:
        return makePumpkinFace();
    case PUMPKIN_TOP:
        return makePumpkinTop();
    case BAMBOO:
        return makeBamboo(false);
    case BAMBOO_TOP:
        return makeBamboo(true);
    case VINES:
        return makeVines();
    case MUD:
        return makeMud();
    case MANGROVE_SIDE:
        return makeLogSide(112, 58, 46, 331u);
    case MANGROVE_TOP:
        return makeLogTop(104, 54, 42, 168, 92, 70);
    case LEAVES_MANGROVE:
        return makeLeaves(62, 116, 40, 333u);
    case LEAVES_BAMBOO:
        return makeLeaves(118, 176, 62, 335u);
    default:
        return PixelArt::blank(S);
    }
}

Image sand(int r, int g, int b, unsigned int seed)
{
    return makeSand(r, g, b, seed);
}

Image stone(int r, int g, int b, unsigned int seed)
{
    return makeStone(r, g, b, seed);
}

Image planks(int r, int g, int b, bool nails)
{
    return makePlanks(r, g, b, nails);
}

Image bark(int r, int g, int b, unsigned int seed)
{
    return makeLogSide(r, g, b, seed);
}

Image crystal()
{
    return makeCrystal();
}

Image lantern()
{
    return makeLantern();
}

Image stoneBricks(int r, int g, int b, bool algae, unsigned int seed)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int row = y / 8;
            const int bx = (x + (row % 2) * 8) % 16;
            const int brick = (x + (row % 2) * 8) / 16 + row * 3;
            const int tint = shadeOf(brick, row, seed, 8);
            const int tone = stepped((fbm(x, y, 4, seed + 1u) - 0.5f) * 24.0f, 6);
            const int s = shadeOf(x, y, seed + 2u, 3);
            const int hang = 4 + static_cast<int>(noise(x, 0, 8, 1, seed + 3u) * 8.0f);
            int v = tint + tone + s;
            if (y % 8 == 7 || bx == 15)
            {
                set(p, x, y, r * 3 / 5 + s, g * 3 / 5 + s, b * 3 / 5 + s); // mortar
                continue;
            }
            if (y % 8 == 0 || bx == 0)
                v += 14; // the brick's lit edge
            else if (y % 8 == 6 || bx == 14)
                v -= 12;
            if (algae && y < hang && hash(x, y, seed + 4u) % 4u != 0)
                set(p, x, y, 72 + s, 124 + s + tone, 80 + s); // algae growing down from the top
            else
                set(p, x, y, r + v, g + v, b + v);
        }
    return p;
}

Image coral(int r, int g, int b, unsigned int seed)
{
    // living coral: soft lumps full of small round pores, each dark inside
    // with a light rim on its lower side
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = stepped((fbm(x, y, 4, seed) - 0.5f) * 40.0f, 8);
            const int s = shadeOf(x, y, seed + 1u, 4);
            set(p, x, y, r + tone + s, g + tone + s, b + tone + s);
        }
    const Cells pores(5, seed + 2u, 0.9f);
    for (int i = 0; i < 25; ++i)
    {
        const int cx = static_cast<int>(pores.px[i]), cy = static_cast<int>(pores.py[i]);
        for (int dy = -1; dy <= 2; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int x = (cx + dx + S) % S, y = (cy + dy + S) % S;
                if (dy == 2)
                    set(p, x, y, r + 36, g + 36, b + 30); // lit rim
                else if (dy >= 0 && dx >= -1)
                    set(p, x, y, r * 2 / 5, g * 2 / 5, b * 2 / 5); // the pore
            }
    }
    return p;
}

// A polished block with a bevelled border: light on the top and left edge,
// dark on the bottom and right, and a few glints.
Image bevelled(int r, int g, int b, unsigned int seed, bool flecks)
{
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int tone = stepped((fbm(x, y, 4, seed) - 0.5f) * 30.0f, 6);
            const int s = shadeOf(x, y, seed + 1u, 3);
            int v = tone + s;
            if (x < 2 || y < 2)
                v += 34;
            else if (x > 29 || y > 29)
                v -= 40;
            else if (x < 4 || y < 4)
                v += 14;
            else if (x > 27 || y > 27)
                v -= 16;
            set(p, x, y, r + v, g + v, b + v);
        }
    for (int i = 0; i < 10; ++i)
    {
        const int x = 5 + static_cast<int>(hash(i, 0, seed + 2u) % 22u);
        const int y = 5 + static_cast<int>(hash(i, 1, seed + 2u) % 22u);
        if (flecks)
        {
            set(p, x, y, 236, 204, 92); // gold in the stone
            set(p, x + 1, y, 200, 162, 60);
        }
        else
        {
            set(p, x, y, 255, 255, 236);
            set(p, x + 1, y + 1, r + 40, g + 40, b + 30);
        }
    }
    return p;
}

Image gold()
{
    return bevelled(236, 192, 52, 601u, false);
}

Image lapis()
{
    return bevelled(40, 74, 172, 611u, true);
}

Image marble()
{
    // white stone with grey veins wandering across it
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float n = fbm(x, y, 3, 621u);
            const float vein = fabsf(sinf(static_cast<float>(x + y) * 0.18f + n * 10.0f));
            const int s = shadeOf(x, y, 622u, 3);
            if (vein < 0.12f)
                set(p, x, y, 168 + s, 166 + s, 160 + s);
            else
            {
                const int v = stepped((n - 0.5f) * 20.0f, 5) + s;
                set(p, x, y, 230 + v, 226 + v, 214 + v);
            }
        }
    return p;
}

Image pillar()
{
    // a fluted column of pale stone, carved bands top and bottom, algae at
    // its foot
    Image p = PixelArt::blank(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 631u, 4);
            int r = 206, g = 198, b = 168;
            const int flute = x % 8;
            if (y < 4 || y > 27)
            {
                r = 186;
                g = 178;
                b = 148;
                if (y == 3 || y == 28)
                    r = g = b = 150;
            }
            else if (flute == 0 || flute == 1)
            {
                r = 162;
                g = 154;
                b = 128; // flute shadow
            }
            else if (flute == 6)
            {
                r += 14;
                g += 14;
                b += 10;
            }
            if (y > 20 && hash(x / 2, y / 2, 632u) % 4u == 0)
            {
                r = 112;
                g = 148;
                b = 104;
            }
            set(p, x, y, r + s, g + s, b + s);
        }
    return p;
}

bool repeatsVertically(int texture)
{
    // tops tile both ways; sides clamp so their top row stays on top,
    // except the ones stretched over several blocks
    return texture == GRASS_TOP || texture == SAND || texture == OAK_SIDE;
}

} // namespace LandTextures
