/************************************************************************
     File:        LandWorld.cpp

     Comment:     See LandWorld.h.

*************************************************************************/

#include <math.h>
#include <deque>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include "LandWorld.h"
#include "LandTextures.h"
#include "OceanWorld.h"
#include "PixelArt.h"
#include "WaterFlow.h"

namespace
{

using PixelArt::hash;

// ---------------------------------------------------------------------
// World layout
// ---------------------------------------------------------------------
const float kBlock = 2.5f;
const float kIslandHalf = 100.0f;
// The highest layer of blocks that touches the sea (the sea is at y = -1;
// layer 0 spans 0 to 2.5): water in it is coloured like the sea.
const int kSeaLayer = 0;
// Blocks each side of the origin: the island is +-100 units (40 blocks), and
// one more ring of open air round it, over the sea, for water running off
// the island to fall through.
const int kHalf = 41;
const int kSizeXZ = 2 * kHalf;
const int kMinY = -2; // block layer j spans y = 2.5 j .. 2.5 j + 2.5
const int kMaxY = 47;
const int kSizeY = kMaxY - kMinY + 1;

// Room kept free around the ride. Blocks are carved out of a tube around
// it; LandWorld::rideObstructions() checks the result against the real
// track.
const float kCarveRadius = 5.6f;
const float kCarveBelow = 1.9f;
const float kCarveAbove = 8.5f;
// What the train itself needs (see rideObstructions()), with a margin:
// nothing is built or planted inside this.
const float kTrainReach = 3.5f;
const float kTrainBelow = 0.5f;
const float kTrainAbove = 5.6f;

enum Block
{
    AIR = 0,
    GRASS,
    DIRT,
    STONE,
    COBBLE,
    MOSSY,
    SAND,
    GRAVEL,
    SNOW,
    SNOWY_DIRT, // snow on top, dirt below
    BASALT,
    MAGMA,
    LAVA,
    CLAY_RED,
    CLAY_ORANGE,
    CLAY_YELLOW,
    CLAY_WHITE,
    CLAY_BROWN,
    CLAY_GREY,
    RED_SAND,
    OAK_LOG,
    BIRCH_LOG,
    SPRUCE_LOG,
    LEAVES_OAK,
    LEAVES_BIRCH,
    LEAVES_SPRUCE,
    LEAVES_CHERRY,
    LEAVES_AUTUMN,
    PLANKS,
    ROOF,
    WINDOW,
    DOOR,
    LANTERN,
    CRYSTAL,
    ORE_COAL,
    ORE_IRON,
    ORE_GOLD,
    ORE_DIAMOND,
    CACTUS,
    MUSHROOM_CAP,
    MUSHROOM_STEM,
    HAY,
    FARMLAND,
    WATER,
    BLOCK_COUNT
};

enum Plant
{
    NO_PLANT = 0,
    TALL_GRASS,
    FERN,
    POPPY,
    DANDELION,
    CORNFLOWER,
    TULIP,
    DEAD_BUSH,
    WHEAT,
    PLANT_COUNT
};

enum Biome
{
    MEADOW = 0,
    ALPINE,
    VOLCANO,
    MESA,
    CRAG
};

// The texture of one face of a block: 0 top, 1 bottom, 2 side.
int textureOf(int block, int face)
{
    namespace T = LandTextures;
    switch (block)
    {
    case GRASS:
        return face == 0 ? T::GRASS_TOP : (face == 1 ? T::DIRT : T::GRASS_SIDE);
    case DIRT:
        return T::DIRT;
    case STONE:
        return T::STONE;
    case COBBLE:
        return T::COBBLE;
    case MOSSY:
        return T::MOSSY;
    case SAND:
        return T::SAND;
    case GRAVEL:
        return T::GRAVEL;
    case SNOW:
        return T::SNOW;
    case SNOWY_DIRT:
        return face == 0 ? T::SNOW : (face == 1 ? T::DIRT : T::SNOW_SIDE);
    case BASALT:
        return T::BASALT;
    case MAGMA:
        return T::MAGMA;
    case LAVA:
        return T::LAVA;
    case CLAY_RED:
        return T::CLAY_RED;
    case CLAY_ORANGE:
        return T::CLAY_ORANGE;
    case CLAY_YELLOW:
        return T::CLAY_YELLOW;
    case CLAY_WHITE:
        return T::CLAY_WHITE;
    case CLAY_BROWN:
        return T::CLAY_BROWN;
    case CLAY_GREY:
        return T::CLAY_GREY;
    case RED_SAND:
        return T::RED_SAND;
    case OAK_LOG:
        return face == 2 ? T::OAK_SIDE : T::OAK_TOP;
    case BIRCH_LOG:
        return face == 2 ? T::BIRCH_SIDE : T::BIRCH_TOP;
    case SPRUCE_LOG:
        return face == 2 ? T::SPRUCE_SIDE : T::SPRUCE_TOP;
    case LEAVES_OAK:
        return T::LEAVES_OAK;
    case LEAVES_BIRCH:
        return T::LEAVES_BIRCH;
    case LEAVES_SPRUCE:
        return T::LEAVES_SPRUCE;
    case LEAVES_CHERRY:
        return T::LEAVES_CHERRY;
    case LEAVES_AUTUMN:
        return T::LEAVES_AUTUMN;
    case PLANKS:
        return T::PLANKS;
    case ROOF:
        return T::ROOF;
    case WINDOW:
        return face == 2 ? T::WINDOW : T::PLANKS;
    case DOOR:
        return face == 2 ? T::DOOR : T::PLANKS;
    case LANTERN:
        return T::LANTERN;
    case CRYSTAL:
        return T::CRYSTAL;
    case ORE_COAL:
        return T::ORE_COAL;
    case ORE_IRON:
        return T::ORE_IRON;
    case ORE_GOLD:
        return T::ORE_GOLD;
    case ORE_DIAMOND:
        return T::ORE_DIAMOND;
    case CACTUS:
        return face == 2 ? T::CACTUS_SIDE : T::CACTUS_TOP;
    case MUSHROOM_CAP:
        return T::MUSHROOM_CAP;
    case MUSHROOM_STEM:
        return T::MUSHROOM_STEM;
    case HAY:
        return face == 2 ? T::HAY_SIDE : T::HAY_TOP;
    case FARMLAND:
        return face == 0 ? T::FARMLAND : T::DIRT;
    case WATER:
        return T::WATER;
    default:
        return T::STONE;
    }
}

int plantTexture(int plant)
{
    return LandTextures::TALL_GRASS + (plant - TALL_GRASS);
}

// Trees and the like: the ground is what lies under them.
bool isGrowth(int block)
{
    return (block >= OAK_LOG && block <= LEAVES_AUTUMN) || block == CACTUS ||
           block == MUSHROOM_CAP || block == MUSHROOM_STEM;
}

// How strongly a block shines by itself.
float blockGlow(int block)
{
    switch (block)
    {
    case LAVA:
        return 1.0f;
    case LANTERN:
        return 1.3f;
    case CRYSTAL:
        return 1.0f;
    case WINDOW:
        return 0.5f;
    case MAGMA:
        return 0.22f;
    case ORE_DIAMOND:
        return 0.1f;
    default:
        return 0.0f;
    }
}

// ---------------------------------------------------------------------
// Noise and small helpers
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

// Two octaves, 0..1.
float fbm(float x, float z, unsigned int seed)
{
    return valueNoise(x, z, seed) * 0.67f + valueNoise(x * 2.1f, z * 2.1f, seed + 1u) * 0.33f;
}

// 0 at a, 1 at b, smooth in between; a may be larger than b.
float ramp(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

float centerOf(int b)
{
    return (static_cast<float>(b) + 0.5f) * kBlock;
}

// Is this column part of the island, not the ring of air over the sea?
bool onIsland(int bx, int bz)
{
    return fabsf(centerOf(bx)) < kIslandHalf && fabsf(centerOf(bz)) < kIslandHalf;
}

int blockOf(float v)
{
    return static_cast<int>(floorf(v / kBlock));
}

// A chance of one in n for this column.
bool oneIn(int bx, int bz, unsigned int seed, unsigned int n)
{
    return hash(bx, bz, seed) % n == 0;
}

// Distance from (x, z) to a line of points (x, z pairs).
float distanceToLine(float x, float z, const float *points, int count)
{
    float best = 1.0e9f;
    for (int i = 0; i + 1 < count; ++i)
    {
        const float ax = points[2 * i], az = points[2 * i + 1];
        const float bx = points[2 * i + 2], bz = points[2 * i + 3];
        const float ex = bx - ax, ez = bz - az;
        float t = ((x - ax) * ex + (z - az) * ez) / (ex * ex + ez * ez);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float dx = x - (ax + ex * t), dz = z - (az + ez * t);
        const float d = sqrtf(dx * dx + dz * dz);
        if (d < best)
            best = d;
    }
    return best;
}

// ---------------------------------------------------------------------
// The lie of the land
// ---------------------------------------------------------------------
const float kAlpineX = -56.0f, kAlpineZ = 24.0f, kAlpineRadius = 56.0f, kAlpineHeight = 112.0f;
const float kVolcanoX = 58.0f, kVolcanoZ = 58.0f, kVolcanoRadius = 44.0f, kVolcanoHeight = 66.0f;
const float kCraterRadius = 9.5f;
const int kLavaLakeTop = 18; // the lava lake fills the crater up to this layer (y = 47.5)

// The crags in the north east: x, z, radius, height.
const float kCrags[2][4] = {{48.0f, -72.0f, 27.0f, 54.0f}, {78.0f, -50.0f, 27.0f, 64.0f}};

// The canyon runs from the south shore north into the badlands.
const float kCanyon[] = {8.0f, 112.0f, 9.0f, 84.0f, 14.0f, 64.0f, 20.0f, 44.0f, 19.0f, 27.0f};
const int kCanyonPoints = 5;

// Lava rivers from the crater rim to the shore.
const float kLavaSouth[] = {58.0f, 67.5f, 61.0f, 80.0f, 57.0f, 92.0f, 58.0f, 103.0f};
const float kLavaEast[] = {67.5f, 58.0f, 82.0f, 63.0f, 93.0f, 57.0f, 103.0f, 58.0f};

// The waterfall on the north side of the snow mountain, where no track runs:
// the blocks of the lip (x from kFallX0 to kFallX1, z = kFallLipZ), the
// height it falls and the river from its pool to the north shore (x, z).
const int kFallX0 = -21, kFallX1 = -18, kFallLipZ = 24;
const int kFallDrop = 9;
const float kRiver[] = {-47.0f, 80.0f, -51.0f, 90.0f, -48.0f, 101.0f};

// The village green in the north west, the meadow pond and the island
// that floats above it.
const float kVillageX = -70.0f, kVillageZ = -72.0f, kVillageRadius = 27.0f;
const int kVillageTop = 1; // the green's top layer (y = 5)
const float kPondX = 51.0f, kPondZ = -36.0f, kPondRadius = 6.5f;
const int kFloatX = 16, kFloatZ = -15, kFloatTop = 25; // blocks; top at y = 65

struct Site
{
    float height;
    int biome;
};

Site siteAt(float x, float z)
{
    Site site;
    const float coast = kIslandHalf - fmaxf(fabsf(x), fabsf(z));
    const float inland = ramp(2.0f, 24.0f, coast);
    site.height = 1.0f + inland * 4.2f + (fbm(x * 0.035f, z * 0.035f, 3u) - 0.5f) * 6.0f * inland +
                  (valueNoise(x * 0.09f, z * 0.09f, 5u) - 0.5f) * 1.5f * inland;
    site.biome = MEADOW;

    // the snow mountain
    {
        const float dx = x - kAlpineX, dz = z - kAlpineZ;
        const float warp = 1.0f + (valueNoise(x * 0.045f, z * 0.045f, 11u) - 0.5f) * 0.3f;
        const float r = sqrtf(dx * dx + dz * dz) / kAlpineRadius * warp;
        if (r < 1.0f)
        {
            const float h = kAlpineHeight * (1.0f - r) *
                            (0.92f + 0.16f * valueNoise(x * 0.035f, z * 0.035f, 13u));
            if (h > site.height)
            {
                site.height = h;
                site.biome = ALPINE;
            }
        }
    }

    // the volcano, with its crater
    {
        const float dx = x - kVolcanoX, dz = z - kVolcanoZ;
        const float d = sqrtf(dx * dx + dz * dz);
        const float warp = 1.0f + (valueNoise(x * 0.05f, z * 0.05f, 17u) - 0.5f) * 0.2f;
        const float r = d / kVolcanoRadius * warp;
        if (r < 1.0f)
        {
            float h = kVolcanoHeight * powf(1.0f - r, 0.95f);
            if (d < kCraterRadius)
            {
                const float t = d / kCraterRadius;
                h = fminf(h, 42.5f + t * t * t * 9.0f);
            }
            if (h > site.height)
            {
                site.height = h;
                site.biome = VOLCANO;
            }
        }
    }

    // the crags
    for (const float *crag : kCrags)
    {
        const float dx = x - crag[0], dz = z - crag[1];
        const float warp = 1.0f + (valueNoise(x * 0.07f, z * 0.07f, 19u) - 0.5f) * 0.4f;
        const float r = sqrtf(dx * dx + dz * dz) / crag[2] * warp;
        if (r < 1.0f)
        {
            const float h = crag[3] * powf(1.0f - r, 0.85f) *
                            (0.8f + 0.4f * valueNoise(x * 0.11f, z * 0.11f, 23u));
            if (h > site.height)
            {
                site.height = h;
                site.biome = CRAG;
            }
        }
    }

    // the badlands: a stepped plateau around the canyon
    const float canyon = distanceToLine(x, z, kCanyon, kCanyonPoints);
    {
        float h =
            (27.0f + 7.0f * valueNoise(x * 0.05f, z * 0.05f, 31u)) * ramp(34.0f, 18.0f, canyon);
        if (h > 4.0f)
            h = floorf(h / 5.0f + 0.5f) * 5.0f;
        if (h > site.height)
        {
            site.height = h;
            site.biome = MESA;
        }
    }
    // ... and the canyon itself, down to below sea level
    if (canyon < 13.5f)
    {
        const float bed = -3.2f;
        site.height = bed + (site.height - bed) * ramp(6.5f, 13.0f, canyon);
        site.biome = MESA;
    }
    return site;
}

// ---------------------------------------------------------------------
// The voxel grid
// ---------------------------------------------------------------------
struct Grid
{
    std::vector<unsigned char> block;
    std::vector<unsigned char> plant;
    std::vector<unsigned char> sky;    // 0..15
    std::vector<unsigned char> warm;   // light of lava and lanterns, 0..14
    std::vector<unsigned char> cool;   // light of crystals, 0..14
    std::vector<unsigned char> carved; // 1: cleared for the ride
    std::vector<unsigned char> level;  // water: 0 source, 1..7 flowing, 8 falling
    std::vector<unsigned char> biome;  // per column

    Grid()
        : block(static_cast<size_t>(kSizeXZ * kSizeY * kSizeXZ), AIR),
          plant(block.size(), NO_PLANT), sky(block.size(), 0), warm(block.size(), 0),
          cool(block.size(), 0), carved(block.size(), 0), level(block.size(), 0),
          biome(static_cast<size_t>(kSizeXZ * kSizeXZ), MEADOW)
    {
    }

    static bool insideColumn(int bx, int bz)
    {
        return bx >= -kHalf && bx < kHalf && bz >= -kHalf && bz < kHalf;
    }
    static bool inside(int bx, int j, int bz)
    {
        return insideColumn(bx, bz) && j >= kMinY && j <= kMaxY;
    }
    static size_t index(int bx, int j, int bz)
    {
        return (static_cast<size_t>(bx + kHalf) * kSizeY + static_cast<size_t>(j - kMinY)) *
                   kSizeXZ +
               static_cast<size_t>(bz + kHalf);
    }
    static size_t column(int bx, int bz)
    {
        return static_cast<size_t>(bx + kHalf) * kSizeXZ + static_cast<size_t>(bz + kHalf);
    }
    int get(int bx, int j, int bz) const
    {
        if (!inside(bx, j, bz))
            return (j < kMinY && insideColumn(bx, bz)) ? STONE : AIR;
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
    // Solid and not see-through: water lets light through and shows what is
    // behind it, so it hides no faces and casts no ambient occlusion.
    bool opaque(int bx, int j, int bz) const
    {
        const int b = get(bx, j, bz);
        return b != AIR && b != WATER;
    }
    int skyAt(int bx, int j, int bz) const
    {
        return inside(bx, j, bz) ? sky[index(bx, j, bz)] : (j < kMinY ? 0 : 15);
    }
    int warmAt(int bx, int j, int bz) const
    {
        return inside(bx, j, bz) ? warm[index(bx, j, bz)] : 0;
    }
    int coolAt(int bx, int j, int bz) const
    {
        return inside(bx, j, bz) ? cool[index(bx, j, bz)] : 0;
    }
    // Highest layer of the ground in a column (not counting trees), or kMinY - 1.
    int top(int bx, int bz) const
    {
        for (int j = kMaxY; j >= kMinY; --j)
        {
            const int b = get(bx, j, bz);
            if (b != AIR && !isGrowth(b))
                return j;
        }
        return kMinY - 1;
    }
    // Clears a column above layer j.
    void clearAbove(int bx, int j, int bz)
    {
        for (int k = j + 1; k <= kMaxY; ++k)
            put(bx, k, bz, AIR);
    }
};

// For every column: the lowest and highest the ride passes within reach of
// it. Nothing may be built or planted in that band; trees keep away from a
// wider one.
struct Clearance
{
    std::vector<float> low, high;         // the train itself
    std::vector<float> nearLow, nearHigh; // room for a tree's crown

    Clearance()
        : low(static_cast<size_t>(kSizeXZ * kSizeXZ), 1.0e9f), high(low.size(), -1.0e9f),
          nearLow(low.size(), 1.0e9f), nearHigh(low.size(), -1.0e9f)
    {
    }
    void add(const std::vector<float> &ride)
    {
        const float wide = 9.0f;
        for (size_t i = 0; i + 2 < ride.size(); i += 3)
        {
            const float x = ride[i], y = ride[i + 1], z = ride[i + 2];
            if (fabsf(x) > kIslandHalf + wide || fabsf(z) > kIslandHalf + wide)
                continue;
            for (int bx = blockOf(x - wide); bx <= blockOf(x + wide); ++bx)
                for (int bz = blockOf(z - wide); bz <= blockOf(z + wide); ++bz)
                {
                    if (!Grid::insideColumn(bx, bz))
                        continue;
                    const size_t c = Grid::column(bx, bz);
                    // distance to the column's square
                    const float x0 = static_cast<float>(bx) * kBlock;
                    const float z0 = static_cast<float>(bz) * kBlock;
                    const float nx = fmaxf(x0, fminf(x, x0 + kBlock));
                    const float nz = fmaxf(z0, fminf(z, z0 + kBlock));
                    const float d2 = (x - nx) * (x - nx) + (z - nz) * (z - nz);
                    if (d2 < kTrainReach * kTrainReach)
                    {
                        low[c] = fminf(low[c], y - kTrainBelow);
                        high[c] = fmaxf(high[c], y + kTrainAbove);
                    }
                    if (d2 < wide * wide)
                    {
                        nearLow[c] = fminf(nearLow[c], y);
                        nearHigh[c] = fmaxf(nearHigh[c], y);
                    }
                }
        }
    }
    // Would a block in this cell be in the train's way?
    bool blocks(int bx, int j, int bz) const
    {
        if (!Grid::insideColumn(bx, bz))
            return false;
        const size_t c = Grid::column(bx, bz);
        const float bottom = static_cast<float>(j) * kBlock;
        return bottom + kBlock > low[c] && bottom < high[c];
    }
    // May something as tall as a tree stand on layer j of this column?
    bool roomForTree(int bx, int j, int bz) const
    {
        if (!Grid::insideColumn(bx, bz))
            return false;
        const size_t c = Grid::column(bx, bz);
        if (nearHigh[c] < -1.0e8f)
            return true;
        const float ground = static_cast<float>(j + 1) * kBlock;
        return ground + 24.0f < nearLow[c] || ground > nearHigh[c] + 14.0f;
    }
};

// Things that move, placed when the world is built.
struct Animal
{
    float x, z;
    int kind; // 0 sheep, 1 pig, 2 cow
    float phase;
};

struct Extras
{
    std::vector<Animal> animals;
    float mill[3] = {0.0f, 0.0f, 0.0f}; // hub of the windmill's sails
    bool hasMill = false;
};

// ---------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------
int clayBand(int j)
{
    const int bands[6] = {CLAY_RED, CLAY_ORANGE, CLAY_YELLOW, CLAY_WHITE, CLAY_BROWN, CLAY_GREY};
    // bands one or two layers thick, the same all over the badlands
    return bands[hash(j / 2 + (j % 5 == 0 ? 1 : 0), 0, 43u) % 6u];
}

int stoneOrOre(int bx, int j, int bz, bool rich)
{
    const unsigned int h = hash(bx * 31 + j, bz * 17 + j * 7, 47u);
    if (h % 37u == 0)
        return ORE_COAL;
    if (h % 83u == 1)
        return ORE_IRON;
    if (h % 149u == 2)
        return ORE_GOLD;
    if (rich && h % 97u == 3)
        return ORE_DIAMOND;
    return STONE;
}

void buildTerrain(Grid &g)
{
    std::vector<int> tops(static_cast<size_t>(kSizeXZ * kSizeXZ), 0);
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const Site site = siteAt(centerOf(bx), centerOf(bz));
            int top = static_cast<int>(floorf(site.height / kBlock + 0.5f)) - 1;
            top = top < kMinY ? kMinY : (top > kMaxY - 1 ? kMaxY - 1 : top);
            tops[Grid::column(bx, bz)] = top;
            g.biome[Grid::column(bx, bz)] = static_cast<unsigned char>(site.biome);
        }

    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            if (!onIsland(bx, bz))
                continue; // the ring over the sea stays open
            const float cx = centerOf(bx), cz = centerOf(bz);
            const int top = tops[Grid::column(bx, bz)];
            const int biome = g.biome[Grid::column(bx, bz)];
            const float topY = static_cast<float>(top + 1) * kBlock;
            const float coast = kIslandHalf - fmaxf(fabsf(cx), fabsf(cz));

            // how much higher or lower the neighbours are: cliffs are bare rock
            int steep = 0;
            for (int k = 0; k < 4; ++k)
            {
                const int nx = bx + (k == 0 ? 1 : (k == 1 ? -1 : 0));
                const int nz = bz + (k == 2 ? 1 : (k == 3 ? -1 : 0));
                if (!Grid::insideColumn(nx, nz) || !onIsland(nx, nz))
                    continue;
                const int d = top - tops[Grid::column(nx, nz)];
                steep = d > steep ? d : (-d > steep ? -d : steep);
            }
            const float patch = valueNoise(cx * 0.13f, cz * 0.13f, 53u);

            for (int j = kMinY; j <= top; ++j)
            {
                const int depth = top - j;
                int b = STONE;
                switch (biome)
                {
                case ALPINE: {
                    const float snowLine = 56.0f + patch * 10.0f;
                    if (topY > snowLine)
                        b = depth < 2 ? SNOW : stoneOrOre(bx, j, bz, false);
                    else if (topY > 34.0f || steep >= 3)
                        b = (depth == 0 && steep < 2 && patch > 0.55f)
                                ? (topY > 44.0f ? SNOWY_DIRT : GRASS)
                                : stoneOrOre(bx, j, bz, false);
                    else
                        b = depth == 0 ? (topY > 26.0f && patch > 0.4f ? SNOWY_DIRT : GRASS)
                                       : (depth < 3 ? DIRT : stoneOrOre(bx, j, bz, false));
                    break;
                }
                case VOLCANO:
                    if (topY < 9.0f && patch > 0.45f)
                        b = depth == 0 ? GRASS : (depth < 2 ? DIRT : BASALT);
                    else if (depth == 0 && topY > 30.0f && hash(bx, bz, 57u) % 7u == 0)
                        b = MAGMA;
                    else if (depth == 0 && patch > 0.7f)
                        b = GRAVEL; // ash
                    else
                        b = BASALT;
                    break;
                case MESA:
                    if (topY <= 0.0f)
                        b = depth == 0 ? (patch > 0.5f ? GRAVEL : SAND) : STONE; // the canyon bed
                    else if (depth == 0 && steep < 2 && topY > 12.0f)
                        b = RED_SAND;
                    else
                        b = clayBand(j);
                    break;
                case CRAG:
                    if (topY > 44.0f && depth == 0)
                        b = SNOW;
                    else if (steep < 2 && topY < 26.0f)
                        b = depth == 0 ? GRASS : (depth < 2 ? DIRT : stoneOrOre(bx, j, bz, true));
                    else if (depth == 0 && patch > 0.62f)
                        b = MOSSY;
                    else
                        b = stoneOrOre(bx, j, bz, true);
                    break;
                default:
                    if (topY <= 2.5f && coast < 9.0f)
                        b = depth < 3 ? SAND : STONE; // the beach
                    else
                        b = depth == 0 ? GRASS : (depth < 3 ? DIRT : stoneOrOre(bx, j, bz, false));
                    break;
                }
                g.put(bx, j, bz, b);
            }
        }
}

// The lava lake in the crater.
void buildLavaLake(Grid &g)
{
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float dx = centerOf(bx) - kVolcanoX, dz = centerOf(bz) - kVolcanoZ;
            if (dx * dx + dz * dz > kCraterRadius * kCraterRadius)
                continue;
            for (int j = g.top(bx, bz) + 1; j <= kLavaLakeTop; ++j)
                g.put(bx, j, bz, LAVA);
        }
}

