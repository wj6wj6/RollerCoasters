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

#include "Frustum.h"

#include "LandTextures.h"
#include "WorldShape.h"
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
// The ride circles the island at kScale times its first distance
// (WorldShape.h); the layout below is given in the first units where noted.
const float kScale = WorldShape::kScale;
const int kHalf = 136; // blocks each side of the origin: +-680 units
const int kSizeXZ = 2 * kHalf;
const int kMinY = -10; // block layer j spans y = 5j .. 5j + 5
const int kMaxY = 10;  // the islet's hills rise to y = 55
const int kSizeY = kMaxY - kMinY + 1;
const int kDeepestTop = -9; // top layer of the deep sea bed (y = -40)

// Nothing that grows or is built on the sea bed reaches above this layer
// (y = -15), so the whale, the rays and the turtles can roam above it.
const int kSceneryTop = -4;

// Room kept free around the ride. Blocks are carved out of a tube around it;
// plants keep out of a slightly different one because they are thinner but
// sway. OceanWorld::rideObstructions() checks both against the real track.
const float kCarveRadius = 8.5f;
const float kCarveBelow = 3.0f;
const float kCarveAbove = 10.0f;
const float kPlantRadius = 8.0f;
const float kPlantBelow = 1.0f;
const float kPlantAbove = 9.0f;

enum Block
{
    AIR = 0,
    ISLAND, // under the island: solid but drawn by LandWorld
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
    CORAL_YELLOW,
    CORAL_RED,
    CRYSTAL,
    LANTERN, // glowing lamp block
    GOLD,
    MARBLE,
    LAPIS,
    GRASS,     // on the islet above the sea
    GRASS_TOP, // only its top face's texture, never a block
    BLOCK_COUNT
};

