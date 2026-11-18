/************************************************************************
     File:        LandWorld.cpp

     Comment:     See LandWorld.h.

*************************************************************************/

#include <math.h>
#include <stdlib.h>
#include <deque>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include "LandWorld.h"
#include "Frustum.h"
#include "LandTextures.h"
#include "OceanWorld.h"
#include "PixelArt.h"
#include "WaterFlow.h"
#include "WorldShape.h"

namespace
{

using PixelArt::hash;

// ---------------------------------------------------------------------
// World layout
// ---------------------------------------------------------------------
const float kBlock = 2.5f;
const float kScale = WorldShape::kScale; // see WorldShape.h
const float kLift = WorldShape::kLift;
// The editor's flat island keeps the first size: +-100 units.
const float kFlatHalf = 100.0f;
// The highest layer of blocks that touches the sea (the sea is at y = -1;
// layer 0 spans 0 to 2.5): water in it is coloured like the sea.
const int kSeaLayer = 0;
// The grid's columns: the main island reaches at most
// WorldShape::kIslandReach (278 units, 111 blocks) from the middle, the
// north-east land (WorldShape.h) out to x = 525, z = -525; two more rings of
// open air round them, over the sea, for water running off the land to
// fall through. Blocks x from kMinBX up to (not including) kMaxBX, and z.
const int kMinBX = -113, kMaxBX = 212;
const int kMinBZ = -212, kMaxBZ = 113;
const int kSizeX = kMaxBX - kMinBX;
const int kSizeZ = kMaxBZ - kMinBZ;
// The grid's extent in world units, x and z.
const float kGridLo[2] = {static_cast<float>(kMinBX) * 2.5f, static_cast<float>(kMinBZ) * 2.5f};
const float kGridHi[2] = {static_cast<float>(kMaxBX) * 2.5f, static_cast<float>(kMaxBZ) * 2.5f};
const int kMinY = -2; // block layer j spans y = 2.5 j .. 2.5 j + 2.5
const int kMaxY = 90; // the jagged peaks top out near y = 220
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
    ICE,    // packed ice: the ice spikes
    JACK_O, // a carved pumpkin lantern, lit
    MUD,    // the mangrove swamp's ground
    MANGROVE_LOG,
    LEAVES_MANGROVE,
    LEAVES_BAMBOO, // the canopy over a bamboo thicket
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
    PETALS, // fallen cherry blossom
    OXEYE,
    ALLIUM,
    TULIP_ORANGE,
    BAMBOO,     // a stalk; they stack into tall canes
    BAMBOO_TOP, // the leafy top of a cane
    VINES,      // hanging under the mangroves' crowns
    SNOW_LAYER, // fallen snow, 1 to 8 eighths of a block deep (Grid::level)
    PLANT_COUNT
};