// Lets lava run along a line of points (x, z pairs): it takes the place of
// the top block and never climbs, cutting a gully where the ground rises. A
// wide river also takes the columns beside it. (Water is not traced: it
// spreads from its sources by itself, see spreadWater().)
void traceLava(Grid &g, const float *points, int count, bool wide)
{
    int level = kMaxY;
    int lastX = 0, lastZ = 0;
    bool started = false;
    std::vector<int> cells; // bx, bz, level of every cell of the channel

    auto visit = [&](int bx, int bz) {
        if (started && bx == lastX && bz == lastZ)
            return;
        if (!Grid::insideColumn(bx, bz))
        {
            started = false;
            return;
        }
        const int top = g.top(bx, bz);
        const int here = top < level ? top : level;
        g.clearAbove(bx, here, bz);
        g.put(bx, here, bz, LAVA);
        if (here - 1 >= kMinY && g.get(bx, here - 1, bz) == AIR)
            g.put(bx, here - 1, bz, STONE);
        cells.push_back(bx);
        cells.push_back(bz);
        cells.push_back(here);
        level = here;
        lastX = bx;
        lastZ = bz;
        started = true;
    };

    for (int i = 0; i + 1 < count; ++i)
    {
        const float ax = points[2 * i], az = points[2 * i + 1];
        const float ex = points[2 * i + 2] - ax, ez = points[2 * i + 3] - az;
        const int steps = static_cast<int>(sqrtf(ex * ex + ez * ez) * 2.0f) + 1;
        for (int s = 0; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const int bx = blockOf(ax + ex * t), bz = blockOf(az + ez * t);
            // move one side at a time so the channel has no diagonal gaps
            if (started && bx != lastX && bz != lastZ)
                visit(bx, lastZ);
            visit(bx, bz);
        }
    }

    if (!wide)
        return;
    for (size_t i = 0; i + 2 < cells.size(); i += 3)
        for (int k = 0; k < 4; ++k)
        {
            const int bx = cells[i] + (k == 0 ? 1 : (k == 1 ? -1 : 0));
            const int bz = cells[i + 1] + (k == 2 ? 1 : (k == 3 ? -1 : 0));
            const int here = cells[i + 2];
            if (!Grid::insideColumn(bx, bz) || g.top(bx, bz) < here || g.get(bx, here, bz) == LAVA)
                continue;
            g.clearAbove(bx, here, bz);
            g.put(bx, here, bz, LAVA);
        }
}