enum Plant
{
    NO_PLANT = 0,
    SEAGRASS,
    KELP,
    FAN_CORAL,
    FAN_CORAL_GOLD,
    FAN_CORAL_VIOLET,
    TUBE_CORAL,
    ANEMONE,
    GLOW_BUD,
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

bool isCoral(int block)
{
    return block >= CORAL_PINK && block <= CORAL_RED;
}

// The demo ride, the "Grand Tour". The first 25 points are the underwater
// part: it leaves the island to the east and dives through ring gates into
// the reef, turns south through the kelp forest and a rock arch, runs west
// through the ruins, past the jellyfish and the wreck, north through a
// lamp-lit portal into the crystal tunnel, and climbs out through more rings
// back onto the island. It stays a few units above the sea bed; everything in
// its way is carved out, whichever spline type draws it.
//
// The rest is the land part (LandWorld builds the island around it): a
// spiral up the snow mountain on a ledge, a dive past the floating island
// into the badlands canyon, low over the water past the waterfall, out
// through the canyon wall, over a lava river and up across the volcano's
// crater, over a bridge above the outgoing track, through the mine tunnel
// in the crags and down into the meadow.
const float kDemoTrack[][3] = {{30, 6, -20},
                               {72, 6, -8},
                               {108, 2, 4},
                               {140, -12, 14},
                               {172, -27, 2},
                               {204, -33, -14},
                               {232, -34, 22},
                               {236, -34, 78},
                               {214, -34, 128},
                               {170, -33, 166},
                               {112, -34, 188},
                               {50, -35, 188},
                               {-12, -35, 188},
                               {-74, -34, 190},
                               {-132, -33, 196},
                               {-180, -32, 168},
                               {-205, -31, 112},
                               {-208, -30, 48},
                               {-206, -30, -18},
                               {-196, -25, -78},
                               {-168, -14, -134},
                               {-126, -1, -146},
                               {-84, 10, -152},
                               {-44, 12, -112},
                               {-10, 9, -62},
                               // land
                               {-20, 14, -34},
                               {-50, 24, -18},
                               {-84, 32, -2},
                               {-91, 42, 28},
                               {-73, 52, 47},
                               {-52, 62, 48},
                               {-37, 70, 32},
                               {-28, 74, 6},
                               {-2, 70, -12},
                               {20, 52, 4},
                               {22, 26, 30},
                               {14, 9, 58},
                               {8, 6, 82},
                               {32, 9, 93},
                               {62, 14, 95},
                               {92, 34, 86},
                               {86, 56, 64},
                               {60, 58, 58},
                               {72, 46, 24},
                               {90, 34, -8},
                               {84, 30, -46},
                               {56, 26, -88},
                               {26, 18, -74},
                               {14, 10, -46}};
const int kDemoCount = static_cast<int>(sizeof(kDemoTrack) / sizeof(kDemoTrack[0]));

// A control point of the ride in the world: the first layout's, scaled out
// and lifted (WorldShape.h).
float trackPoint(int i, int k)
{
    const float v = kDemoTrack[((i % kDemoCount) + kDemoCount) % kDemoCount][k];
    return k == 1 ? WorldShape::lifted(v) : WorldShape::scaled(v);
}
const int kLandStart = 24; // the control point where the ride comes back ashore

// Control points the scenery is placed around.
const int kReefArchPoint = 5;
const int kArchPoint = 9;
const float kDiveGates[3] = {2.0f, 2.4f, 2.8f};
const float kPortalGate = 15.55f;
const float kExitGates[3] = {20.55f, 21.0f, 21.45f};

// ---------------------------------------------------------------------
// Pixel art. The blocks share the island's 32 x 32 textures (LandTextures);
// the plants are 16 x 16 and drawn here, texel by texel.
// ---------------------------------------------------------------------
int shadeOf(int x, int y, unsigned int seed, int range)
{
    return static_cast<int>(hash(x, y, seed) % static_cast<unsigned int>(2 * range + 1)) - range;
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

Image makeFanCoral(int r, int g, int b)
{
    Image p = PixelArt::blank();
    for (int k = 0; k < 7; ++k)
    {
        const float a = (-55.0f + 18.0f * static_cast<float>(k)) * 3.14159265f / 180.0f;
        const int x1 = 8 + static_cast<int>(floorf(sinf(a) * 7.0f + 0.5f));
        const int y1 = 15 - static_cast<int>(floorf(cosf(a) * 13.0f + 0.5f));
        line(p, 8, 15, x1, y1, r, g, b);
        set(p, x1, y1, r + 60, g + 70, b + 60); // tip
        // A side twig half way up.
        const int xm = (8 + x1) / 2;
        const int ym = (15 + y1) / 2;
        line(p, xm, ym, xm + (k < 3 ? -2 : 2), ym - 2, r + 16, g + 24, b + 20);
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

// A short stalk with a ring of pale tentacles.
Image makeAnemone()
{
    Image p = PixelArt::blank();
    for (int y = 11; y < S; ++y)
        for (int x = 5; x <= 10; ++x)
        {
            const int s = shadeOf(x, y, 181u, 8);
            set(p, x, y, 208 + s, 84 + s, 122 + s);
        }
    for (int k = 0; k < 8; ++k)
    {
        const int x0 = 4 + k;
        const int lean = k < 4 ? k - 4 : k - 3;
        const int height = 6 + static_cast<int>(hash(k, 0, 182u) % 4u);
        const int x1 = x0 + lean;
        const int y1 = 11 - height;
        line(p, x0, 11, x1, y1, 250, 150, 176);
        set(p, x1, y1, 255, 236, 200); // glowing tip
    }
    return p;
}

// A cluster of small glowing bulbs on short stems.
Image makeGlowBud()
{
    Image p = PixelArt::blank();
    const int xs[3] = {3, 8, 12};
    const int tops[3] = {9, 6, 10};
    for (int k = 0; k < 3; ++k)
    {
        for (int y = tops[k] + 3; y < S; ++y)
            set(p, xs[k], y, 60, 120, 96);
        for (int dy = 0; dy < 3; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const bool corner = dx != 0 && dy != 1;
                if (corner)
                    set(p, xs[k] + dx, tops[k] + dy, 110, 230, 190);
                else
                    set(p, xs[k] + dx, tops[k] + dy, 215, 255, 220);
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

// Position on the demo ride at parameter u (one unit per control point).
void ridePoint(float u, int type, float out[3])
{
    const float whole = floorf(u);
    const int seg = static_cast<int>(whole);
    const float t = u - whole;
    for (int k = 0; k < 3; ++k)
        out[k] = splineComponent(trackPoint(seg - 1, k), trackPoint(seg, k), trackPoint(seg + 1, k),
                                 trackPoint(seg + 2, k), t, type);
}

// Closely spaced points along the ride for all three spline types: x, y, z.
std::vector<float> sampleRide()
{
    const int steps = 160;
    std::vector<float> xyz;
    xyz.reserve(static_cast<size_t>(3 * kDemoCount * steps * 3));
    for (int type = 1; type <= 3; ++type)
        for (int i = 0; i < kDemoCount * steps; ++i)
        {
            float q[3];
            ridePoint(static_cast<float>(i) / static_cast<float>(steps), type, q);
            xyz.push_back(q[0]);
            xyz.push_back(q[1]);
            xyz.push_back(q[2]);
        }
    return xyz;
}

// Direction of travel (unit, cardinal spline) and the horizontal direction
// to the left of it.
void rideFrame(float u, float forward[3], float left[3])
{
    float a[3], b[3];
    ridePoint(u - 0.05f, 2, a);
    ridePoint(u + 0.05f, 2, b);
    float length = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        forward[k] = b[k] - a[k];
        length += forward[k] * forward[k];
    }
    length = sqrtf(length);
    for (int k = 0; k < 3; ++k)
        forward[k] /= length;
    const float flat = sqrtf(forward[0] * forward[0] + forward[2] * forward[2]);
    left[0] = forward[2] / flat;
    left[1] = 0.0f;
    left[2] = -forward[0] / flat;
}

// For every column of the grid: the lowest and highest the ride passes
// within reach of it. Plants stay out of that band.
struct Clearance
{
    std::vector<float> low, high;

    Clearance()
        : low(static_cast<size_t>(kSizeXZ * kSizeXZ), 1.0e9f),
          high(static_cast<size_t>(kSizeXZ * kSizeXZ), -1.0e9f)
    {
    }
    static size_t at(int bx, int bz)
    {
        return static_cast<size_t>(bx + kHalf) * kSizeXZ + static_cast<size_t>(bz + kHalf);
    }
    void add(const std::vector<float> &ride)
    {
        for (size_t i = 0; i + 2 < ride.size(); i += 3)
        {
            const float x = ride[i], y = ride[i + 1], z = ride[i + 2];
            for (int bx = blockOf(x - kPlantRadius); bx <= blockOf(x + kPlantRadius); ++bx)
                for (int bz = blockOf(z - kPlantRadius); bz <= blockOf(z + kPlantRadius); ++bz)
                {
                    if (bx < -kHalf || bx >= kHalf || bz < -kHalf || bz >= kHalf)
                        continue;
                    const float dx = centerOf(bx) - x;
                    const float dz = centerOf(bz) - z;
                    if (dx * dx + dz * dz > kPlantRadius * kPlantRadius)
                        continue;
                    const size_t c = at(bx, bz);
                    low[c] = fminf(low[c], y);
                    high[c] = fmaxf(high[c], y);
                }
        }
    }
    // Would a plant in this cell reach into the room the train needs?
    bool blocks(int bx, int j, int bz) const
    {
        if (bx < -kHalf || bx >= kHalf || bz < -kHalf || bz >= kHalf)
            return false;
        const size_t c = at(bx, bz);
        const float bottom = static_cast<float>(j) * kBlock;
        return bottom + kBlock > low[c] - kPlantBelow && bottom < high[c] + kPlantAbove;
    }
};

// The ruins, the wreck and the jellyfish need level ground: no outcrops here.
bool keptFlat(float cx, float cz)
{
    cx /= kScale; // the first layout's units
    cz /= kScale;
    return cx > -150.0f && cx < 100.0f && cz > 150.0f && cz < 285.0f;
}

// The shallow reef west of the island, towards the sunset: a shelf of sand
// one block under the surface layer, with coral on it that shows through
// the water from the shore and the ride. Its outline wanders with noise.
bool onShelf(float cx, float cz)
{
    cx /= kScale; // the first layout's units
    cz /= kScale;
    const float ex = (cx + 130.0f) / 28.0f;
    const float ez = (cz - 25.0f) / 96.0f;
    const float wobble = (valueNoise(cx * 0.05f, cz * 0.05f, 37u) - 0.5f) * 0.7f;
    return ex * ex + ez * ez < 1.0f + wobble && cx < -108.0f;
}

bool inReef(float cx, float cz)
{
    if (onShelf(cx, cz))
        return true;
    cx /= kScale; // the first layout's units
    cz /= kScale;
    return (cx > 112.0f && cx < 262.0f && cz > -72.0f && cz < 62.0f) ||
           (cx > -215.0f && cx < -105.0f && cz > -190.0f && cz < -92.0f);
}

// The top layer of the terraced sea bed at a column centre, `edge` units out
// from the island's edge (before outcrops, the islet and the carving).
int seaBedTop(float cx, float cz, float edge)
{
    cx /= kScale; // the first layout's units
    cz /= kScale;
    edge /= kScale;
    float depth =
        1.0f + (edge - 6.0f) / 11.0f + (valueNoise(cx * 0.025f, cz * 0.025f, 7u) - 0.5f) * 2.5f;
    depth = fmaxf(1.0f, fminf(8.0f, depth));
    if (onShelf(cx, cz))
        return -3; // the shallow reef's shelf: its coral fills layer -2
    return -static_cast<int>(floorf(depth + 0.5f)) - 1;
}

// Sand and stone terraces that step down away from the island, rocky
// outcrops on the deep bed, and the rocky islet (its tunnel is carved later).
void buildTerrain(Grid &g)
{
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float cx = centerOf(bx);
            const float cz = centerOf(bz);
            const float edge = -WorldShape::inland(cx, cz);
            if (edge < 0.0f)
            {
                for (int j = kMinY; j <= -2; ++j)
                    g.put(bx, j, bz, SAND);
                g.put(bx, -1, bz, ISLAND);
                continue;
            }
            int top = seaBedTop(cx, cz, edge);

            // The islet: a rough dome west of the island that breaks the surface.
            const float ex = (cx / kScale + 205.0f) / 58.0f;
            const float ez = (cz / kScale - 25.0f) / 118.0f;
            const float e2 = ex * ex + ez * ez;
            bool islet = false;
            if (e2 < 1.0f)
            {
                // a dome with hills and hollows on it, not a flat table
                const float hills = valueNoise(cx * 0.012f, cz * 0.012f, 12u) * 0.65f +
                                    valueNoise(cx * 0.03f, cz * 0.03f, 14u) * 0.35f;
                const float h = WorldShape::lifted(
                    -40.0f + (1.0f - e2) * 50.0f +
                    (valueNoise(cx * 0.06f / kScale, cz * 0.06f / kScale, 9u) - 0.5f) * 14.0f +
                    (hills - 0.45f) * (16.0f + 22.0f * (1.0f - e2)) +
                    (valueNoise(cx * 0.08f, cz * 0.08f, 10u) - 0.5f) * 4.0f);
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
                {
                    // mostly sand: all of it near and above the sea, with
                    // grass on the higher tops; rock only deep under water,
                    // so no dark stone shows at or above the waterline
                    b = (j >= -3) ? SAND : CAVE_ROCK;
                    if (j == top && top >= 2 && hash(bx, bz, 17u) % 7u != 0)
                        b = GRASS;
                }
                else if (j == top)
                    b = gravel ? STONE : SAND;
                else if (j == top - 1)
                    b = SAND;
                g.put(bx, j, bz, b);
            }
            if (islet)
                continue;

            // Outcrops and low mesas: something to see in the distance and
            // for the ride to cut through.
            const float rock = valueNoise(cx * 0.021f / kScale, cz * 0.021f / kScale, 23u) * 0.7f +
                               valueNoise(cx * 0.07f / kScale, cz * 0.07f / kScale, 29u) * 0.3f;
            if (edge > 50.0f * kScale && rock > 0.62f && !keptFlat(cx, cz))
            {
                int extra = 1 + static_cast<int>((rock - 0.62f) * 30.0f);
                if (top + extra > kSceneryTop - 1)
                    extra = kSceneryTop - 1 - top;
                for (int k = 1; k <= extra; ++k)
                    g.put(bx, top + k, bz,
                          (k == extra || hash(bx + k, bz, 24u) % 3u == 0) ? STONE : CAVE_ROCK);
            }
            else if (hash(bx, bz, 15u) % 53u == 0 && top < -3)
                g.put(bx, top + 1, bz, STONE); // a lone boulder
        }
}

// A rough rock rising from the sea bed to about topY, radiusA long in the
// direction of angle and radiusB across it.
void rockBlob(Grid &g, float cx, float cz, float radiusA, float radiusB, float angle, float topY,
              unsigned int seed)
{
    const float ca = cosf(angle);
    const float sa = sinf(angle);
    const float reach = (radiusA > radiusB ? radiusA : radiusB) + kBlock;
    for (int bx = blockOf(cx - reach); bx <= blockOf(cx + reach); ++bx)
        for (int bz = blockOf(cz - reach); bz <= blockOf(cz + reach); ++bz)
        {
            const float dx = centerOf(bx) - cx;
            const float dz = centerOf(bz) - cz;
            const float a = (dx * ca + dz * sa) / radiusA;
            const float b = (dz * ca - dx * sa) / radiusB;
            const float d2 = a * a + b * b;
            if (d2 >= 1.0f)
                continue;
            const int base = g.top(bx, bz);
            if (base < kMinY || g.get(bx, base, bz) == ISLAND)
                continue;
            const float baseY = static_cast<float>(base + 1) * kBlock;
            const float n = valueNoise(centerOf(bx) * 0.11f, centerOf(bz) * 0.11f, seed);
            const float height = (topY - baseY) * powf(1.0f - d2, 0.4f) + (n - 0.5f) * 7.0f;
            int top = base + static_cast<int>(floorf(height / kBlock + 0.5f));
            if (top > kSceneryTop)
                top = kSceneryTop;
            for (int j = base + 1; j <= top; ++j)
                g.put(bx, j, bz,
                      (j == top || hash(bx * 5 + j, bz, seed) % 3u != 0) ? STONE : CAVE_ROCK);
        }
}

// Rock arches across the ride (the ride is carved through them afterwards)
// and spires beside it.
void buildRocks(Grid &g)
{
    const int arches[2] = {kReefArchPoint, kArchPoint};
    for (int k = 0; k < 2; ++k)
    {
        float forward[3], left[3];
        rideFrame(static_cast<float>(arches[k]), forward, left);
        const float c[3] = {trackPoint(arches[k], 0), trackPoint(arches[k], 1),
                            trackPoint(arches[k], 2)};
        rockBlob(g, c[0], c[2], 13.0f, 44.0f, atan2f(forward[2], forward[0]), -16.0f,
                 static_cast<unsigned int>(61 + k));
    }

    struct Spire
    {
        float u, side, radius, topY;
    };
    const Spire spires[] = {
        {4.4f, 21.0f, 8.0f, -20.0f},  {5.6f, -22.0f, 9.0f, -18.0f},  {6.5f, 24.0f, 10.0f, -17.0f},
        {7.5f, -24.0f, 8.0f, -22.0f}, {8.4f, 22.0f, 7.0f, -24.0f},   {9.6f, -30.0f, 10.0f, -18.0f},
        {10.4f, 23.0f, 9.0f, -17.0f}, {14.5f, -24.0f, 9.0f, -18.0f}, {15.1f, 24.0f, 10.0f, -17.0f},
        {19.4f, 21.0f, 8.0f, -22.0f}, {19.8f, -22.0f, 9.0f, -20.0f}, {20.3f, 23.0f, 7.0f, -24.0f}};
    unsigned int seed = 71u;
    for (const Spire &s : spires)
    {
        float c[3], forward[3], left[3];
        ridePoint(s.u, 2, c);
        rideFrame(s.u, forward, left);
        rockBlob(g, c[0] + left[0] * s.side, c[2] + left[2] * s.side, s.radius, s.radius * 0.8f,
                 atan2f(forward[2], forward[0]), s.topY, seed++);
    }
}

// A gate across the ride at parameter u: a round ring of marble and lapis,
// or a square stone portal with a lamp in each upper corner. It stands
// upright in the grid plane closest to square-on to the ride, one block
// thick, so it reads as a clean blocky ring.
void buildGate(Grid &g, float u, bool portal)
{
    float c[3], forward[3], left[3];
    ridePoint(u, 2, c);
    rideFrame(u, forward, left);
    const bool acrossZ = fabsf(forward[0]) > fabsf(forward[2]); // the ring spans y and z
    const int plane = blockOf(acrossZ ? c[0] : c[2]);
    const float inner = 11.0f;
    const float outer = 17.0f;
    const int reach = static_cast<int>(outer / kBlock) + 1;
    const int centre = blockOf(acrossZ ? c[2] : c[0]);
    for (int a = centre - reach; a <= centre + reach; ++a)
        for (int j = blockOf(c[1]) - reach; j <= blockOf(c[1]) + reach; ++j)
        {
            const int bx = acrossZ ? plane : a;
            const int bz = acrossZ ? a : plane;
            if (!Grid::inside(bx, j, bz) || g.get(bx, j, bz) == ISLAND)
                continue;
            const float s = centerOf(a) - (acrossZ ? c[2] : c[0]);
            const float v = centerOf(j) - c[1];
            if (portal)
            {
                const float m = fmaxf(fabsf(s), fabsf(v));
                if (m < inner || m >= outer)
                    continue;
                const bool corner = fabsf(s) >= inner && fabsf(v) >= inner;
                g.put(bx, j, bz,
                      corner ? (v > 0.0f ? LANTERN : BRICK)
                             : (fabsf(s) > fabsf(v) ? PILLAR : BRICK));
            }
            else
            {
                const float radius = sqrtf(s * s + v * v);
                if (radius < inner || radius >= outer)
                    continue;
                const float turn = (atan2f(v, s) + 3.14159265f) / 6.2831853f;
                g.put(bx, j, bz, static_cast<int>(turn * 12.0f) % 2 == 0 ? MARBLE : LAPIS);
            }
        }
}

void putCoral(Grid &g, int bx, int j, int bz, int coral)
{
    if (j <= kSceneryTop - 1 && Grid::inside(bx, j, bz) && g.get(bx, j, bz) == AIR)
        g.put(bx, j, bz, coral);
}

// Coral mounds and branching coral trees in five colours. Built before the
// ride is carved, so none of it ends up on the rails.
void buildReefs(Grid &g)
{
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float cx = centerOf(bx);
            const float cz = centerOf(bz);
            if (!inReef(cx, cz))
                continue;
            const int top = g.top(bx, bz);
            if (top < kMinY || top > -3 || g.get(bx, top, bz) == ISLAND ||
                isCoral(g.get(bx, top, bz)))
                continue;
            const unsigned int h = hash(bx, bz, 301u);
            const float pick =
                valueNoise(cx * 0.05f, cz * 0.05f, 19u) + static_cast<float>(h % 100u) * 0.004f;
            const int coral = CORAL_PINK + static_cast<int>(pick * 5.0f) % 5;

            if (valueNoise(cx * 0.08f, cz * 0.08f, 17u) > 0.56f)
            {
                const int height = 1 + static_cast<int>(h % 2u);
                for (int k = 1; k <= height; ++k)
                    putCoral(g, bx, top + k, bz, coral);
            }
            else if (h % 31u == 0)
            {
                // A tree: a trunk with short arms, some of them turning up.
                const int trunk = 2 + static_cast<int>((h >> 8) % 3u);
                for (int k = 1; k <= trunk; ++k)
                {
                    putCoral(g, bx, top + k, bz, coral);
                    if (k < 2)
                        continue;
                    for (int d = 0; d < 4; ++d)
                    {
                        const unsigned int arm = hash(bx * 7 + d, bz * 3 + k, 303u);
                        if (arm % 3u != 0)
                            continue;
                        putCoral(g, bx + dirs[d][0], top + k, bz + dirs[d][1], coral);
                        if (arm % 2u == 0)
                            putCoral(g, bx + dirs[d][0], top + k + 1, bz + dirs[d][1], coral);
                    }
                }
            }
        }
}

// The coral of the shallow reef: patches of one colour each, mostly the
// warm pinks, reds and oranges that glow at sunset, with sand showing
// between them. One layer only (y = -10 .. -5), so it stays four units
// under the surface and the ride passes well above it.
void buildShallowReef(Grid &g)
{
    const int warm[5] = {CORAL_PINK, CORAL_RED, CORAL_ORANGE, CORAL_VIOLET, CORAL_YELLOW};
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float cx = centerOf(bx), cz = centerOf(bz);
            if (!onShelf(cx, cz) || g.get(bx, -3, bz) == AIR || g.get(bx, -2, bz) != AIR)
                continue;
            const float patch = valueNoise(cx * 0.09f, cz * 0.09f, 43u);
            const unsigned int h = hash(bx, bz, 307u);
            if (patch < 0.3f || h % 9u == 0)
                continue; // sand between the patches
            const float colour = valueNoise(cx * 0.04f, cz * 0.04f, 47u);
            // the warm colours twice as often as violet and yellow
            const int pick = colour < 0.28f   ? 0
                             : colour < 0.5f  ? 1
                             : colour < 0.68f ? 2
                             : colour < 0.84f ? 3
                                              : 4;
            g.put(bx, -2, bz, warm[(pick + static_cast<int>(h % 7u == 0)) % 5]);
            if (g.get(bx, -3, bz) == SAND && h % 4u == 0)
                g.put(bx, -3, bz, STONE); // rock under the coral here and there
        }
}

// A giant open clam with a glowing pearl, hinge towards (hx, hz).
void buildClam(Grid &g, int bx, int bz, int hx, int hz)
{
    const int base = g.top(bx, bz);
    if (base < kMinY || base > kSceneryTop - 4 || g.get(bx, base, bz) == ISLAND)
        return;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
        {
            g.put(bx + dx, base + 1, bz + dz, MARBLE);
            // The upper shell stands open along the hinge side.
            if (dx * hx + dz * hz == 1)
            {
                g.put(bx + dx, base + 2, bz + dz, MARBLE);
                if (dx * hz == 0 && dz * hx == 0)
                    g.put(bx + dx, base + 3, bz + dz, MARBLE);
            }
        }
    g.put(bx, base + 2, bz, LANTERN);
}

void buildClams(Grid &g)
{
    struct Spot
    {
        float u, side;
    };
    const Spot spots[] = {{3.7f, -19.0f}, {4.8f, 19.0f},  {5.3f, -30.0f},
                          {6.2f, -20.0f}, {19.6f, 34.0f}, {20.1f, -19.0f}};
    for (const Spot &s : spots)
    {
        float c[3], forward[3], left[3];
        ridePoint(s.u, 2, c);
        rideFrame(s.u, forward, left);
        // Open towards the ride.
        const int toRide = s.side > 0.0f ? -1 : 1;
        const bool alongX = fabsf(left[0]) > fabsf(left[2]);
        const int hx = alongX ? (left[0] * static_cast<float>(toRide) > 0.0f ? -1 : 1) : 0;
        const int hz = alongX ? 0 : (left[2] * static_cast<float>(toRide) > 0.0f ? -1 : 1);
        buildClam(g, blockOf(c[0] + left[0] * s.side), blockOf(c[2] + left[2] * s.side), hx, hz);
    }
}

// A column standing on whatever is there, at most up to the scenery limit.
void buildColumn(Grid &g, int bx, int bz, int height, int block, int cap)
{
    const int base = g.top(bx, bz);
    for (int k = 1; k <= height; ++k)
    {
        const int j = base + k;
        if (j > kSceneryTop)
            break;
        g.put(bx, j, bz, (k == height && cap != AIR) ? cap : block);
    }
}

// A sunken colonnade the ride runs through, with a lamp-lit gate at each
// end and a stepped temple behind it.
// The first layout's block x scaled out (blocks of the ruins along the ride).
int scaledColumn(int b)
{
    return static_cast<int>(floorf((static_cast<float>(b) + 0.5f) * kScale - 0.5f + 0.5f));
}

// The ride runs west through the ruins along z = 470 (block 94); the first
// layout had it at block 37, so the ruins move out by this many blocks.
const int kRuinsShift = 57;

void buildRuins(Grid &g)
{
    const int z = kRuinsShift;
    // Paved floor with some slabs missing.
    for (int bx = scaledColumn(-10); bx <= scaledColumn(18); ++bx)
        for (int bz = 32 + z; bz <= 42 + z; ++bz)
            if (hash(bx, bz, 201u) % 7u != 0)
                g.put(bx, g.top(bx, bz), bz, BRICK);

    // Gates: two tall pillars and a lintel across the ride, lamps at its ends.
    const int gates[3] = {scaledColumn(17), scaledColumn(4), scaledColumn(-9)};
    for (int gate = 0; gate < 3; ++gate)
    {
        const int bx = gates[gate];
        buildColumn(g, bx, 34 + z, 4, PILLAR, AIR);
        buildColumn(g, bx, 40 + z, 4, PILLAR, AIR);
        const int lintel = kDeepestTop + 5;
        for (int bz = 33 + z; bz <= 41 + z; ++bz)
            g.put(bx, lintel, bz, (bz == 33 + z || bz == 41 + z) ? LANTERN : BRICK);
    }

    // Two rows of columns, some broken off with their drums lying nearby,
    // the whole ones carrying a lamp.
    for (int bx = scaledColumn(13); bx >= scaledColumn(-5); bx -= 4)
        for (int side = 0; side < 2; ++side)
        {
            if (bx == gates[1] || bx == gates[1] + 1 || bx == gates[1] - 1)
                continue;
            const int bz = (side == 0 ? 33 : 41) + z;
            const unsigned int h = hash(bx, side, 202u);
            const int height = 2 + static_cast<int>(h % 4u);
            buildColumn(g, bx, bz, height, PILLAR, (height == 5 && h % 3u != 0) ? LANTERN : AIR);
            if (height < 4)
            {
                const int fx = bx + (hash(bx, side, 203u) % 2u == 0 ? 1 : -1);
                const int fz = bz + (side == 0 ? -1 : 1);
                buildColumn(g, fx, fz, 1, PILLAR, AIR);
                buildColumn(g, fx + 1, fz, 1, PILLAR, AIR);
            }
        }

    // A low broken wall behind the south row.
    for (int bx = scaledColumn(-7); bx <= scaledColumn(15); ++bx)
        buildColumn(g, bx, 43 + z, 1 + static_cast<int>(hash(bx, 0, 204u) % 3u), BRICK, AIR);

    // The stepped temple, a lamp and gold on top: larger now.
    const int cx = scaledColumn(4), cz = 52 + z;
    for (int tier = 0; tier < 5; ++tier)
    {
        const int half = 6 - tier;
        for (int bx = cx - half; bx <= cx + half; ++bx)
            for (int bz = cz - half; bz <= cz + half; ++bz)
            {
                const bool corner = abs(bx - cx) == half && abs(bz - cz) == half;
                g.put(bx, kDeepestTop + 1 + tier, bz, corner ? PILLAR : BRICK);
            }
    }
    g.put(cx, kDeepestTop + 6, cz, LANTERN);
    g.put(cx + 1, kDeepestTop + 5, cz + 1, GOLD);
    g.put(cx - 1, kDeepestTop + 5, cz - 1, GOLD);
}

// A wrecked ship lying beside the ride: stern castle, masts, and a breach
// in the side that shows the lamp-lit gold in the hold.
void buildWreck(Grid &g)
{
    const int x0 = scaledColumn(-21) - 6, x1 = x0 + 13; // bow to stern
    const int z0 = 43 + kRuinsShift, z1 = z0 + 6;
    const int keel = kDeepestTop + 1;
    for (int bx = x0; bx <= x1; ++bx)
    {
        const int fromBow = bx - x0;
        const int fromStern = x1 - bx;
        // The hull narrows towards the ends.
        const int inset =
            fromBow == 0 ? 3 : (fromBow == 1 ? 2 : ((fromBow == 2 || fromStern == 0) ? 1 : 0));
        const bool castle = fromStern <= 3;
        for (int bz = z0 + inset; bz <= z1 - inset; ++bz)
        {
            const bool side =
                bz == z0 + inset || bz == z1 - inset || fromBow == 0 || fromStern == 0;
            const bool breach = bz == z0 && (fromBow == 6 || fromBow == 7);
            g.put(bx, keel, bz, PLANKS);
            if (!side)
            {
                if (castle)
                    g.put(bx, keel + 3, bz, PLANKS); // what is left of the deck
                continue;
            }
            if (breach)
                continue;
            // The sides have rotted to different heights.
            const int height = castle ? 4 : (hash(bx, bz, 211u) % 4u == 0 ? 1 : 2);
            for (int k = 1; k <= height; ++k)
                g.put(bx, keel + k, bz, (castle && k == height) ? LOG : PLANKS);
        }
    }
    // The hold, seen through the breach.
    g.put(x0 + 6, keel + 1, z0 + 1, GOLD);
    g.put(x0 + 7, keel + 1, z0 + 1, GOLD);
    g.put(x0 + 7, keel + 2, z0 + 1, GOLD);
    g.put(x0 + 7, keel + 1, z0 + 2, GOLD);
    g.put(x0 + 6, keel + 1, z0 + 2, LANTERN);
    // Main mast with its yard, and the fore mast snapped off and lying outside.
    for (int j = keel + 1; j <= kSceneryTop; ++j)
        g.put(x0 + 5, j, z0 + 3, LOG);
    for (int bz = z0 + 1; bz <= z1 - 1; ++bz)
        g.put(x0 + 5, kSceneryTop - 1, bz, LOG);
    g.put(x0 + 2, keel + 1, z0 + 3, LOG);
    g.put(x0 + 2, keel + 2, z0 + 3, LOG);
    for (int bx = x0 + 1; bx <= x0 + 4; ++bx)
        g.put(bx, keel, z1 + 2, LOG);
}

// Removes every block the train could touch, for all three spline types:
// this is what makes the tunnel, the arches and the gaps in the gates.
void carveRide(Grid &g, const std::vector<float> &ride)
{
    for (size_t i = 0; i + 2 < ride.size(); i += 3)
    {
        const float x = ride[i], y = ride[i + 1], z = ride[i + 2];
        const int jLow = blockOf(y - kCarveBelow);
        const int jHigh = blockOf(y + kCarveAbove);
        for (int bx = blockOf(x - kCarveRadius); bx <= blockOf(x + kCarveRadius); ++bx)
            for (int bz = blockOf(z - kCarveRadius); bz <= blockOf(z + kCarveRadius); ++bz)
            {
                const float dx = centerOf(bx) - x;
                const float dz = centerOf(bz) - z;
                if (dx * dx + dz * dz > kCarveRadius * kCarveRadius)
                    continue;
                for (int j = jLow; j <= jHigh; ++j)
                {
                    const float cy = centerOf(j);
                    if (cy > y - kCarveBelow && cy <= y + kCarveAbove && g.get(bx, j, bz) != ISLAND)
                        g.put(bx, j, bz, AIR);
                }
            }
    }
}

// Crystals in the tunnel walls.
void embedCrystals(Grid &g)
{
    for (int bx = scaledColumn(-52); bx <= scaledColumn(-28); ++bx)
        for (int bz = scaledColumn(-24); bz <= scaledColumn(30); ++bz)
            for (int j = -8; j <= -2; ++j)
            {
                if (g.get(bx, j, bz) != CAVE_ROCK)
                    continue;
                const bool open = !g.solid(bx + 1, j, bz) || !g.solid(bx - 1, j, bz) ||
                                  !g.solid(bx, j, bz + 1) || !g.solid(bx, j, bz - 1) ||
                                  !g.solid(bx, j - 1, bz);
                if (open && hash(bx * 31 + j, bz, 311u) % 26u == 0)
                    g.put(bx, j, bz, CRYSTAL);
            }
}

// Everything that grows: on every surface under water, picked by what it
// stands on and how much light gets there, and never where the ride runs.
void decorate(Grid &g, const Clearance &clear)
{
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float cx = centerOf(bx);
            const float cz = centerOf(bz);
            if (WorldShape::inland(cx, cz) > 0.0f)
                continue;
            const float fx = cx / kScale, fz = cz / kScale; // the first layout's units
            const bool kelpForest = fx > 135.0f && fx < 265.0f && fz > 40.0f && fz < 185.0f;
            const bool reef = inReef(cx, cz);
            float meadow = (valueNoise(cx * 0.03f, cz * 0.03f, 27u) - 0.5f) / 0.3f;
            meadow = fmaxf(0.0f, fminf(1.0f, meadow));

            for (int j = kMinY; j <= -3; ++j)
            {
                const int ground = g.get(bx, j, bz);
                const int above = j + 1;
                if (ground == AIR || ground == ISLAND || ground == LOG || ground == LANTERN ||
                    ground == GOLD || ground == CRYSTAL || ground == MARBLE || ground == LAPIS)
                    continue;
                if (g.solid(bx, above, bz) || clear.blocks(bx, above, bz))
                    continue;
                const size_t at = Grid::index(bx, above, bz);
                if (g.plant[at] != NO_PLANT)
                    continue;
                const unsigned int h = hash(bx * 131 + j * 17, bz, 305u);
                const float r = static_cast<float>(h % 1000u) / 1000.0f;
                const bool rock = ground == STONE || ground == CAVE_ROCK;

                int plant = NO_PLANT;
                if (g.sky[at] < 9)
                {
                    // Caves and deep overhangs: only things that glow.
                    if (rock)
                        plant = r < 0.05f ? CRYSTAL_SPIKE : (r < 0.12f ? GLOW_BUD : NO_PLANT);
                }
                else if (isCoral(ground))
                    plant = r < 0.15f   ? FAN_CORAL
                            : r < 0.29f ? FAN_CORAL_GOLD
                            : r < 0.42f ? FAN_CORAL_VIOLET
                            : r < 0.54f ? TUBE_CORAL
                            : r < 0.66f ? ANEMONE
                                        : NO_PLANT;
                else if (kelpForest && (rock || ground == SAND) && r < 0.36f)
                    plant = KELP;
                else if (ground == SAND)
                {
                    const float grass = 0.04f + 0.42f * meadow;
                    plant = r < grass            ? SEAGRASS
                            : r < grass + 0.012f ? STARFISH
                            : r < grass + 0.030f ? TUBE_CORAL
                            : r < grass + 0.044f ? GLOW_BUD
                                                 : NO_PLANT;
                    if (plant == NO_PLANT && reef && r < grass + 0.16f)
                        plant = (h >> 12) % 3u == 0   ? FAN_CORAL
                                : (h >> 12) % 3u == 1 ? FAN_CORAL_GOLD
                                                      : ANEMONE;
                }
                else if (rock)
                {
                    const float scale = reef ? 2.0f : 1.0f;
                    plant = r < 0.05f * scale   ? FAN_CORAL_VIOLET
                            : r < 0.10f * scale ? ANEMONE
                            : r < 0.15f * scale ? TUBE_CORAL
                            : r < 0.19f * scale ? FAN_CORAL_GOLD
                            : r < 0.44f         ? (r < 0.40f ? NO_PLANT : GLOW_BUD)
                            : r < 0.52f         ? SEAGRASS
                                                : NO_PLANT;
                }
                else if (ground == BRICK || ground == PILLAR)
                    plant = r < 0.07f   ? SEAGRASS
                            : r < 0.11f ? GLOW_BUD
                            : r < 0.14f ? CRYSTAL_SPIKE
                            : r < 0.17f ? ANEMONE
                                        : NO_PLANT;
                else if (ground == PLANKS)
                    plant = r < 0.06f ? SEAGRASS
                                      : (r < 0.10f ? ANEMONE : (r < 0.13f ? GLOW_BUD : NO_PLANT));

                if (plant == KELP)
                {
                    const int height = 3 + static_cast<int>((h >> 10) % 5u);
                    for (int k = 0; k < height; ++k)
                    {
                        const int cell = above + k;
                        if (cell > kSceneryTop || g.solid(bx, cell, bz) ||
                            clear.blocks(bx, cell, bz))
                            break;
                        g.plant[Grid::index(bx, cell, bz)] = KELP;
                    }
                }
                else if (plant != NO_PLANT)
                    g.plant[at] = static_cast<unsigned char>(plant);
            }
        }
}

// Minecraft-style light, part 1: sky light pours straight down open columns
// and spreads sideways losing one level per block.
struct Cell
{
    int bx, j, bz;
};
const int kDirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

void flood(const Grid &g, std::vector<unsigned char> &light, std::deque<Cell> &queue)
{
    while (!queue.empty())
    {
        const Cell c = queue.front();
        queue.pop_front();
        const int level = light[Grid::index(c.bx, c.j, c.bz)] - 1;
        if (level <= 0)
            continue;
        for (const int *d : kDirs)
        {
            const int x = c.bx + d[0], j = c.j + d[1], z = c.bz + d[2];
            if (!Grid::inside(x, j, z) || g.solid(x, j, z))
                continue;
            unsigned char &s = light[Grid::index(x, j, z)];
            if (s < level)
            {
                s = static_cast<unsigned char>(level);
                queue.push_back(Cell{x, j, z});
            }
        }
    }
}

void spreadSky(Grid &g)
{
    std::deque<Cell> queue;
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMaxY; j >= kMinY && !g.solid(bx, j, bz); --j)
            {
                g.sky[Grid::index(bx, j, bz)] = 15;
                queue.push_back(Cell{bx, j, bz});
            }
    flood(g, g.sky, queue);
}

// Part 2: crystals, lamps and glowing plants light their surroundings the
// same way.
void spreadGlow(Grid &g)
{
    std::deque<Cell> queue;
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const size_t i = Grid::index(bx, j, bz);
                const int block = g.block[i];
                if (block == CRYSTAL || block == LANTERN)
                {
                    const unsigned char level = block == LANTERN ? 14 : 13;
                    for (const int *d : kDirs)
                    {
                        const int x = bx + d[0], y = j + d[1], z = bz + d[2];
                        if (Grid::inside(x, y, z) && !g.solid(x, y, z) &&
                            g.glow[Grid::index(x, y, z)] < level)
                        {
                            g.glow[Grid::index(x, y, z)] = level;
                            queue.push_back(Cell{x, y, z});
                        }
                    }
                }
                else if (block == AIR)
                {
                    const int plant = g.plant[i];
                    const unsigned char level =
                        plant == CRYSTAL_SPIKE ? 11 : (plant == GLOW_BUD ? 9 : 0);
                    if (level > g.glow[i])
                    {
                        g.glow[i] = level;
                        queue.push_back(Cell{bx, j, bz});
                    }
                }
            }
    flood(g, g.glow, queue);
}

