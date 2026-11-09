/************************************************************************
     File:        OceanWorld.cpp

     Comment:     Voxel sea floor, scenery and lighting for the underwater
                  ride. See OceanWorld.h.

*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <math.h>
#include <stdlib.h>

#include <deque>
#include <vector>

#include "OceanWorld.h"
#include "PixelArt.h"

namespace
{

using PixelArt::hash;
using PixelArt::Image;
using PixelArt::set;

const int S = PixelArt::kSize;

// ---------------------------------------------------------------------
// World layout
// ---------------------------------------------------------------------
const float kBlock = 5.0f;
const float kWaterLevel = -1.0f;
const float kIslandHalf = 100.0f;
const int kHalf = 64; // blocks each side of the origin: +-320 units
const int kSizeXZ = 2 * kHalf;
const int kMinY = -10; // block layer j spans y = 5j .. 5j + 5
const int kMaxY = 3;
const int kSizeY = kMaxY - kMinY + 1;
const int kDeepestTop = -9; // top layer of the deep sea bed (y = -40)

enum Block
{
    AIR = 0,
    ISLAND, // under the grass island: solid but drawn by GrassGround
    SAND,
    STONE,
    CAVE_ROCK,
    BRICK,
    PILLAR,
    PLANKS,
    LOG,
    CORAL_PINK,
    CORAL_ORANGE,
    CORAL_VIOLET,
    CRYSTAL,
    BLOCK_COUNT
};

enum Plant
{
    NO_PLANT = 0,
    SEAGRASS,
    KELP,
    FAN_CORAL,
    TUBE_CORAL,
    CRYSTAL_SPIKE,
    STARFISH,
    PLANT_COUNT
};

// Texture slots: one per block (index = Block) then one per plant.
int plantTexture(int plant)
{
    return BLOCK_COUNT + plant;
}
const int kTextureCount = BLOCK_COUNT + PLANT_COUNT;

// The demo ride, also used to carve the tunnel and keep the gates clear.
const float kDemoTrack[][3] = {
    {20, 6, 0},       {70, 6, 0},        {110, 0, 0},      {145, -14, -10},  {185, -20, -30},
    {220, -22, 10},   {215, -24, 80},    {175, -26, 140},  {110, -28, 175},  {40, -28, 190},
    {-30, -28, 190},  {-100, -30, 200},  {-170, -28, 170}, {-200, -26, 100}, {-200, -26, 30},
    {-200, -24, -40}, {-185, -16, -110}, {-140, -4, -150}, {-90, 8, -125},   {-60, 10, -60},
    {-20, 8, -20}};
const int kDemoCount = static_cast<int>(sizeof(kDemoTrack) / sizeof(kDemoTrack[0]));

// ---------------------------------------------------------------------
// Pixel art. Every texture is 16 x 16 and drawn here, texel by texel.
// ---------------------------------------------------------------------
int shadeOf(int x, int y, unsigned int seed, int range)
{
    return static_cast<int>(hash(x, y, seed) % static_cast<unsigned int>(2 * range + 1)) - range;
}

Image makeSand()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const unsigned int h = hash(x, y, 21u);
            const int s = shadeOf(x, y, 22u, 8);
            if (h % 13u == 0)
                set(p, x, y, 186 + s, 170 + s, 126 + s); // dark grain
            else if (h % 19u == 0)
                set(p, x, y, 238, 230, 206); // shell fragment
            else
                set(p, x, y, 214 + s, 200 + s, 152 + s);
        }
    return p;
}

Image makeStone()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const unsigned int h = hash(x, y, 31u);
            const int s = shadeOf(x, y, 32u, 9);
            int v = 124 + s;
            if (h % 7u == 0)
                v -= 22;
            else if (h % 11u == 0)
                v += 18;
            set(p, x, y, v, v + 5, v + 11);
        }
    // A few short cracks.
    for (int k = 0; k < 3; ++k)
    {
        int x = static_cast<int>(hash(k, 0, 33u) % 16u);
        int y = static_cast<int>(hash(k, 1, 33u) % 16u);
        for (int step = 0; step < 6; ++step)
        {
            set(p, x, y, 86, 90, 98);
            x = (x + 1) % S;
            y = (y + static_cast<int>(hash(k, step, 34u) % 3u) - 1 + S) % S;
        }
    }
    return p;
}

Image makeCaveRock()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const unsigned int h = hash(x, y, 41u);
            const int s = shadeOf(x, y, 42u, 7);
            const bool blotch = (hash(x / 3, y / 3, 43u) % 4u) == 0;
            if (h % 37u == 0)
                set(p, x, y, 120, 96, 160); // mineral speck
            else if (blotch)
                set(p, x, y, 52 + s, 58 + s, 72 + s);
            else
                set(p, x, y, 70 + s, 77 + s, 92 + s);
        }
    return p;
}

Image makeBricks()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
    {
        const int row = y / 4;
        const int offset = (row % 2) * 4;
        for (int x = 0; x < S; ++x)
        {
            const int bx = (x + offset) % 8;
            const int brick = (x + offset) / 8 + row * 3;
            const int s = shadeOf(x, y, 51u, 6);
            const int tint = static_cast<int>(hash(brick, row, 52u) % 15u) - 7;
            const bool moss =
                y < 3 + static_cast<int>(hash(x, 0, 53u) % 5u) && hash(x, y, 54u) % 3u != 0;
            if (y % 4 == 3 || bx == 7)
                set(p, x, y, 88 + s, 96 + s, 90 + s); // mortar
            else if (moss)
                set(p, x, y, 74 + s, 124 + s, 82 + s); // algae growing down from the top
            else
                set(p, x, y, 146 + s + tint, 154 + s + tint, 144 + s + tint);
        }
    }
    return p;
}

Image makePillar()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 61u, 5);
            int r = 206, g = 198, b = 168;
            if (y < 2 || y > 13)
            {
                r = 184;
                g = 176;
                b = 146; // carved band
            }
            else if (x % 4 == 0)
            {
                r = 168;
                g = 160;
                b = 132; // flute shadow
            }
            else if (x % 4 == 3)
            {
                r += 10;
                g += 10;
                b += 8;
            }
            if (y > 9 && hash(x, y, 62u) % 4u == 0)
            {
                r = 120;
                g = 150;
                b = 110; // algae near the foot
            }
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
            const int s = shadeOf(x, y, 71u, 6);
            const int tint = static_cast<int>(hash(board, 0, 72u) % 17u) - 8;
            if (y % 4 == 3)
                set(p, x, y, 62 + s, 42 + s, 28 + s); // seam
            else if ((x == 1 || x == 14) && y % 4 == 1)
                set(p, x, y, 58, 60, 64); // nail
            else if (hash(x, y, 73u) % 23u == 0)
                set(p, x, y, 88 + s, 100 + s, 80 + s); // weed
            else if (hash(x / 3, y, 74u) % 5u == 0)
                set(p, x, y, 96 + s + tint, 66 + s + tint, 40 + s); // grain
            else
                set(p, x, y, 116 + s + tint, 82 + s + tint, 50 + s);
        }
    return p;
}

Image makeLog()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 81u, 6);
            if (x % 3 == 0 || hash(x, y / 2, 82u) % 6u == 0)
                set(p, x, y, 68 + s, 46 + s, 30 + s);
            else
                set(p, x, y, 90 + s, 64 + s, 42 + s);
        }
    return p;
}

Image makeCoralBlock(int r, int g, int b, unsigned int seed)
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const unsigned int h = hash(x, y, seed);
            const int s = shadeOf(x, y, seed + 1u, 10);
            if (h % 6u == 0)
                set(p, x, y, r * 6 / 10 + s, g * 6 / 10 + s, b * 6 / 10 + s); // pore
            else if (h % 9u == 0)
                set(p, x, y, r + 30, g + 30, b + 30); // bump
            else
                set(p, x, y, r + s, g + s, b + s);
        }
    return p;
}

Image makeCrystal()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const int s = shadeOf(x, y, 91u, 6);
            if ((x + y) % 6 == 0)
                set(p, x, y, 200, 250, 255); // bright facet edge
            else if ((x - y + 16) % 7 == 0)
                set(p, x, y, 40 + s, 140 + s, 190 + s);
            else
                set(p, x, y, 80 + s, 205 + s, 238 + s);
        }
    return p;
}

// Line of texels, for the plant shapes.
void line(Image &p, int x0, int y0, int x1, int y1, int r, int g, int b)
{
    const int steps = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    for (int i = 0; i <= steps; ++i)
    {
        const float t = steps == 0 ? 0.0f : static_cast<float>(i) / static_cast<float>(steps);
        const int x = x0 + static_cast<int>(floorf(static_cast<float>(x1 - x0) * t + 0.5f));
        const int y = y0 + static_cast<int>(floorf(static_cast<float>(y1 - y0) * t + 0.5f));
        set(p, x, y, r, g, b);
    }
}

Image makeSeagrass()
{
    Image p = PixelArt::blank();
    for (int k = 0; k < 5; ++k)
    {
        const int x0 = 1 + k * 3 + static_cast<int>(hash(k, 0, 101u) % 2u);
        const int height = 7 + static_cast<int>(hash(k, 1, 101u) % 8u);
        for (int i = 0; i < height; ++i)
        {
            const int y = S - 1 - i;
            const int x = x0 + ((i / 4) % 2 == 0 ? 0 : (k % 2 == 0 ? 1 : -1));
            const int s = shadeOf(x, y, 102u, 10);
            if (i >= height - 2)
                set(p, x, y, 120 + s, 196 + s, 110 + s); // lighter tip
            else
                set(p, x, y, 62 + s, 146 + s, 78 + s);
        }
    }
    return p;
}

Image makeKelp()
{
    // Tiles vertically: the stalk runs the full height, leaves alternate.
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
    {
        const int s = shadeOf(7, y, 111u, 6);
        set(p, 7, y, 84 + s, 116 + s, 44 + s);
        set(p, 8, y, 96 + s, 128 + s, 50 + s);
    }
    for (int k = 0; k < 4; ++k)
    {
        const bool right = k % 2 == 0;
        const int y = k * 4;
        for (int i = 0; i < 4; ++i)
        {
            const int x = right ? 9 + i : 6 - i;
            const int s = shadeOf(x, y, 112u, 8);
            set(p, x, y + (i > 1 ? 1 : 0), 112 + s, 156 + s, 62 + s);
            set(p, x, y + 1 + (i > 1 ? 1 : 0), 98 + s, 140 + s, 54 + s);
        }
    }
    return p;
}

Image makeFanCoral()
{
    Image p = PixelArt::blank();
    for (int k = 0; k < 7; ++k)
    {
        const float a = (-55.0f + 18.0f * static_cast<float>(k)) * 3.14159265f / 180.0f;
        const int x1 = 8 + static_cast<int>(floorf(sinf(a) * 7.0f + 0.5f));
        const int y1 = 15 - static_cast<int>(floorf(cosf(a) * 13.0f + 0.5f));
        line(p, 8, 15, x1, y1, 214, 72, 104);
        set(p, x1, y1, 252, 150, 170); // tip
        // A side twig half way up.
        const int xm = (8 + x1) / 2;
        const int ym = (15 + y1) / 2;
        line(p, xm, ym, xm + (k < 3 ? -2 : 2), ym - 2, 230, 96, 124);
    }
    return p;
}

Image makeTubeCoral()
{
    Image p = PixelArt::blank();
    const int xs[4] = {1, 5, 9, 12};
    for (int k = 0; k < 4; ++k)
    {
        const int height = 7 + static_cast<int>(hash(k, 0, 121u) % 7u);
        for (int i = 0; i < height; ++i)
        {
            const int y = S - 1 - i;
            for (int dx = 0; dx < 3; ++dx)
            {
                const int s = shadeOf(xs[k] + dx, y, 122u, 8);
                if (i >= height - 2)
                    set(p, xs[k] + dx, y, 150, 210, 255); // pale rim
                else if (dx == 1 && i >= height - 4)
                    set(p, xs[k] + dx, y, 20, 40, 90); // dark opening
                else
                    set(p, xs[k] + dx, y, 52 + s, 108 + s, 214 + s);
            }
        }
    }
    return p;
}

Image makeCrystalSpike()
{
    Image p = PixelArt::blank();
    const int centers[3] = {8, 4, 12};
    const int heights[3] = {15, 9, 11};
    const int widths[3] = {3, 2, 2};
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < heights[k]; ++i)
        {
            const int y = S - 1 - i;
            const int w = widths[k] * (heights[k] - i) / heights[k];
            for (int dx = -w; dx <= w; ++dx)
            {
                const bool edge = dx == -w || dx == w;
                if (edge)
                    set(p, centers[k] + dx, y, 200, 250, 255);
                else
                    set(p, centers[k] + dx, y, 80, 210, 240);
            }
        }
    return p;
}

Image makeStarfish()
{
    Image p = PixelArt::blank();
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float dx = static_cast<float>(x) - 7.5f;
            const float dy = static_cast<float>(y) - 7.5f;
            const float r = sqrtf(dx * dx + dy * dy);
            const float a = atan2f(dy, dx);
            const float arm = 2.2f + 5.6f * powf(fabsf(cosf(2.5f * a)), 3.0f);
            if (r < arm)
            {
                if (hash(x, y, 131u) % 5u == 0)
                    set(p, x, y, 250, 190, 120); // bumps
                else
                    set(p, x, y, 226, 104, 58);
            }
        }
    return p;
}

// ---------------------------------------------------------------------
// Noise and splines (CPU side)
// ---------------------------------------------------------------------
float lattice(int x, int z, unsigned int seed)
{
    return static_cast<float>(hash(x, z, seed) & 0xffffu) / 65535.0f;
}

float valueNoise(float x, float z, unsigned int seed)
{
    const float fx = floorf(x);
    const float fz = floorf(z);
    const int ix = static_cast<int>(fx);
    const int iz = static_cast<int>(fz);
    float tx = x - fx;
    float tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a =
        lattice(ix, iz, seed) + (lattice(ix + 1, iz, seed) - lattice(ix, iz, seed)) * tx;
    const float b = lattice(ix, iz + 1, seed) +
                    (lattice(ix + 1, iz + 1, seed) - lattice(ix, iz + 1, seed)) * tx;
    return a + (b - a) * tz;
}

// Same curves as CTrack (Track.cpp): 1 linear, 2 cardinal, 3 B-spline.
float splineComponent(float p0, float p1, float p2, float p3, float t, int type)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    if (type == 1)
        return p1 + (p2 - p1) * t;
    if (type == 2)
        return 0.5f * (2.0f * p1 + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                       (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    return ((1.0f - 3.0f * t + 3.0f * t2 - t3) * p0 + (4.0f - 6.0f * t2 + 3.0f * t3) * p1 +
            (1.0f + 3.0f * t + 3.0f * t2 - 3.0f * t3) * p2 + t3 * p3) /
           6.0f;
}

// ---------------------------------------------------------------------
// The voxel grid
// ---------------------------------------------------------------------
struct Grid
{
    std::vector<unsigned char> block;
    std::vector<unsigned char> plant;
    std::vector<unsigned char> sky;  // 0..15
    std::vector<unsigned char> glow; // 0..14

    Grid()
        : block(static_cast<size_t>(kSizeXZ * kSizeY * kSizeXZ), AIR),
          plant(block.size(), NO_PLANT), sky(block.size(), 0), glow(block.size(), 0)
    {
    }

    static bool inside(int bx, int j, int bz)
    {
        return bx >= -kHalf && bx < kHalf && bz >= -kHalf && bz < kHalf && j >= kMinY && j <= kMaxY;
    }
    static size_t index(int bx, int j, int bz)
    {
        return (static_cast<size_t>(bx + kHalf) * kSizeY + static_cast<size_t>(j - kMinY)) *
                   kSizeXZ +
               static_cast<size_t>(bz + kHalf);
    }
    int get(int bx, int j, int bz) const
    {
        if (!inside(bx, j, bz))
            return (j <= kDeepestTop) ? STONE : AIR;
        return block[index(bx, j, bz)];
    }
    void put(int bx, int j, int bz, int b)
    {
        if (inside(bx, j, bz))
            block[index(bx, j, bz)] = static_cast<unsigned char>(b);
    }
    bool solid(int bx, int j, int bz) const
    {
        return get(bx, j, bz) != AIR;
    }
    int skyAt(int bx, int j, int bz) const
    {
        return inside(bx, j, bz) ? sky[index(bx, j, bz)] : 15;
    }
    int glowAt(int bx, int j, int bz) const
    {
        return inside(bx, j, bz) ? glow[index(bx, j, bz)] : 0;
    }
    // Highest solid layer of a column, or kMinY - 1.
    int top(int bx, int bz) const
    {
        for (int j = kMaxY; j >= kMinY; --j)
            if (solid(bx, j, bz))
                return j;
        return kMinY - 1;
    }
};

float centerOf(int b)
{
    return (static_cast<float>(b) + 0.5f) * kBlock;
}

int blockOf(float v)
{
    return static_cast<int>(floorf(v / kBlock));
}

// Sand and stone terraces that step down away from the island, and the
// rocky islet (with its tunnel carved later).
void buildTerrain(Grid &g)
{
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float cx = centerOf(bx);
            const float cz = centerOf(bz);
            const float edge = fmaxf(fabsf(cx), fabsf(cz)) - kIslandHalf;
            if (edge < 0.0f)
            {
                for (int j = kMinY; j <= -2; ++j)
                    g.put(bx, j, bz, SAND);
                g.put(bx, -1, bz, ISLAND);
                continue;
            }
            float depth = 1.0f + (edge - 6.0f) / 11.0f +
                          (valueNoise(cx * 0.025f, cz * 0.025f, 7u) - 0.5f) * 2.5f;
            depth = fmaxf(1.0f, fminf(8.0f, depth));
            int top = -static_cast<int>(floorf(depth + 0.5f)) - 1;

            // The islet: a rough dome west of the island that breaks the surface.
            const float ex = (cx + 205.0f) / 58.0f;
            const float ez = (cz - 25.0f) / 118.0f;
            const float e2 = ex * ex + ez * ez;
            bool islet = false;
            if (e2 < 1.0f)
            {
                const float h = -40.0f + (1.0f - e2) * 58.0f +
                                (valueNoise(cx * 0.06f, cz * 0.06f, 9u) - 0.5f) * 14.0f;
                const int hillTop = blockOf(h);
                if (hillTop > top)
                {
                    top = hillTop > kMaxY ? kMaxY : hillTop;
                    islet = true;
                }
            }

            const bool gravel = valueNoise(cx * 0.04f, cz * 0.04f, 13u) > 0.64f;
            for (int j = kMinY; j <= top; ++j)
            {
                int b = CAVE_ROCK;
                if (islet)
                    b = (j >= 0) ? STONE : CAVE_ROCK;
                else if (j == top)
                    b = gravel ? STONE : SAND;
                else if (j == top - 1)
                    b = SAND;
                g.put(bx, j, bz, b);
            }
            // Occasional boulders on the open sea bed.
            if (!islet && hash(bx, bz, 15u) % 53u == 0 && top < -3)
                g.put(bx, top + 1, bz, STONE);
        }
}

// A sunken colonnade the ride passes through, with a gate at each end.
void buildRuins(Grid &g)
{
    // Paved floor with some slabs missing.
    for (int bx = -9; bx <= 17; ++bx)
        for (int bz = 32; bz <= 42; ++bz)
        {
            const int top = g.top(bx, bz);
            if (hash(bx, bz, 201u) % 7u != 0)
                g.put(bx, top, bz, BRICK);
        }

    // Gates: two tall pillars and a lintel across the ride.
    struct Gate
    {
        int bx, bzA, bzB;
    };
    const Gate gates[2] = {{16, 33, 39}, {-8, 35, 41}};
    for (const Gate &gate : gates)
    {
        for (int j = -8; j <= -4; ++j)
        {
            g.put(gate.bx, j, gate.bzA, PILLAR);
            g.put(gate.bx, j, gate.bzB, PILLAR);
        }
        for (int bz = gate.bzA; bz <= gate.bzB; ++bz)
            g.put(gate.bx, -3, bz, BRICK);
    }

    // Two rows of columns, some broken off, with fallen drums nearby.
    for (int bx = 12; bx >= -4; bx -= 4)
        for (int side = 0; side < 2; ++side)
        {
            const int bz = side == 0 ? 33 : 41;
            const int height = 2 + static_cast<int>(hash(bx, side, 202u) % 4u);
            for (int j = -8; j < -8 + height; ++j)
                g.put(bx, j, bz, PILLAR);
            if (height < 4)
            {
                const int fx = bx + (hash(bx, side, 203u) % 2u == 0 ? 1 : -1);
                const int fz = bz + (side == 0 ? -1 : 1);
                g.put(fx, -8, fz, PILLAR);
                g.put(fx + 1, -8, fz, PILLAR);
            }
        }

    // A low back wall behind the south row.
    for (int bx = -6; bx <= 14; ++bx)
    {
        const int height = 1 + static_cast<int>(hash(bx, 0, 204u) % 3u);
        for (int j = -8; j < -8 + height; ++j)
            g.put(bx, j, 43, BRICK);
    }
}

// A wrecked boat lying beside the ride, mast snapped.
void buildWreck(Grid &g)
{
    const int x0 = -26, x1 = -16; // bow to stern
    const int z0 = 43, z1 = 47;
    for (int bx = x0; bx <= x1; ++bx)
    {
        // The hull narrows towards the ends.
        const int inset = (bx == x0 || bx == x1) ? 2 : ((bx == x0 + 1 || bx == x1 - 1) ? 1 : 0);
        for (int bz = z0 + inset; bz <= z1 - inset; ++bz)
        {
            const bool wall = bz == z0 + inset || bz == z1 - inset || inset == 2;
            const bool hole = bx == x0 + 4 && bz == z1 - inset; // a breach in the side
            if (bz > z0 && bz < z1)
                g.put(bx, -8, bz, PLANKS); // keel and bottom
            if (wall && !hole)
            {
                g.put(bx, -7, bz, PLANKS);
                // The port side has rotted lower.
                if (bz == z1 - inset || bx % 3 != 0)
                    g.put(bx, -6, bz, PLANKS);
            }
        }
    }
    // Part of the deck is left at the stern.
    for (int bx = x1 - 3; bx <= x1 - 1; ++bx)
        for (int bz = z0 + 1; bz <= z1 - 1; ++bz)
            g.put(bx, -6, bz, PLANKS);
    // The mast and its broken top lying across the hull.
    for (int j = -7; j <= -3; ++j)
        g.put(-21, j, 45, LOG);
    for (int bx = -24; bx <= -22; ++bx)
        g.put(bx, -6, 46, LOG);
}

// Removes blocks along every spline type of the demo ride, so the train
// never goes through anything: this is what makes the tunnel.
void carveRide(Grid &g)
{
    for (int type = 1; type <= 3; ++type)
        for (int seg = 0; seg < kDemoCount; ++seg)
        {
            const float *p[4];
            for (int i = 0; i < 4; ++i)
                p[i] = kDemoTrack[(seg + i - 1 + kDemoCount) % kDemoCount];
            for (int step = 0; step < 48; ++step)
            {
                const float t = static_cast<float>(step) / 48.0f;
                float q[3];
                for (int k = 0; k < 3; ++k)
                    q[k] = splineComponent(p[0][k], p[1][k], p[2][k], p[3][k], t, type);
                const int jLow = blockOf(q[1] - 2.5f);
                const int jHigh = blockOf(q[1] + 10.0f);
                for (int bx = blockOf(q[0] - 9.0f); bx <= blockOf(q[0] + 9.0f); ++bx)
                    for (int bz = blockOf(q[2] - 9.0f); bz <= blockOf(q[2] + 9.0f); ++bz)
                    {
                        const float dx = centerOf(bx) - q[0];
                        const float dz = centerOf(bz) - q[2];
                        if (dx * dx + dz * dz > 8.5f * 8.5f)
                            continue;
                        for (int j = jLow; j <= jHigh; ++j)
                        {
                            const float cy = centerOf(j);
                            if (cy > q[1] - 2.5f && cy <= q[1] + 10.0f &&
                                g.get(bx, j, bz) != ISLAND)
                                g.put(bx, j, bz, AIR);
                        }
                    }
            }
        }
}

// Coral, kelp, grass, starfish and crystals on top of the finished blocks.
void decorate(Grid &g)
{
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float cx = centerOf(bx);
            const float cz = centerOf(bz);
            if (fmaxf(fabsf(cx), fabsf(cz)) < kIslandHalf)
                continue;
            const unsigned int h = hash(bx, bz, 301u);
            const float r = static_cast<float>(h % 1000u) / 1000.0f;
            const bool reef = cx > 115.0f && cx < 240.0f && cz > -65.0f && cz < 60.0f;
            const bool kelpForest = cx > 135.0f && cx < 250.0f && cz > 40.0f && cz < 185.0f;
            const bool nearRuins = cx > -60.0f && cx < 100.0f && cz > 150.0f && cz < 235.0f;

            int top = g.top(bx, bz);
            if (top >= kMaxY || top < kMinY)
                continue;

            // Coral mounds on the reef.
            if (reef && valueNoise(cx * 0.08f, cz * 0.08f, 17u) > 0.55f && top + 2 < -1)
            {
                const float pick = valueNoise(cx * 0.05f, cz * 0.05f, 19u);
                const int coral =
                    pick < 0.38f ? CORAL_PINK : (pick < 0.62f ? CORAL_ORANGE : CORAL_VIOLET);
                const int height = 1 + static_cast<int>(h % 2u);
                for (int k = 1; k <= height; ++k)
                    g.put(bx, top + k, bz, coral);
                top += height;
            }

            const int above = top + 1;
            if (above > -2 || g.solid(bx, above, bz))
                continue; // no plants above the water or where the ride runs
            const size_t at = Grid::index(bx, above, bz);
            const int ground = g.get(bx, top, bz);
            if (ground == ISLAND || ground == LOG)
                continue;

            if (kelpForest && r < 0.34f)
            {
                const int height = 3 + static_cast<int>((h >> 10) % 4u);
                for (int k = 0; k < height; ++k)
                {
                    const int j = above + k;
                    if (j > -2 || g.solid(bx, j, bz))
                        break;
                    g.plant[Grid::index(bx, j, bz)] = KELP;
                }
            }
            else if (reef && r < 0.62f)
                g.plant[at] = static_cast<unsigned char>(
                    r < 0.28f ? FAN_CORAL : (r < 0.48f ? TUBE_CORAL : SEAGRASS));
            else if (nearRuins && ground != SAND && r < 0.10f)
                g.plant[at] = CRYSTAL_SPIKE;
            else if (ground == SAND && r < 0.012f)
                g.plant[at] = STARFISH;
            else if (r < 0.16f)
                g.plant[at] = SEAGRASS;
            else if (r < 0.19f)
                g.plant[at] = TUBE_CORAL;
        }

    // Crystals in the tunnel: some wall blocks become crystal, and spikes
    // grow from the floor.
    for (int bx = -52; bx <= -28; ++bx)
        for (int bz = -26; bz <= 32; ++bz)
            for (int j = -8; j <= -2; ++j)
            {
                if (g.get(bx, j, bz) != CAVE_ROCK)
                    continue;
                const bool open = !g.solid(bx + 1, j, bz) || !g.solid(bx - 1, j, bz) ||
                                  !g.solid(bx, j, bz + 1) || !g.solid(bx, j, bz - 1) ||
                                  !g.solid(bx, j - 1, bz);
                const unsigned int h = hash(bx * 31 + j, bz, 311u);
                if (open && h % 34u == 0)
                    g.put(bx, j, bz, CRYSTAL);
                else if (!g.solid(bx, j + 1, bz) && h % 23u == 1 && Grid::inside(bx, j + 1, bz))
                    g.plant[Grid::index(bx, j + 1, bz)] = CRYSTAL_SPIKE;
            }
}

// Minecraft-style light: sky light pours straight down open columns and
// spreads sideways losing one level per block; crystals do the same.
void spreadLight(Grid &g)
{
    struct Cell
    {
        int bx, j, bz;
    };
    const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

    std::deque<Cell> queue;
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMaxY; j >= kMinY && !g.solid(bx, j, bz); --j)
            {
                g.sky[Grid::index(bx, j, bz)] = 15;
                queue.push_back(Cell{bx, j, bz});
            }
    while (!queue.empty())
    {
        const Cell c = queue.front();
        queue.pop_front();
        const int level = g.sky[Grid::index(c.bx, c.j, c.bz)] - 1;
        if (level <= 0)
            continue;
        for (const int *d : dirs)
        {
            const int x = c.bx + d[0], j = c.j + d[1], z = c.bz + d[2];
            if (!Grid::inside(x, j, z) || g.solid(x, j, z))
                continue;
            unsigned char &s = g.sky[Grid::index(x, j, z)];
            if (s < level)
            {
                s = static_cast<unsigned char>(level);
                queue.push_back(Cell{x, j, z});
            }
        }
    }

    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const size_t i = Grid::index(bx, j, bz);
                if (g.block[i] == CRYSTAL)
                {
                    for (const int *d : dirs)
                    {
                        const int x = bx + d[0], y = j + d[1], z = bz + d[2];
                        if (Grid::inside(x, y, z) && !g.solid(x, y, z) &&
                            g.glow[Grid::index(x, y, z)] < 13)
                        {
                            g.glow[Grid::index(x, y, z)] = 13;
                            queue.push_back(Cell{x, y, z});
                        }
                    }
                }
                else if (g.plant[i] == CRYSTAL_SPIKE && g.glow[i] < 11)
                {
                    g.glow[i] = 11;
                    queue.push_back(Cell{bx, j, bz});
                }
            }
    while (!queue.empty())
    {
        const Cell c = queue.front();
        queue.pop_front();
        const int level = g.glow[Grid::index(c.bx, c.j, c.bz)] - 1;
        if (level <= 0)
            continue;
        for (const int *d : dirs)
        {
            const int x = c.bx + d[0], j = c.j + d[1], z = c.bz + d[2];
            if (!Grid::inside(x, j, z) || g.solid(x, j, z))
                continue;
            unsigned char &s = g.glow[Grid::index(x, j, z)];
            if (s < level)
            {
                s = static_cast<unsigned char>(level);
                queue.push_back(Cell{x, j, z});
            }
        }
    }
}

float skyFactor(float level)
{
    return powf(0.8f, 15.0f - level);
}

float glowFactor(float level)
{
    return level <= 0.0f ? 0.0f : powf(0.62f, 13.0f - level);
}

// ---------------------------------------------------------------------
// Meshing
// ---------------------------------------------------------------------
struct Vertex
{
    float pos[3];
    float normal[3];
    float uv[2];
    float color[3];
    float extra[4]; // glow, sway, crystal light, sky light
};

typedef std::vector<std::vector<Vertex>> Batches; // one batch per texture

// Six faces: normal axis, sign, and the corners as unit cube offsets.
struct Face
{
    int normal[3];
    int corner[4][3];
    float uv[4][2];
};
const Face kFaces[6] = {
    {{1, 0, 0}, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
    {{-1, 0, 0}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
    {{0, 1, 0}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
    {{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
    {{0, 0, 1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{0, 1}, {1, 1}, {1, 0}, {0, 0}}},
    {{0, 0, -1}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {{0, 1}, {1, 1}, {1, 0}, {0, 0}}}};

void addBlockFaces(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const int b = g.get(bx, j, bz);
    const float tint = 0.9f + 0.1f * static_cast<float>(hash(bx * 7 + j, bz, 401u) % 100u) / 100.0f;
    for (const Face &f : kFaces)
    {
        const int ox = bx + f.normal[0], oy = j + f.normal[1], oz = bz + f.normal[2];
        if (g.solid(ox, oy, oz))
            continue;
        // The two axes in the plane of the face.
        int axisA = 0, axisB = 0;
        if (f.normal[0] != 0)
        {
            axisA = 1;
            axisB = 2;
        }
        else if (f.normal[1] != 0)
        {
            axisA = 0;
            axisB = 2;
        }
        else
        {
            axisA = 0;
            axisB = 1;
        }
        for (int c = 0; c < 4; ++c)
        {
            int da[3] = {0, 0, 0};
            int db[3] = {0, 0, 0};
            da[axisA] = f.corner[c][axisA] == 1 ? 1 : -1;
            db[axisB] = f.corner[c][axisB] == 1 ? 1 : -1;
            const bool sideA = g.solid(ox + da[0], oy + da[1], oz + da[2]);
            const bool sideB = g.solid(ox + db[0], oy + db[1], oz + db[2]);
            const bool corner = g.solid(ox + da[0] + db[0], oy + da[1] + db[1], oz + da[2] + db[2]);
            const int open =
                (sideA && sideB) ? 0 : 3 - (sideA ? 1 : 0) - (sideB ? 1 : 0) - (corner ? 1 : 0);
            const float ao[4] = {0.45f, 0.62f, 0.8f, 1.0f};

            // Smooth light: average the open cells around this corner.
            float sky = static_cast<float>(g.skyAt(ox, oy, oz));
            float glow = static_cast<float>(g.glowAt(ox, oy, oz));
            float count = 1.0f;
            const int samples[3][3] = {
                {ox + da[0], oy + da[1], oz + da[2]},
                {ox + db[0], oy + db[1], oz + db[2]},
                {ox + da[0] + db[0], oy + da[1] + db[1], oz + da[2] + db[2]}};
            const bool blocked[3] = {sideA, sideB, corner || (sideA && sideB)};
            for (int s = 0; s < 3; ++s)
                if (!blocked[s])
                {
                    sky += static_cast<float>(g.skyAt(samples[s][0], samples[s][1], samples[s][2]));
                    glow +=
                        static_cast<float>(g.glowAt(samples[s][0], samples[s][1], samples[s][2]));
                    count += 1.0f;
                }

            Vertex v;
            v.pos[0] = static_cast<float>(bx + f.corner[c][0]) * kBlock;
            v.pos[1] = static_cast<float>(j + f.corner[c][1]) * kBlock;
            v.pos[2] = static_cast<float>(bz + f.corner[c][2]) * kBlock;
            for (int k = 0; k < 3; ++k)
            {
                v.normal[k] = static_cast<float>(f.normal[k]);
                v.color[k] = tint;
            }
            v.uv[0] = f.uv[c][0];
            v.uv[1] = f.uv[c][1];
            v.extra[0] = b == CRYSTAL ? 1.0f : 0.0f;
            v.extra[1] = 0.0f;
            v.extra[2] = glowFactor(glow / count);
            v.extra[3] = skyFactor(sky / count) * ao[open];
            batches[static_cast<size_t>(b)].push_back(v);
        }
    }
}

void addPlant(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const size_t i = Grid::index(bx, j, bz);
    const int plant = g.plant[i];
    const float sky = skyFactor(static_cast<float>(g.sky[i]));
    const float glow = glowFactor(static_cast<float>(g.glow[i]));
    const unsigned int h = hash(bx, bz * 13 + j, 411u);
    const float tint = 0.85f + 0.15f * static_cast<float>(h % 100u) / 100.0f;
    std::vector<Vertex> &out = batches[static_cast<size_t>(plantTexture(plant))];

    Vertex v;
    v.normal[0] = 0.0f;
    v.normal[1] = 1.0f;
    v.normal[2] = 0.0f;
    v.color[0] = v.color[1] = v.color[2] = tint;
    v.extra[0] = plant == CRYSTAL_SPIKE ? 1.0f : (plant == TUBE_CORAL ? 0.12f : 0.0f);
    v.extra[2] = glow;
    v.extra[3] = sky;

    const float x0 = static_cast<float>(bx) * kBlock;
    const float y0 = static_cast<float>(j) * kBlock;
    const float z0 = static_cast<float>(bz) * kBlock;

    if (plant == STARFISH)
    {
        // Lies flat on the sand, turned a random way.
        const float a = static_cast<float>(h % 360u) * 3.14159265f / 180.0f;
        const float cx = x0 + kBlock * 0.5f, cz = z0 + kBlock * 0.5f;
        const float r = kBlock * 0.38f;
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        v.extra[1] = 0.0f;
        for (int c = 0; c < 4; ++c)
        {
            const float ca = a + 0.785398f + 1.570796f * static_cast<float>(c);
            v.pos[0] = cx + cosf(ca) * r;
            v.pos[1] = y0 + 0.06f;
            v.pos[2] = cz + sinf(ca) * r;
            v.uv[0] = uv[c][0];
            v.uv[1] = uv[c][1];
            out.push_back(v);
        }
        return;
    }

    // How far up a kelp stalk this cell is, for the sway.
    float swayBottom = 0.0f, swayTop = 0.0f;
    if (plant == KELP)
    {
        int level = 0;
        while (g.get(bx, j - 1 - level, bz) == AIR && Grid::inside(bx, j - 1 - level, bz) &&
               g.plant[Grid::index(bx, j - 1 - level, bz)] == KELP)
            ++level;
        swayBottom = 0.22f * static_cast<float>(level);
        swayTop = 0.22f * static_cast<float>(level + 1);
    }
    else if (plant == SEAGRASS)
        swayTop = 0.18f;

    // Two quads crossing diagonally through the cell.
    const float inset = 0.6f;
    const float diag[2][4] = {{x0 + inset, z0 + inset, x0 + kBlock - inset, z0 + kBlock - inset},
                              {x0 + inset, z0 + kBlock - inset, x0 + kBlock - inset, z0 + inset}};
    for (int q = 0; q < 2; ++q)
    {
        const float corners[4][4] = {{diag[q][0], y0, diag[q][1], 1.0f},
                                     {diag[q][2], y0, diag[q][3], 1.0f},
                                     {diag[q][2], y0 + kBlock, diag[q][3], 0.0f},
                                     {diag[q][0], y0 + kBlock, diag[q][1], 0.0f}};
        for (int c = 0; c < 4; ++c)
        {
            v.pos[0] = corners[c][0];
            v.pos[1] = corners[c][1];
            v.pos[2] = corners[c][2];
            v.uv[0] = (c == 0 || c == 3) ? 0.0f : 1.0f;
            v.uv[1] = corners[c][3];
            v.extra[1] = corners[c][3] > 0.5f ? swayBottom : swayTop;
            out.push_back(v);
        }
    }
}

// The flat far sea bed beyond the grid, out to the horizon.
void addFarFloor(Batches &batches)
{
    const float inner = static_cast<float>(kHalf) * kBlock;
    const float outer = 1500.0f;
    const float y = static_cast<float>(kDeepestTop + 1) * kBlock;
    const float rects[4][4] = {{-outer, -outer, outer, -inner},
                               {-outer, inner, outer, outer},
                               {-outer, -inner, -inner, inner},
                               {inner, -inner, outer, inner}};
    for (const float *r : rects)
    {
        const float xs[4] = {r[0], r[0], r[2], r[2]};
        const float zs[4] = {r[1], r[3], r[3], r[1]};
        for (int c = 0; c < 4; ++c)
        {
            Vertex v;
            v.pos[0] = xs[c];
            v.pos[1] = y;
            v.pos[2] = zs[c];
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
            v.uv[0] = xs[c] / kBlock;
            v.uv[1] = zs[c] / kBlock;
            v.color[0] = v.color[1] = v.color[2] = 0.95f;
            v.extra[0] = v.extra[1] = v.extra[2] = 0.0f;
            v.extra[3] = 1.0f;
            batches[SAND].push_back(v);
        }
    }
}

void emit(const Vertex &v)
{
    glColor3fv(v.color);
    glMultiTexCoord4fv(GL_TEXTURE1, v.extra);
    glTexCoord2fv(v.uv);
    glNormal3fv(v.normal);
    glVertex3fv(v.pos);
}

unsigned int compileList(const Batches &batches, const std::vector<unsigned int> &textures)
{
    const GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    for (size_t t = 0; t < batches.size(); ++t)
    {
        if (batches[t].empty())
            continue;
        glBindTexture(GL_TEXTURE_2D, textures[t]);
        glBegin(GL_QUADS);
        for (const Vertex &v : batches[t])
            emit(v);
        glEnd();
    }
    glEndList();
    return list;
}

// ---------------------------------------------------------------------
// Fish
// ---------------------------------------------------------------------
void fishBox(float x0, float y0, float z0, float x1, float y1, float z1)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glVertex3f(x0, y0, z1);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x0, y1, z1);
    glNormal3f(0, 0, -1);
    glVertex3f(x1, y0, z0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x0, y1, z0);
    glVertex3f(x1, y1, z0);
    glNormal3f(0, 1, 0);
    glVertex3f(x0, y1, z0);
    glVertex3f(x0, y1, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z0);
    glNormal3f(0, -1, 0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x0, y0, z1);
    glNormal3f(1, 0, 0);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z1);
    glVertex3f(x1, y0, z1);
    glNormal3f(-1, 0, 0);
    glVertex3f(x0, y0, z1);
    glVertex3f(x0, y1, z1);
    glVertex3f(x0, y1, z0);
    glVertex3f(x0, y0, z0);
    glEnd();
}

} // namespace

OceanWorld::OceanWorld() : m_built(false), m_solidList(0), m_plantList(0)
{
}

float OceanWorld::waterLevel()
{
    return kWaterLevel;
}

void OceanWorld::releaseResources()
{
    m_built = false;
    m_solidList = 0;
    m_plantList = 0;
    m_textures.clear();
}

std::vector<float> OceanWorld::demoTrack()
{
    std::vector<float> xyz;
    for (int i = 0; i < kDemoCount; ++i)
        for (int k = 0; k < 3; ++k)
            xyz.push_back(kDemoTrack[i][k]);
    return xyz;
}

void OceanWorld::shadowBounds(float lo[3], float hi[3])
{
    lo[0] = -270.0f;
    lo[1] = -45.0f;
    lo[2] = -110.0f;
    hi[0] = 250.0f;
    hi[1] = 20.0f;
    hi[2] = 245.0f;
}

const std::vector<float> &OceanWorld::bubbleSources()
{
    static const float sources[][3] = {{-105.0f, -38.0f, 225.0f}, {-92.0f, -36.0f, 230.0f},
                                       {170.0f, -18.0f, -40.0f},  {200.0f, -26.0f, 20.0f},
                                       {150.0f, -14.0f, 25.0f},   {20.0f, -38.0f, 212.0f},
                                       {60.0f, -38.0f, 168.0f},   {-195.0f, -28.0f, 60.0f},
                                       {-205.0f, -28.0f, -10.0f}, {225.0f, -34.0f, 120.0f}};
    static const std::vector<float> list(&sources[0][0],
                                         &sources[0][0] + sizeof(sources) / sizeof(float));
    return list;
}

void OceanWorld::build()
{
    Grid g;
    buildTerrain(g);
    buildRuins(g);
    buildWreck(g);
    carveRide(g);
    decorate(g);
    spreadLight(g);

    m_textures.assign(kTextureCount, 0);
    const Image images[kTextureCount] = {PixelArt::blank(), // AIR (unused)
                                         PixelArt::blank(), // ISLAND (never drawn)
                                         makeSand(),
                                         makeStone(),
                                         makeCaveRock(),
                                         makeBricks(),
                                         makePillar(),
                                         makePlanks(),
                                         makeLog(),
                                         makeCoralBlock(226, 96, 140, 501u),
                                         makeCoralBlock(236, 142, 62, 511u),
                                         makeCoralBlock(146, 92, 204, 521u),
                                         makeCrystal(),
                                         PixelArt::blank(), // NO_PLANT
                                         makeSeagrass(),
                                         makeKelp(),
                                         makeFanCoral(),
                                         makeTubeCoral(),
                                         makeCrystalSpike(),
                                         makeStarfish()};
    for (int t = 0; t < kTextureCount; ++t)
        if (t != AIR && t != ISLAND && t != plantTexture(NO_PLANT))
            m_textures[static_cast<size_t>(t)] =
                PixelArt::upload(images[t], t == plantTexture(KELP) || t == SAND);

    Batches solids(kTextureCount);
    Batches plants(kTextureCount);
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const int b = g.get(bx, j, bz);
                if (b != AIR && b != ISLAND)
                    addBlockFaces(g, bx, j, bz, solids);
                else if (b == AIR && g.plant[Grid::index(bx, j, bz)] != NO_PLANT)
                    addPlant(g, bx, j, bz, plants);
            }
    addFarFloor(solids);

    m_solidList = compileList(solids, m_textures);
    m_plantList = compileList(plants, m_textures);
    m_built = true;
}

void OceanWorld::draw(bool shadowPass, float time)
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    if (!m_built)
        build();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glCallList(m_solidList);
    if (!shadowPass)
    {
        glCallList(m_plantList);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        drawFish(time);
    }
    glPopAttrib();
}

void OceanWorld::drawFish(float time)
{
    struct School
    {
        float x, y, z, radius, speed;
        unsigned char body[3], fin[3];
    };
    const School schools[] = {
        {175.0f, -10.0f, 5.0f, 16.0f, 0.35f, {250, 200, 60}, {40, 60, 120}},
        {195.0f, -16.0f, 110.0f, 20.0f, -0.25f, {230, 120, 50}, {250, 250, 250}},
        {30.0f, -22.0f, 205.0f, 18.0f, 0.3f, {90, 150, 230}, {250, 220, 80}},
        {-110.0f, -26.0f, 215.0f, 14.0f, -0.4f, {190, 196, 210}, {120, 130, 150}},
        {-190.0f, -20.0f, 150.0f, 12.0f, 0.3f, {60, 210, 200}, {30, 90, 110}}};
    for (const School &s : schools)
        for (int i = 0; i < 12; ++i)
        {
            const float phase =
                static_cast<float>(i) * 0.52f + static_cast<float>(hash(i, 0, 601u) % 100u) * 0.01f;
            const float a = time * s.speed + phase;
            const float r = s.radius * (0.7f + 0.3f * sinf(static_cast<float>(i) * 1.7f));
            const float x = s.x + cosf(a) * r;
            const float z = s.z + sinf(a) * r;
            const float y = s.y + sinf(a * 2.0f + static_cast<float>(i)) * 2.0f +
                            static_cast<float>(i % 3) * 1.5f;
            // Swim along the circle (the model faces +z); the tail wags.
            const float dir = s.speed > 0.0f ? 1.0f : -1.0f;
            const float heading = atan2f(-sinf(a) * dir, cosf(a) * dir);
            glPushMatrix();
            glTranslatef(x, y, z);
            glRotatef(heading * 57.29578f, 0.0f, 1.0f, 0.0f);
            glColor3ubv(s.body);
            fishBox(-0.25f, -0.35f, -0.6f, 0.25f, 0.35f, 0.6f);
            glColor3ubv(s.fin);
            glRotatef(sinf(time * 8.0f + phase * 5.0f) * 25.0f, 0.0f, 1.0f, 0.0f);
            fishBox(-0.06f, -0.3f, -1.05f, 0.06f, 0.3f, -0.6f);
            glPopMatrix();
        }
}