// A river of still water (sources) one block deep in a trench along a line
// of points, starting at `level` and never climbing: where the ground drops
// the river steps down with it. width: blocks across.
void buildRiver(Grid &g, const float *points, int count, int level, int width)
{
    int lastX = 1 << 20, lastZ = 1 << 20;
    auto visit = [&](int bx, int bz) {
        if (bx == lastX && bz == lastZ)
            return;
        lastX = bx;
        lastZ = bz;
        for (int w = 0; w < width; ++w)
        {
            const int x = bx + w;
            if (!Grid::insideColumn(x, bz) || !onIsland(x, bz))
                continue;
            const int top = g.top(x, bz);
            level = top < level ? top : level;
            g.clearAbove(x, level, bz);
            g.put(x, level, bz, WATER);
            if (g.get(x, level - 1, bz) == AIR)
                g.put(x, level - 1, bz, GRAVEL);
        }
    };
    for (int i = 0; i + 1 < count; ++i)
    {
        const float ax = points[2 * i], az = points[2 * i + 1];
        const float ex = points[2 * i + 2] - ax, ez = points[2 * i + 3] - az;
        const int steps = static_cast<int>(sqrtf(ex * ex + ez * ez) * 2.0f) + 1;
        for (int s = 0; s <= steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const int bx = blockOf(ax + ex * t), bz = blockOf(az + ez * t);
            if (bx != lastX && bz != lastZ && lastX != (1 << 20))
                visit(bx, lastZ); // no diagonal gaps
            visit(bx, bz);
        }
    }
}

// A waterfall in a rocky gorge cut into the north side of the snow mountain:
// a stream four blocks wide runs out of the rock to a sheer stone lip and
// falls kFallDrop blocks into a pool; the gorge opens north and a river runs
// from the pool down to the shore and into the sea. Only the sources are put
// here; the fall itself is the water flowing over the lip (spreadWater).
void buildWaterfall(Grid &g)
{
    const int lip = g.top((kFallX0 + kFallX1) / 2, kFallLipZ);
    const int pool = lip - kFallDrop;
    const int cx = (kFallX0 + kFallX1) / 2;

    // The gorge: open down to the pool from the cliff out to the north, wider
    // further out, its walls bare stone with the grass left on top.
    for (int bz = kFallLipZ + 1; bz <= kFallLipZ + 9; ++bz)
    {
        const int half = 3 + (bz - kFallLipZ) / 3;
        for (int bx = cx - half - 2; bx <= cx + half + 2; ++bx)
        {
            if (!Grid::insideColumn(bx, bz))
                continue;
            const int top = g.top(bx, bz);
            if (bx >= cx - half && bx <= cx + half)
            {
                g.clearAbove(bx, pool, bz);
                for (int j = kMinY; j <= pool; ++j)
                    if (g.get(bx, j, bz) == AIR)
                        g.put(bx, j, bz, STONE);
                g.put(bx, pool, bz, hash(bx, bz, 451u) % 3u == 0 ? GRAVEL : STONE);
            }
            else
                for (int j = pool; j < top; ++j) // the walls
                    g.put(bx, j, bz, hash(bx * 3 + j, bz, 453u) % 4u == 0 ? MOSSY : STONE);
        }
    }
    // The cliff behind the fall, sheer stone up to the lip, and stone round it.
    for (int bx = cx - 4; bx <= cx + 4; ++bx)
        for (int bz = kFallLipZ - 3; bz <= kFallLipZ; ++bz)
        {
            const int top = g.top(bx, bz);
            for (int j = pool; j <= (top > lip ? top : lip); ++j)
                g.put(bx, j, bz, hash(bx * 5 + j, bz, 455u) % 5u == 0 ? MOSSY : STONE);
        }
    // The stream: sources in a channel cut into the rock above the lip.
    for (int bx = kFallX0; bx <= kFallX1; ++bx)
        for (int bz = kFallLipZ - 2; bz <= kFallLipZ; ++bz)
        {
            g.clearAbove(bx, lip, bz);
            g.put(bx, lip, bz, WATER);
        }
    // The pool, round and a block deep, under the fall.
    for (int bx = cx - 3; bx <= cx + 3; ++bx)
        for (int bz = kFallLipZ + 1; bz <= kFallLipZ + 6; ++bz)
        {
            const float dx = static_cast<float>(bx - cx),
                        dz = static_cast<float>(bz - kFallLipZ - 3);
            if (dx * dx + dz * dz * 1.4f > 11.0f)
                continue;
            g.put(bx, pool, bz, WATER);
            g.put(bx, pool - 1, bz, GRAVEL);
        }
    // And the river from the pool to the sea.
    const float start[] = {centerOf(cx), centerOf(kFallLipZ + 6), kRiver[0], kRiver[1]};
    buildRiver(g, start, 2, pool, 2);
    buildRiver(g, kRiver, static_cast<int>(sizeof(kRiver) / sizeof(kRiver[0]) / 2), pool, 2);
}

// The room that must stay free of water round the ride: within kWaterReach
// sideways of the centre line (the rails reach 1.6, the train 3), from
// kWaterBelow under the rails to kWaterAbove over them, so no water runs
// beside, under or over the track. Nothing holds the water back from it: the
// springs are placed where the water runs down clear of it by itself, and
// LandRideClearanceCheck checks that (LandWorld::waterNearRide()).
const float kWaterReach = 6.0f;
const float kWaterBelow = 6.0f;
const float kWaterAbove = 10.0f;

// Marks the cells of that room. ride: x, y, z for each point.
std::vector<unsigned char> waterZone(const std::vector<float> &ride)
{
    const float reach = kWaterReach;
    std::vector<unsigned char> zone(static_cast<size_t>(kSizeXZ * kSizeY * kSizeXZ), 0);
    for (size_t i = 0; i + 2 < ride.size(); i += 3)
    {
        const float x = ride[i], y = ride[i + 1], z = ride[i + 2];
        for (int bx = blockOf(x - reach); bx <= blockOf(x + reach); ++bx)
            for (int bz = blockOf(z - reach); bz <= blockOf(z + reach); ++bz)
            {
                if (!Grid::insideColumn(bx, bz))
                    continue;
                const float x0 = static_cast<float>(bx) * kBlock;
                const float z0 = static_cast<float>(bz) * kBlock;
                const float nx = fmaxf(x0, fminf(x, x0 + kBlock));
                const float nz = fmaxf(z0, fminf(z, z0 + kBlock));
                if ((x - nx) * (x - nx) + (z - nz) * (z - nz) >= reach * reach)
                    continue;
                for (int j = blockOf(y - kWaterBelow); j <= blockOf(y + kWaterAbove); ++j)
                    if (Grid::inside(bx, j, bz))
                        zone[Grid::index(bx, j, bz)] = 1;
            }
    }
    return zone;
}