float skyFactor(float level)
{
    return powf(0.8f, 15.0f - level);
}

float glowFactor(float level)
{
    return level <= 0.0f ? 0.0f : powf(0.62f, 13.0f - level);
}

// How strongly a block or plant shines by itself.
float blockGlow(int block)
{
    return block == CRYSTAL ? 1.0f : (block == LANTERN ? 1.3f : (block == GOLD ? 0.18f : 0.0f));
}

float plantGlow(int plant)
{
    switch (plant)
    {
    case CRYSTAL_SPIKE:
        return 1.0f;
    case GLOW_BUD:
        return 0.9f;
    case ANEMONE:
        return 0.14f;
    case TUBE_CORAL:
        return 0.12f;
    case FAN_CORAL:
    case FAN_CORAL_GOLD:
    case FAN_CORAL_VIOLET:
        return 0.07f;
    default:
        return 0.0f;
    }
}

// Builds the whole world into the grid: no OpenGL here, so tests can use it.
void buildWorld(Grid &g)
{
    const std::vector<float> ride = sampleRide();
    Clearance clear;
    clear.add(ride);

    buildTerrain(g);
    buildRocks(g);
    buildReefs(g);
    buildShallowReef(g);
    buildClams(g);
    buildRuins(g);
    buildWreck(g);
    for (float u : kDiveGates)
        buildGate(g, u, false);
    for (float u : kExitGates)
        buildGate(g, u, false);
    buildGate(g, kPortalGate, true);
    carveRide(g, ride);
    embedCrystals(g);
    spreadSky(g);
    decorate(g, clear);
    spreadGlow(g);
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
            v.extra[0] = blockGlow(b);
            v.extra[1] = 0.0f;
            v.extra[2] = glowFactor(glow / count);
            v.extra[3] = skyFactor(sky / count) * ao[open];
            const int texture = (b == GRASS && f.normal[1] == 1) ? GRASS_TOP : b;
            batches[static_cast<size_t>(texture)].push_back(v);
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
    v.extra[0] = plantGlow(plant);
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
    else if (plant == ANEMONE)
        swayTop = 0.08f;
    else if (plant == FAN_CORAL || plant == FAN_CORAL_GOLD || plant == FAN_CORAL_VIOLET)
        swayTop = 0.04f;

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
    const float outer = 3500.0f;
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
// Boxes
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

// ---------------------------------------------------------------------
// Sea life. Everything is built from boxes and animated by moving them.
// All the models face +z.
// ---------------------------------------------------------------------
// Extra lighting data for what follows: glow, no sway, no crystal light,
// full sky light.
void setGlow(float glow)
{
    glMultiTexCoord4f(GL_TEXTURE1, glow, 0.0f, 0.0f, 1.0f);
}

// The patch of water a creature keeps to: it wanders about inside the
// ellipse (radiusX, radiusZ) around (x, y, z) and never leaves it. `speed`
// sets how briskly, `phase` makes each creature's path its own.
struct Lap
{
    float x, y, z, radiusX, radiusZ, speed, phase, scale;
};

// A smooth path that keeps within -1..1 on each axis and takes a long time
// to repeat: a few sine waves whose periods do not line up. With `nimble`
// a quicker, smaller wave is added, for small fish that dart about.
void wanderAt(float a, float phase, bool nimble, float out[3])
{
    const float p = phase * 7.31f;
    float x = 0.62f * sinf(a + p) + 0.38f * sinf(a * 2.31f + p * 1.7f + 1.3f);
    float z = 0.62f * cosf(a * 0.83f + p * 0.6f) + 0.38f * sinf(a * 1.87f + p * 2.3f + 4.1f);
    float y = 0.6f * sinf(a * 1.31f + p * 1.1f + 2.0f) + 0.4f * sinf(a * 2.9f + p * 0.7f);
    if (nimble)
    {
        x = 0.85f * x + 0.15f * sinf(a * 4.13f + p * 3.1f);
        z = 0.85f * z + 0.15f * cosf(a * 3.7f + p * 2.9f);
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// Moves to where the creature is now, nose pointing the way it is swimming
// (tilting up and down a little as it climbs and dives). `offset` moves the
// whole patch, which is how a school drifts about together.
void enterLap(const Lap &lap, float time, float bob, bool nimble = false,
              const float *offset = nullptr)
{
    // laps are given in the first layout's units: scaled out, at the same
    // speed through the water
    const float a = time * fabsf(lap.speed) / kScale + lap.phase;
    float here[3], ahead[3];
    wanderAt(a, lap.phase, nimble, here);
    wanderAt(a + 0.05f, lap.phase, nimble, ahead);
    const float scaleAxis[3] = {lap.radiusX * kScale, bob, lap.radiusZ * kScale};
    float position[3], heading[3];
    for (int k = 0; k < 3; ++k)
    {
        position[k] = here[k] * scaleAxis[k];
        heading[k] = (ahead[k] - here[k]) * scaleAxis[k];
    }
    const float flat = sqrtf(heading[0] * heading[0] + heading[2] * heading[2]);
    const float turn = flat > 1.0e-5f ? atan2f(heading[0], heading[2]) : 0.0f;
    float pitch = flat > 1.0e-5f ? atan2f(heading[1], flat) : 0.0f;
    pitch = fmaxf(-0.6f, fminf(0.6f, pitch));
    glPushMatrix();
    glTranslatef(lap.x * kScale + position[0] + (offset ? offset[0] : 0.0f),
                 lap.y + position[1] + (offset ? offset[1] : 0.0f),
                 lap.z * kScale + position[2] + (offset ? offset[2] : 0.0f));
    glRotatef(turn * 57.29578f, 0.0f, 1.0f, 0.0f);
    glRotatef(-pitch * 57.29578f, 1.0f, 0.0f, 0.0f);
    glScalef(lap.scale, lap.scale, lap.scale);
}

void drawFishShape(const unsigned char body[3], const unsigned char fin[3], float wag)
{
    glColor3ubv(body);
    fishBox(-0.22f, -0.35f, -0.55f, 0.22f, 0.35f, 0.6f);
    fishBox(-0.16f, -0.24f, 0.6f, 0.16f, 0.2f, 0.85f);
    // a black dot of an eye on each side of the head
    glColor3ub(16, 16, 20);
    fishBox(-0.235f, 0.06f, 0.34f, 0.235f, 0.2f, 0.48f);
    glColor3ubv(fin);
    fishBox(-0.24f, -0.37f, -0.05f, 0.24f, 0.37f, 0.15f); // stripe
    fishBox(-0.05f, 0.35f, -0.35f, 0.05f, 0.6f, 0.2f);    // dorsal fin
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -0.55f);
    glRotatef(wag, 0.0f, 1.0f, 0.0f);
    fishBox(-0.06f, -0.38f, -0.5f, 0.06f, 0.38f, 0.0f);
    glPopMatrix();
}

void drawShark(float time)
{
    glColor3ub(92, 108, 128);
    fishBox(-0.7f, -0.6f, -2.0f, 0.7f, 0.7f, 2.2f);
    fishBox(-0.5f, -0.45f, 2.2f, 0.5f, 0.4f, 3.6f);
    glColor3ub(16, 16, 20);
    fishBox(-0.52f, 0.02f, 2.75f, 0.52f, 0.2f, 2.95f); // eyes
    glColor3ub(214, 220, 226);
    fishBox(-0.6f, -0.75f, -1.6f, 0.6f, -0.6f, 3.2f); // pale belly
    glColor3ub(74, 88, 108);
    fishBox(-0.08f, 0.7f, -0.6f, 0.08f, 1.7f, 0.5f); // dorsal fin
    fishBox(0.7f, -0.5f, 0.2f, 2.0f, -0.35f, 1.1f);  // pectoral fins
    fishBox(-2.0f, -0.5f, 0.2f, -0.7f, -0.35f, 1.1f);
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, -2.0f);
    glRotatef(sinf(time * 2.4f) * 18.0f, 0.0f, 1.0f, 0.0f);
    fishBox(-0.45f, -0.4f, -1.8f, 0.45f, 0.45f, 0.0f);
    glTranslatef(0.0f, 0.0f, -1.8f);
    glRotatef(sinf(time * 2.4f - 0.9f) * 22.0f, 0.0f, 1.0f, 0.0f);
    fishBox(-0.08f, -1.1f, -1.1f, 0.08f, 1.5f, 0.0f); // tail fin
    glPopMatrix();
}

void drawTurtle(float time)
{
    glColor3ub(58, 112, 70);
    fishBox(-1.1f, -0.35f, -1.4f, 1.1f, 0.35f, 1.4f);
    glColor3ub(96, 150, 84);
    fishBox(-0.8f, 0.35f, -1.05f, 0.8f, 0.6f, 1.05f); // top of the shell
    glColor3ub(206, 196, 140);
    fishBox(-0.95f, -0.48f, -1.2f, 0.95f, -0.35f, 1.2f); // plastron
    glColor3ub(120, 160, 96);
    fishBox(-0.35f, -0.2f, 1.4f, 0.35f, 0.3f, 2.1f); // head
    glColor3ub(16, 16, 20);
    fishBox(-0.37f, 0.08f, 1.8f, 0.37f, 0.22f, 1.94f); // eyes
    glColor3ub(120, 160, 96);
    fishBox(-0.75f, -0.1f, -1.9f, -0.3f, 0.05f, -1.3f);
    fishBox(0.3f, -0.1f, -1.9f, 0.75f, 0.05f, -1.3f);
    const float flap = sinf(time * 2.0f) * 26.0f;
    for (int side = -1; side <= 1; side += 2)
    {
        glPushMatrix();
        glTranslatef(1.1f * static_cast<float>(side), 0.0f, 0.8f);
        glRotatef(flap * static_cast<float>(side), 0.0f, 0.0f, 1.0f);
        if (side > 0)
            fishBox(0.0f, -0.08f, -0.35f, 1.6f, 0.08f, 0.35f);
        else
            fishBox(-1.6f, -0.08f, -0.35f, 0.0f, 0.08f, 0.35f);
        glPopMatrix();
    }
}

void drawRay(float time)
{
    glColor3ub(52, 68, 96);
    fishBox(-1.2f, -0.1f, -1.2f, 1.2f, 0.25f, 1.4f);
    fishBox(-0.08f, -0.03f, -4.2f, 0.08f, 0.07f, -1.2f); // tail
    fishBox(-0.9f, -0.05f, 1.4f, -0.5f, 0.15f, 2.0f);    // head fins
    fishBox(0.5f, -0.05f, 1.4f, 0.9f, 0.15f, 2.0f);
    glColor3ub(16, 16, 20);
    fishBox(-1.22f, 0.08f, 1.0f, 1.22f, 0.2f, 1.14f); // eyes
    glColor3ub(214, 222, 230);
    fishBox(-1.1f, -0.2f, -1.0f, 1.1f, -0.1f, 1.2f); // white belly
    for (int side = -1; side <= 1; side += 2)
    {
        const float s = static_cast<float>(side);
        glColor3ub(52, 68, 96);
        glPushMatrix();
        glTranslatef(1.2f * s, 0.05f, 0.0f);
        glRotatef(sinf(time * 1.6f) * 22.0f * s, 0.0f, 0.0f, 1.0f);
        if (side > 0)
            fishBox(0.0f, -0.1f, -1.0f, 1.8f, 0.12f, 1.1f);
        else
            fishBox(-1.8f, -0.1f, -1.0f, 0.0f, 0.12f, 1.1f);
        glTranslatef(1.8f * s, 0.0f, 0.0f);
        glRotatef(sinf(time * 1.6f - 0.8f) * 28.0f * s, 0.0f, 0.0f, 1.0f);
        glColor3ub(44, 58, 84);
        if (side > 0)
            fishBox(0.0f, -0.07f, -0.6f, 1.5f, 0.09f, 0.7f);
        else
            fishBox(-1.5f, -0.07f, -0.6f, 0.0f, 0.09f, 0.7f);
        glPopMatrix();
    }
}

void drawWhale(float time)
{
    glColor3ub(58, 84, 120);
    fishBox(-2.0f, -1.6f, -5.0f, 2.0f, 2.0f, 5.0f);
    fishBox(-1.8f, -1.2f, 5.0f, 1.8f, 1.6f, 8.6f);
    glColor3ub(16, 16, 20);
    fishBox(-1.83f, -0.5f, 6.0f, 1.83f, -0.1f, 6.4f); // eyes
    glColor3ub(58, 84, 120);
    fishBox(-0.25f, 2.0f, -2.6f, 0.25f, 2.7f, -1.4f); // dorsal fin
    glColor3ub(204, 214, 224);
    fishBox(-1.8f, -2.1f, -4.0f, 1.8f, -1.6f, 8.2f); // throat and belly
    glColor3ub(46, 68, 100);
    fishBox(2.0f, -1.5f, 1.0f, 4.8f, -1.2f, 3.2f); // flippers
    fishBox(-4.8f, -1.5f, 1.0f, -2.0f, -1.2f, 3.2f);
    const float beat = sinf(time * 0.9f);
    glPushMatrix();
    glTranslatef(0.0f, 0.2f, -5.0f);
    glRotatef(beat * 8.0f, 1.0f, 0.0f, 0.0f);
    glColor3ub(58, 84, 120);
    fishBox(-1.4f, -1.2f, -4.5f, 1.4f, 1.3f, 0.0f);
    glTranslatef(0.0f, 0.0f, -4.5f);
    glRotatef(sinf(time * 0.9f - 0.7f) * 12.0f, 1.0f, 0.0f, 0.0f);
    fishBox(-0.8f, -0.6f, -3.4f, 0.8f, 0.7f, 0.0f);
    glColor3ub(46, 68, 100);
    fishBox(-3.2f, -0.2f, -4.8f, 3.2f, 0.2f, -3.2f); // fluke
    glPopMatrix();
}

void drawJellyfish(float time, float seed, const unsigned char colour[3])
{
    const float pulse = sinf(time * 1.8f + seed * 9.0f);
    glColor3ubv(colour);
    glPushMatrix();
    glScalef(1.0f + 0.13f * pulse, 1.0f - 0.1f * pulse, 1.0f + 0.13f * pulse);
    fishBox(-0.8f, 0.0f, -0.8f, 0.8f, 0.9f, 0.8f);
    fishBox(-0.55f, 0.9f, -0.55f, 0.55f, 1.25f, 0.55f);
    glPopMatrix();
    for (int k = 0; k < 4; ++k)
    {
        const float x = (k % 2 == 0 ? -0.45f : 0.45f);
        const float z = (k / 2 == 0 ? -0.45f : 0.45f);
        const float sway = sinf(time * 1.3f + seed * 5.0f + static_cast<float>(k)) * 0.18f;
        fishBox(x - 0.07f, -1.2f, z - 0.07f, x + 0.07f, 0.0f, z + 0.07f);
        fishBox(x - 0.06f + sway, -2.4f, z - 0.06f, x + 0.06f + sway, -1.2f, z + 0.06f);
    }
}

} // namespace