enum Biome
{
    MEADOW = 0,
    ALPINE,
    VOLCANO,
    MESA,
    CRAG,
    BAMBOO_JUNGLE, // the north-east land's hills
    MANGROVE       // the north-east land's swamp, facing the strait
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
    case ICE:
        return T::ICE;
    case JACK_O:
        return face == 0 || face == 1 ? T::PUMPKIN_TOP : T::PUMPKIN_FACE;
    case MUD:
        return T::MUD;
    case MANGROVE_LOG:
        return face == 2 ? T::MANGROVE_SIDE : T::MANGROVE_TOP;
    case LEAVES_MANGROVE:
        return T::LEAVES_MANGROVE;
    case LEAVES_BAMBOO:
        return T::LEAVES_BAMBOO;
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
           block == MUSHROOM_CAP || block == MUSHROOM_STEM || block == MANGROVE_LOG ||
           block == LEAVES_MANGROVE || block == LEAVES_BAMBOO;
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
    case JACK_O:
        return 0.6f;
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

// Is this column part of the island, not the sea round it?
bool onIsland(int bx, int bz)
{
    return WorldShape::inland(centerOf(bx), centerOf(bz)) > 0.0f;
}

// ... of the editor's flat island?
bool onFlatIsland(int bx, int bz)
{
    return fabsf(centerOf(bx)) < kFlatHalf && fabsf(centerOf(bz)) < kFlatHalf;
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
// The mountains, the volcano, the crags, the canyon and the lava rivers are
// given in the units of the first layout (WorldShape.h): siteAt() looks
// them up at the world position divided by kScale, and lifts the heights.
const float kAlpineX = -56.0f, kAlpineZ = 24.0f, kAlpineRadius = 56.0f, kAlpineHeight = 112.0f;
const float kVolcanoX = 58.0f, kVolcanoZ = 58.0f, kVolcanoRadius = 44.0f, kVolcanoHeight = 66.0f;
const float kCraterRadius = 9.5f;
const int kLavaLakeTop = 28; // the lava lake fills the crater up to this layer (y = 72.5)

// The crags in the north east: x, z, radius, height.
const float kCrags[2][4] = {{48.0f, -72.0f, 27.0f, 54.0f}, {78.0f, -50.0f, 27.0f, 64.0f}};

// The canyon runs from the south shore north into the badlands.
const float kCanyon[] = {8.0f, 112.0f, 9.0f, 84.0f, 14.0f, 64.0f, 20.0f, 44.0f, 19.0f, 27.0f};
const int kCanyonPoints = 5;

// Lava rivers from the crater rim to the shore.
const float kLavaSouth[] = {58.0f, 67.5f, 61.0f, 80.0f, 57.0f, 92.0f, 58.0f, 103.0f};
const float kLavaEast[] = {67.5f, 58.0f, 82.0f, 63.0f, 93.0f, 57.0f, 103.0f, 58.0f};

// The rest is in world units and blocks.
// The waterfall on the north side of the snow mountain, where no track runs:
// the blocks of the lip (x from kFallX0 to kFallX1, z = kFallLipZ), the
// height it falls and the river from its pool to the north shore (x, z,
// in the first layout's units).
const int kFallX0 = -52, kFallX1 = -44, kFallLipZ = 61;
const int kFallDrop = 14;
const float kRiver[] = {-47.0f, 80.0f, -51.0f, 90.0f, -48.0f, 101.0f};

// The village green in the north west, the meadow ponds and the island
// that floats above the first one.
const float kVillageX = -160.0f, kVillageZ = -165.0f, kVillageRadius = 55.0f;
const int kVillageTop = 2; // the green's top layer (y = 7.5)
const float kPondX = 127.5f, kPondZ = -90.0f, kPondRadius = 11.0f;
const int kFloatX = 41, kFloatZ = -37, kFloatTop = 38; // blocks; top at y = 97.5

// A line of the first layout (x, z pairs), in world units.
std::vector<float> scaledLine(const float *points, int count)
{
    std::vector<float> out(points, points + 2 * count);
    for (float &v : out)
        v *= kScale;
    return out;
}

struct Site
{
    float height;
    int biome;
};

// The land at (x, z) of the first layout, coast units from the shore.
Site firstSite(float x, float z, float coast)
{
    Site site;
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
            // jagged peaks: sharp ridges running out from the summit, with
            // crags along them, rather than one smooth cone
            const float ridge = 1.0f - fabsf(valueNoise(x * 0.07f, z * 0.07f, 15u) * 2.0f - 1.0f);
            const float crags = valueNoise(x * 0.22f, z * 0.22f, 16u) - 0.5f;
            const float h = kAlpineHeight * (1.0f - r) *
                                (0.72f + 0.16f * valueNoise(x * 0.035f, z * 0.035f, 13u) +
                                 0.34f * ridge * ridge) +
                            crags * 9.0f * (1.0f - r);
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

// The land at world position (x, z): the first layout scaled up, with finer
// knolls and hollows on the meadows, mountains and crags that the first
// layout was too small to hold (the badlands keep their level steps, the
// volcano its smooth cone).
// The north-east land at world position (x, z): mangrove swamp at sea
// level on the side facing the strait, rising to bamboo-clad hills to the
// north-east.
Site northEastSite(float x, float z)
{
    Site site;
    const float coast = WorldShape::inlandNorthEast(x, z);
    const float dx = x - WorldShape::kNorthEastX, dz = z - WorldShape::kNorthEastZ;
    // how far towards the north-east: -1 at the strait, +1 on the far shore
    const float along = (dx - dz) * 0.7071f / WorldShape::kNorthEastRadius;
    const float hills =
        fbm(x * 0.012f, z * 0.012f, 711u) * 0.7f + valueNoise(x * 0.045f, z * 0.045f, 713u) * 0.3f;
    const float swamp = 0.4f + (valueNoise(x * 0.05f, z * 0.05f, 715u) - 0.5f) * 3.0f;
    const float high = 4.0f + ramp(4.0f, 45.0f, coast) * (10.0f + hills * 42.0f);
    const float t = ramp(-0.35f, 0.05f, along); // 0 swamp, 1 hills
    site.height = swamp + (high - swamp) * t;
    site.biome = t < 0.5f ? MANGROVE : BAMBOO_JUNGLE;
    if (coast < 6.0f)
        site.height = fminf(site.height, 1.5f + coast * 0.3f); // a low shore
    return site;
}

Site siteAt(float x, float z)
{
    if (WorldShape::inlandNorthEast(x, z) > WorldShape::inlandMain(x, z))
        return northEastSite(x, z);
    const float coast = WorldShape::inland(x, z) / kScale;
    Site site = firstSite(x / kScale, z / kScale, coast);
    site.height = WorldShape::lifted(site.height);
    if (site.height > 3.0f && (site.biome == MEADOW || site.biome == ALPINE || site.biome == CRAG))
        site.height += (fbm(x * 0.05f, z * 0.05f, 81u) - 0.5f) * (3.0f + site.height * 0.06f) +
                       (valueNoise(x * 0.16f, z * 0.16f, 83u) - 0.5f) * 1.6f;
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
    std::vector<unsigned char> level;  // water: 0 source, 1..7 flowing, 8 falling;
                                       // a snow layer: its depth in eighths
    std::vector<unsigned char> biome;  // per column

    Grid()
        : block(static_cast<size_t>(kSizeX) * kSizeY * kSizeZ, AIR), plant(block.size(), NO_PLANT),
          sky(block.size(), 0), warm(block.size(), 0), cool(block.size(), 0),
          carved(block.size(), 0), level(block.size(), 0),
          biome(static_cast<size_t>(kSizeX) * kSizeZ, MEADOW)
    {
    }

    static bool insideColumn(int bx, int bz)
    {
        return bx >= kMinBX && bx < kMaxBX && bz >= kMinBZ && bz < kMaxBZ;
    }
    static bool inside(int bx, int j, int bz)
    {
        return insideColumn(bx, bz) && j >= kMinY && j <= kMaxY;
    }
    static size_t index(int bx, int j, int bz)
    {
        return (static_cast<size_t>(bx - kMinBX) * kSizeY + static_cast<size_t>(j - kMinY)) *
                   kSizeZ +
               static_cast<size_t>(bz - kMinBZ);
    }
    static size_t column(int bx, int bz)
    {
        return static_cast<size_t>(bx - kMinBX) * kSizeZ + static_cast<size_t>(bz - kMinBZ);
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
        : low(static_cast<size_t>(kSizeX) * kSizeZ, 1.0e9f), high(low.size(), -1.0e9f),
          nearLow(low.size(), 1.0e9f), nearHigh(low.size(), -1.0e9f)
    {
    }
    void add(const std::vector<float> &ride)
    {
        const float wide = 9.0f;
        for (size_t i = 0; i + 2 < ride.size(); i += 3)
        {
            const float x = ride[i], y = ride[i + 1], z = ride[i + 2];
            if (x < kGridLo[0] - wide || x > kGridHi[0] + wide || z < kGridLo[1] - wide ||
                z > kGridHi[1] + wide)
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
    // the cherry grove round the lake: its middle and how high its ground
    // is, for the falling petals
    float grove[3] = {0.0f, 0.0f, 0.0f};
    bool hasGrove = false;
    // the flower meadow round the giant oak
    float meadow[2] = {0.0f, 0.0f};
    bool hasMeadow = false;
};

const float kGroveRadius = 75.0f; // world units
const float kMeadowRadius = 85.0f;

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
    std::vector<int> tops(static_cast<size_t>(kSizeX) * kSizeZ, 0);
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            const Site site = siteAt(centerOf(bx), centerOf(bz));
            int top = static_cast<int>(floorf(site.height / kBlock + 0.5f)) - 1;
            top = top < kMinY ? kMinY : (top > kMaxY - 1 ? kMaxY - 1 : top);
            tops[Grid::column(bx, bz)] = top;
            g.biome[Grid::column(bx, bz)] = static_cast<unsigned char>(site.biome);
        }

    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            if (!onIsland(bx, bz))
                continue; // the ring over the sea stays open
            const float cx = centerOf(bx), cz = centerOf(bz);
            const int top = tops[Grid::column(bx, bz)];
            const int biome = g.biome[Grid::column(bx, bz)];
            const float topY = static_cast<float>(top + 1) * kBlock;
            const float coast = WorldShape::inland(cx, cz);

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
                    const float snowLine = (56.0f + patch * 10.0f) * kLift;
                    if (topY > snowLine)
                        b = depth < 2 ? SNOW : stoneOrOre(bx, j, bz, false);
                    else if (topY > 34.0f * kLift || steep >= 3)
                        b = (depth == 0 && steep < 2 && patch > 0.55f)
                                ? (topY > 44.0f * kLift ? SNOWY_DIRT : GRASS)
                                : stoneOrOre(bx, j, bz, false);
                    else
                        b = depth == 0 ? (topY > 26.0f * kLift && patch > 0.4f ? SNOWY_DIRT : GRASS)
                                       : (depth < 3 ? DIRT : stoneOrOre(bx, j, bz, false));
                    break;
                }
                case VOLCANO:
                    if (topY < 9.0f * kLift && patch > 0.45f)
                        b = depth == 0 ? GRASS : (depth < 2 ? DIRT : BASALT);
                    else if (depth == 0 && patch > 0.7f)
                        b = GRAVEL; // ash
                    else
                        b = BASALT;
                    break;
                case MESA:
                    if (topY <= 0.0f)
                        b = depth == 0 ? (patch > 0.5f ? GRAVEL : SAND) : STONE; // the canyon bed
                    else if (depth == 0 && steep < 2 && topY > 12.0f * kLift)
                        b = RED_SAND;
                    else
                        b = clayBand(j);
                    break;
                case CRAG:
                    // the high crags' tops are bare rock under a layer of
                    // snow (snowfall()), not blocks of it
                    if (steep < 2 && topY < 26.0f * kLift)
                        b = depth == 0 ? GRASS : (depth < 2 ? DIRT : stoneOrOre(bx, j, bz, true));
                    else if (depth == 0 && patch > 0.62f && topY <= 44.0f * kLift)
                        b = MOSSY;
                    else
                        b = stoneOrOre(bx, j, bz, true);
                    break;
                case MANGROVE:
                    b = depth < 3 ? MUD : (depth < 5 ? DIRT : STONE);
                    break;
                case BAMBOO_JUNGLE:
                    if (steep >= 4 && depth == 0)
                        b = MOSSY;
                    else
                        b = depth == 0 ? GRASS : (depth < 3 ? DIRT : stoneOrOre(bx, j, bz, false));
                    break;
                default:
                    if (topY <= 2.5f && coast < 14.0f)
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
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            const float dx = centerOf(bx) / kScale - kVolcanoX;
            const float dz = centerOf(bz) / kScale - kVolcanoZ;
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
// a stream nine blocks wide runs out of the rock to a sheer stone lip and
// falls kFallDrop blocks into a pool; the gorge opens north and a river runs
// from the pool down to the shore and into the sea. Only the sources are put
// here; the fall itself is the water flowing over the lip (spreadWater).
void buildWaterfall(Grid &g)
{
    const int cx = (kFallX0 + kFallX1) / 2;
    const int lip = g.top(cx, kFallLipZ);
    const int pool = lip - kFallDrop;
    const int width = (kFallX1 - kFallX0) / 2; // half the fall's width

    // The gorge: open down to the pool from the cliff out to the north, wider
    // further out, its walls bare stone with the grass left on top.
    for (int bz = kFallLipZ + 1; bz <= kFallLipZ + 22; ++bz)
    {
        const int half = width + 3 + (bz - kFallLipZ) / 3;
        for (int bx = cx - half - 3; bx <= cx + half + 3; ++bx)
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
    for (int bx = cx - width - 6; bx <= cx + width + 6; ++bx)
        for (int bz = kFallLipZ - 5; bz <= kFallLipZ; ++bz)
        {
            const int top = g.top(bx, bz);
            for (int j = pool; j <= (top > lip ? top : lip); ++j)
                g.put(bx, j, bz, hash(bx * 5 + j, bz, 455u) % 5u == 0 ? MOSSY : STONE);
        }
    // The stream: sources in a channel cut into the rock above the lip.
    for (int bx = kFallX0; bx <= kFallX1; ++bx)
        for (int bz = kFallLipZ - 4; bz <= kFallLipZ; ++bz)
        {
            g.clearAbove(bx, lip, bz);
            g.put(bx, lip, bz, WATER);
        }
    // The pool, round and a block deep, under the fall.
    const float poolRadius = static_cast<float>(width + 4);
    for (int bx = cx - width - 5; bx <= cx + width + 5; ++bx)
        for (int bz = kFallLipZ + 1; bz <= kFallLipZ + 14; ++bz)
        {
            const float dx = static_cast<float>(bx - cx),
                        dz = static_cast<float>(bz - kFallLipZ - 6) * 0.8f;
            if (dx * dx + dz * dz > poolRadius * poolRadius)
                continue;
            g.put(bx, pool, bz, WATER);
            g.put(bx, pool - 1, bz, GRAVEL);
        }
    // And the river from the pool to the sea.
    const std::vector<float> river =
        scaledLine(kRiver, static_cast<int>(sizeof(kRiver) / sizeof(kRiver[0]) / 2));
    const float start[] = {centerOf(cx), centerOf(kFallLipZ + 12), river[0], river[1]};
    buildRiver(g, start, 2, pool, 3);
    buildRiver(g, river.data(), static_cast<int>(river.size() / 2), pool, 3);
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
    std::vector<unsigned char> zone(static_cast<size_t>(kSizeX) * kSizeY * kSizeZ, 0);
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
    WaterFlow::Field field(kSizeX, kSizeY, kSizeZ);
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const size_t i = Grid::index(bx, j, bz);
                const int x = bx - kMinBX, y = j - kMinY, z = bz - kMinBZ;
                const int b = g.block[i];
                if (b == WATER)
                    field.addSource(x, y, z);
                else if (b != AIR || g.carved[i])
                    field.setCell(x, y, z, WaterFlow::SOLID);
                else if (j <= -1)
                    field.setCell(x, y, z, WaterFlow::SINK);
            }
    field.spread();
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const int x = bx - kMinBX, y = j - kMinY, z = bz - kMinBZ;
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

// A hot spring on the badlands, after the user's reference: a round pool
// of clear water in bands of colour, yellow then orange then red, out to the
// red sand, on a level shelf.
void buildSpringPool(Grid &g, float x, float z, float radius)
{
    const int level = g.top(blockOf(x), blockOf(z));
    const int rings[3] = {CLAY_YELLOW, CLAY_ORANGE, CLAY_RED};
    for (int bx = blockOf(x - radius - 8.0f); bx <= blockOf(x + radius + 8.0f); ++bx)
        for (int bz = blockOf(z - radius - 8.0f); bz <= blockOf(z + radius + 8.0f); ++bz)
        {
            if (!Grid::insideColumn(bx, bz))
                continue;
            const float dx = centerOf(bx) - x, dz = centerOf(bz) - z;
            const float d = sqrtf(dx * dx + dz * dz) + (lattice(bx, bz, 497u) - 0.5f) * 1.5f;
            if (d > radius + 7.5f)
                continue;
            g.clearAbove(bx, level, bz);
            for (int j = kMinY; j <= level; ++j)
                if (g.get(bx, j, bz) == AIR)
                    g.put(bx, j, bz, CLAY_ORANGE);
            if (d <= radius)
            {
                g.put(bx, level, bz, WATER);
                g.put(bx, level - 1, bz, d < radius * 0.6f ? WATER : CLAY_WHITE);
                g.put(bx, level - 2, bz, CLAY_WHITE);
            }
            else
                g.put(bx, level, bz,
                      d < radius + 7.0f ? rings[static_cast<int>((d - radius) / 2.5f)] : RED_SAND);
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
    const float radius = 7.5f; // in blocks
    for (int dx = -9; dx <= 9; ++dx)
        for (int dz = -9; dz <= 9; ++dz)
        {
            const float d =
                sqrtf(static_cast<float>(dx * dx + dz * dz)) + (lattice(dx, dz, 67u) - 0.5f) * 1.4f;
            if (d > radius)
                continue;
            const int depth = 1 + static_cast<int>((1.0f - (d / radius) * (d / radius)) * 10.0f);
            for (int k = 0; k < depth; ++k)
                g.put(kFloatX + dx, kFloatTop - k, kFloatZ + dz,
                      k == 0 ? GRASS
                             : (k < 2 ? DIRT : (oneIn(dx + k, dz, 69u, 9) ? ORE_GOLD : STONE)));
        }
    // the spring: sources in a channel to the east edge; the water runs over
    // the edge and falls (spreadWater)
    for (int k = 3; k <= 7; ++k)
        g.put(kFloatX + k, kFloatTop, kFloatZ, WATER);
    g.put(kFloatX + 7, kFloatTop - 1, kFloatZ, STONE);
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
        if (g.get(x1 + 1, floor, (z0 + z1) / 2 + 1) == AIR)
            g.put(x1 + 1, floor, (z0 + z1) / 2 + 1, JACK_O); // a pumpkin lantern by the door
    }
    else
    {
        g.put((x0 + x1) / 2, floor, z1, DOOR);
        g.put((x0 + x1) / 2, floor + 1, z1, PLANKS);
        if (g.get((x0 + x1) / 2 + 1, floor, z1 + 1) == AIR)
            g.put((x0 + x1) / 2 + 1, floor, z1 + 1, JACK_O);
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

// Is the box of cells clear of the train (and inside the island)?
bool roomForBuilding(const Clearance &clear, int x0, int x1, int j0, int j1, int z0, int z1)
{
    for (int bx = x0; bx <= x1; ++bx)
        for (int bz = z0; bz <= z1; ++bz)
        {
            if (!Grid::insideColumn(bx, bz))
                return false;
            for (int j = j0; j <= j1; ++j)
                if (clear.blocks(bx, j, bz))
                    return false;
        }
    return true;
}

// The village: the first layout's cottages, well, wheat field, windmill and
// lamp posts, spread out to 1.6 times their spacing round the centre of a
// larger green, and more cottages round it. Every building stands only
// where all of it is clear of the train.
void buildVillage(Grid &g, Extras &extras, const Clearance &clear)
{
    // level the green, blending into the meadow around it
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            const float dx = centerOf(bx) - kVillageX, dz = centerOf(bz) - kVillageZ;
            const float d = sqrtf(dx * dx + dz * dz);
            if (d > kVillageRadius)
                continue;
            const int top = g.top(bx, bz);
            if (top < 0 && d > kVillageRadius - 12.0f)
                continue; // leave the beach alone
            int target = kVillageTop;
            if (d > kVillageRadius - 10.0f)
                target = top > kVillageTop ? top - 1 : (top < kVillageTop ? top + 1 : top);
            if (g.biome[Grid::column(bx, bz)] != MEADOW)
                continue;
            g.clearAbove(bx, target, bz);
            for (int j = kMinY; j <= target; ++j)
                g.put(bx, j, bz, j == target ? GRASS : (j > target - 3 ? DIRT : STONE));
        }

    // the first layout's blocks round its centre (-28, -29), moved out
    const int cx = blockOf(kVillageX), cz = blockOf(kVillageZ);
    auto vx = [&](int b) {
        return cx + static_cast<int>(floorf(static_cast<float>(b + 28) * 1.6f + 0.5f));
    };
    auto vz = [&](int b) {
        return cz + static_cast<int>(floorf(static_cast<float>(b + 29) * 1.6f + 0.5f));
    };
    const int floor = kVillageTop + 1;
    auto house = [&](int x0, int z0, int w, int d, bool doorOnX) {
        if (roomForBuilding(clear, x0 - 1, x0 + w + 1, floor - 1, floor + 8, z0 - 1, z0 + d + 1))
            buildHouse(g, x0, z0, x0 + w, z0 + d, floor, doorOnX);
    };

    // gravel lanes crossing at the well, out to the edge of the green
    for (int bx = vx(-37) - 6; bx <= vx(-19) + 6; ++bx)
        if (!clear.blocks(bx, kVillageTop, vz(-30)))
            g.put(bx, kVillageTop, vz(-30), GRAVEL);
    for (int bz = vz(-37) - 6; bz <= vz(-20) + 6; ++bz)
        if (!clear.blocks(vx(-27), kVillageTop, bz))
            g.put(vx(-27), kVillageTop, bz, GRAVEL);

    house(vx(-35), vz(-37), 4, 4, false);
    house(vx(-24), vz(-36), 4, 4, false);
    house(vx(-36), vz(-26), 5, 4, true);
    // the newer cottages further out
    house(cx - 22, cz + 6, 4, 5, true);
    house(cx + 8, cz - 22, 5, 4, false);
    house(cx + 14, cz + 12, 4, 4, false);
    house(cx - 8, cz + 16, 5, 4, false);
    house(cx - 24, cz - 8, 4, 4, true);

    // the church: a stone nave with tall lit windows and a steep roof, and
    // a bell tower at its west end with a lantern for a bell; at the first
    // of a few spots round the green where it is clear of the train
    const int churchSpots[][2] = {
        {cx + 6, cz + 4}, {cx - 4, cz - 20}, {cx + 16, cz - 6}, {cx - 26, cz + 12}};
    for (const int *spot : churchSpots)
    {
        const int x0 = spot[0], z0 = spot[1], x1 = x0 + 9, z1 = z0 + 5;
        if (!roomForBuilding(clear, x0 - 2, x1 + 1, floor - 1, floor + 16, z0 - 1, z1 + 1))
            continue;
        footing(g, x0 - 1, x1, z0, z1, floor);
        fill(g, x0, x1, floor, floor + 4, z0, z1, COBBLE);
        fill(g, x0 + 1, x1 - 1, floor, floor + 4, z0 + 1, z1 - 1, AIR); // the inside
        for (int bx = x0 + 2; bx < x1; bx += 2)
        {
            g.put(bx, floor + 2, z0, WINDOW);
            g.put(bx, floor + 3, z0, WINDOW);
            g.put(bx, floor + 2, z1, WINDOW);
            g.put(bx, floor + 3, z1, WINDOW);
        }
        g.put(x1, floor, (z0 + z1) / 2, DOOR);
        g.put(x1, floor + 1, (z0 + z1) / 2, DOOR);
        for (int k = 0; z0 - 1 + k <= z1 + 1 - k; ++k)
            fill(g, x0, x1 + 1, floor + 5 + k, floor + 5 + k, z0 - 1 + k, z1 + 1 - k, ROOF);
        // the bell tower
        const int tz = (z0 + z1) / 2 - 1;
        fill(g, x0 - 2, x0, floor, floor + 10, tz, tz + 2, COBBLE);
        g.put(x0 - 1, floor + 9, tz, AIR); // open sides round the bell
        g.put(x0 - 1, floor + 9, tz + 2, AIR);
        g.put(x0 - 2, floor + 9, tz + 1, AIR);
        g.put(x0, floor + 9, tz + 1, AIR);
        g.put(x0 - 1, floor + 9, tz + 1, LANTERN);
        fill(g, x0 - 2, x0, floor + 11, floor + 11, tz, tz + 2, ROOF);
        g.put(x0 - 1, floor + 12, tz + 1, ROOF);
        g.put(x0 - 1, floor + 13, tz + 1, OAK_LOG); // the cross's post
        break;
    }

    // the well at the crossing
    const int wx = vx(-27), wz = vz(-30);
    if (roomForBuilding(clear, wx - 1, wx + 1, floor, floor + 3, wz - 1, wz + 1))
    {
        fill(g, wx - 1, wx + 1, floor, floor, wz - 1, wz + 1, COBBLE);
        g.put(wx, floor, wz, WATER);
        g.put(wx - 1, floor + 1, wz - 1, OAK_LOG);
        g.put(wx + 1, floor + 1, wz + 1, OAK_LOG);
        g.put(wx - 1, floor + 2, wz - 1, OAK_LOG);
        g.put(wx + 1, floor + 2, wz + 1, OAK_LOG);
        fill(g, wx - 1, wx + 1, floor + 3, floor + 3, wz - 1, wz + 1, ROOF);
    }

    // the wheat field, with a ditch down the middle and hay bales beside it
    const int fx = vx(-25), fz = vz(-27);
    for (int bx = fx; bx <= fx + 8; ++bx)
        for (int bz = fz; bz <= fz + 7; ++bz)
        {
            if (clear.blocks(bx, floor, bz))
                continue;
            g.put(bx, kVillageTop, bz, bx == fx + 4 ? WATER : FARMLAND);
            if (bx != fx + 4)
                g.plant[Grid::index(bx, floor, bz)] = WHEAT;
        }
    g.put(fx - 1, floor, fz + 3, HAY);
    g.put(fx - 1, floor, fz + 4, HAY);
    g.put(fx - 1, floor + 1, fz + 4, HAY);

    // the windmill: a stone tower with a timber top, the sails facing south
    const int mx = vx(-22), mz = vz(-20);
    if (roomForBuilding(clear, mx - 2, mx + 2, floor, floor + 12, mz - 2, mz + 7))
    {
        footing(g, mx - 1, mx + 1, mz - 1, mz + 1, floor);
        fill(g, mx - 1, mx + 1, floor, floor + 3, mz - 1, mz + 1, COBBLE);
        fill(g, mx - 1, mx + 1, floor + 4, floor + 6, mz - 1, mz + 1, PLANKS);
        for (int j = floor + 4; j <= floor + 6; ++j)
        {
            g.put(mx - 1, j, mz - 1, OAK_LOG);
            g.put(mx + 1, j, mz - 1, OAK_LOG);
            g.put(mx - 1, j, mz + 1, OAK_LOG);
            g.put(mx + 1, j, mz + 1, OAK_LOG);
        }
        fill(g, mx - 2, mx + 2, floor + 7, floor + 7, mz - 2, mz + 2, ROOF);
        fill(g, mx - 1, mx + 1, floor + 8, floor + 8, mz - 1, mz + 1, ROOF);
        g.put(mx, floor + 9, mz, ROOF);
        g.put(mx, floor, mz + 1, DOOR);
        g.put(mx, floor + 5, mz + 1, WINDOW);
        extras.mill[0] = centerOf(mx);
        extras.mill[1] = static_cast<float>(floor + 6) * kBlock;
        // the hub stands clear of the roof's overhang, so the sails turn in
        // front of it
        extras.mill[2] = static_cast<float>(mz + 3) * kBlock + 0.8f;
        extras.hasMill = true;
    }

    const int lamps[][2] = {{vx(-29), vz(-32)}, {vx(-25), vz(-28)}, {vx(-29), vz(-21)},
                            {vx(-19), vz(-30)}, {cx - 12, cz + 12}, {cx + 12, cz - 12}};
    for (const int *lamp : lamps)
        if (roomForBuilding(clear, lamp[0], lamp[0], floor, floor + 2, lamp[1], lamp[1]))
            buildLampPost(g, lamp[0], lamp[1], floor);
}

// A tower: a square shaft of stone with lit windows, a stepped pointed
// roof and a lantern on top. x0, z0: its corner; size: its width.
void buildTower(Grid &g, int x0, int z0, int size, int floor, int height)
{
    const int x1 = x0 + size - 1, z1 = z0 + size - 1;
    fill(g, x0, x1, floor, floor + height - 1, z0, z1, COBBLE);
    for (int j = floor + 3; j < floor + height - 1; j += 3)
    {
        g.put((x0 + x1) / 2, j, z0, WINDOW);
        g.put((x0 + x1) / 2, j, z1, WINDOW);
        g.put(x0, j, (z0 + z1) / 2, WINDOW);
        g.put(x1, j, (z0 + z1) / 2, WINDOW);
    }
    // the roof steps in from an overhang to a point
    int top = floor + height;
    for (int k = -1; x0 + k <= x1 - k; ++k)
        fill(g, x0 + k, x1 - k, top + k + 1, top + k + 1, z0 + k, z1 - k, ROOF);
    const int peak = top + (size + 1) / 2 + 1;
    g.put((x0 + x1) / 2, peak, (z0 + z1) / 2, LANTERN);
}

// The castle by the waterfall, after the user's reference: a curtain wall
// with battlements and four corner towers round a gravel court, a gate in
// the side facing the fall, and at the back a keep of stone and timber with
// lit windows and a tall main tower. Where the ground falls away a stone
// footing carries it, like a castle on a crag. It looks for the spot near
// the fall that needs the least footing, all of it clear of the train.
// Returns the block at the middle of the castle in at, or false if there
// was no room for it.
bool buildCastle(Grid &g, const Clearance &clear, int at[2])
{
    const int width = 15, depth = 13; // x and z, in blocks
    const int cx = (kFallX0 + kFallX1) / 2;
    const int lip = g.top(cx, kFallLipZ); // the stream at the top of the fall
    int bestX = 0, bestZ = 0, bestFloor = 0, bestScore = 1 << 30;
    for (int x0 = cx - 60; x0 <= cx + 40; x0 += 2)
        for (int z0 = kFallLipZ - 50; z0 <= kFallLipZ + 24; z0 += 2)
        {
            const int x1 = x0 + width - 1, z1 = z0 + depth - 1;
            // not in the gorge or on the fall's cliff
            if (x1 >= cx - 18 && x0 <= cx + 18 && z1 >= kFallLipZ - 8)
                continue;
            int high = kMinY - 1, low = kMaxY + 1;
            bool land = true;
            for (int bx = x0; bx <= x1 && land; ++bx)
                for (int bz = z0; bz <= z1; ++bz)
                {
                    if (!onIsland(bx, bz))
                    {
                        land = false;
                        break;
                    }
                    const int top = g.top(bx, bz);
                    const int b = g.get(bx, top, bz);
                    if (b == WATER || b == LAVA)
                        land = false;
                    high = top > high ? top : high;
                    low = top < low ? top : low;
                }
            const int floor = high + 1;
            if (!land || high - low > 9 || floor <= 2 || floor > kMaxY - 28)
                continue;
            const int away = abs(x0 + width / 2 - cx) + abs(z0 + depth / 2 - kFallLipZ);
            const int score = (high - low) * 6 + abs(floor - lip) + away;
            if (score >= bestScore)
                continue;
            if (!roomForBuilding(clear, x0 - 1, x1 + 1, low, floor + 26, z0 - 1, z1 + 1))
                continue;
            bestScore = score;
            bestX = x0;
            bestZ = z0;
            bestFloor = floor;
        }
    if (bestScore == 1 << 30)
        return false;

    const int x0 = bestX, z0 = bestZ, x1 = x0 + width - 1, z1 = z0 + depth - 1;
    at[0] = x0 + width / 2;
    at[1] = z0 + depth / 2;
    const int floor = bestFloor;
    // the gate faces the fall
    const bool gateNorth = z0 + depth / 2 < kFallLipZ;
    footing(g, x0, x1, z0, z1, floor);
    for (int bx = x0; bx <= x1; ++bx)
        for (int bz = z0; bz <= z1; ++bz)
            g.put(bx, floor - 1, bz, hash(bx, bz, 461u) % 4u == 0 ? COBBLE : GRAVEL); // the court

    // the curtain wall, five high, with battlements
    const int wallTop = floor + 4;
    for (int bx = x0; bx <= x1; ++bx)
        for (int bz = z0; bz <= z1; ++bz)
        {
            const bool edge = bx == x0 || bx == x1 || bz == z0 || bz == z1;
            if (!edge)
                continue;
            fill(g, bx, bx, floor, wallTop, bz, bz,
                 hash(bx * 3, bz, 463u) % 6u == 0 ? MOSSY : COBBLE);
            if ((bx + bz) % 2 == 0)
                g.put(bx, wallTop + 1, bz, COBBLE);
        }
    // the gate: an opening three wide and three high, lanterns either side
    const int gx = (x0 + x1) / 2, gz = gateNorth ? z1 : z0;
    fill(g, gx - 1, gx + 1, floor, floor + 2, gz, gz, AIR);
    fill(g, gx - 1, gx + 1, floor + 3, floor + 3, gz, gz, OAK_LOG);
    const int out = gateNorth ? 1 : -1;
    g.put(gx - 2, floor + 2, gz + out, LANTERN);
    g.put(gx + 2, floor + 2, gz + out, LANTERN);

    // corner towers, four wide, rising well above the wall
    buildTower(g, x0 - 1, z0 - 1, 4, floor, 10);
    buildTower(g, x1 - 2, z0 - 1, 4, floor, 10);
    buildTower(g, x0 - 1, z1 - 2, 4, floor, 10);
    buildTower(g, x1 - 2, z1 - 2, 4, floor, 10);

    // the keep at the back of the court, away from the gate: two floors of
    // stone, three of timber between log posts, rows of lit windows
    const int kx0 = x0 + 3, kx1 = x1 - 3;
    const int kz0 = gateNorth ? z0 + 2 : z1 - 7, kz1 = kz0 + 5;
    fill(g, kx0, kx1, floor, floor + 2, kz0, kz1, COBBLE);
    fill(g, kx0, kx1, floor + 3, floor + 8, kz0, kz1, PLANKS);
    for (int j = floor + 3; j <= floor + 8; ++j)
        for (int bx = kx0; bx <= kx1; bx += 3)
        {
            g.put(bx, j, kz0, OAK_LOG);
            g.put(bx, j, kz1, OAK_LOG);
        }
    for (int j = floor + 1; j <= floor + 7; j += 3)
    {
        for (int bx = kx0 + 1; bx < kx1; ++bx)
            if ((bx - kx0) % 3 != 0)
            {
                g.put(bx, j, kz0, WINDOW);
                g.put(bx, j, kz1, WINDOW);
            }
        g.put(kx0, j, (kz0 + kz1) / 2, WINDOW);
        g.put(kx1, j, (kz0 + kz1) / 2, WINDOW);
    }
    g.put((kx0 + kx1) / 2, floor, gateNorth ? kz1 : kz0, DOOR);
    g.put((kx0 + kx1) / 2, floor + 1, gateNorth ? kz1 : kz0, DOOR);
    // its roof: a steep gable along x
    for (int k = 0; kz0 - 1 + k <= kz1 + 1 - k; ++k)
        fill(g, kx0 - 1, kx1 + 1, floor + 9 + k, floor + 9 + k, kz0 - 1 + k, kz1 + 1 - k, ROOF);

    // the main tower rising out of the keep
    const int tx = (kx0 + kx1) / 2 - 1;
    buildTower(g, tx, kz0 + 1, 4, floor, 20);

    // lamp posts in the court
    buildLampPost(g, x0 + 3, gateNorth ? z1 - 3 : z0 + 3, floor);
    buildLampPost(g, x1 - 3, gateNorth ? z1 - 3 : z0 + 3, floor);
    return true;
}

// A lighthouse on the green headland that reaches furthest out to sea, away
// from the lava: a tower in
// red and white bands on a stone base, a gallery, a lamp room of lanterns
// that glows at night and a pointed cap, with the keeper's cottage beside it.
// away: a block to stay at least 60 blocks from (the castle).
void buildLighthouse(Grid &g, const Clearance &clear, const int away[2])
{
    int bestX = 0, bestZ = 0, bestTop = 0;
    float bestReach = -1.0e9f;
    for (int step = 0; step < 120; ++step)
    {
        const float a = static_cast<float>(step) * 3.0f * 3.14159265f / 180.0f;
        const float dx = cosf(a), dz = sinf(a);
        // the rounded square's edge in this direction, and the shore
        const float ex = fabsf(dx), ez = fabsf(dz);
        const float edge =
            WorldShape::kIslandHalf / sqrtf(sqrtf(ex * ex * ex * ex + ez * ez * ez * ez));
        float r = edge - 40.0f;
        while (r < edge + 40.0f && WorldShape::inland(dx * r, dz * r) > 7.0f)
            r += kBlock;
        const int bx = blockOf(dx * r), bz = blockOf(dz * r);
        if (!Grid::insideColumn(bx, bz) || r - edge <= bestReach ||
            (bx - away[0]) * (bx - away[0]) + (bz - away[1]) * (bz - away[1]) < 60 * 60)
            continue;
        const int top = g.top(bx, bz);
        const int b = g.get(bx, top, bz);
        if (top < 0 || top > 6 || (b != GRASS && b != SAND && b != STONE && b != GRAVEL) ||
            g.biome[Grid::column(bx, bz)] != MEADOW)
            continue;
        // on a green headland, well away from the lava rivers
        bool lava = false;
        for (int lx = bx - 12; lx <= bx + 12 && !lava; ++lx)
            for (int lz = bz - 12; lz <= bz + 12; ++lz)
                if (Grid::insideColumn(lx, lz) && g.get(lx, g.top(lx, lz), lz) == LAVA)
                {
                    lava = true;
                    break;
                }
        if (lava)
            continue;
        if (!roomForBuilding(clear, bx - 6, bx + 6, top, top + 22, bz - 6, bz + 6))
            continue;
        bestReach = r - edge;
        bestX = bx;
        bestZ = bz;
        bestTop = top;
    }
    if (bestReach < -1.0e8f)
        return;

    const int x = bestX, z = bestZ, floor = bestTop + 1;
    footing(g, x - 2, x + 2, z - 2, z + 2, floor);
    fill(g, x - 2, x + 2, floor, floor, z - 2, z + 2, COBBLE);
    // the banded tower
    for (int j = floor + 1; j <= floor + 13; ++j)
    {
        const int band = ((j - floor - 1) / 2) % 2 == 0 ? CLAY_WHITE : CLAY_RED;
        fill(g, x - 1, x + 1, j, j, z - 1, z + 1, band);
    }
    g.put(x, floor + 1, z + 1, DOOR);
    g.put(x, floor + 2, z + 1, DOOR);
    g.put(x, floor + 7, z - 1, WINDOW);
    g.put(x + 1, floor + 10, z, WINDOW);
    // the gallery, the lamp room and the cap
    fill(g, x - 2, x + 2, floor + 14, floor + 14, z - 2, z + 2, COBBLE);
    fill(g, x - 1, x + 1, floor + 15, floor + 16, z - 1, z + 1, LANTERN);
    fill(g, x - 2, x + 2, floor + 17, floor + 17, z - 2, z + 2, ROOF);
    fill(g, x - 1, x + 1, floor + 18, floor + 18, z - 1, z + 1, ROOF);
    g.put(x, floor + 19, z, ROOF);
    // the keeper's cottage on the landward side
    const float len = sqrtf(static_cast<float>(x * x + z * z));
    const int hx = x - static_cast<int>(floorf(static_cast<float>(x) / len * 7.0f + 0.5f)) - 2;
    const int hz = z - static_cast<int>(floorf(static_cast<float>(z) / len * 7.0f + 0.5f)) - 2;
    const int hfloor = g.top(hx + 2, hz + 2) + 1;
    if (hfloor >= 1 &&
        roomForBuilding(clear, hx - 1, hx + 5, hfloor - 1, hfloor + 8, hz - 1, hz + 5))
        buildHouse(g, hx, hz, hx + 4, hz + 3, hfloor, false);
}

// A timber footbridge over the pool below the fall, three blocks above the
// water, resting on the gorge's walls at both ends, with a few log rail
// posts and a lantern at each end; left out if it would not reach rock on
// both sides or the train would pass through it.
void buildBridge(Grid &g, const Clearance &clear)
{
    const int cx = (kFallX0 + kFallX1) / 2;
    const int pool = g.top(cx, kFallLipZ + 6); // the pool's water
    const int deck = pool + 4;
    const int z0 = kFallLipZ + 4, z1 = z0 + 1;
    int x0 = cx, x1 = cx;
    while (x0 > cx - 20 && g.get(x0 - 1, deck, z0) == AIR && g.get(x0 - 1, deck, z1) == AIR)
        --x0;
    while (x1 < cx + 20 && g.get(x1 + 1, deck, z0) == AIR && g.get(x1 + 1, deck, z1) == AIR)
        ++x1;
    --x0;
    ++x1;
    if (g.get(x0, deck, z0) == AIR || g.get(x1, deck, z0) == AIR)
        return; // no rock to rest on at one end
    if (!roomForBuilding(clear, x0, x1, deck, deck + 3, z0, z1))
        return;
    for (int bx = x0; bx <= x1; ++bx)
        for (int bz = z0; bz <= z1; ++bz)
            g.put(bx, deck, bz, PLANKS);
    for (int bx = x0 + 1; bx < x1; bx += 3)
    {
        g.put(bx, deck + 1, z0 - 1, OAK_LOG);
        g.put(bx, deck, z0 - 1, PLANKS);
        g.put(bx, deck + 1, z1 + 1, OAK_LOG);
        g.put(bx, deck, z1 + 1, PLANKS);
    }
    g.put(x0, deck + 1, z0, LANTERN);
    g.put(x1, deck + 1, z1, LANTERN);
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
        if (x < kGridLo[0] - kCarveRadius || x > kGridHi[0] + kCarveRadius ||
            z < kGridLo[1] - kCarveRadius || z > kGridHi[1] + kCarveRadius)
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
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
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

// A spruce: rings of needles that narrow to a point. Where it snows the
// snow settles on it in a thin layer (snowfall()).
void spruce(std::vector<Placed> &out, int bx, int base, int bz, int trunk)
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
    out.push_back(Placed{bx, base + trunk + 1, bz, LEAVES_SPRUCE});
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

// Is every column within `radius` of (bx, bz) open meadow, its top grass or
// dirt (nothing built on it), between low and high?
// biome: the one every column must be in, or -1 for the snow mountain's
// foot, where the meadow meets the spruce forest (either will do).
bool openGround(const Grid &g, int bx, int bz, int radius, int biome, int &low, int &high)
{
    low = kMaxY + 1;
    high = kMinY - 1;
    for (int x = bx - radius; x <= bx + radius; ++x)
        for (int z = bz - radius; z <= bz + radius; ++z)
        {
            if (!Grid::insideColumn(x, z) || !onIsland(x, z))
                return false;
            const int here = g.biome[Grid::column(x, z)];
            if (biome >= 0 ? here != biome : (here != ALPINE && here != MEADOW))
                return false;
            const int top = g.top(x, z);
            const int b = g.get(x, top, z);
            if (b != GRASS && b != DIRT && b != SNOWY_DIRT)
                return false;
            low = top < low ? top : low;
            high = top > high ? top : high;
        }
    return true;
}

// Is a round area (radius in blocks, layers j0 to j1) clear of the train?
bool roomForRound(const Clearance &clear, int bx, int bz, int radius, int j0, int j1)
{
    for (int x = bx - radius; x <= bx + radius; ++x)
        for (int z = bz - radius; z <= bz + radius; ++z)
            if ((x - bx) * (x - bx) + (z - bz) * (z - bz) <= radius * radius)
                for (int j = j0; j <= j1; ++j)
                    if (clear.blocks(x, j, z))
                        return false;
    return true;
}

bool openMeadow(const Grid &g, int bx, int bz, int radius, int &low, int &high)
{
    return openGround(g, bx, bz, radius, MEADOW, low, high);
}

// Does the ride pass within about 30 blocks of (bx, bz), so it can be seen
// from the train?
bool seenFromRide(const Clearance &clear, int bx, int bz, int from, int to)
{
    for (int r = from; r <= to; r += 3)
        for (int k = 0; k < 16; ++k)
        {
            const float a = static_cast<float>(k) * 0.3927f;
            const int x = bx + static_cast<int>(cosf(a) * static_cast<float>(r));
            const int z = bz + static_cast<int>(sinf(a) * static_cast<float>(r));
            if (Grid::insideColumn(x, z) && clear.nearHigh[Grid::column(x, z)] > -1.0e8f)
                return true;
        }
    return false;
}

// A lake in the open meadow, where the train passes in sight of it: a
// basin cut down to its lowest shore, sandy banks, a little island
// with a tree in the middle and a wooden jetty. It is all still water at
// one level below its rim, so nothing flows out of it.
void buildLake(Grid &g, const Clearance &clear, Extras &extras)
{
    const int radius = 10;
    int bestX = 0, bestZ = 0, bestLevel = 0, bestScore = 1 << 30;
    for (int bx = kMinBX + radius + 4; bx < kMaxBX - radius - 4; bx += 4)
        for (int bz = kMinBZ + radius + 4; bz < kMaxBZ - radius - 4; bz += 4)
        {
            const float vx = centerOf(bx) - kVillageX, vz = centerOf(bz) - kVillageZ;
            if (vx * vx + vz * vz < (kVillageRadius + 40.0f) * (kVillageRadius + 40.0f))
                continue;
            int low, high;
            if (!openMeadow(g, bx, bz, radius + 3, low, high) || high - low > 8 || low < 0)
                continue;
            const int score = (high - low) * 10;
            if (score >= bestScore || !seenFromRide(clear, bx, bz, radius + 8, radius + 22))
                continue;
            // the lake and its banks, and three blocks more (water keeps six
            // units from the track), clear of the train: a round area
            if (!roomForRound(clear, bx, bz, radius + 5, low - 3, high + 6))
                continue;
            bestScore = score;
            bestX = bx;
            bestZ = bz;
            bestLevel = low; // every column round it reaches this layer: no spilling
        }
    if (bestScore == 1 << 30)
        return;

    const int level = bestLevel;
    extras.grove[0] = centerOf(bestX);
    extras.grove[1] = static_cast<float>(level + 1) * kBlock;
    extras.grove[2] = centerOf(bestZ);
    extras.hasGrove = true;
    for (int x = bestX - radius - 2; x <= bestX + radius + 2; ++x)
        for (int z = bestZ - radius - 2; z <= bestZ + radius + 2; ++z)
        {
            const float dx = static_cast<float>(x - bestX), dz = static_cast<float>(z - bestZ);
            // a round lake whose shore wanders a little
            const float d = sqrtf(dx * dx + dz * dz) + (lattice(x, z, 471u) - 0.5f) * 2.0f;
            if (d > static_cast<float>(radius) + 2.0f)
                continue;
            if (d > static_cast<float>(radius))
            {
                // the sandy bank, a block above the water
                g.clearAbove(x, level + 1, z);
                g.put(x, level + 1, z, SAND);
                continue;
            }
            const int deep = d < static_cast<float>(radius) * 0.5f ? 2 : 1;
            g.clearAbove(x, level, z);
            for (int k = 0; k < deep; ++k)
                g.put(x, level - k, z, WATER);
            g.put(x, level - deep, z, hash(x, z, 473u) % 3u == 0 ? GRAVEL : SAND);
        }
    // the island and its tree
    for (int x = bestX - 2; x <= bestX + 2; ++x)
        for (int z = bestZ - 2; z <= bestZ + 2; ++z)
            if (abs(x - bestX) + abs(z - bestZ) <= 3)
                g.put(x, level + 1, z, abs(x - bestX) + abs(z - bestZ) <= 1 ? GRASS : SAND);
    std::vector<Placed> tree;
    roundTree(tree, bestX, level + 2, bestZ, OAK_LOG, LEAVES_CHERRY, 5, true);
    commit(g, clear, tree);
    // the jetty from the south bank
    for (int z = bestZ + radius + 1; z >= bestZ + radius - 5; --z)
    {
        g.put(bestX + 6, level + 1, z, PLANKS);
        if (z % 2 == 0)
            g.put(bestX + 6, level, z, OAK_LOG);
    }
    g.put(bestX + 6, level + 2, bestZ + radius - 5, LANTERN);
}

// A giant oak standing alone in the meadow where the train passes in sight
// of it: a trunk three blocks thick with roots, boughs and a broad crown.
void buildGiantTree(Grid &g, const Clearance &clear, Extras &extras)
{
    int bestX = 0, bestZ = 0, bestTop = 0, bestScore = 1 << 30;
    for (int bx = kMinBX + 12; bx < kMaxBX - 12; bx += 3)
        for (int bz = kMinBZ + 12; bz < kMaxBZ - 12; bz += 3)
        {
            const float vx = centerOf(bx) - kVillageX, vz = centerOf(bz) - kVillageZ;
            if (vx * vx + vz * vz < (kVillageRadius + 20.0f) * (kVillageRadius + 20.0f))
                continue;
            int low, high;
            if (!openMeadow(g, bx, bz, 4, low, high) || high - low > 1)
                continue;
            const int score = abs(bx - 20) + abs(bz + 40); // the open meadow in the south
            if (score >= bestScore || !seenFromRide(clear, bx, bz, 12, 24))
                continue;
            if (!roomForBuilding(clear, bx - 10, bx + 10, low, low + 30, bz - 10, bz + 10))
                continue;
            bestScore = score;
            bestX = bx;
            bestZ = bz;
            bestTop = high;
        }
    if (bestScore == 1 << 30)
        return;

    extras.meadow[0] = centerOf(bestX);
    extras.meadow[1] = centerOf(bestZ);
    extras.hasMeadow = true;
    std::vector<Placed> tree;
    const int base = bestTop + 1, trunk = 16;
    for (int k = 0; k < trunk; ++k)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                if (k < trunk - 3 || (dx == 0 && dz == 0))
                    tree.push_back(Placed{bestX + dx, base + k, bestZ + dz, OAK_LOG});
    // roots spreading over the ground
    const int roots[4][2] = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}};
    for (const int *r : roots)
    {
        tree.push_back(Placed{bestX + r[0], base, bestZ + r[1], OAK_LOG});
        tree.push_back(Placed{bestX + r[0] * 3 / 2, base, bestZ + r[1] * 3 / 2, OAK_LOG});
    }
    // boughs reaching out, and the crown: a flattened ball of leaves
    for (const int *r : roots)
        for (int k = 1; k <= 4; ++k)
            tree.push_back(
                Placed{bestX + r[0] / 2 * k, base + 10 + k / 2, bestZ + r[1] / 2 * k, OAK_LOG});
    const int crown = base + trunk - 2;
    for (int dx = -9; dx <= 9; ++dx)
        for (int dz = -9; dz <= 9; ++dz)
            for (int dy = -4; dy <= 5; ++dy)
            {
                const float d = sqrtf(static_cast<float>(dx * dx + dz * dz) +
                                      static_cast<float>(dy * dy) * 2.6f);
                if (d > 8.5f + (lattice(bestX + dx * 7 + dy, bestZ + dz, 477u) - 0.5f) * 2.0f)
                    continue;
                tree.push_back(Placed{bestX + dx, crown + dy, bestZ + dz, LEAVES_OAK});
            }
    commit(g, clear, tree);
}

// A spruce-log cabin: a cobble footing, walls of spruce logs, lit windows,
// a door with a pumpkin lantern beside it and a steep roof.
void buildCabin(Grid &g, int x0, int z0, int w, int d, int floor)
{
    const int x1 = x0 + w, z1 = z0 + d, wallTop = floor + 2;
    footing(g, x0, x1, z0, z1, floor);
    fill(g, x0, x1, floor, wallTop, z0, z1, SPRUCE_LOG);
    fill(g, x0 + 1, x1 - 1, floor, wallTop, z0 + 1, z1 - 1, AIR);
    for (int bx = x0 + 2; bx < x1; bx += 2)
    {
        g.put(bx, floor + 1, z0, WINDOW);
        g.put(bx, floor + 1, z1, WINDOW);
    }
    g.put((x0 + x1) / 2, floor, z1, DOOR);
    g.put((x0 + x1) / 2, floor + 1, z1, DOOR);
    g.put((x0 + x1) / 2 + 1, floor, z1 + 1, JACK_O);
    for (int k = 0; z0 - 1 + k <= z1 + 1 - k; ++k)
        fill(g, x0 - 1, x1 + 1, wallTop + 1 + k, wallTop + 1 + k, z0 - 1 + k, z1 + 1 - k, ROOF);
    g.put(x0 + 1, wallTop + 3, z0 + 1, COBBLE); // the chimney
    g.put(x0 + 1, wallTop + 4, z0 + 1, COBBLE);
}

// A camp in the spruce forest at the foot of the snow mountain, where the
// train passes in sight of it: log cabins round a campfire, lanterns on
// posts and pumpkin lanterns by the doors, warm against the blue of dusk.
// The camp's cabins round its middle: x, z of a corner, and width.
const int kCabins[4][3] = {{-9, -9, 5}, {4, -9, 5}, {-9, 4, 4}, {4, 4, 5}};

void buildTaigaCamp(Grid &g, const Clearance &clear)
{
    const int radius = 10;
    int bestX = 0, bestZ = 0, bestFloor = 0, bestScore = 1 << 30;
    for (int bx = kMinBX + radius + 2; bx < kMaxBX - radius - 2; bx += 2)
        for (int bz = kMinBZ + radius + 2; bz < kMaxBZ - radius - 2; bz += 2)
        {
            // at the snow mountain's foot, in the first layout's units
            const float mx = centerOf(bx) / kScale - kAlpineX;
            const float mz = centerOf(bz) / kScale - kAlpineZ;
            const float fromPeak = sqrtf(mx * mx + mz * mz) / kAlpineRadius;
            if (fromPeak > 1.25f)
                continue;
            int low, high;
            if (!openGround(g, bx, bz, radius, -1, low, high) || high - low > 6 ||
                static_cast<float>(high) * kBlock > 52.0f * kLift)
                continue;
            const int score = (high - low) * 10 + static_cast<int>(fromPeak * 40.0f);
            if (score >= bestScore || !seenFromRide(clear, bx, bz, radius, radius + 18))
                continue;
            // the square and the fire, and at least three of the cabins
            if (!roomForRound(clear, bx, bz, 6, low, high + 6))
                continue;
            int cabins = 0;
            for (const int *c : kCabins)
                if (roomForBuilding(clear, bx + c[0] - 1, bx + c[0] + c[2] + 1, low, high + 12,
                                    bz + c[1] - 1, bz + c[1] + 5 + 1))
                    ++cabins;
            if (cabins < 3)
                continue;
            bestScore = score;
            bestX = bx;
            bestZ = bz;
            bestFloor = high + 1;
        }
    if (bestScore == 1 << 30)
        return;

    const int x = bestX, z = bestZ, floor = bestFloor;
    // level the camp ground and lay a gravel square in the middle
    for (int bx = x - radius; bx <= x + radius; ++bx)
        for (int bz = z - radius; bz <= z + radius; ++bz)
        {
            bool free = true;
            for (int j = floor - 4; j <= floor + 6 && free; ++j)
                free = !clear.blocks(bx, j, bz);
            if (!free)
                continue; // the ride passes this corner
            g.clearAbove(bx, floor - 1, bz);
            for (int j = floor - 4; j < floor; ++j)
                if (g.get(bx, j, bz) == AIR)
                    g.put(bx, j, bz, DIRT);
            const bool square = abs(bx - x) <= 3 && abs(bz - z) <= 3;
            g.put(bx, floor - 1, bz, square ? GRAVEL : SNOWY_DIRT);
        }
    // the campfire: logs round a glowing heart, in a ring of stones
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz)
            g.put(x + dx, floor - 1, z + dz, (dx == 0 && dz == 0) ? LANTERN : COBBLE);
    g.put(x - 1, floor, z, SPRUCE_LOG);
    g.put(x + 1, floor, z, SPRUCE_LOG);
    // the cabins round the fire, each where it is clear of the train
    for (const int *c : kCabins)
        if (roomForBuilding(clear, x + c[0] - 1, x + c[0] + c[2] + 1, floor - 1, floor + 12,
                            z + c[1] - 1, z + c[1] + 5 + 1))
            buildCabin(g, x + c[0], z + c[1], c[2], 4, floor);
    const int posts[4][2] = {{x - 4, z}, {x + 4, z}, {x, z - 5}, {x, z + 5}};
    for (const int *post : posts)
        buildLampPost(g, post[0], post[1], floor);
    // firewood stacked by the fire
    g.put(x + 3, floor, z + 2, SPRUCE_LOG);
    g.put(x + 3, floor, z + 3, SPRUCE_LOG);
    g.put(x + 3, floor + 1, z + 2, SPRUCE_LOG);
}

// Fields round the village: a few plots of wheat on farmland with ditches
// of water between the rows and gravel lanes round them, on level meadow
// clear of the train.
void buildFarms(Grid &g, const Clearance &clear)
{
    const int vx = blockOf(kVillageX), vz = blockOf(kVillageZ);
    int fields = 0;
    for (int k = 0; k < 16 && fields < 4; ++k)
    {
        const float a = static_cast<float>(k) * 0.3927f;
        const int x0 = vx + static_cast<int>(cosf(a) * 32.0f) - 5;
        const int z0 = vz + static_cast<int>(sinf(a) * 32.0f) - 4;
        const int x1 = x0 + 11, z1 = z0 + 9;
        int low, high;
        if (!openMeadow(g, (x0 + x1) / 2, (z0 + z1) / 2, 7, low, high) || high - low > 2)
            continue;
        if (!roomForBuilding(clear, x0 - 1, x1 + 1, low, high + 3, z0 - 1, z1 + 1))
            continue;
        const int level = low;
        for (int x = x0 - 1; x <= x1 + 1; ++x)
            for (int z = z0 - 1; z <= z1 + 1; ++z)
            {
                g.clearAbove(x, level, z);
                for (int j = level - 2; j < level; ++j)
                    if (g.get(x, j, z) == AIR)
                        g.put(x, j, z, DIRT);
                const bool lane = x == x0 - 1 || x == x1 + 1 || z == z0 - 1 || z == z1 + 1;
                if (lane)
                    g.put(x, level, z, GRAVEL);
                else if ((z - z0) % 4 == 2)
                    g.put(x, level, z, WATER); // the ditch
                else
                {
                    g.put(x, level, z, FARMLAND);
                    g.plant[Grid::index(x, level + 1, z)] = WHEAT;
                }
            }
        g.put(x1 + 2, level + 1, z0, HAY);
        g.put(x1 + 2, level + 1, z0 + 1, HAY);
        g.put(x1 + 2, level + 2, z0, HAY);
        ++fields;
    }
}

// Ice spikes on the snowfields high on the jagged peaks: tall pillars of
// packed ice, thick at the foot and thinning to a point.
void buildIceSpikes(Grid &g, const Clearance &clear)
{
    std::vector<Placed> spike;
    for (int bx = kMinBX + 2; bx < kMaxBX - 2; ++bx)
        for (int bz = kMinBZ + 2; bz < kMaxBZ - 2; ++bz)
        {
            if (g.biome[Grid::column(bx, bz)] != ALPINE || hash(bx, bz, 481u) % 70u != 0)
                continue;
            const int top = g.top(bx, bz);
            if (g.get(bx, top, bz) != SNOW || top + 16 > kMaxY)
                continue;
            const int height = 5 + static_cast<int>(hash(bx, bz, 483u) % 10u);
            const bool thick = height > 9;
            spike.clear();
            for (int k = 1; k <= height; ++k)
            {
                spike.push_back(Placed{bx, top + k, bz, ICE});
                if (thick && k <= height / 2)
                {
                    spike.push_back(Placed{bx + 1, top + k, bz, ICE});
                    spike.push_back(Placed{bx, top + k, bz + 1, ICE});
                    spike.push_back(Placed{bx + 1, top + k, bz + 1, ICE});
                }
                else if (k <= height * 3 / 4 && k % 2 == 0)
                    spike.push_back(Placed{bx + 1, top + k, bz, ICE});
            }
            commit(g, clear, spike);
        }
}

// Hoodoos on the badlands: thin pillars of banded clay standing on the
// plateaus, as wind and water leave them, each with a harder cap.
void buildHoodoos(Grid &g, const Clearance &clear)
{
    std::vector<Placed> pillar;
    for (int bx = kMinBX + 2; bx < kMaxBX - 2; ++bx)
        for (int bz = kMinBZ + 2; bz < kMaxBZ - 2; ++bz)
        {
            if (g.biome[Grid::column(bx, bz)] != MESA || hash(bx, bz, 491u) % 90u != 0)
                continue;
            const int top = g.top(bx, bz);
            const int b = g.get(bx, top, bz);
            if (top < 4 || top + 16 > kMaxY ||
                (b != RED_SAND && b != CLAY_RED && b != CLAY_ORANGE && b != CLAY_YELLOW &&
                 b != CLAY_WHITE && b != CLAY_BROWN && b != CLAY_GREY))
                continue;
            const int height = 7 + static_cast<int>(hash(bx, bz, 493u) % 9u);
            pillar.clear();
            for (int k = 1; k <= height; ++k)
            {
                // three wide at the foot, two up the middle, one at the top
                const int band = clayBand(top + k);
                const int half = k <= height * 2 / 5 ? 1 : 0;
                const int extra = k <= height * 3 / 4 ? 1 : 0;
                for (int dx = -half; dx <= half + (half == 0 ? extra : 0); ++dx)
                    for (int dz = -half; dz <= half + (half == 0 ? extra : 0); ++dz)
                        pillar.push_back(Placed{bx + dx, top + k, bz + dz, band});
            }
            // the harder cap, a little wider than the neck
            for (int dx = 0; dx <= 1; ++dx)
                for (int dz = 0; dz <= 1; ++dz)
                    pillar.push_back(Placed{bx + dx, top + height + 1, bz + dz, CLAY_BROWN});
            commit(g, clear, pillar);
        }
}

// The mangrove swamp's water: pools of still water in the mud at sea
// level, but none in the room kept free of water round the ride.
void buildSwampWater(Grid &g, const std::vector<unsigned char> &zone)
{
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            if (g.biome[Grid::column(bx, bz)] != MANGROVE)
                continue;
            const float cx = centerOf(bx), cz = centerOf(bz);
            const int top = g.top(bx, bz);
            if (g.get(bx, top, bz) != MUD)
                continue;
            const bool pool = WorldShape::inlandNorthEast(cx, cz) >= 4.0f &&
                              valueNoise(cx * 0.06f, cz * 0.06f, 721u) >= 0.48f && top <= 1 &&
                              !zone[Grid::index(bx, 0, bz)] && !zone[Grid::index(bx, -1, bz)];
            if (!pool)
            {
                // the mud round the pools comes up to their surface, so the
                // still water has nowhere to run
                for (int j = top + 1; j <= 0; ++j)
                    if (!g.carved[Grid::index(bx, j, bz)])
                        g.put(bx, j, bz, MUD);
                continue;
            }
            g.clearAbove(bx, -1, bz);
            g.put(bx, -1, bz, MUD);
            g.put(bx, 0, bz, WATER);
        }
}

// A mangrove: a trunk raised on arching roots, a broad crown and vines
// hanging from its edge.
void mangrove(Grid &g, const Clearance &clear, int bx, int base, int bz, unsigned int h)
{
    std::vector<Placed> tree;
    const int lift = 3, trunk = 5 + static_cast<int>(h % 3u);
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const int *d : dirs)
    {
        // a root from the mud two blocks out, arching up into the trunk
        tree.push_back(Placed{bx + d[0] * 2, base, bz + d[1] * 2, MANGROVE_LOG});
        tree.push_back(Placed{bx + d[0] * 2, base + 1, bz + d[1] * 2, MANGROVE_LOG});
        tree.push_back(Placed{bx + d[0], base + 2, bz + d[1], MANGROVE_LOG});
    }
    for (int k = lift; k < lift + trunk; ++k)
        tree.push_back(Placed{bx, base + k, bz, MANGROVE_LOG});
    const int crown = base + lift + trunk - 2;
    for (int dx = -4; dx <= 4; ++dx)
        for (int dz = -4; dz <= 4; ++dz)
            for (int dy = 0; dy <= 3; ++dy)
            {
                const float r =
                    sqrtf(static_cast<float>(dx * dx + dz * dz)) + static_cast<float>(dy) * 0.9f;
                if (r > 3.7f + (lattice(bx + dx * 5 + dy, bz + dz, 731u) - 0.5f) * 1.5f)
                    continue;
                if (dx == 0 && dz == 0 && dy < 2)
                    continue;
                tree.push_back(Placed{bx + dx, crown + dy, bz + dz, LEAVES_MANGROVE});
            }
    if (!commit(g, clear, tree))
        return;
    // vines hanging down from the crown's underside
    for (int dx = -4; dx <= 4; ++dx)
        for (int dz = -4; dz <= 4; ++dz)
        {
            if (g.get(bx + dx, crown, bz + dz) != LEAVES_MANGROVE ||
                hash(bx + dx, bz + dz, 733u) % 3u != 0)
                continue;
            const int length = 2 + static_cast<int>(hash(bx + dx, bz + dz, 735u) % 5u);
            for (int k = 1; k <= length; ++k)
            {
                const int j = crown - k;
                const size_t cell = Grid::index(bx + dx, j, bz + dz);
                if (!Grid::inside(bx + dx, j, bz + dz) || g.block[cell] != AIR || g.carved[cell] ||
                    clear.blocks(bx + dx, j, bz + dz))
                    break;
                g.plant[cell] = VINES;
            }
        }
}

// A bamboo cane: stalks stacked up, a leafy top; only where every cell is
// open and clear of the train. Says whether it grew.
bool bambooCane(Grid &g, const Clearance &clear, int bx, int base, int bz, int height)
{
    for (int k = 0; k < height; ++k)
    {
        const size_t cell = Grid::index(bx, base + k, bz);
        if (!Grid::inside(bx, base + k, bz) || g.block[cell] != AIR || g.carved[cell] ||
            g.plant[cell] != NO_PLANT || clear.blocks(bx, base + k, bz))
            return false;
    }
    for (int k = 0; k < height; ++k)
        g.plant[Grid::index(bx, base + k, bz)] = k == height - 1 ? BAMBOO_TOP : BAMBOO;
    return true;
}

// Leaves round a cane's top, in the open cells beside it and one above,
// so from afar a thicket is a pale green canopy on slender stalks.
void bambooCanopy(Grid &g, const Clearance &clear, int bx, int top, int bz)
{
    for (int dy = -2; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
            {
                const int reach = abs(dx) + abs(dz);
                if (dy == 1 ? reach != 0 : (reach == 0 || (dy == -2 && reach > 1)))
                    continue;
                // the corners and the lowest ring only now and then
                const unsigned int h = hash(bx + dx, bz + dz * 7 + dy * 31, 747u);
                if ((reach == 2 || dy == -2) && h % 3u != 0)
                    continue;
                const int x = bx + dx, y = top + dy, z = bz + dz;
                const size_t cell = Grid::index(x, y, z);
                if (!Grid::inside(x, y, z) || g.block[cell] != AIR || g.carved[cell] ||
                    g.plant[cell] != NO_PLANT || clear.blocks(x, y, z))
                    continue;
                g.block[cell] = LEAVES_BAMBOO;
            }
}

// The north-east land's growth: mangroves standing in the swamp, and on
// the hills a bamboo jungle, dense in thickets, with a few tall broadleaf
// trees hung with vines.
void plantNorthEast(Grid &g, const Clearance &clear)
{
    // the canes' tops, for their leaves once every cane has grown
    std::vector<Placed> caneTops;
    for (int bx = kMinBX + 5; bx < kMaxBX - 5; ++bx)
        for (int bz = kMinBZ + 5; bz < kMaxBZ - 5; ++bz)
        {
            const int biome = g.biome[Grid::column(bx, bz)];
            if (biome != MANGROVE && biome != BAMBOO_JUNGLE)
                continue;
            const int top = g.top(bx, bz);
            const int ground = g.get(bx, top, bz);
            const unsigned int h = hash(bx, bz, 741u);
            const float cx = centerOf(bx), cz = centerOf(bz);
            if (biome == MANGROVE)
            {
                if ((ground == MUD || ground == WATER) && h % 34u == 0 && top + 14 < kMaxY &&
                    !crowded(g, bx, top + 1, bz, 7))
                    mangrove(g, clear, bx, top + 1, bz, h >> 4);
                continue;
            }
            if (ground != GRASS || top + 20 > kMaxY)
                continue;
            const float thicket = valueNoise(cx * 0.04f, cz * 0.04f, 743u);
            if (thicket > 0.34f && h % 4u != 0)
            {
                const int height = 8 + static_cast<int>((h >> 8) % 9u);
                if (bambooCane(g, clear, bx, top + 1, bz, height))
                    caneTops.push_back(Placed{bx, top + height, bz, LEAVES_BAMBOO});
            }
            else if (h % 61u == 0 && !crowded(g, bx, top + 1, bz, 3))
            {
                std::vector<Placed> tree;
                roundTree(tree, bx, top + 1, bz, OAK_LOG, LEAVES_OAK, 9, true);
                if (commit(g, clear, tree))
                    for (int dx = -3; dx <= 3; ++dx)
                        for (int dz = -3; dz <= 3; ++dz)
                            for (int k = 1; k <= 4; ++k)
                            {
                                const int j = top + 8 - k;
                                const size_t cell = Grid::index(bx + dx, j, bz + dz);
                                if (abs(dx) + abs(dz) < 3 || hash(bx + dx, bz + dz, 745u) % 3u ||
                                    !Grid::inside(bx + dx, j, bz + dz) || g.block[cell] != AIR ||
                                    clear.blocks(bx + dx, j, bz + dz))
                                    break;
                                g.plant[cell] = VINES;
                            }
            }
        }
    for (const Placed &cane : caneTops)
        bambooCanopy(g, clear, cane.bx, cane.j, cane.bz);
}

void plantTrees(Grid &g, const Clearance &clear, const Extras &extras)
{
    // the windmill's hub, or nowhere when there is none
    const float millHub[2] = {extras.hasMill ? extras.mill[0] : 1.0e6f, extras.mill[2]};
    std::vector<Placed> blocks;
    for (int bx = kMinBX + 1; bx < kMaxBX - 1; ++bx)
        for (int bz = kMinBZ + 1; bz < kMaxBZ - 1; ++bz)
        {
            const int top = g.top(bx, bz);
            if (top < kMinY || top + 12 > kMaxY)
                continue;
            const int ground = g.get(bx, top, bz);
            const int base = top + 1;
            if (g.get(bx, base, bz) != AIR || g.carved[Grid::index(bx, base, bz)])
                continue;
            const float cx = centerOf(bx), cz = centerOf(bz);
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
            else if (biome == BAMBOO_JUNGLE || biome == MANGROVE)
                continue; // plantNorthEast()
            else if (ground == GRASS || ground == SNOWY_DIRT)
            {
                if (!clear.roomForTree(bx, top, bz))
                    continue;
                // nothing tall where the windmill's sails turn
                const float millX = cx - millHub[0], millZ = cz - millHub[1];
                if (millX * millX + millZ * millZ < 16.0f * 16.0f)
                    continue;
                const float gx = cx - extras.grove[0], gz = cz - extras.grove[2];
                const bool grove =
                    extras.hasGrove && gx * gx + gz * gz < kGroveRadius * kGroveRadius;
                const float mx = cx - extras.meadow[0], mz = cz - extras.meadow[1];
                const bool flowerMeadow =
                    extras.hasMeadow && mx * mx + mz * mz < kMeadowRadius * kMeadowRadius;
                if (grove && biome == MEADOW)
                {
                    // the cherry grove: blossom trees close together
                    if (h % 9u == 0 && !crowded(g, bx, base, bz, 3))
                    {
                        roundTree(blocks, bx, base, bz, OAK_LOG, LEAVES_CHERRY,
                                  5 + static_cast<int>((h >> 8) % 2u), true);
                        commit(g, clear, blocks);
                    }
                    continue;
                }
                if (flowerMeadow && biome == MEADOW && h % 7u != 0)
                    continue; // the flower meadow stays open
                const float villageX = cx - kVillageX, villageZ = cz - kVillageZ;
                const bool village =
                    villageX * villageX + villageZ * villageZ < kVillageRadius * kVillageRadius;
                if (biome == ALPINE || ground == SNOWY_DIRT)
                {
                    if (h % 5u == 0 && !crowded(g, bx, base, bz, 1))
                        spruce(blocks, bx, base, bz, 6 + static_cast<int>((h >> 8) % 3u));
                }
                else if (biome == CRAG || biome == VOLCANO)
                {
                    if (h % 9u == 0 && !crowded(g, bx, base, bz, 2))
                        roundTree(blocks, bx, base, bz, biome == CRAG ? OAK_LOG : BIRCH_LOG,
                                  LEAVES_AUTUMN, 4 + static_cast<int>((h >> 8) % 2u), false);
                }
                else if (cz < -76.0f * kScale && cx > 4.0f * kScale && cx < 44.0f * kScale)
                {
                    // the mushroom shore
                    if (h % 11u == 0 && !crowded(g, bx, base, bz, 2))
                        giantMushroom(blocks, bx, base, bz, 3 + static_cast<int>((h >> 8) % 3u));
                }
                else
                {
                    // woods in patches, single trees between them; cherry
                    // blossom in the middle of the island
                    const float dx = cx - 22.0f * kScale, dz = cz + 22.0f * kScale;
                    const bool blossom = dx * dx + dz * dz < 34.0f * 34.0f * kScale * kScale;
                    const unsigned int rarity = village ? 60u : (wood > 0.56f ? 6u : 34u);
                    if (h % rarity == 0 && !crowded(g, bx, base, bz, 2))
                    {
                        const unsigned int kind = (h >> 8) % 10u;
                        const int trunk = 4 + static_cast<int>((h >> 12) % 3u);
                        if (blossom && kind < 6u)
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
void plantFlowers(Grid &g, const Clearance &clear, const Extras &extras)
{
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
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
                else if (extras.hasGrove &&
                         (cx - extras.grove[0]) * (cx - extras.grove[0]) +
                                 (cz - extras.grove[2]) * (cz - extras.grove[2]) <
                             kGroveRadius * kGroveRadius)
                    // fallen blossom under the cherry trees, with grass
                    plant = h % 5u < 3u ? PETALS : (h % 5u == 3u ? TALL_GRASS : NO_PLANT);
                else if (extras.hasMeadow &&
                         (cx - extras.meadow[0]) * (cx - extras.meadow[0]) +
                                 (cz - extras.meadow[1]) * (cz - extras.meadow[1]) <
                             kMeadowRadius * kMeadowRadius)
                {
                    // the flower meadow: a sea of flowers in drifts of colour
                    const int kinds[7] = {POPPY, DANDELION, CORNFLOWER,  TULIP,
                                          OXEYE, ALLIUM,    TULIP_ORANGE};
                    const int kind =
                        kinds[static_cast<int>(valueNoise(cx * 0.045f, cz * 0.045f, 103u) * 6.99f)];
                    plant = h % 5u < 3u ? kind : (h % 5u == 3u ? kinds[(h >> 8) % 7u] : TALL_GRASS);
                }
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
    // the first layout's spots, scaled
    const float firstSpots[][2] = {
        {-58.0f, -86.0f}, {-84.0f, -62.0f}, {-46.0f, -58.0f}, {-80.0f, -90.0f}, {-62.0f, -50.0f},
        {6.0f, -24.0f},   {34.0f, -44.0f},  {-4.0f, -6.0f},   {58.0f, -22.0f},  {44.0f, 4.0f},
        {18.0f, 6.0f},    {70.0f, -24.0f},  {-30.0f, -74.0f}, {0.0f, -84.0f}};
    std::vector<std::vector<float>> spots;
    for (const float *spot : firstSpots)
        spots.push_back({spot[0] * kScale, spot[1] * kScale});
    int count = 0;
    for (const std::vector<float> &spot : spots)
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
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
            for (int j = kMaxY; j >= kMinY && !g.opaque(bx, j, bz); --j)
            {
                g.sky[Grid::index(bx, j, bz)] = 15;
                queue.push_back(Cell{bx, j, bz});
            }
    // light also comes in from the open sides of the island
    auto edge = [&](int bx, int j, int bz) {
        if (!g.opaque(bx, j, bz) && g.sky[Grid::index(bx, j, bz)] < 14)
        {
            g.sky[Grid::index(bx, j, bz)] = 14;
            queue.push_back(Cell{bx, j, bz});
        }
    };
    for (int j = kMinY; j <= kMaxY; ++j)
    {
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            edge(kMinBX, j, bz);
            edge(kMaxBX - 1, j, bz);
        }
        for (int bx = kMinBX; bx < kMaxBX; ++bx)
        {
            edge(bx, j, kMinBZ);
            edge(bx, j, kMaxBZ - 1);
        }
    }
    flood(g, g.sky, queue);
}

void spreadGlow(Grid &g)
{
    std::deque<Cell> warm, cool;
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const int block = g.block[Grid::index(bx, j, bz)];
                int level = 0;
                bool isCool = false;
                if (block == LAVA)
                    level = 13;
                else if (block == LANTERN)
                    level = 14;
                else if (block == JACK_O)
                    level = 12;
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

// Turns the ground on the banks of the lava into magma, as lava that has
// begun to cool: most of the blocks right beside it, fewer a step further,
// none beyond, so the magma follows the lava instead of dotting the slope.
void crustLava(Grid &g)
{
    std::vector<int> crust;
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            const int top = g.top(bx, bz);
            if (top < kMinY)
                continue;
            const int b = g.get(bx, top, bz);
            if (b != BASALT && b != GRAVEL && b != STONE)
                continue;
            int nearest = 3; // in blocks, counting both ways
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    const int d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
                    if (d == 0 || d >= nearest || !Grid::insideColumn(bx + dx, bz + dz))
                        continue;
                    for (int j = top - 1; j <= top + 1; ++j)
                        if (g.get(bx + dx, j, bz + dz) == LAVA)
                            nearest = d;
                }
            const unsigned int roll = hash(bx, bz, 57u) % 10u;
            if ((nearest == 1 && roll < 7u) || (nearest == 2 && roll < 3u))
            {
                crust.push_back(bx);
                crust.push_back(top);
                crust.push_back(bz);
            }
        }
    for (size_t i = 0; i + 2 < crust.size(); i += 3)
        g.put(crust[i], crust[i + 1], crust[i + 2], MAGMA);
}

// ---------------------------------------------------------------------
// Building the world: no OpenGL here, so tests can use it.
// ---------------------------------------------------------------------
// Where snow lies, per column, from the bare terrain: 2 on snowy ground
// and high up on the mountains and crags, where it settles on whatever is
// there, the rock ledges too; 1 lower on the
// alpine slopes, where the sunny grass is bare but the trees hold snow;
// 0 elsewhere.
std::vector<unsigned char> snowCover(const Grid &g)
{
    std::vector<unsigned char> cover(static_cast<size_t>(kSizeX) * kSizeZ, 0);
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            const int top = g.top(bx, bz);
            const int ground = g.get(bx, top, bz);
            const int biome = g.biome[Grid::column(bx, bz)];
            const float topY = static_cast<float>(top + 1) * kBlock;
            unsigned char c = 0;
            if (ground == SNOW || ground == SNOWY_DIRT ||
                ((biome == CRAG || biome == ALPINE) && topY > 44.0f * kLift))
                c = 2;
            else if (biome == ALPINE && topY > 30.0f * kLift)
                c = 1;
            cover[Grid::column(bx, bz)] = c;
        }
    return cover;
}

// Snow settles as a thin layer, as it does in a block game: on the ground
// a few eighths of a block deep, drifting deeper and shallower (one to three eighths); on leaves,
// roofs and rock only an eighth or two. Never on water, lava, ice or a
// light, and never in the train's way.
void snowfall(Grid &g, const Clearance &clear, const std::vector<unsigned char> &cover)
{
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            const int c = cover[Grid::column(bx, bz)];
            if (c == 0)
                continue;
            int j = kMaxY;
            while (j >= kMinY && g.get(bx, j, bz) == AIR)
                --j;
            if (j < kMinY || j >= kMaxY)
                continue;
            const int b = g.get(bx, j, bz);
            if (b == WATER || b == LAVA || b == ICE || b == SNOW || blockGlow(b) > 0.0f ||
                (c == 1 && !isGrowth(b)))
                continue;
            const size_t cell = Grid::index(bx, j + 1, bz);
            if (g.plant[cell] != NO_PLANT || g.carved[cell] || clear.blocks(bx, j + 1, bz))
                continue;
            int depth = 1 + static_cast<int>(hash(bx, bz, 753u) % 2u);
            if (b == SNOWY_DIRT || b == STONE || b == GRAVEL)
            {
                const float drift = valueNoise(centerOf(bx) * 0.09f, centerOf(bz) * 0.09f, 751u);
                depth = 1 + static_cast<int>(drift * 2.99f);
            }
            g.plant[cell] = SNOW_LAYER;
            g.level[cell] = static_cast<unsigned char>(depth);
        }
}

void buildScenery(Grid &g, Extras &extras)
{
    const std::vector<float> ride = OceanWorld::rideSamples();
    Clearance clear;
    clear.add(ride);

    buildTerrain(g);
    const std::vector<unsigned char> cover = snowCover(g); // before anything is built on it
    buildLavaLake(g);
    traceLava(g, scaledLine(kLavaSouth, 4).data(), 4, true);
    traceLava(g, scaledLine(kLavaEast, 4).data(), 4, true);
    crustLava(g);
    buildVillage(g, extras, clear);
    buildPond(g, kPondX, kPondZ, kPondRadius);
    buildSpringPool(g, 12.5f, 102.5f, 9.0f); // a hot spring on the badlands
    buildWaterfall(g);
    buildFloatingIsland(g);

    carveRide(g, ride);
    const std::vector<float> along(ride.begin() + static_cast<long>(ride.size() / 3 * 2),
                                   ride.end());
    lightTunnels(g, along, clear);
    int castle[2] = {1 << 20, 1 << 20};
    buildCastle(g, clear, castle);
    buildBridge(g, clear);
    buildLighthouse(g, clear, castle);
    buildLake(g, clear, extras);
    buildFarms(g, clear);
    buildGiantTree(g, clear, extras);
    buildTaigaCamp(g, clear);
    buildIceSpikes(g, clear);
    buildHoodoos(g, clear);
    buildSwampWater(g, waterZone(ride));
    // after the carving, so the water flows round the ride's room
    spreadWater(g);

    plantTrees(g, clear, extras);
    plantNorthEast(g, clear);
    spreadSky(g);
    plantFlowers(g, clear, extras);
    snowfall(g, clear, cover);
    spreadGlow(g);
    placeAnimals(g, clear, extras);
}

// The editor's island: level grass with a few trees and flowers around
// the edge, clear of the default track in the middle.
void buildFlat(Grid &g)
{
    const Clearance clear;
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
            if (onFlatIsland(bx, bz))
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
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            const float cx = centerOf(bx), cz = centerOf(bz);
            if (!onFlatIsland(bx, bz) || cx * cx + cz * cz < 68.0f * 68.0f ||
                g.get(bx, 0, bz) != AIR)
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

// A bamboo cane's stalk is a thin square post, not just the crossed
// pictures, so it stays solid from any side and far away; its faces show
// the stalk's strip of the picture, shaded a little darker round the post.
// The leafy top adds a wider spray of leaves square to the crossed one.
void addBambooStalk(bool top, bool leafy, float cx, float y0, float cz, unsigned int h, Vertex v,
                    std::vector<Vertex> &out)
{
    const float half = 0.26f + 0.08f * static_cast<float>((h >> 4) % 4u) / 3.0f;
    const float u0 = 13.2f / 32.0f, u1 = 18.8f / 32.0f;
    const float tint = v.color[0];
    const float shades[4] = {1.0f, 0.82f, 0.7f, 0.9f};
    const float corners[5][2] = {
        {-half, -half}, {half, -half}, {half, half}, {-half, half}, {-half, -half}};
    // the stalk's picture starts at the top's leaves halfway up the cell
    const float vTop = top ? 0.5f : 0.0f;
    const float y1 = top ? y0 + kBlock * 0.5f : y0 + kBlock;
    for (int f = 0; f < 4; ++f)
    {
        v.color[0] = v.color[1] = v.color[2] = tint * shades[f];
        const float quad[4][4] = {{corners[f][0], y0, corners[f][1], 1.0f},
                                  {corners[f + 1][0], y0, corners[f + 1][1], 1.0f},
                                  {corners[f + 1][0], y1, corners[f + 1][1], vTop},
                                  {corners[f][0], y1, corners[f][1], vTop}};
        for (int c = 0; c < 4; ++c)
        {
            v.pos[0] = cx + quad[c][0];
            v.pos[1] = quad[c][1];
            v.pos[2] = cz + quad[c][2];
            v.uv[0] = (c == 0 || c == 3) ? u0 : u1;
            v.uv[1] = quad[c][3];
            out.push_back(v);
        }
    }
    if (!top && !leafy)
        return;
    v.color[0] = v.color[1] = v.color[2] = tint;
    // only the picture's upper half, which holds the leaves and no stalk;
    // the top's spray is the widest, the ones below it smaller and turned
    const float spread = kBlock * (top ? 1.15f : 0.8f);
    const float low = y0 + kBlock * (top ? 0.35f : 0.2f), high = low + kBlock * (top ? 1.3f : 0.9f);
    // the top's two sprays face along x and z, the lower ones diagonally
    const float axes[2][2][2] = {{{1.0f, 0.0f}, {0.0f, 1.0f}},
                                 {{0.7071f, 0.7071f}, {-0.7071f, 0.7071f}}};
    for (int q = 0; q < 2; ++q)
    {
        const float ax = axes[top ? 0 : 1][q][0] * spread;
        const float az = axes[top ? 0 : 1][q][1] * spread;
        const float quad[4][4] = {{cx - ax, low, cz - az, 0.5f},
                                  {cx + ax, low, cz + az, 0.5f},
                                  {cx + ax, high, cz + az, 0.0f},
                                  {cx - ax, high, cz - az, 0.0f}};
        for (int c = 0; c < 4; ++c)
        {
            v.pos[0] = quad[c][0];
            v.pos[1] = quad[c][1];
            v.pos[2] = quad[c][2];
            v.uv[0] = (c == 0 || c == 3) ? 0.0f : 1.0f;
            v.uv[1] = quad[c][3];
            out.push_back(v);
        }
    }
}

// How deep the snow in a cell lies, in blocks; 0 when there is none.
float snowDepth(const Grid &g, int bx, int j, int bz)
{
    if (!Grid::inside(bx, j, bz))
        return 0.0f;
    const size_t i = Grid::index(bx, j, bz);
    return g.plant[i] == SNOW_LAYER ? static_cast<float>(g.level[i]) / 8.0f : 0.0f;
}

// A snow layer: a thin slab on the block below, its top and the parts of
// its sides that stand above the snow (or the open air) next to it. Drawn
// with the solid blocks, so it is lit, shaded and casts shadows like them.
void addSnowLayer(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const size_t i = Grid::index(bx, j, bz);
    const float depth = snowDepth(g, bx, j, bz);
    std::vector<Vertex> &out = batches[static_cast<size_t>(LandTextures::SNOW)];
    const float x0 = static_cast<float>(bx) * kBlock, x1 = x0 + kBlock;
    const float z0 = static_cast<float>(bz) * kBlock, z1 = z0 + kBlock;
    const float y0 = static_cast<float>(j) * kBlock, top = y0 + depth * kBlock;
    const float tint =
        0.95f + 0.05f * static_cast<float>(hash(bx * 7 + j, bz, 401u) % 100u) / 100.0f;
    Vertex v;
    v.extra[0] = 0.0f;
    v.extra[1] = 0.0f;
    v.extra[2] = glowFactor(static_cast<float>(g.cool[i]));
    const float sky = skyFactor(static_cast<float>(g.sky[i]));
    auto corner = [&](float x, float y, float z, float u, float w) {
        v.pos[0] = x;
        v.pos[1] = y;
        v.pos[2] = z;
        v.uv[0] = u;
        v.uv[1] = w;
        out.push_back(v);
    };

    v.normal[0] = 0.0f;
    v.normal[1] = 1.0f;
    v.normal[2] = 0.0f;
    v.color[0] = v.color[1] = v.color[2] = tint;
    v.extra[3] = sky;
    corner(x0, top, z0, 0.0f, 0.0f);
    corner(x0, top, z1, 0.0f, 1.0f);
    corner(x1, top, z1, 1.0f, 1.0f);
    corner(x1, top, z0, 1.0f, 0.0f);

    // the sides, a little darker, wound like the blocks' faces
    for (const Face &f : kFaces)
    {
        if (f.normal[1] != 0)
            continue;
        const int ox = bx + f.normal[0], oz = bz + f.normal[2];
        if (g.opaque(ox, j, oz))
            continue;
        const float low = y0 + snowDepth(g, ox, j, oz) * kBlock;
        if (low >= top)
            continue;
        for (int k = 0; k < 3; ++k)
            v.normal[k] = static_cast<float>(f.normal[k]);
        v.extra[3] = sky * 0.85f;
        // the strip of the texture as tall as the side
        const float vLow = 1.0f - (low - y0) / kBlock, vTop = 1.0f - depth;
        for (int c = 0; c < 4; ++c)
        {
            const bool up = f.corner[c][1] == 1;
            corner(f.corner[c][0] == 1 ? x1 : x0, up ? top : low, f.corner[c][2] == 1 ? z1 : z0,
                   f.uv[c][0], up ? vTop : vLow);
        }
    }
}

// Two quads crossing diagonally, smaller than the cell and off centre.
void addPlant(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const size_t i = Grid::index(bx, j, bz);
    const int plant = g.plant[i];
    // bamboo and vines stack into columns: a whole cell tall, in the same
    // place in every cell of the column and still, so they join up
    const bool stacked = plant == BAMBOO || plant == BAMBOO_TOP || plant == VINES;
    const unsigned int h = stacked ? hash(bx, bz * 13, 411u) : hash(bx, bz * 13 + j, 411u);
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
    const float size =
        stacked ? kBlock : (crop ? 2.3f : (plant == TALL_GRASS || plant == FERN ? 1.9f : 1.6f));
    const float half = size * 0.5f;
    const float cx = static_cast<float>(bx) * kBlock + 1.25f +
                     (crop ? 0.0f : (static_cast<float>((h >> 8) % 100u) / 100.0f - 0.5f) * 0.9f);
    const float cz = static_cast<float>(bz) * kBlock + 1.25f +
                     (crop ? 0.0f : (static_cast<float>((h >> 16) % 100u) / 100.0f - 0.5f) * 0.9f);
    const float y0 = static_cast<float>(j) * kBlock;
    const float sway = plant == DEAD_BUSH || stacked ? 0.0f : 0.09f;
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
    if (plant == BAMBOO || plant == BAMBOO_TOP)
    {
        // leaves spray out of the cane's upper cells too, so a thicket's
        // crowns join into a green canopy
        bool leafy = false;
        for (int k = 1; k <= 3 && Grid::inside(bx, j + k, bz); ++k)
            if (g.plant[Grid::index(bx, j + k, bz)] == BAMBOO_TOP)
                leafy = k < 3 || (h >> 12) % 2u == 0;
        addBambooStalk(plant == BAMBOO_TOP, leafy, cx, y0, cz, h, v, out);
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
// The mesh is cut into chunks of kChunk x kChunk columns, each with its
// own display lists and the box round its blocks, so only the chunks in
// view are drawn (the camera's view, the light's in the shadow pass, the
// mirrored one for the water's reflection).
const int kChunk = 16;

struct LandChunk
{
    unsigned int solid = 0, plant = 0;
    unsigned int water = 0; // see-through, drawn last (drawWater)
    float lo[3], hi[3];
};

struct LandWorld::Scene
{
    Grid grid;
    Extras extras;
    bool built = false;
    std::vector<LandChunk> chunks;
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
    m_trestles.clear();
    m_supportPath.clear();
}

void LandWorld::shadowBounds(float lo[3], float hi[3])
{
    lo[0] = kGridLo[0];
    lo[1] = static_cast<float>(kMinY) * kBlock;
    lo[2] = kGridLo[1];
    hi[0] = kGridHi[0];
    hi[1] = static_cast<float>(kMaxY + 1) * kBlock;
    hi[2] = kGridHi[1];
}

// The scenery for the checks below, built once: it is the same every time,
// and building it takes a few seconds (in a debug build much longer).
static const Grid &checkedScenery()
{
    static std::unique_ptr<Grid> grid;
    if (!grid)
    {
        grid.reset(new Grid);
        Extras extras;
        buildScenery(*grid, extras);
    }
    return *grid;
}

int LandWorld::rideObstructions(const std::vector<float> &path)
{
    const Grid &g = checkedScenery();

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
    const Grid &g = checkedScenery();
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

    for (int cx = kMinBX; cx < kMaxBX; cx += kChunk)
        for (int cz = kMinBZ; cz < kMaxBZ; cz += kChunk)
        {
            Batches solids(LandTextures::TEXTURE_COUNT);
            Batches plants(LandTextures::TEXTURE_COUNT);
            Batches water(LandTextures::TEXTURE_COUNT);
            int low = kMaxY + 1, high = kMinY - 1;
            for (int bx = cx; bx < cx + kChunk && bx < kMaxBX; ++bx)
                for (int bz = cz; bz < cz + kChunk && bz < kMaxBZ; ++bz)
                    for (int j = kMinY; j <= kMaxY; ++j)
                    {
                        const size_t cell = Grid::index(bx, j, bz);
                        const int b = s.grid.block[cell];
                        if (b == AIR && s.grid.plant[cell] == NO_PLANT)
                            continue;
                        low = j < low ? j : low;
                        high = j > high ? j : high;
                        if (b == WATER)
                            addWaterFaces(s.grid, bx, j, bz, water);
                        else if (b != AIR)
                            addBlockFaces(s.grid, bx, j, bz, solids);
                        else if (s.grid.plant[cell] == SNOW_LAYER)
                            addSnowLayer(s.grid, bx, j, bz, solids);
                        else
                            addPlant(s.grid, bx, j, bz, plants);
                    }
            if (high < low)
                continue; // nothing in it
            LandChunk chunk;
            chunk.lo[0] = static_cast<float>(cx) * kBlock;
            chunk.lo[1] = static_cast<float>(low) * kBlock;
            chunk.lo[2] = static_cast<float>(cz) * kBlock;
            chunk.hi[0] = static_cast<float>(cx + kChunk) * kBlock;
            chunk.hi[1] = static_cast<float>(high + 1) * kBlock + 1.0f; // plants sway a little
            chunk.hi[2] = static_cast<float>(cz + kChunk) * kBlock;
            chunk.solid = compileList(solids, m_textures);
            chunk.plant = compileList(plants, m_textures);
            chunk.water = compileList(water, m_textures);
            s.chunks.push_back(chunk);
        }
    s.built = true;
    return s;
}

void LandWorld::draw(bool flat, bool shadowPass, float time)
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    const Scene &s = scene(flat);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    const Frustum view = Frustum::current();
    m_chunksDrawn = 0;
    for (const LandChunk &c : s.chunks)
        if (view.sees(c.lo, c.hi))
        {
            glCallList(c.solid);
            ++m_chunksDrawn;
        }
    if (!shadowPass)
    {
        // the fixed pipeline cuts the plants out here; the shader does it itself
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        for (const LandChunk &c : s.chunks)
            if (view.sees(c.lo, c.hi))
                glCallList(c.plant);
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
    const Frustum view = Frustum::current();
    std::vector<unsigned int> lists;
    for (const LandChunk &c : s.chunks)
        if (view.sees(c.lo, c.hi))
            lists.push_back(c.water);
    for (unsigned int list : lists)
        glCallList(list);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    for (unsigned int list : lists)
        glCallList(list);
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
        box(-0.5f, -0.5f, -3.4f, 0.5f, 0.5f, 0.3f); // the shaft, back to the tower
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
    for (int i = 0; i < 40; ++i)
    {
        const float span = 2600.0f;
        float x = static_cast<float>(hash(i, 0, 441u) % 2600u) +
                  time * (3.0f + static_cast<float>(i % 3));
        x = x - floorf(x / span) * span - span * 0.5f;
        const float z = static_cast<float>(hash(i, 1, 441u) % 2200u) - 1100.0f;
        const float y = 235.0f + static_cast<float>(hash(i, 2, 441u) % 60u);
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
        {-25.0f, 156.0f, 210.0f}, {375.0f, 93.0f, 240.0f}, {75.0f, 87.0f, -375.0f}};
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

    // cherry blossom drifting down through the grove, carried by the breeze
    if (s.extras.hasGrove)
    {
        setGlow(0.08f);
        for (int i = 0; i < 220; ++i)
        {
            const float fi = static_cast<float>(i);
            const float a = static_cast<float>(hash(i, 0, 501u) % 6283u) / 1000.0f;
            const float r = sqrtf(static_cast<float>(hash(i, 1, 501u) % 1000u) / 1000.0f) *
                            kGroveRadius * 0.85f;
            const float fall =
                7.0f + static_cast<float>(i % 5) * 1.5f; // seconds from crown to grass
            const float t = fmodf(time + fi * 0.73f, fall) / fall;
            const float x =
                s.extras.grove[0] + cosf(a) * r + t * 9.0f + sinf(time * 0.9f + fi) * 1.4f;
            const float z = s.extras.grove[2] + sinf(a) * r + cosf(time * 0.7f + fi * 1.3f) * 1.4f;
            const float y = s.extras.grove[1] + 18.0f * (1.0f - t);
            const int tone = static_cast<int>(hash(i, 2, 501u) % 30u);
            colour(246, 170 + tone, 206 + tone / 2);
            glPushMatrix();
            glTranslatef(x, y, z);
            glRotatef(time * 90.0f + fi * 37.0f, 0.3f, 1.0f, 0.2f);
            box(-0.16f, -0.03f, -0.12f, 0.16f, 0.03f, 0.12f);
            glPopMatrix();
        }
        setGlow(0.0f);
    }
}

// Where a post of a trestle stands: the ground under (x, z) below `from`
// height. Water is gone through to its bed and lava stood in; off the island
// the post goes down to the sea bed.
float LandWorld::postGround(float x, float z, float from) const
{
    const int bx = blockOf(x), bz = blockOf(z);
    if (!Grid::insideColumn(bx, bz) || !onIsland(bx, bz) || !m_scenery)
        return OceanWorld::seaFloor(x, z);
    const Grid &g = m_scenery->grid;
    int j = blockOf(from);
    j = j > kMaxY ? kMaxY : j;
    for (; j >= kMinY; --j)
    {
        const int b = g.get(bx, j, bz);
        if (b != AIR && b != WATER && !isGrowth(b))
            return static_cast<float>(j + 1) * kBlock;
    }
    return OceanWorld::seaFloor(x, z);
}

// Works out the trestles for a track: one every kTrestleSpacing units along
// it, wherever it is above the water and well above the ground. Each of the
// two posts finds its own ground, so on a slope neither hangs in the air,
// and a post that would pass through the room of another stretch of track
// below is left out (the other one still stands); so are the cross beams.
void LandWorld::planSupports(const std::vector<float> &path)
{
    m_trestles.clear();
    const size_t count = path.size() / 3;
    if (count < 4)
        return;
    const float spacing = 9.0f; // along the track, between trestles
    const float lean = 2.0f;    // how far out a tall post's foot may go
    // Is the stretch of track far from sample n (along it) in the way of a
    // vertical post at (x, z) from y0 up to y1?
    auto blocked = [&](size_t n, float x, float z, float y0, float y1) {
        for (size_t k = 0; k < count; ++k)
        {
            const size_t along = k > n ? k - n : n - k;
            if (along < 14 || count - along < 14)
                continue; // this stretch of track itself
            const float dx = path[3 * k] - x, dz = path[3 * k + 2] - z;
            if (dx * dx + dz * dz > 3.4f * 3.4f)
                continue;
            const float ty = path[3 * k + 1];
            if (ty + 5.6f > y0 && ty - 0.6f < y1) // the train's room there
                return true;
        }
        return false;
    };
    float travelled = spacing;
    for (size_t n = 0; n < count; ++n)
    {
        const size_t next = (n + 1) % count;
        float ax = path[3 * next] - path[3 * n], az = path[3 * next + 2] - path[3 * n + 2];
        const float length = sqrtf(ax * ax + az * az);
        travelled += length;
        if (travelled < spacing || length < 1.0e-4f)
            continue;
        const float x = path[3 * n], y = path[3 * n + 1], z = path[3 * n + 2];
        if (y < OceanWorld::waterLevel() + 0.5f)
            continue; // under water the track needs no posts
        travelled = 0.0f;
        ax /= length;
        az /= length;
        Trestle t;
        t.x = x;
        t.z = z;
        t.yaw = atan2f(ax, az) * 57.29578f;
        t.top = y - 0.2f; // up to the bottom of the sleepers
        bool any = false;
        for (int side = 0; side < 2; ++side)
        {
            const float s = side == 0 ? -1.0f : 1.0f;
            const float centre = postGround(x, z, t.top);
            const float foot = s * (1.5f + fminf((t.top - centre) * 0.06f, lean));
            // local x is (az, -ax) in the world
            const float fx = x + foot * az, fz = z - foot * ax;
            t.foot[side] = foot;
            t.ground[side] = postGround(fx, fz, t.top);
            t.post[side] = t.top - t.ground[side] > 1.2f &&
                           !blocked(n, fx, fz, t.ground[side], t.top) &&
                           !blocked(n, x + 1.3f * s * az, z - 1.3f * s * ax, t.ground[side], t.top);
            any = any || t.post[side];
        }
        if (any)
            m_trestles.push_back(t);
    }
    m_supportPath = path;
}

void LandWorld::drawSupports(const std::vector<float> &path, bool shadowPass)
{
    if (!m_scenery || !m_scenery->built)
        return;
    if (path != m_supportPath)
        planSupports(path);

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    if (!shadowPass)
    {
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glBindTexture(GL_TEXTURE_2D, m_textures[LandTextures::OAK_SIDE]);
        glColor3f(0.92f, 0.9f, 0.86f);
    }
    setGlow(0.0f);
    const float post = 0.28f;
    for (const Trestle &t : m_trestles)
    {
        glPushMatrix();
        glTranslatef(t.x, 0.0f, t.z);
        glRotatef(t.yaw, 0.0f, 1.0f, 0.0f);
        // Two posts that lean in towards the top, each from its own ground.
        for (int side = 0; side < 2; ++side)
        {
            if (!t.post[side])
                continue;
            const float ground = t.ground[side];
            const float height = t.top - ground;
            const float xs[2] = {t.foot[side], side == 0 ? -1.3f : 1.3f};
            glBegin(GL_QUADS);
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
                glVertex3f(xs[1] + cx[a], t.top, cz[a]);
                glTexCoord2f(0.25f, 0.0f);
                glVertex3f(xs[1] + cx[b], t.top, cz[b]);
                glTexCoord2f(0.25f, height / 5.0f);
                glVertex3f(xs[0] + cx[b], ground, cz[b]);
            }
            glEnd();
        }
        // Cross beams between the posts, down to the higher ground.
        if (t.post[0] && t.post[1])
        {
            const float low = fmaxf(t.ground[0], t.ground[1]) + 2.0f;
            for (float h = t.top - 0.3f; h > low; h -= 6.0f)
            {
                // where each post is at this height
                const float r0 = -1.3f + (t.foot[0] + 1.3f) * (t.top - h) / (t.top - t.ground[0]);
                const float r1 = 1.3f + (t.foot[1] - 1.3f) * (t.top - h) / (t.top - t.ground[1]);
                box(r0 - post, h - 0.4f, -0.2f, r1 + post, h, 0.2f);
            }
        }
        glPopMatrix();
    }
    glPopAttrib();
}