// Lets the water flow from every water block (the sources) by the rules of
// a block game (WaterFlow.h): down first, seven blocks sideways at most and
// towards the nearest way down within four. Below the sea level the sea
// takes it. (The room carved for the ride is solid to it, but the springs
// are placed so it never gets there; see waterZone.)
void spreadWater(Grid &g)
{
    WaterFlow::Field field(kSizeXZ, kSizeY, kSizeXZ);
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const size_t i = Grid::index(bx, j, bz);
                const int x = bx + kHalf, y = j - kMinY, z = bz + kHalf;
                const int b = g.block[i];
                if (b == WATER)
                    field.addSource(x, y, z);
                else if (b != AIR || g.carved[i])
                    field.setCell(x, y, z, WaterFlow::SOLID);
                else if (j <= -1)
                    field.setCell(x, y, z, WaterFlow::SINK);
            }
    field.spread();
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const int x = bx + kHalf, y = j - kMinY, z = bz + kHalf;
                if (field.cell(x, y, z) != WaterFlow::WATER)
                    continue;
                const size_t i = Grid::index(bx, j, bz);
                g.block[i] = WATER;
                g.level[i] = static_cast<unsigned char>(field.level(x, y, z));
                // water falling into the sea reaches down to its surface
                if (j == 0 && g.get(bx, -1, bz) == AIR && !g.carved[Grid::index(bx, -1, bz)])
                {
                    g.put(bx, -1, bz, WATER);
                    g.level[Grid::index(bx, -1, bz)] = WaterFlow::FALLING;
                }
            }
}

// A round pond flush with a level bank.
void buildPond(Grid &g, float x, float z, float radius)
{
    const int level = g.top(blockOf(x), blockOf(z));
    for (int bx = blockOf(x - radius - 4.0f); bx <= blockOf(x + radius + 4.0f); ++bx)
        for (int bz = blockOf(z - radius - 4.0f); bz <= blockOf(z + radius + 4.0f); ++bz)
        {
            if (!Grid::insideColumn(bx, bz))
                continue;
            const float dx = centerOf(bx) - x, dz = centerOf(bz) - z;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d > radius + 3.0f)
                continue;
            g.clearAbove(bx, level, bz);
            for (int j = kMinY; j <= level; ++j)
                if (g.get(bx, j, bz) == AIR)
                    g.put(bx, j, bz, DIRT);
            if (d <= radius)
            {
                g.put(bx, level, bz, WATER);
                g.put(bx, level - 1, bz, SAND);
            }
            else
                g.put(bx, level, bz, d < radius + 1.4f ? SAND : GRASS);
        }
}

// The floating island: grass on top, tapering to a point of rock below,
// with a spring that falls from its edge to the pond in the meadow.
void buildFloatingIsland(Grid &g)
{
    const float radius = 4.3f; // in blocks
    for (int dx = -5; dx <= 5; ++dx)
        for (int dz = -5; dz <= 5; ++dz)
        {
            const float d =
                sqrtf(static_cast<float>(dx * dx + dz * dz)) + (lattice(dx, dz, 67u) - 0.5f) * 0.8f;
            if (d > radius)
                continue;
            const int depth = 1 + static_cast<int>((1.0f - (d / radius) * (d / radius)) * 6.5f);
            for (int k = 0; k < depth; ++k)
                g.put(kFloatX + dx, kFloatTop - k, kFloatZ + dz,
                      k == 0 ? GRASS
                             : (k < 2 ? DIRT : (oneIn(dx + k, dz, 69u, 9) ? ORE_GOLD : STONE)));
        }
    // the spring: three sources in a channel to the east edge; the water
    // runs over the edge and falls (spreadWater)
    g.put(kFloatX + 2, kFloatTop, kFloatZ, WATER);
    g.put(kFloatX + 3, kFloatTop, kFloatZ, WATER);
    g.put(kFloatX + 4, kFloatTop, kFloatZ, WATER);
    g.put(kFloatX + 4, kFloatTop - 1, kFloatZ, STONE);
}

// ---------------------------------------------------------------------
// The village
// ---------------------------------------------------------------------
void fill(Grid &g, int x0, int x1, int j0, int j1, int z0, int z1, int block)
{
    for (int bx = x0; bx <= x1; ++bx)
        for (int j = j0; j <= j1; ++j)
            for (int bz = z0; bz <= z1; ++bz)
                g.put(bx, j, bz, block);
}

// Clears the plot and puts cobble under it down to the ground.
void footing(Grid &g, int x0, int x1, int z0, int z1, int floor)
{
    for (int bx = x0; bx <= x1; ++bx)
        for (int bz = z0; bz <= z1; ++bz)
        {
            g.clearAbove(bx, floor - 1, bz);
            for (int j = floor - 1; j >= kMinY && (j == floor - 1 || g.get(bx, j, bz) == AIR); --j)
                g.put(bx, j, bz, COBBLE);
        }
}

// A cottage: plank walls between log corner posts on a cobble footing, lit
// windows, a door facing +z or +x and a gabled roof of shingles.
void buildHouse(Grid &g, int x0, int z0, int x1, int z1, int floor, bool doorOnX)
{
    const int wallTop = floor + 2;
    footing(g, x0, x1, z0, z1, floor);
    fill(g, x0, x1, floor, wallTop, z0, z1, PLANKS);
    for (int j = floor; j <= wallTop; ++j)
    {
        g.put(x0, j, z0, OAK_LOG);
        g.put(x1, j, z0, OAK_LOG);
        g.put(x0, j, z1, OAK_LOG);
        g.put(x1, j, z1, OAK_LOG);
    }
    for (int bx = x0 + 1; bx < x1; bx += 2)
    {
        g.put(bx, floor + 1, z0, WINDOW);
        g.put(bx, floor + 1, z1, WINDOW);
    }
    for (int bz = z0 + 1; bz < z1; bz += 2)
    {
        g.put(x0, floor + 1, bz, WINDOW);
        g.put(x1, floor + 1, bz, WINDOW);
    }
    if (doorOnX)
    {
        g.put(x1, floor, (z0 + z1) / 2, DOOR);
        g.put(x1, floor + 1, (z0 + z1) / 2, PLANKS);
    }
    else
    {
        g.put((x0 + x1) / 2, floor, z1, DOOR);
        g.put((x0 + x1) / 2, floor + 1, z1, PLANKS);
    }
    // the roof: slabs that step in from both long sides, one block of eaves
    const bool ridgeAlongX = (x1 - x0) >= (z1 - z0);
    for (int k = 0;; ++k)
    {
        const int a0 = (ridgeAlongX ? z0 : x0) - 1 + k;
        const int a1 = (ridgeAlongX ? z1 : x1) + 1 - k;
        if (a0 > a1)
            break;
        if (ridgeAlongX)
            fill(g, x0 - 1, x1 + 1, wallTop + 1 + k, wallTop + 1 + k, a0, a1, ROOF);
        else
            fill(g, a0, a1, wallTop + 1 + k, wallTop + 1 + k, z0 - 1, z1 + 1, ROOF);
    }
    // chimney
    g.put(x0 + 1, wallTop + 2, z0 + 1, COBBLE);
    g.put(x0 + 1, wallTop + 3, z0 + 1, COBBLE);
    g.put(x0 + 1, wallTop + 4, z0 + 1, COBBLE);
}

void buildLampPost(Grid &g, int bx, int bz, int floor)
{
    g.put(bx, floor, bz, OAK_LOG);
    g.put(bx, floor + 1, bz, OAK_LOG);
    g.put(bx, floor + 2, bz, LANTERN);
}

void buildVillage(Grid &g, Extras &extras)
{
    // level the green, blending into the meadow around it
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float dx = centerOf(bx) - kVillageX, dz = centerOf(bz) - kVillageZ;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d > kVillageRadius)
                continue;
            const int top = g.top(bx, bz);
            if (top < 0 && d > kVillageRadius - 8.0f)
                continue; // leave the beach alone
            int target = kVillageTop;
            if (d > kVillageRadius - 6.0f)
                target = top > kVillageTop ? top - 1 : (top < kVillageTop ? top + 1 : top);
            if (g.biome[Grid::column(bx, bz)] != MEADOW)
                continue;
            g.clearAbove(bx, target, bz);
            for (int j = kMinY; j <= target; ++j)
                g.put(bx, j, bz, j == target ? GRASS : (j > target - 3 ? DIRT : STONE));
        }

    const int floor = kVillageTop + 1;
    // gravel lanes
    for (int bx = -37; bx <= -19; ++bx)
        g.put(bx, kVillageTop, -30, GRAVEL);
    for (int bz = -37; bz <= -20; ++bz)
        g.put(-27, kVillageTop, bz, GRAVEL);

    buildHouse(g, -35, -37, -31, -33, floor, false);
    buildHouse(g, -24, -36, -20, -32, floor, false);
    buildHouse(g, -36, -26, -31, -22, floor, true);

    // the well at the crossing
    fill(g, -28, -26, floor, floor, -31, -29, COBBLE);
    g.put(-27, floor, -30, WATER);
    g.put(-28, floor + 1, -31, OAK_LOG);
    g.put(-26, floor + 1, -29, OAK_LOG);
    g.put(-28, floor + 2, -31, OAK_LOG);
    g.put(-26, floor + 2, -29, OAK_LOG);
    fill(g, -28, -26, floor + 3, floor + 3, -31, -29, ROOF);

    // the wheat field, with a ditch down the middle and hay bales beside it
    for (int bx = -25; bx <= -19; ++bx)
        for (int bz = -27; bz <= -22; ++bz)
        {
            g.put(bx, kVillageTop, bz, bx == -22 ? WATER : FARMLAND);
            if (bx != -22)
                g.plant[Grid::index(bx, floor, bz)] = WHEAT;
        }
    g.put(-26, floor, -24, HAY);
    g.put(-26, floor, -23, HAY);
    g.put(-26, floor + 1, -23, HAY);

    // the windmill: a stone tower with a timber top, the sails facing south
    footing(g, -23, -21, -21, -19, floor);
    fill(g, -23, -21, floor, floor + 3, -21, -19, COBBLE);
    fill(g, -23, -21, floor + 4, floor + 6, -21, -19, PLANKS);
    for (int j = floor + 4; j <= floor + 6; ++j)
    {
        g.put(-23, j, -21, OAK_LOG);
        g.put(-21, j, -21, OAK_LOG);
        g.put(-23, j, -19, OAK_LOG);
        g.put(-21, j, -19, OAK_LOG);
    }
    fill(g, -24, -20, floor + 7, floor + 7, -22, -18, ROOF);
    fill(g, -23, -21, floor + 8, floor + 8, -21, -19, ROOF);
    g.put(-22, floor + 9, -20, ROOF);
    g.put(-22, floor, -19, DOOR);
    g.put(-22, floor + 5, -19, WINDOW);
    extras.mill[0] = centerOf(-22);
    extras.mill[1] = static_cast<float>(floor + 6) * kBlock;
    extras.mill[2] = static_cast<float>(-18) * kBlock + 1.2f;
    extras.hasMill = true;

    buildLampPost(g, -29, -32, floor);
    buildLampPost(g, -25, -28, floor);
    buildLampPost(g, -29, -21, floor);
    buildLampPost(g, -19, -30, floor);
}