OceanWorld::OceanWorld() : m_built(false), m_farList(0)
{
}

float OceanWorld::seaFloor(float x, float z)
{
    const int bx = blockOf(x), bz = blockOf(z);
    const float cx = centerOf(bx), cz = centerOf(bz);
    const float edge = -WorldShape::inland(cx, cz);
    if (edge < 0.0f)
        return 0.0f; // under the island
    return static_cast<float>(seaBedTop(cx, cz, edge) + 1) * kBlock;
}

float OceanWorld::waterLevel()
{
    return kWaterLevel;
}

void OceanWorld::releaseResources()
{
    m_built = false;
    m_chunks.clear();
    m_farList = 0;
    m_textures.clear();
}

std::vector<float> OceanWorld::demoTrack()
{
    std::vector<float> xyz;
    for (int i = 0; i < kDemoCount; ++i)
        for (int k = 0; k < 3; ++k)
            xyz.push_back(trackPoint(i, k));
    return xyz;
}

std::vector<float> OceanWorld::rideSamples()
{
    return sampleRide();
}

int OceanWorld::landStart()
{
    return kLandStart;
}

void OceanWorld::shadowBounds(float lo[3], float hi[3])
{
    lo[0] = -275.0f * kScale;
    lo[1] = -45.0f;
    lo[2] = -200.0f * kScale;
    hi[0] = 270.0f * kScale;
    hi[1] = 45.0f;
    hi[2] = 285.0f * kScale;
}