// ---------------------------------------------------------------------
// The ride through it
// ---------------------------------------------------------------------
// Clears a tube around the ride and lays a bed of gravel under it.
void carveRide(Grid &g, const std::vector<float> &ride)
{
    for (size_t i = 0; i + 2 < ride.size(); i += 3)
    {
        const float x = ride[i], y = ride[i + 1], z = ride[i + 2];
        if (fabsf(x) > kIslandHalf + kCarveRadius || fabsf(z) > kIslandHalf + kCarveRadius)
            continue;
        const int jLow = blockOf(y - kCarveBelow);
        const int jHigh = blockOf(y + kCarveAbove);
        for (int bx = blockOf(x - kCarveRadius); bx <= blockOf(x + kCarveRadius); ++bx)
            for (int bz = blockOf(z - kCarveRadius); bz <= blockOf(z + kCarveRadius); ++bz)
            {
                if (!Grid::insideColumn(bx, bz))
                    continue;
                const float dx = centerOf(bx) - x;
                const float dz = centerOf(bz) - z;
                if (dx * dx + dz * dz > kCarveRadius * kCarveRadius)
                    continue;
                for (int j = jLow; j <= jHigh; ++j)
                {
                    const float cy = centerOf(j);
                    if (cy > y - kCarveBelow && cy <= y + kCarveAbove && Grid::inside(bx, j, bz))
                    {
                        g.put(bx, j, bz, AIR);
                        g.plant[Grid::index(bx, j, bz)] = NO_PLANT;
                        g.carved[Grid::index(bx, j, bz)] = 1;
                    }
                }
            }
    }
    // A thin roof left over a cutting would hang in the air: take it off.
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j < kMaxY; ++j)
            {
                if (!g.carved[Grid::index(bx, j, bz)] || g.get(bx, j + 1, bz) == AIR)
                    continue;
                int thick = 0;
                while (j + 1 + thick <= kMaxY && g.get(bx, j + 1 + thick, bz) != AIR)
                    ++thick;
                if (thick > 4)
                    continue;
                for (int k = 1; k <= thick; ++k)
                    if (g.get(bx, j + k, bz) != LAVA && g.get(bx, j + k, bz) != WATER)
                        g.put(bx, j + k, bz, AIR);
            }

    // the bed: whatever ground is left right under the rails
    for (size_t i = 0; i + 2 < ride.size(); i += 3)
    {
        const float x = ride[i], y = ride[i + 1], z = ride[i + 2];
        for (int bx = blockOf(x - 2.4f); bx <= blockOf(x + 2.4f); ++bx)
            for (int bz = blockOf(z - 2.4f); bz <= blockOf(z + 2.4f); ++bz)
            {
                const int j = blockOf(y - kCarveBelow - 0.01f);
                if (!Grid::inside(bx, j, bz) || g.carved[Grid::index(bx, j, bz)])
                    continue;
                const int b = g.get(bx, j, bz);
                if (b != AIR && b != LAVA && b != WATER && !isGrowth(b))
                    g.put(bx, j, bz, GRAVEL);
            }
    }
}

// Lanterns and crystals in the walls where the ride runs under a roof.
// along: the B-spline ride, x, y, z for each point.
void lightTunnels(Grid &g, const std::vector<float> &along, const Clearance &clear)
{
    float travelled = 0.0f;
    float next = 0.0f;
    int side = 1;
    for (size_t i = 3; i + 2 < along.size(); i += 3)
    {
        const float x = along[i], y = along[i + 1], z = along[i + 2];
        const float dx = x - along[i - 3], dz = z - along[i - 1];
        const float step = sqrtf(dx * dx + dz * dz);
        travelled += step;
        if (travelled < next || step < 1.0e-4f)
            continue;
        if (!g.solid(blockOf(x), blockOf(y + kCarveAbove + 1.5f), blockOf(z)))
            continue; // open sky above
        next = travelled + 11.0f;
        side = -side;
        // a little way into the wall, at head height
        const float s = static_cast<float>(side);
        const float lx = x + dz / step * s * (kCarveRadius + 1.6f);
        const float lz = z - dx / step * s * (kCarveRadius + 1.6f);
        const int bx = blockOf(lx), bz = blockOf(lz), j = blockOf(y + 4.0f);
        if (!Grid::inside(bx, j, bz) || clear.blocks(bx, j, bz))
            continue;
        const int here = g.get(bx, j, bz);
        if (here == AIR || here == LAVA || here == WATER)
            continue;
        g.put(bx, j, bz, hash(bx, bz, 71u) % 3u == 0 ? CRYSTAL : LANTERN);
    }
}

// ---------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------
struct Placed
{
    int bx, j, bz, block;
};

// Puts the blocks down only if every one of them has room.
bool commit(Grid &g, const Clearance &clear, const std::vector<Placed> &blocks)
{
    for (const Placed &p : blocks)
        if (!Grid::inside(p.bx, p.j, p.bz) || clear.blocks(p.bx, p.j, p.bz) ||
            (g.get(p.bx, p.j, p.bz) != AIR && !isGrowth(g.get(p.bx, p.j, p.bz))))
            return false;
    for (const Placed &p : blocks)
        if (g.get(p.bx, p.j, p.bz) == AIR)
        {
            g.put(p.bx, p.j, p.bz, p.block);
            g.plant[Grid::index(p.bx, p.j, p.bz)] = NO_PLANT;
        }
    return true;
}

// A round-crowned tree: oak, birch, cherry or an autumn one.
void roundTree(std::vector<Placed> &out, int bx, int base, int bz, int log, int leaves, int trunk,
               bool wide)
{
    for (int k = 0; k < trunk; ++k)
        out.push_back(Placed{bx, base + k, bz, log});
    const int crown = base + trunk - 2;
    for (int layer = 0; layer < 4; ++layer)
    {
        const int radius = layer < 2 ? (wide ? 3 : 2) : (layer == 2 ? (wide ? 2 : 1) : 1);
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
            {
                if (dx == 0 && dz == 0 && layer < 2)
                    continue; // the trunk
                const bool corner =
                    (dx == radius || dx == -radius) && (dz == radius || dz == -radius);
                if (corner && (layer == 3 || hash(bx + dx, bz + dz + layer, 73u) % 2u == 0))
                    continue;
                out.push_back(Placed{bx + dx, crown + layer, bz + dz, leaves});
            }
    }
}

// A spruce: rings of needles that narrow to a point, snow on top if asked.
void spruce(std::vector<Placed> &out, int bx, int base, int bz, int trunk, bool snowy)
{
    for (int k = 0; k < trunk; ++k)
        out.push_back(Placed{bx, base + k, bz, SPRUCE_LOG});
    int j = base + 2;
    for (int ring = 0; j < base + trunk; ++ring, ++j)
    {
        const int left = base + trunk - j;
        const int radius =
            left > 4 ? (ring % 2 == 0 ? 2 : 1) : (left > 1 ? (ring % 2 == 0 ? 1 : 0) : 0);
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
            {
                if ((dx == 0 && dz == 0) ||
                    (radius == 2 && (dx == 2 || dx == -2) && (dz == 2 || dz == -2)))
                    continue;
                out.push_back(Placed{bx + dx, j, bz + dz, LEAVES_SPRUCE});
            }
    }
    out.push_back(Placed{bx, base + trunk, bz, LEAVES_SPRUCE});
    out.push_back(Placed{bx, base + trunk + 1, bz, snowy ? SNOW : LEAVES_SPRUCE});
}

void giantMushroom(std::vector<Placed> &out, int bx, int base, int bz, int stem)
{
    for (int k = 0; k < stem; ++k)
        out.push_back(Placed{bx, base + k, bz, MUSHROOM_STEM});
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz)
        {
            const bool corner = (dx == 2 || dx == -2) && (dz == 2 || dz == -2);
            if (corner)
                continue;
            const bool rim = dx == 2 || dx == -2 || dz == 2 || dz == -2;
            out.push_back(Placed{bx + dx, base + stem - (rim ? 1 : 0), bz + dz, MUSHROOM_CAP});
        }
}

// Is there already a trunk within `reach` columns?
bool crowded(const Grid &g, int bx, int j, int bz, int reach)
{
    for (int dx = -reach; dx <= reach; ++dx)
        for (int dz = -reach; dz <= reach; ++dz)
        {
            const int b = g.get(bx + dx, j, bz + dz);
            const int above = g.get(bx + dx, j + 1, bz + dz);
            if ((b >= OAK_LOG && b <= SPRUCE_LOG) || (above >= OAK_LOG && above <= SPRUCE_LOG) ||
                b == MUSHROOM_STEM || b == CACTUS || (b >= PLANKS && b <= LANTERN) || b == COBBLE)
                return true;
        }
    return false;
}

void plantTrees(Grid &g, const Clearance &clear)
{
    std::vector<Placed> blocks;
    for (int bx = -kHalf + 1; bx < kHalf - 1; ++bx)
        for (int bz = -kHalf + 1; bz < kHalf - 1; ++bz)
        {
            const int top = g.top(bx, bz);
            if (top < kMinY || top + 12 > kMaxY)
                continue;
            const int ground = g.get(bx, top, bz);
            const int base = top + 1;
            if (g.get(bx, base, bz) != AIR || g.carved[Grid::index(bx, base, bz)])
                continue;
            const float cx = centerOf(bx), cz = centerOf(bz);
            const float topY = static_cast<float>(base) * kBlock;
            const int biome = g.biome[Grid::column(bx, bz)];
            const unsigned int h = hash(bx, bz, 79u);
            const float wood = valueNoise(cx * 0.045f, cz * 0.045f, 83u);
            blocks.clear();

            if (ground == RED_SAND)
            {
                // cacti on the badlands
                if (h % 23u == 0 && !crowded(g, bx, base, bz, 1))
                    for (int k = 0; k < 2 + static_cast<int>((h >> 8) % 2u); ++k)
                        blocks.push_back(Placed{bx, base + k, bz, CACTUS});
            }
            else if (ground == GRASS || ground == SNOWY_DIRT)
            {
                if (!clear.roomForTree(bx, top, bz))
                    continue;
                const float villageX = cx - kVillageX, villageZ = cz - kVillageZ;
                const bool village =
                    villageX * villageX + villageZ * villageZ < kVillageRadius * kVillageRadius;
                if (biome == ALPINE || ground == SNOWY_DIRT)
                {
                    if (h % 5u == 0 && !crowded(g, bx, base, bz, 1))
                        spruce(blocks, bx, base, bz, 6 + static_cast<int>((h >> 8) % 3u),
                               ground == SNOWY_DIRT || topY > 30.0f);
                }
                else if (biome == CRAG || biome == VOLCANO)
                {
                    if (h % 9u == 0 && !crowded(g, bx, base, bz, 2))
                        roundTree(blocks, bx, base, bz, biome == CRAG ? OAK_LOG : BIRCH_LOG,
                                  LEAVES_AUTUMN, 4 + static_cast<int>((h >> 8) % 2u), false);
                }
                else if (cz < -76.0f && cx > 4.0f && cx < 44.0f)
                {
                    // the mushroom shore
                    if (h % 11u == 0 && !crowded(g, bx, base, bz, 2))
                        giantMushroom(blocks, bx, base, bz, 3 + static_cast<int>((h >> 8) % 3u));
                }
                else
                {
                    // woods in patches, single trees between them; cherry
                    // blossom in the middle of the island
                    const float dx = cx - 22.0f, dz = cz + 22.0f;
                    const bool grove = dx * dx + dz * dz < 34.0f * 34.0f;
                    const unsigned int rarity = village ? 60u : (wood > 0.56f ? 6u : 34u);
                    if (h % rarity == 0 && !crowded(g, bx, base, bz, 2))
                    {
                        const unsigned int kind = (h >> 8) % 10u;
                        const int trunk = 4 + static_cast<int>((h >> 12) % 3u);
                        if (grove && kind < 6u)
                            roundTree(blocks, bx, base, bz, OAK_LOG, LEAVES_CHERRY, trunk, true);
                        else if (kind < 4u)
                            roundTree(blocks, bx, base, bz, BIRCH_LOG, LEAVES_BIRCH, trunk + 1,
                                      false);
                        else
                            roundTree(blocks, bx, base, bz, OAK_LOG, LEAVES_OAK, trunk, kind == 9u);
                    }
                }
            }
            if (!blocks.empty())
                commit(g, clear, blocks);
        }

    // one cherry tree on the floating island
    blocks.clear();
    roundTree(blocks, kFloatX - 1, kFloatTop + 1, kFloatZ + 1, OAK_LOG, LEAVES_CHERRY, 4, false);
    commit(g, clear, blocks);
}

// Flowers, grass and bushes on open ground. Needs the sky light.
void plantFlowers(Grid &g, const Clearance &clear)
{
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j < kMaxY; ++j)
            {
                const int ground = g.get(bx, j, bz);
                if (ground != GRASS && ground != RED_SAND && ground != SNOWY_DIRT)
                    continue;
                const size_t above = Grid::index(bx, j + 1, bz);
                if (g.block[above] != AIR || g.plant[above] != NO_PLANT || g.carved[above] ||
                    g.sky[above] < 13 || clear.blocks(bx, j + 1, bz))
                    continue;
                const float cx = centerOf(bx), cz = centerOf(bz);
                const unsigned int h = hash(bx * 3 + j, bz, 89u);
                int plant = NO_PLANT;
                if (ground == RED_SAND)
                    plant = h % 9u == 0 ? DEAD_BUSH : NO_PLANT;
                else if (ground == SNOWY_DIRT)
                    plant = h % 6u == 0 ? FERN : NO_PLANT;
                else
                {
                    // flowers grow in drifts of one kind
                    const float drift = valueNoise(cx * 0.07f, cz * 0.07f, 97u);
                    const int kinds[4] = {POPPY, DANDELION, CORNFLOWER, TULIP};
                    const int kind =
                        kinds[static_cast<int>(valueNoise(cx * 0.03f, cz * 0.03f, 101u) * 3.99f) &
                              3];
                    if (drift > 0.6f && h % 3u == 0)
                        plant = kind;
                    else if (h % 19u == 0)
                        plant = kinds[(h >> 8) % 4u];
                    else if (h % 3u == 1)
                        plant = g.biome[Grid::column(bx, bz)] == ALPINE && h % 2u == 0 ? FERN
                                                                                       : TALL_GRASS;
                }
                g.plant[above] = static_cast<unsigned char>(plant);
            }
}

// Animals graze where the meadow is open and level.
void placeAnimals(const Grid &g, const Clearance &clear, Extras &extras)
{
    const float spots[][2] = {
        {-58.0f, -86.0f}, {-84.0f, -62.0f}, {-46.0f, -58.0f}, {-80.0f, -90.0f}, {-62.0f, -50.0f},
        {6.0f, -24.0f},   {34.0f, -44.0f},  {-4.0f, -6.0f},   {58.0f, -22.0f},  {44.0f, 4.0f},
        {18.0f, 6.0f},    {70.0f, -24.0f},  {-30.0f, -74.0f}, {0.0f, -84.0f}};
    int count = 0;
    for (const float *spot : spots)
    {
        const int bx = blockOf(spot[0]), bz = blockOf(spot[1]);
        const int top = g.top(bx, bz);
        bool fine = g.get(bx, top, bz) == GRASS;
        for (int dx = -3; dx <= 3 && fine; ++dx)
            for (int dz = -3; dz <= 3 && fine; ++dz)
            {
                const int there = g.top(bx + dx, bz + dz);
                const size_t c = Grid::column(bx + dx, bz + dz);
                if (there > top + 1 || there < top - 1 || clear.nearHigh[c] > -1.0e8f)
                    fine = false;
            }
        if (!fine)
            continue;
        Animal a;
        a.x = spot[0];
        a.z = spot[1];
        a.kind = count % 3;
        a.phase = static_cast<float>(count) * 2.3f;
        extras.animals.push_back(a);
        // a companion
        a.kind = (count + 1) % 3;
        a.phase += 1.7f;
        extras.animals.push_back(a);
        ++count;
    }
}

// ---------------------------------------------------------------------
// Baked light, as in OceanWorld
// ---------------------------------------------------------------------
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
            if (!Grid::inside(x, j, z) || g.opaque(x, j, z))
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

// Sky light pours down every open column and one level less far for every
// block sideways, so cuttings stay bright and tunnels get dark.
void spreadSky(Grid &g)
{
    std::deque<Cell> queue;
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMaxY; j >= kMinY && !g.opaque(bx, j, bz); --j)
            {
                g.sky[Grid::index(bx, j, bz)] = 15;
                queue.push_back(Cell{bx, j, bz});
            }
    // light also comes in from the open sides of the island
    for (int j = kMinY; j <= kMaxY; ++j)
        for (int k = -kHalf; k < kHalf; ++k)
        {
            const int edges[4][2] = {{-kHalf, k}, {kHalf - 1, k}, {k, -kHalf}, {k, kHalf - 1}};
            for (const int *e : edges)
                if (!g.opaque(e[0], j, e[1]) && g.sky[Grid::index(e[0], j, e[1])] < 14)
                {
                    g.sky[Grid::index(e[0], j, e[1])] = 14;
                    queue.push_back(Cell{e[0], j, e[1]});
                }
        }
    flood(g, g.sky, queue);
}

void spreadGlow(Grid &g)
{
    std::deque<Cell> warm, cool;
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const int block = g.block[Grid::index(bx, j, bz)];
                int level = 0;
                bool isCool = false;
                if (block == LAVA)
                    level = 13;
                else if (block == LANTERN)
                    level = 14;
                else if (block == WINDOW)
                    level = 8;
                else if (block == MAGMA)
                    level = 6;
                else if (block == CRYSTAL)
                {
                    level = 12;
                    isCool = true;
                }
                if (level == 0)
                    continue;
                for (const int *d : kDirs)
                {
                    const int x = bx + d[0], y = j + d[1], z = bz + d[2];
                    if (!Grid::inside(x, y, z) || g.opaque(x, y, z))
                        continue;
                    unsigned char &s = (isCool ? g.cool : g.warm)[Grid::index(x, y, z)];
                    if (s < level)
                    {
                        s = static_cast<unsigned char>(level);
                        (isCool ? cool : warm).push_back(Cell{x, y, z});
                    }
                }
            }
    flood(g, g.warm, warm);
    flood(g, g.cool, cool);
}

float skyFactor(float level)
{
    return powf(0.8f, 15.0f - level);
}

float glowFactor(float level)
{
    return level <= 0.0f ? 0.0f : powf(0.7f, 14.0f - level);
}

// ---------------------------------------------------------------------
// Building the world: no OpenGL here, so tests can use it.
// ---------------------------------------------------------------------
void buildScenery(Grid &g, Extras &extras)
{
    const std::vector<float> ride = OceanWorld::rideSamples();
    Clearance clear;
    clear.add(ride);

    buildTerrain(g);
    buildLavaLake(g);
    traceLava(g, kLavaSouth, 4, true);
    traceLava(g, kLavaEast, 4, true);
    buildVillage(g, extras);
    buildPond(g, kPondX, kPondZ, kPondRadius);
    buildPond(g, 5.0f, 41.0f, 3.2f); // a still pool on the badlands
    buildWaterfall(g);
    buildFloatingIsland(g);

    carveRide(g, ride);
    const std::vector<float> along(ride.begin() + static_cast<long>(ride.size() / 3 * 2),
                                   ride.end());
    lightTunnels(g, along, clear);
    // after the carving, so the water flows round the ride's room
    spreadWater(g);

    plantTrees(g, clear);
    spreadSky(g);
    plantFlowers(g, clear);
    spreadGlow(g);
    placeAnimals(g, clear, extras);
}

// The editor's island: level grass with a few trees and flowers around
// the edge, clear of the default track in the middle.
void buildFlat(Grid &g)
{
    const Clearance clear;
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            if (onIsland(bx, bz))
            {
                g.put(bx, -2, bz, DIRT);
                g.put(bx, -1, bz, GRASS);
            }
    const int trees[][3] = {{-33, -27, 0}, {-31, 28, 1}, {33, -28, 1}, {31, 27, 0},  {0, 35, 2},
                            {-36, 2, 0},   {36, 0, 2},   {2, -36, 0},  {-20, 36, 1}, {22, -35, 2}};
    std::vector<Placed> blocks;
    for (const int *tree : trees)
    {
        blocks.clear();
        if (tree[2] == 0)
            roundTree(blocks, tree[0], 0, tree[1], OAK_LOG, LEAVES_OAK, 5, false);
        else if (tree[2] == 1)
            roundTree(blocks, tree[0], 0, tree[1], BIRCH_LOG, LEAVES_BIRCH, 6, false);
        else
            roundTree(blocks, tree[0], 0, tree[1], OAK_LOG, LEAVES_CHERRY, 5, true);
        commit(g, clear, blocks);
    }
    spreadSky(g);
    // grass and flowers outside the middle, where the track is edited
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
        {
            const float cx = centerOf(bx), cz = centerOf(bz);
            if (cx * cx + cz * cz < 68.0f * 68.0f || g.get(bx, 0, bz) != AIR)
                continue;
            const unsigned int h = hash(bx, bz, 89u);
            const int kinds[4] = {POPPY, DANDELION, CORNFLOWER, TULIP};
            if (h % 17u == 0)
                g.plant[Grid::index(bx, 0, bz)] = static_cast<unsigned char>(kinds[(h >> 8) % 4u]);
            else if (h % 4u == 0)
                g.plant[Grid::index(bx, 0, bz)] = TALL_GRASS;
        }
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
        // Water is see-through: a block shows its face behind it.
        if (g.opaque(ox, oy, oz))
            continue;
        if (oy < kMinY)
            continue; // the underside of the island
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
        const int texture = textureOf(b, f.normal[1] > 0 ? 0 : (f.normal[1] < 0 ? 1 : 2));
        for (int c = 0; c < 4; ++c)
        {
            int da[3] = {0, 0, 0};
            int db[3] = {0, 0, 0};
            da[axisA] = f.corner[c][axisA] == 1 ? 1 : -1;
            db[axisB] = f.corner[c][axisB] == 1 ? 1 : -1;
            const bool sideA = g.opaque(ox + da[0], oy + da[1], oz + da[2]);
            const bool sideB = g.opaque(ox + db[0], oy + db[1], oz + db[2]);
            const bool corner =
                g.opaque(ox + da[0] + db[0], oy + da[1] + db[1], oz + da[2] + db[2]);
            const int open =
                (sideA && sideB) ? 0 : 3 - (sideA ? 1 : 0) - (sideB ? 1 : 0) - (corner ? 1 : 0);
            const float ao[4] = {0.5f, 0.66f, 0.82f, 1.0f};

            // Smooth light: average the open cells around this corner.
            float sky = static_cast<float>(g.skyAt(ox, oy, oz));
            float warm = static_cast<float>(g.warmAt(ox, oy, oz));
            float cool = static_cast<float>(g.coolAt(ox, oy, oz));
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
                    warm +=
                        static_cast<float>(g.warmAt(samples[s][0], samples[s][1], samples[s][2]));
                    cool +=
                        static_cast<float>(g.coolAt(samples[s][0], samples[s][1], samples[s][2]));
                    count += 1.0f;
                }
            // Lava and lantern light is warm: it reddens the surface and
            // makes it glow a little, the crystals' light is the shader's.
            const float heat = glowFactor(warm / count);

            Vertex v;
            v.pos[0] = static_cast<float>(bx + f.corner[c][0]) * kBlock;
            v.pos[1] = static_cast<float>(j + f.corner[c][1]) * kBlock;
            v.pos[2] = static_cast<float>(bz + f.corner[c][2]) * kBlock;
            for (int k = 0; k < 3; ++k)
                v.normal[k] = static_cast<float>(f.normal[k]);
            const float self = blockGlow(b);
            v.color[0] = tint;
            v.color[1] = tint * (self > 0.0f ? 1.0f : 1.0f - 0.3f * heat);
            v.color[2] = tint * (self > 0.0f ? 1.0f : 1.0f - 0.55f * heat);
            v.uv[0] = f.uv[c][0];
            v.uv[1] = f.uv[c][1];
            v.extra[0] = self + heat * 0.5f;
            v.extra[1] = 0.0f;
            v.extra[2] = glowFactor(cool / count);
            v.extra[3] = skyFactor(sky / count) * ao[open];
            batches[static_cast<size_t>(texture)].push_back(v);
        }
    }
}

// How high the water in a cell stands, in blocks: full when water is above
// it or it is falling, otherwise as in a block game: a source 8/9 of the
// block, flowing water an ninth less for every block it has flowed.
float waterHeight(const Grid &g, int bx, int j, int bz)
{
    if (g.get(bx, j, bz) != WATER)
        return 0.0f;
    const int level = g.level[Grid::index(bx, j, bz)];
    if (level == WaterFlow::FALLING || g.get(bx, j + 1, bz) == WATER)
        return 1.0f;
    return static_cast<float>(8 - level) / 9.0f;
}