const std::vector<float> &OceanWorld::bubbleSources()
{
    static const float sources[][3] = {
        {-98.0f, -38.0f, 228.0f},  {-84.0f, -38.0f, 224.0f},   {190.0f, -37.0f, -30.0f},
        {222.0f, -38.0f, 40.0f},   {160.0f, -24.0f, 30.0f},    {20.0f, -38.0f, 172.0f},
        {60.0f, -38.0f, 206.0f},   {22.0f, -18.0f, 252.0f},    {-190.0f, -36.0f, 150.0f},
        {-200.0f, -28.0f, -70.0f}, {-150.0f, -22.0f, -150.0f}, {245.0f, -38.0f, 110.0f},
        {-60.0f, -38.0f, 176.0f},  {-100.0f, -38.0f, 204.0f}};
    // in the first layout's units: scaled out, the depths kept
    static std::vector<float> list;
    if (list.empty())
        for (const float *p : sources)
        {
            list.push_back(p[0] * kScale);
            list.push_back(p[1]);
            list.push_back(p[2] * kScale);
        }
    return list;
}

int OceanWorld::rideObstructions(const std::vector<float> &path)
{
    Grid g;
    buildWorld(g);

    // The room the train takes: 3 units either side of the centre line, from
    // just under the sleepers to above the rider's head. Plants are thinner
    // than their cell but sway, and may not come within reach of a sleeper.
    const float trainReach = 3.0f;
    const float plantReach = 2.2f;
    const float plantInset = 0.6f - 2.0f;
    int count = 0;
    for (size_t i = 0; i + 2 < path.size(); i += 3)
    {
        const float x = path[i], y = path[i + 1], z = path[i + 2];
        bool hit = false;
        for (int bx = blockOf(x - 6.0f); bx <= blockOf(x + 6.0f) && !hit; ++bx)
            for (int bz = blockOf(z - 6.0f); bz <= blockOf(z + 6.0f) && !hit; ++bz)
                for (int j = blockOf(y - 0.3f); j <= blockOf(y + 5.0f) && !hit; ++j)
                {
                    if (!Grid::inside(bx, j, bz))
                        continue;
                    const size_t cell = Grid::index(bx, j, bz);
                    const int block = g.block[cell];
                    const bool plant = block == AIR && g.plant[cell] != NO_PLANT;
                    if (block == ISLAND || (block == AIR && !plant))
                        continue;
                    const float inset = plant ? plantInset : 0.0f;
                    const float x0 = static_cast<float>(bx) * kBlock + inset;
                    const float z0 = static_cast<float>(bz) * kBlock + inset;
                    const float nx = fmaxf(x0, fminf(x, x0 + kBlock - 2.0f * inset));
                    const float nz = fmaxf(z0, fminf(z, z0 + kBlock - 2.0f * inset));
                    const float reach = plant ? plantReach : trainReach;
                    if ((x - nx) * (x - nx) + (z - nz) * (z - nz) < reach * reach)
                        hit = true;
                }
        if (hit)
            ++count;
    }
    return count;
}