// The faces of a water block: towards the air, and the part of a side that
// stands above lower water next to it. Lit by the light of the open cell
// each face looks into.
void addWaterFaces(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const float top = waterHeight(g, bx, j, bz);
    // Where it meets the sea it takes the sea's colour; everywhere else,
    // falling or still, it is the same water.
    const int texture = j <= kSeaLayer ? LandTextures::SEA_WATER : LandTextures::WATER;
    std::vector<Vertex> &out = batches[static_cast<size_t>(texture)];
    for (const Face &f : kFaces)
    {
        const int ox = bx + f.normal[0], oy = j + f.normal[1], oz = bz + f.normal[2];
        const int beside = g.get(ox, oy, oz);
        if (oy < kMinY)
            continue;
        float bottom = 0.0f; // the part of a side face that shows, in blocks
        if (f.normal[1] > 0)
        {
            if (beside == WATER)
                continue;
        }
        else if (f.normal[1] < 0)
        {
            if (beside != AIR)
                continue;
        }
        else if (beside == WATER)
        {
            bottom = waterHeight(g, ox, oy, oz);
            if (bottom >= top)
                continue;
        }
        else if (beside != AIR)
            continue;

        const int lx = beside == AIR ? ox : bx, ly = beside == AIR ? oy : j,
                  lz = beside == AIR ? oz : bz;
        Vertex v;
        for (int k = 0; k < 3; ++k)
        {
            v.normal[k] = static_cast<float>(f.normal[k]);
            v.color[k] = 1.0f;
        }
        v.extra[0] = 0.0f;
        v.extra[1] = 0.0f;
        v.extra[2] = glowFactor(static_cast<float>(g.coolAt(lx, ly, lz)));
        v.extra[3] = skyFactor(static_cast<float>(g.skyAt(lx, ly, lz)));
        for (int c = 0; c < 4; ++c)
        {
            const float y = f.normal[1] > 0
                                ? top
                                : (f.normal[1] < 0 ? 0.0f : (f.corner[c][1] == 1 ? top : bottom));
            v.pos[0] = static_cast<float>(bx + f.corner[c][0]) * kBlock;
            v.pos[1] = (static_cast<float>(j) + y) * kBlock;
            v.pos[2] = static_cast<float>(bz + f.corner[c][2]) * kBlock;
            v.uv[0] = f.uv[c][0];
            // side faces show as much of the texture as they are tall
            v.uv[1] = f.normal[1] != 0 ? f.uv[c][1] : 1.0f - y;
            out.push_back(v);
        }
    }
}

// Two quads crossing diagonally, smaller than the cell and off centre.
void addPlant(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const size_t i = Grid::index(bx, j, bz);
    const int plant = g.plant[i];
    const unsigned int h = hash(bx, bz * 13 + j, 411u);
    const float tint = 0.86f + 0.14f * static_cast<float>(h % 100u) / 100.0f;
    std::vector<Vertex> &out = batches[static_cast<size_t>(plantTexture(plant))];

    Vertex v;
    v.normal[0] = 0.0f;
    v.normal[1] = 1.0f;
    v.normal[2] = 0.0f;
    v.color[0] = v.color[1] = v.color[2] = tint;
    v.extra[0] = 0.0f;
    v.extra[2] = glowFactor(static_cast<float>(g.cool[i]));
    v.extra[3] = skyFactor(static_cast<float>(g.sky[i]));

    const bool crop = plant == WHEAT;
    const float size = crop ? 2.3f : (plant == TALL_GRASS || plant == FERN ? 1.9f : 1.6f);
    const float half = size * 0.5f;
    const float cx = static_cast<float>(bx) * kBlock + 1.25f +
                     (crop ? 0.0f : (static_cast<float>((h >> 8) % 100u) / 100.0f - 0.5f) * 0.9f);
    const float cz = static_cast<float>(bz) * kBlock + 1.25f +
                     (crop ? 0.0f : (static_cast<float>((h >> 16) % 100u) / 100.0f - 0.5f) * 0.9f);
    const float y0 = static_cast<float>(j) * kBlock;
    const float sway = plant == DEAD_BUSH ? 0.0f : 0.09f;
    for (int q = 0; q < 2; ++q)
    {
        const float sx = q == 0 ? half : -half;
        const float corners[4][4] = {{cx - half, y0, cz - sx, 1.0f},
                                     {cx + half, y0, cz + sx, 1.0f},
                                     {cx + half, y0 + size, cz + sx, 0.0f},
                                     {cx - half, y0 + size, cz - sx, 0.0f}};
        for (int c = 0; c < 4; ++c)
        {
            v.pos[0] = corners[c][0];
            v.pos[1] = corners[c][1];
            v.pos[2] = corners[c][2];
            v.uv[0] = (c == 0 || c == 3) ? 0.0f : 1.0f;
            v.uv[1] = corners[c][3];
            v.extra[1] = corners[c][3] > 0.5f ? 0.0f : sway;
            out.push_back(v);
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
// Boxes, for everything that moves
// ---------------------------------------------------------------------
void setGlow(float glow)
{
    glMultiTexCoord4f(GL_TEXTURE1, glow, 0.0f, 0.0f, 1.0f);
}

void box(float x0, float y0, float z0, float x1, float y1, float z1)
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
    glVertex3f(x0, y1, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z0);
    glVertex3f(x0, y1, z0);
    glNormal3f(0, -1, 0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x0, y0, z1);
    glNormal3f(1, 0, 0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z1);
    glNormal3f(-1, 0, 0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x0, y0, z1);
    glVertex3f(x0, y1, z1);
    glVertex3f(x0, y1, z0);
    glEnd();
}

void colour(int r, int g, int b)
{
    glColor3ub(static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b));
}

// A sheep, a pig or a cow, facing +z, standing on y = 0.
void drawAnimal(int kind, float stride)
{
    const int coats[3][3] = {{236, 236, 230}, {240, 164, 170}, {92, 62, 44}};
    const int skins[3][3] = {{214, 180, 160}, {240, 164, 170}, {92, 62, 44}};
    const int *coat = coats[kind];
    const int *skin = skins[kind];
    const float fat = kind == 0 ? 0.12f : 0.0f; // wool
    // legs, swinging in pairs
    for (int leg = 0; leg < 4; ++leg)
    {
        const float x = (leg % 2 == 0) ? -0.45f : 0.2f;
        const float z = leg < 2 ? 0.75f : -0.95f;
        const float swing = ((leg == 0 || leg == 3) ? stride : -stride) * 0.25f;
        colour(skin[0] - 30, skin[1] - 30, skin[2] - 30);
        box(x, 0.0f, z + swing, x + 0.25f, 0.7f, z + swing + 0.25f);
    }
    colour(coat[0], coat[1], coat[2]);
    box(-0.55f - fat, 0.65f, -1.1f - fat, 0.55f + fat, 1.5f + fat, 1.1f + fat);
    if (kind == 2)
    {
        // a cow's white patches
        colour(240, 238, 232);
        box(-0.56f, 0.9f, -0.5f, 0.56f, 1.51f, 0.2f);
        box(-0.3f, 1.2f, 0.6f, 0.57f, 1.52f, 1.0f);
    }
    colour(skin[0], skin[1], skin[2]);
    box(-0.35f, 1.0f, 1.1f, 0.35f, 1.7f, 1.7f); // head
    colour(30, 30, 34);
    box(-0.36f, 1.4f, 1.55f, -0.24f, 1.52f, 1.71f); // eyes
    box(0.24f, 1.4f, 1.55f, 0.36f, 1.52f, 1.71f);
    if (kind == 1)
    {
        colour(250, 190, 196);
        box(-0.16f, 1.12f, 1.7f, 0.16f, 1.34f, 1.8f); // snout
    }
    if (kind == 2)
    {
        colour(226, 220, 200);
        box(-0.45f, 1.7f, 1.3f, -0.33f, 1.9f, 1.42f); // horns
        box(0.33f, 1.7f, 1.3f, 0.45f, 1.9f, 1.42f);
    }
}

// A hot-air balloon: a stepped envelope in two colours, ropes and a basket.
void drawBalloon(const int a[3], const int b[3])
{
    const float rings[7][2] = {{2.2f, 0.0f}, {4.2f, 1.6f},  {5.6f, 3.6f}, {6.0f, 6.0f},
                               {5.4f, 8.6f}, {4.0f, 10.8f}, {2.2f, 12.4f}};
    for (int i = 0; i < 7; ++i)
    {
        const int *c = i % 2 == 0 ? a : b;
        colour(c[0], c[1], c[2]);
        const float r = rings[i][0];
        const float top = i < 6 ? rings[i + 1][1] : 13.6f;
        box(-r, rings[i][1], -r, r, top, r);
    }
    colour(70, 50, 34);
    for (int k = 0; k < 4; ++k)
    {
        const float x = (k % 2 == 0) ? -1.1f : 1.0f;
        const float z = k < 2 ? -1.1f : 1.0f;
        box(x, -3.4f, z, x + 0.1f, 0.0f, z + 0.1f);
    }
    colour(150, 108, 60);
    box(-1.3f, -5.0f, -1.3f, 1.3f, -3.4f, 1.3f);
    colour(255, 200, 90);
    setGlow(1.2f);
    box(-0.4f, -1.2f, -0.4f, 0.4f, -0.5f, 0.4f); // the burner
    setGlow(0.0f);
}

} // namespace

// ---------------------------------------------------------------------
// LandWorld
// ---------------------------------------------------------------------
struct LandWorld::Scene
{
    Grid grid;
    Extras extras;
    bool built = false;
    unsigned int solidList = 0;
    unsigned int plantList = 0;
    unsigned int waterList = 0; // see-through, drawn last (drawWater)
};

LandWorld::LandWorld()
{
}

LandWorld::~LandWorld()
{
}

void LandWorld::releaseResources()
{
    // the grids are rebuilt with the lists; that takes a fraction of a second
    m_scenery.reset();
    m_flat.reset();
    m_textures.clear();
}

void LandWorld::shadowBounds(float lo[3], float hi[3])
{
    lo[0] = -kIslandHalf - 2.0f;
    lo[1] = static_cast<float>(kMinY) * kBlock;
    lo[2] = -kIslandHalf - 2.0f;
    hi[0] = kIslandHalf + 2.0f;
    hi[1] = static_cast<float>(kMaxY + 1) * kBlock;
    hi[2] = kIslandHalf + 2.0f;
}

int LandWorld::rideObstructions(const std::vector<float> &path)
{
    Grid g;
    Extras extras;
    buildScenery(g, extras);

    // The room the train takes: 3 units either side of the centre line, from
    // just under the sleepers to above the riders' raised hands. Plants are
    // smaller than their cell but sway.
    const float trainReach = 3.0f;
    const float plantReach = 2.2f;
    int count = 0;
    for (size_t i = 0; i + 2 < path.size(); i += 3)
    {
        const float x = path[i], y = path[i + 1], z = path[i + 2];
        bool hit = false;
        for (int bx = blockOf(x - 4.0f); bx <= blockOf(x + 4.0f) && !hit; ++bx)
            for (int bz = blockOf(z - 4.0f); bz <= blockOf(z + 4.0f) && !hit; ++bz)
                for (int j = blockOf(y - 0.3f); j <= blockOf(y + 5.0f) && !hit; ++j)
                {
                    if (!Grid::inside(bx, j, bz))
                        continue;
                    const size_t cell = Grid::index(bx, j, bz);
                    const bool plant = g.block[cell] == AIR && g.plant[cell] != NO_PLANT;
                    if (g.block[cell] == AIR && !plant)
                        continue;
                    const float inset = plant ? 0.3f : 0.0f;
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

int LandWorld::waterNearRide(const std::vector<float> &path)
{
    Grid g;
    Extras extras;
    buildScenery(g, extras);
    // the same room the scenery keeps free of water, round the given path
    const std::vector<unsigned char> zone = waterZone(path);
    int count = 0;
    for (size_t i = 0; i < zone.size(); ++i)
        if (zone[i] && g.block[i] == WATER)
            ++count;
    return count;
}

LandWorld::Scene &LandWorld::scene(bool flat)
{
    std::unique_ptr<Scene> &slot = flat ? m_flat : m_scenery;
    if (!slot)
        slot.reset(new Scene);
    Scene &s = *slot;
    if (s.built)
        return s;

    if (m_textures.empty())
    {
        m_textures.assign(LandTextures::TEXTURE_COUNT, 0);
        for (int t = 0; t < LandTextures::TEXTURE_COUNT; ++t)
            m_textures[static_cast<size_t>(t)] =
                PixelArt::upload(LandTextures::make(t), LandTextures::repeatsVertically(t));
    }

    if (flat)
        buildFlat(s.grid);
    else
        buildScenery(s.grid, s.extras);

    Batches solids(LandTextures::TEXTURE_COUNT);
    Batches plants(LandTextures::TEXTURE_COUNT);
    Batches water(LandTextures::TEXTURE_COUNT);
    for (int bx = -kHalf; bx < kHalf; ++bx)
        for (int bz = -kHalf; bz < kHalf; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const size_t cell = Grid::index(bx, j, bz);
                if (s.grid.block[cell] == WATER)
                    addWaterFaces(s.grid, bx, j, bz, water);
                else if (s.grid.block[cell] != AIR)
                    addBlockFaces(s.grid, bx, j, bz, solids);
                else if (s.grid.plant[cell] != NO_PLANT)
                    addPlant(s.grid, bx, j, bz, plants);
            }
    s.solidList = compileList(solids, m_textures);
    s.plantList = compileList(plants, m_textures);
    s.waterList = compileList(water, m_textures);
    s.built = true;
    return s;
}

void LandWorld::draw(bool flat, bool shadowPass, float time)
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    const Scene &s = scene(flat);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glCallList(s.solidList);
    if (!shadowPass)
    {
        // the fixed pipeline cuts the plants out here; the shader does it itself
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        glCallList(s.plantList);
        glDisable(GL_ALPHA_TEST);
        if (!flat)
            drawMoving(s, time);
    }
    setGlow(0.0f);
    glPopAttrib();
}

void LandWorld::drawWater(bool flat)
{
    if (!(flat ? m_flat : m_scenery) || !(flat ? m_flat : m_scenery)->built)
        return;
    const Scene &s = flat ? *m_flat : *m_scenery;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT |
                 GL_DEPTH_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    // First only the depth of the water, then its colour where it is the
    // nearest surface: so only the front face of a body of water is blended
    // over what is behind it, and the faces inside it and at its back (the
    // planes between the blocks) do not show through. The target's alpha is
    // left as it was for the post effects.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glCallList(s.waterList);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glCallList(s.waterList);
    setGlow(0.0f);
    glPopAttrib();
}

void LandWorld::drawMoving(const Scene &s, float time)
{
    const Grid &g = s.grid;

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);

    // the windmill's sails
    if (s.extras.hasMill)
    {
        glPushMatrix();
        glTranslatef(s.extras.mill[0], s.extras.mill[1], s.extras.mill[2]);
        colour(96, 70, 44);
        box(-0.5f, -0.5f, -1.2f, 0.5f, 0.5f, 0.3f); // the shaft
        glRotatef(time * 22.0f, 0.0f, 0.0f, 1.0f);
        for (int blade = 0; blade < 4; ++blade)
        {
            glPushMatrix();
            glRotatef(90.0f * static_cast<float>(blade), 0.0f, 0.0f, 1.0f);
            colour(120, 90, 54);
            box(-0.22f, 0.0f, -0.05f, 0.22f, 11.5f, 0.3f);
            colour(240, 236, 224);
            box(0.22f, 2.0f, 0.0f, 2.9f, 11.0f, 0.16f);
            glPopMatrix();
        }
        glPopMatrix();
    }

    // animals wander around their spot, facing the way they walk
    for (const Animal &a : s.extras.animals)
    {
        const float t = time * 0.16f + a.phase;
        const float x = a.x + sinf(t) * 4.5f + sinf(t * 0.37f + a.phase) * 2.0f;
        const float z = a.z + cosf(t * 0.83f) * 4.5f;
        const float vx = cosf(t) * 4.5f + cosf(t * 0.37f + a.phase) * 0.74f;
        const float vz = -sinf(t * 0.83f) * 3.7f;
        const int bx = blockOf(x), bz = blockOf(z);
        if (!Grid::insideColumn(bx, bz))
            continue;
        const float ground = static_cast<float>(g.top(bx, bz) + 1) * kBlock;
        glPushMatrix();
        glTranslatef(x, ground, z);
        glRotatef(atan2f(vx, vz) * 57.29578f, 0.0f, 1.0f, 0.0f);
        glScalef(1.25f, 1.25f, 1.25f);
        drawAnimal(a.kind, sinf(time * 5.0f + a.phase));
        glPopMatrix();
    }

    // blocky clouds drifting east, high above the peaks
    setGlow(0.12f);
    colour(250, 252, 255);
    for (int i = 0; i < 16; ++i)
    {
        const float span = 1400.0f;
        float x = static_cast<float>(hash(i, 0, 441u) % 1400u) +
                  time * (3.0f + static_cast<float>(i % 3));
        x = x - floorf(x / span) * span - span * 0.5f;
        const float z = static_cast<float>(hash(i, 1, 441u) % 1300u) - 650.0f;
        const float y = 165.0f + static_cast<float>(hash(i, 2, 441u) % 50u);
        for (int part = 0; part < 4; ++part)
        {
            const float px = x + static_cast<float>(hash(i, part, 443u) % 40u) - 20.0f;
            const float pz = z + static_cast<float>(hash(i, part, 445u) % 30u) - 15.0f;
            const float w = 14.0f + static_cast<float>(hash(i, part, 447u) % 5u) * 6.0f;
            const float d = 10.0f + static_cast<float>(hash(i, part, 449u) % 4u) * 6.0f;
            box(px - w, y, pz - d, px + w, y + 6.0f + static_cast<float>(part % 2) * 4.0f, pz + d);
        }
    }
    setGlow(0.0f);

    // hot-air balloons, bobbing where the ride gets a good look at them
    const float spots[3][3] = {
        {-10.0f, 104.0f, 84.0f}, {150.0f, 62.0f, 96.0f}, {30.0f, 58.0f, -150.0f}};
    const int paints[3][2][3] = {{{224, 60, 50}, {250, 240, 220}},
                                 {{50, 120, 220}, {250, 206, 60}},
                                 {{240, 150, 40}, {120, 60, 160}}};
    for (int i = 0; i < 3; ++i)
    {
        const float fi = static_cast<float>(i);
        glPushMatrix();
        glTranslatef(spots[i][0] + sinf(time * 0.11f + fi * 2.0f) * 9.0f,
                     spots[i][1] + sinf(time * 0.4f + fi) * 2.5f,
                     spots[i][2] + cosf(time * 0.09f + fi) * 9.0f);
        glRotatef(time * 4.0f + fi * 40.0f, 0.0f, 1.0f, 0.0f);
        drawBalloon(paints[i][0], paints[i][1]);
        glPopMatrix();
    }
}

void LandWorld::drawSupports(const std::vector<float> &rail, bool shadowPass)
{
    if (!m_scenery || !m_scenery->built)
        return;
    const Grid &g = m_scenery->grid;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    if (!shadowPass)
    {
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBindTexture(GL_TEXTURE_2D, m_textures[LandTextures::OAK_SIDE]);
        glColor3f(0.92f, 0.9f, 0.86f);
    }
    setGlow(0.0f);

    // The lowest the track passes over every column, so that a trestle is
    // not stood on a stretch of track running underneath.
    std::vector<float> lowest(static_cast<size_t>(kSizeXZ * kSizeXZ), 1.0e9f);
    for (size_t i = 0; i + 2 < rail.size(); i += 3)
        for (int bx = blockOf(rail[i] - 4.0f); bx <= blockOf(rail[i] + 4.0f); ++bx)
            for (int bz = blockOf(rail[i + 2] - 4.0f); bz <= blockOf(rail[i + 2] + 4.0f); ++bz)
                if (Grid::insideColumn(bx, bz))
                {
                    float &low = lowest[Grid::column(bx, bz)];
                    low = fminf(low, rail[i + 1]);
                }

    const float post = 0.28f;
    const size_t count = rail.size() / 3;
    for (size_t n = 0; n < count; ++n)
    {
        const float x = rail[3 * n], y = rail[3 * n + 1], z = rail[3 * n + 2];
        const int bx = blockOf(x), bz = blockOf(z);
        if (!Grid::insideColumn(bx, bz) || lowest[Grid::column(bx, bz)] < y - 3.0f)
            continue;
        // the ground under the track here
        int j = blockOf(y - 0.7f);
        j = j > kMaxY ? kMaxY : j;
        while (j >= kMinY && (g.get(bx, j, bz) == AIR || isGrowth(g.get(bx, j, bz))))
            --j;
        if (j < kMinY || g.get(bx, j, bz) == LAVA)
            continue;
        const float ground = static_cast<float>(j + 1) * kBlock;
        const float top = y - 0.55f;
        if (top - ground < 1.2f)
            continue;
        // across the track: horizontal, at right angles to the way it runs
        const size_t next = (n + 1) % count;
        float ax = rail[3 * next] - x, az = rail[3 * next + 2] - z;
        const float length = sqrtf(ax * ax + az * az);
        if (length < 1.0e-4f)
            continue;
        ax /= length;
        az /= length;
        glPushMatrix();
        glTranslatef(x, 0.0f, z);
        glRotatef(atan2f(ax, az) * 57.29578f, 0.0f, 1.0f, 0.0f);
        const float height = top - ground;
        // Two posts that lean in towards the top, tied by cross beams.
        for (int side = -1; side <= 1; side += 2)
        {
            const float s = static_cast<float>(side);
            const float foot = s * (1.5f + fminf(height * 0.06f, 2.0f));
            const float head = s * 1.3f;
            glBegin(GL_QUADS);
            const float xs[2] = {foot, head};
            for (int face = 0; face < 4; ++face)
            {
                // corners of the post's square section, going round
                const float cx[4] = {-post, post, post, -post};
                const float cz[4] = {-post, -post, post, post};
                const int a = face, b = (face + 1) % 4;
                const float nx = face == 1 ? 1.0f : (face == 3 ? -1.0f : 0.0f);
                const float nz = face == 0 ? -1.0f : (face == 2 ? 1.0f : 0.0f);
                glNormal3f(nx, 0.0f, nz);
                glTexCoord2f(0.0f, height / 5.0f);
                glVertex3f(xs[0] + cx[a], ground, cz[a]);
                glTexCoord2f(0.0f, 0.0f);
                glVertex3f(xs[1] + cx[a], top, cz[a]);
                glTexCoord2f(0.25f, 0.0f);
                glVertex3f(xs[1] + cx[b], top, cz[b]);
                glTexCoord2f(0.25f, height / 5.0f);
                glVertex3f(xs[0] + cx[b], ground, cz[b]);
            }
            glEnd();
        }
        for (float h = top - 0.3f; h > ground + 2.0f; h -= 6.0f)
        {
            const float t = (h - ground) / height;
            const float reach = 1.3f + (1.0f - t) * (0.2f + fminf(height * 0.06f, 2.0f)) + post;
            box(-reach, h - 0.4f, -0.2f, reach, h, 0.2f);
        }
        glPopMatrix();
    }
    glPopAttrib();
}