void OceanWorld::build()
{
    Grid g;
    buildWorld(g);

    m_textures.assign(kTextureCount, 0);
    const Image images[kTextureCount] = {
        PixelArt::blank(), // AIR (unused)
        PixelArt::blank(), // ISLAND (never drawn)
        // the blocks share the island's 32 x 32 style
        LandTextures::sand(226, 216, 180, 21u), LandTextures::stone(122, 127, 134, 31u),
        LandTextures::stone(68, 75, 90, 41u), // cave rock
        LandTextures::stoneBricks(146, 154, 144, true, 51u), LandTextures::pillar(),
        LandTextures::planks(116, 82, 50, true), // wet, nailed
        LandTextures::bark(92, 66, 44, 81u), LandTextures::coral(226, 96, 140, 501u),
        LandTextures::coral(236, 142, 62, 511u), LandTextures::coral(146, 92, 204, 521u),
        LandTextures::coral(232, 204, 70, 531u), LandTextures::coral(214, 62, 58, 541u),
        LandTextures::crystal(), LandTextures::lantern(), LandTextures::gold(),
        LandTextures::marble(), LandTextures::lapis(), LandTextures::make(LandTextures::GRASS_SIDE),
        LandTextures::make(LandTextures::GRASS_TOP),
        PixelArt::blank(), // NO_PLANT
        makeSeagrass(), makeKelp(), makeFanCoral(214, 72, 104), makeFanCoral(226, 170, 52),
        makeFanCoral(150, 96, 214), makeTubeCoral(), makeAnemone(), makeGlowBud(),
        makeCrystalSpike(), makeStarfish()};
    for (int t = 0; t < kTextureCount; ++t)
        if (t != AIR && t != ISLAND && t != plantTexture(NO_PLANT))
            m_textures[static_cast<size_t>(t)] =
                PixelArt::upload(images[t], t == plantTexture(KELP) || t == SAND);

    // In chunks of 16 x 16 columns, each with the box round its blocks, so
    // only the chunks in view are drawn.
    const int chunkSize = 16;
    for (int cx = -kHalf; cx < kHalf; cx += chunkSize)
        for (int cz = -kHalf; cz < kHalf; cz += chunkSize)
        {
            Batches solids(kTextureCount);
            Batches plants(kTextureCount);
            int low = kMaxY + 1, high = kMinY - 1;
            for (int bx = cx; bx < cx + chunkSize && bx < kHalf; ++bx)
                for (int bz = cz; bz < cz + chunkSize && bz < kHalf; ++bz)
                    for (int j = kMinY; j <= kMaxY; ++j)
                    {
                        const int b = g.get(bx, j, bz);
                        const bool plant = b == AIR && g.plant[Grid::index(bx, j, bz)] != NO_PLANT;
                        if ((b == AIR || b == ISLAND) && !plant)
                            continue;
                        low = j < low ? j : low;
                        high = j > high ? j : high;
                        if (plant)
                            addPlant(g, bx, j, bz, plants);
                        else
                            addBlockFaces(g, bx, j, bz, solids);
                    }
            if (high < low)
                continue;
            Chunk chunk;
            chunk.lo[0] = static_cast<float>(cx) * kBlock;
            chunk.lo[1] = static_cast<float>(low) * kBlock;
            chunk.lo[2] = static_cast<float>(cz) * kBlock;
            chunk.hi[0] = static_cast<float>(cx + chunkSize) * kBlock;
            chunk.hi[1] = static_cast<float>(high + 1) * kBlock + 2.0f; // kelp sways
            chunk.hi[2] = static_cast<float>(cz + chunkSize) * kBlock;
            chunk.solid = compileList(solids, m_textures);
            chunk.plant = compileList(plants, m_textures);
            m_chunks.push_back(chunk);
        }
    Batches beyond(kTextureCount);
    addFarFloor(beyond);
    m_farList = compileList(beyond, m_textures);
    m_built = true;
}

void OceanWorld::draw(bool shadowPass, float time)
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    if (!m_built)
        build();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    const Frustum view = Frustum::current();
    glCallList(m_farList);
    for (const Chunk &c : m_chunks)
        if (view.sees(c.lo, c.hi))
            glCallList(c.solid);
    if (!shadowPass)
    {
        for (const Chunk &c : m_chunks)
            if (view.sees(c.lo, c.hi))
                glCallList(c.plant);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        drawSeaLife(time);
    }
    glPopAttrib();
}

void OceanWorld::drawSeaLife(float time)
{
    setGlow(0.0f);

    // Schools that travel along the ride, a little above the train, the way
    // fish follow a reef edge: the tube carved around the ride is always
    // open water, so they can go its whole underwater length. The fish of a
    // school keep a loose formation and all head the same way; half of the
    // schools go with the ride and half against it. At either end, up by the
    // surface, a school shrinks away and starts again at the other end.
    struct Runner
    {
        float start;     // where along the stretch it is at time 0, 0..1
        float direction; // +1 with the ride, -1 against it
        unsigned char body[3], fin[3];
    };
    const Runner runners[] = {{0.02f, 1.0f, {250, 200, 60}, {40, 60, 120}},
                              {0.18f, -1.0f, {230, 120, 50}, {250, 250, 250}},
                              {0.34f, 1.0f, {90, 150, 230}, {250, 220, 80}},
                              {0.50f, -1.0f, {240, 90, 120}, {250, 240, 200}},
                              {0.66f, 1.0f, {60, 210, 200}, {30, 90, 110}},
                              {0.82f, -1.0f, {250, 170, 60}, {60, 60, 70}},
                              {0.10f, -1.0f, {190, 120, 240}, {250, 230, 120}},
                              {0.58f, 1.0f, {250, 220, 90}, {230, 90, 60}}};
    const float stretchFrom = 3.3f;
    const float stretchLength = 17.0f; // in control points: down to the exit gates
    const float runnerSpeed = 0.11f;   // control points a second, about 7 units
    int school = 0;
    for (const Runner &runner : runners)
    {
        const float schoolSeed = static_cast<float>(school) * 1.9f;
        for (int i = 0; i < 12; ++i)
        {
            const float fi = static_cast<float>(i);
            // The fish's own place in the formation, shifting slowly.
            const float along =
                (static_cast<float>(hash(i, school, 621u) % 100u) * 0.01f - 0.5f) * 0.16f;
            const float lateral =
                (static_cast<float>(hash(i, school, 622u) % 100u) * 0.01f - 0.5f) * 3.4f +
                sinf(time * 0.7f + fi * 1.3f) * 0.35f + runner.direction * 2.0f;
            const float height =
                5.7f + (static_cast<float>(hash(i, school, 623u) % 100u) * 0.01f - 0.5f) * 1.8f +
                sinf(time * 0.9f + fi * 2.1f + schoolSeed) * 0.25f;
            float travelled = runner.start + runner.direction * time * runnerSpeed / stretchLength +
                              along / stretchLength;
            travelled -= floorf(travelled);
            // small near either end, full size in between
            const float edge = fminf(travelled, 1.0f - travelled) / 0.02f;
            const float size = 0.85f * fmaxf(0.0f, fminf(1.0f, edge));
            if (size < 0.02f)
                continue;
            const float u = stretchFrom + travelled * stretchLength;
            float here[3], forward[3], left[3];
            ridePoint(u, 2, here);
            rideFrame(u, forward, left);
            const float heading =
                atan2f(forward[0] * runner.direction, forward[2] * runner.direction);
            const float pitch = asinf(fmaxf(-0.6f, fminf(0.6f, forward[1] * runner.direction)));
            glPushMatrix();
            glTranslatef(here[0] + left[0] * lateral, here[1] + height,
                         here[2] + left[2] * lateral);
            glRotatef(heading * 57.29578f, 0.0f, 1.0f, 0.0f);
            glRotatef(-pitch * 57.29578f, 1.0f, 0.0f, 0.0f);
            glScalef(size, size, size);
            drawFishShape(runner.body, runner.fin, sinf(time * 8.0f + fi * 3.1f) * 25.0f);
            glPopMatrix();
        }
        ++school;
    }

    // Big schools high in the open water, seen against the bright surface.
    // Each roams a long stretch of deep water above everything on the sea
    // bed; its fish keep formation and face the way the school is going.
    struct Swarm
    {
        float x, y, z, radiusX, radiusZ, speed;
        int count;
        unsigned char body[3], fin[3];
    };
    const Swarm swarms[] = {
        {50.0f, -9.5f, 238.0f, 150.0f, 38.0f, 0.06f, 48, {150, 190, 230}, {70, 100, 150}},
        {246.0f, -9.5f, 70.0f, 36.0f, 110.0f, 0.07f, 40, {250, 210, 90}, {230, 120, 40}},
        {120.0f, -8.5f, 250.0f, 110.0f, 30.0f, 0.08f, 36, {240, 130, 150}, {250, 240, 220}},
        {-30.0f, -9.0f, 262.0f, 90.0f, 22.0f, 0.07f, 32, {200, 230, 240}, {90, 140, 170}}};
    for (const Swarm &swarm : swarms)
    {
        const float schoolSeed = static_cast<float>(school) * 1.9f;
        // in the first layout's units, like the laps
        const float a = time * swarm.speed / kScale + schoolSeed;
        float here[3], ahead[3];
        wanderAt(a, schoolSeed, false, here);
        wanderAt(a + 0.02f, schoolSeed, false, ahead);
        const float dx = (ahead[0] - here[0]) * swarm.radiusX * kScale;
        const float dz = (ahead[2] - here[2]) * swarm.radiusZ * kScale;
        const float flat = sqrtf(dx * dx + dz * dz);
        const float fx = flat > 1.0e-5f ? dx / flat : 0.0f; // the way the school is going
        const float fz = flat > 1.0e-5f ? dz / flat : 1.0f;
        const float heading = atan2f(fx, fz);
        for (int i = 0; i < swarm.count; ++i)
        {
            const float fi = static_cast<float>(i);
            // a long, loose formation: further along than across
            const float along =
                (static_cast<float>(hash(i, school, 631u) % 100u) * 0.01f - 0.5f) * 22.0f +
                sinf(time * 0.5f + fi * 1.9f) * 0.8f;
            const float across =
                (static_cast<float>(hash(i, school, 632u) % 100u) * 0.01f - 0.5f) * 9.0f +
                sinf(time * 0.6f + fi * 1.1f) * 0.5f;
            const float height =
                (static_cast<float>(hash(i, school, 633u) % 100u) * 0.01f - 0.5f) * 3.6f +
                sinf(time * 0.8f + fi * 2.7f) * 0.3f;
            glPushMatrix();
            glTranslatef((swarm.x + here[0] * swarm.radiusX) * kScale + fx * along + fz * across,
                         swarm.y + here[1] * 0.8f + height,
                         (swarm.z + here[2] * swarm.radiusZ) * kScale + fz * along - fx * across);
            glRotatef((heading + sinf(time * 1.1f + fi * 1.7f) * 0.12f) * 57.29578f, 0.0f, 1.0f,
                      0.0f);
            drawFishShape(swarm.body, swarm.fin, sinf(time * 7.0f + fi * 2.3f) * 22.0f);
            glPopMatrix();
        }
        ++school;
    }

    // Everything large keeps above the scenery limit (y = -15) and roams its
    // own patch of open water.
    const Lap sharks[] = {{238.0f, -10.5f, 60.0f, 40.0f, 110.0f, 0.075f, 0.0f, 1.4f},
                          {244.0f, -9.0f, 90.0f, 36.0f, 100.0f, 0.06f, 3.3f, 1.2f},
                          {40.0f, -10.0f, 246.0f, 150.0f, 30.0f, 0.055f, 1.2f, 1.5f}};
    for (const Lap &lap : sharks)
    {
        enterLap(lap, time, 0.9f);
        drawShark(time + lap.phase);
        glPopMatrix();
    }

    const Lap turtles[] = {{196.0f, -12.0f, -6.0f, 40.0f, 46.0f, 0.07f, 0.5f, 1.5f},
                           {-66.0f, -11.0f, 176.0f, 55.0f, 17.0f, 0.06f, 2.0f, 1.3f},
                           {-185.0f, -10.0f, -165.0f, 16.0f, 16.0f, 0.16f, 4.0f, 1.4f}};
    for (const Lap &lap : turtles)
    {
        enterLap(lap, time, 1.0f);
        drawTurtle(time + lap.phase);
        glPopMatrix();
    }

    const Lap rays[] = {{120.0f, -9.5f, 232.0f, 115.0f, 38.0f, 0.05f, 0.0f, 2.2f},
                        {-40.0f, -8.5f, 256.0f, 85.0f, 26.0f, 0.055f, 2.6f, 2.0f}};
    for (const Lap &lap : rays)
    {
        enterLap(lap, time, 0.6f);
        drawRay(time + lap.phase);
        glPopMatrix();
    }

    const Lap whale = {214.0f, -7.5f, 112.0f, 58.0f, 118.0f, 0.03f, 0.4f, 2.2f};
    enterLap(whale, time, 0.4f);
    drawWhale(time);
    glPopMatrix();

    // Glowing jellyfish drifting between the ruins and the wreck, kept out
    // of the way of the train.
    const unsigned char jellyColours[3][3] = {{255, 150, 220}, {140, 230, 255}, {190, 150, 255}};
    setGlow(0.75f);
    for (int i = 0; i < 60; ++i)
    {
        const float fx = static_cast<float>(hash(i, 1, 611u) % 1000u) * 0.001f;
        const float fz = static_cast<float>(hash(i, 2, 611u) % 1000u) * 0.001f;
        const float fy = static_cast<float>(hash(i, 3, 611u) % 1000u) * 0.001f;
        const float seed = static_cast<float>(i) * 0.37f;
        // the first layout's region, scaled out
        const float x = (-122.0f + 88.0f * fx) * kScale;
        float z = (168.0f + 40.0f * fz) * kScale;
        const float y = -30.0f + 17.0f * fy;
        // The ride runs through here at about z = 470 - 490, y = -34.
        const float rideZ = (188.0f + (-12.0f - x / kScale) * (8.0f / 120.0f)) * kScale;
        if (y < -21.0f && fabsf(z - rideZ) < 8.0f)
            z = rideZ + (z < rideZ ? -9.0f : 9.0f);
        glPushMatrix();
        glTranslatef(x + sinf(time * 0.21f + seed * 4.0f) * 1.2f,
                     y + sinf(time * 0.5f + seed * 7.0f) * 1.3f,
                     z + cosf(time * 0.17f + seed * 3.0f) * 1.2f);
        const float size = 1.0f + 0.6f * fy;
        glScalef(size, size, size);
        drawJellyfish(time, seed, jellyColours[i % 3]);
        glPopMatrix();
    }
    setGlow(0.0f);
}
