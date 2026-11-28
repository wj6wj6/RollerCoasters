/************************************************************************
     File:        LandWorld.cpp

     Comment:     See LandWorld.h.

*************************************************************************/

#include <math.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include "LandWorld.h"
#include "BlockMesh.h"
#include "CellQueue.h"
#include "Frustum.h"
#include "LandTextures.h"
#include "OceanWorld.h"
#include "PixelArt.h"
#include "TexturePack.h"
#include "WaterFlow.h"
#include "WorldSave.h"
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
const float kGridLo[2] = {static_cast<float>(kMinBX) * kBlock, static_cast<float>(kMinBZ) * kBlock};
const float kGridHi[2] = {static_cast<float>(kMaxBX) * kBlock, static_cast<float>(kMaxBZ) * kBlock};
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
    ICE, // packed ice: the ice spikes
    MUD, // the mangrove swamp's ground
    MANGROVE_LOG,
    LEAVES_MANGROVE,
    LEAVES_BAMBOO, // the canopy over a bamboo thicket
    PATH,          // a trodden dirt path
    SPRUCE_PLANKS, // dark planks
    CLAY,          // the bed of the lakes and ponds
    PODZOL,        // the spruce forest's floor
    COARSE_DIRT,   // where the meadow meets the badlands and the volcano
    STONE_BRICKS,  // dressed stone: the castle, the church, the lighthouse's base
    MOSSY_BRICKS,  // the same, mossy, on the castle's old walls
    DARK_OAK_LOG,  // dark timber: window frames and beams
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
    FENCE,      // an oak fence: a post, and rails to the fences and blocks beside it
    LAMP,       // a lantern, standing on what is below or hanging from what is above
    LILY_PAD,   // floating on still water (in the water's own cell)
    SUGAR_CANE, // stalks by the water's edge; they stack
    // the parts of buildings smaller than a block, as a block game has them;
    // what each is made of and which way it faces is kept in Grid::level
    // (see shapeLevel())
    STAIRS,   // a step: a half block with a quarter block on its back half
    SLAB,     // a half block, low or high in the cell
    TRAPDOOR, // a thin board: closed at the top of the cell, or open against a wall
    POT,      // a flower pot with a flower in it (Grid::level: the flower)
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
        return T::WINDOW;
    case DOOR:
        return face == 2 ? T::DOOR : T::PLANKS;
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
    case MUD:
        return T::MUD;
    case MANGROVE_LOG:
        return face == 2 ? T::MANGROVE_SIDE : T::MANGROVE_TOP;
    case LEAVES_MANGROVE:
        return T::LEAVES_MANGROVE;
    case LEAVES_BAMBOO:
        return T::LEAVES_BAMBOO;
    case PATH:
        return face == 0 ? T::PATH_TOP : (face == 1 ? T::DIRT : T::PATH_SIDE);
    case SPRUCE_PLANKS:
        return T::SPRUCE_PLANKS;
    case CLAY:
        return T::CLAY;
    case PODZOL:
        return face == 0 ? T::PODZOL_TOP : (face == 1 ? T::DIRT : T::PODZOL_SIDE);
    case COARSE_DIRT:
        return T::COARSE_DIRT;
    case STONE_BRICKS:
        return T::STONE_BRICKS;
    case MOSSY_BRICKS:
        return T::MOSSY_BRICKS;
    case DARK_OAK_LOG:
        return face == 2 ? T::DARK_OAK_SIDE : T::DARK_OAK_TOP;
    default:
        return T::STONE;
    }
}

int plantTexture(int plant)
{
    if (plant == LILY_PAD)
        return LandTextures::LILY_PAD;
    if (plant == SUGAR_CANE)
        return LandTextures::SUGAR_CANE;
    return LandTextures::TALL_GRASS + (plant - TALL_GRASS);
}

// The parts of buildings in plant cells (fences, lanterns, steps, slabs,
// trapdoors, flower pots): nothing else is put where one of them is.
bool isFixture(int plant)
{
    return plant == FENCE || plant == LAMP || (plant >= STAIRS && plant <= POT);
}

// The four ways a step can rise, a trapdoor lean or a corner turn, in this
// order: +x, +z, -x, -z.
const int kFacing[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

// What steps and slabs can be made of: their block, its index kept in
// Grid::level.
const int kShapeBlocks[8] = {PLANKS,       SPRUCE_PLANKS, ROOF,  COBBLE,
                             STONE_BRICKS, MOSSY_BRICKS,  STONE, DARK_OAK_LOG};

// Grid::level of a step, slab or trapdoor: bits 0..1 which way it faces
// (kFacing), bit 2 high in the cell (an upside-down step, a high slab, a
// closed trapdoor), bit 3 an outer corner step, bits 4..6 what it is made
// of (kShapeBlocks).
unsigned char shapeLevel(int facing, bool high, bool corner, int block)
{
    int material = 0;
    for (int k = 0; k < 8; ++k)
        if (kShapeBlocks[k] == block)
            material = k;
    return static_cast<unsigned char>((facing & 3) | (high ? 4 : 0) | (corner ? 8 : 0) |
                                      (material << 4));
}

// Leaves are cut out (their pictures have holes), so they hide nothing
// behind them: the faces next to them are drawn, as with fancy leaves in a
// block game.
bool isLeafBlock(int block)
{
    return (block >= LEAVES_OAK && block <= LEAVES_AUTUMN) || block == LEAVES_MANGROVE ||
           block == LEAVES_BAMBOO;
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
    case CRYSTAL:
        return 1.0f;
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

// What the shore at (x, z) of the first layout is like, so the coast is not
// one thin strip of sand all round: rocky (0..1) where the land rises
// straight out of the sea in rock, dunes (0..1) where a wider beach rolls
// in low hills of sand, and a plain beach between them.
void shoreKind(float x, float z, float &rocky, float &dunes)
{
    const float n = valueNoise(x * 0.028f + 3.1f, z * 0.028f - 1.7f, 93u);
    rocky = ramp(0.53f, 0.63f, n);
    dunes = ramp(0.45f, 0.35f, n);
}

// The land at (x, z) of the first layout, coast units from the shore.
Site firstSite(float x, float z, float coast)
{
    Site site;
    const float inland = ramp(2.0f, 24.0f, coast);
    site.height = 1.0f + inland * 4.2f + (fbm(x * 0.035f, z * 0.035f, 3u) - 0.5f) * 6.0f * inland +
                  (valueNoise(x * 0.09f, z * 0.09f, 5u) - 0.5f) * 1.5f * inland;
    site.biome = MEADOW;
    // the shore: rock rising steeply from the water on the headlands, low
    // dunes on the wide beaches
    float rocky, dunes;
    shoreKind(x, z, rocky, dunes);
    if (rocky > 0.0f)
        site.height =
            fmaxf(site.height, 1.0f + rocky * ramp(0.3f, 3.0f, coast) *
                                          (7.0f + 5.0f * valueNoise(x * 0.12f, z * 0.12f, 95u)));
    if (dunes > 0.0f && coast < 16.0f)
    {
        // a wide beach: the land keeps low further in, rolling in dunes
        const float beach = dunes * ramp(16.0f, 9.0f, coast);
        const float rolls =
            1.0f + ramp(1.0f, 3.5f, coast) * valueNoise(x * 0.16f, z * 0.16f, 97u) * 2.6f;
        site.height += (rolls - site.height) * beach;
    }

    // the snow mountain
    {
        const float dx = x - kAlpineX, dz = z - kAlpineZ;
        const float warp = 1.0f + (valueNoise(x * 0.045f, z * 0.045f, 11u) - 0.5f) * 0.3f;
        const float r = sqrtf(dx * dx + dz * dz) / kAlpineRadius * warp;
        if (r < 1.0f)
        {
            // Jagged peaks, as a block game grows them: spurs running out
            // from the summit (ridges round it, by the direction from the
            // top, with gullies between them) and broader ridges across
            // them, so it is a massif with shoulders rather than one cone;
            // the small crags only roughen it a little, as too many of them
            // made every slope a staircase of grey steps.
            const float ax = dx / (sqrtf(dx * dx + dz * dz) + 1e-3f),
                        az = dz / (sqrtf(dx * dx + dz * dz) + 1e-3f);
            const float spur =
                1.0f -
                fabsf(valueNoise(ax * 2.4f + 7.0f, az * 2.4f + 3.0f + r * 1.2f, 14u) * 2.0f - 1.0f);
            const float ridge = 1.0f - fabsf(valueNoise(x * 0.045f, z * 0.045f, 15u) * 2.0f - 1.0f);
            const float crags = valueNoise(x * 0.16f, z * 0.16f, 16u) - 0.5f;
            const float h = kAlpineHeight * (1.0f - r) *
                                (0.58f + 0.12f * valueNoise(x * 0.035f, z * 0.035f, 13u) +
                                 0.35f * spur * spur + 0.3f * ridge * ridge) +
                            crags * 3.5f * (1.0f - r);
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
    // Solid and not see-through: water and glass let light through and show
    // what is behind them, so they hide no faces and cast no ambient
    // occlusion.
    bool opaque(int bx, int j, int bz) const
    {
        const int b = get(bx, j, bz);
        // a door is a thin board in its cell, so it hides nothing beside it
        return b != AIR && b != WATER && b != WINDOW && b != DOOR;
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
    // the jungle temple's middle, in blocks: the bamboo leaves a clearing
    // round it so it can be seen
    int temple[2] = {0, 0};
    bool hasTemple = false;
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
                    // Above the snow line the ground is snow two blocks deep,
                    // so the sides of the small steps are white too and the
                    // slopes read as one field of snow; where they drop
                    // further the rock shows under it in grey bands, and the
                    // sheer cliffs (six blocks or more) are bare. Below it,
                    // grassy slopes, snowy near the line, with rock where
                    // they are steep.
                    const float snowLine = (44.0f + patch * 6.0f) * kLift;
                    if (topY > snowLine)
                        b = (depth < 2 && steep < 6) ? SNOW : stoneOrOre(bx, j, bz, false);
                    else if (topY > 34.0f * kLift || steep >= 3)
                    {
                        if (steep >= 3)
                            b = stoneOrOre(bx, j, bz, false);
                        else if (depth == 0)
                            b = topY > 40.0f * kLift ? SNOWY_DIRT : GRASS;
                        else
                            b = depth < 3 ? DIRT : stoneOrOre(bx, j, bz, false);
                    }
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
                default: {
                    float rocky, dunes;
                    shoreKind(cx / kScale, cz / kScale, rocky, dunes);
                    if (coast < 18.0f && rocky > 0.5f && (steep >= 2 || topY <= 5.0f))
                    {
                        // the headlands' sea cliffs: rock in layers of stone
                        // and cobble, moss on the tops and gravel at the
                        // water's edge, not one grey wall
                        const unsigned int r = hash(bx * 7 + j, bz * 3 - j, 99u) % 10u;
                        if (depth == 0 && topY <= 2.5f)
                            b = patch > 0.45f ? GRAVEL : STONE;
                        else if (depth == 0 && patch > 0.58f)
                            b = MOSSY;
                        else
                            b = (j + static_cast<int>(patch * 3.0f)) % 4 == 0 || r < 2u
                                    ? COBBLE
                                    : stoneOrOre(bx, j, bz, false);
                    }
                    else if ((topY <= 2.5f && coast < 14.0f) ||
                             (dunes > 0.5f && coast < 40.0f && topY <= 8.0f))
                        b = depth < 3 ? SAND : STONE; // the beach, and its dunes
                    else
                        b = depth == 0 ? GRASS : (depth < 3 ? DIRT : stoneOrOre(bx, j, bz, false));
                    break;
                }
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

// Half of a, rounded down also when it is negative (a / 2 rounds towards
// zero): the middle of a run of blocks from b0 to b1 is floorHalf(b0 + b1).
int floorHalf(int a)
{
    return a >= 0 ? a / 2 : -((1 - a) / 2);
}

// Is the cell free for a part of a building: air, with at most grass or a
// flower in it?
bool freeCell(const Grid &g, int bx, int j, int bz)
{
    return Grid::inside(bx, j, bz) && g.get(bx, j, bz) == AIR &&
           !isFixture(g.plant[Grid::index(bx, j, bz)]);
}

// A part of a building in a cell, if it is free.
void putFixture(Grid &g, int bx, int j, int bz, int plant, unsigned char level)
{
    if (!freeCell(g, bx, j, bz))
        return;
    const size_t i = Grid::index(bx, j, bz);
    g.plant[i] = static_cast<unsigned char>(plant);
    g.level[i] = level;
}

// A fence post in a cell, if it is free.
void putFence(Grid &g, int bx, int j, int bz)
{
    putFixture(g, bx, j, bz, FENCE, 0);
}

// A lantern in a cell, if it is free: it stands on the block or fence below,
// or hangs from the block above on a chain.
void putLamp(Grid &g, int bx, int j, int bz)
{
    putFixture(g, bx, j, bz, LAMP, 0);
}

// A step of `block` rising towards kFacing[rises]; upside down if high; an
// outer corner (its upper quarter towards kFacing[rises] and the next way
// round) if corner.
void putStairs(Grid &g, int bx, int j, int bz, int rises, int block, bool high = false,
               bool corner = false)
{
    putFixture(g, bx, j, bz, STAIRS, shapeLevel(rises, high, corner, block));
}

// A slab of `block`, in the lower or upper half of the cell.
void putSlab(Grid &g, int bx, int j, int bz, int block, bool high = false)
{
    putFixture(g, bx, j, bz, SLAB, shapeLevel(0, high, false, block));
}

// A spruce trapdoor: closed, at the top of the cell (a window sill), or
// open against the wall towards kFacing[wall] (a shutter).
void putTrapdoor(Grid &g, int bx, int j, int bz, int wall, bool closed)
{
    putFixture(g, bx, j, bz, TRAPDOOR, shapeLevel(wall, closed, false, PLANKS));
}

// A flower pot with a flower (a Plant) in it.
void putPot(Grid &g, int bx, int j, int bz, int flower)
{
    putFixture(g, bx, j, bz, POT, static_cast<unsigned char>(flower));
}

// A gable roof of steps over the box b0..b1 along the ridge, a0..a1 across
// it, eaves included, the lowest course at layer `base`: two rows of steps
// rising from the eaves to the ridge (a row of slabs where it is odd), the
// inside and the gable ends filled with `gable`, set back a block under
// the steps. alongX: the ridge runs along x (a is z), else along z.
void buildGableRoof(Grid &g, int b0, int b1, int a0, int a1, int base, bool alongX, int roof,
                    int gable)
{
    auto at = [&](int a, int b, int j, int what, int rises) {
        const int bx = alongX ? b : a, bz = alongX ? a : b;
        if (what == STAIRS)
            putStairs(g, bx, j, bz, rises, roof);
        else if (what == SLAB)
            putSlab(g, bx, j, bz, roof);
        else if (g.get(bx, j, bz) == AIR)
        {
            g.put(bx, j, bz, gable);
            g.plant[Grid::index(bx, j, bz)] = NO_PLANT;
        }
    };
    const int up = alongX ? 1 : 0, down = alongX ? 3 : 2; // rising towards +a, -a
    for (int k = 0; a0 + k <= a1 - k; ++k)
    {
        const int lo = a0 + k, hi = a1 - k, j = base + k;
        for (int b = b0; b <= b1; ++b)
        {
            if (lo == hi)
            {
                at(lo, b, j, SLAB, 0);
                continue;
            }
            at(lo, b, j, STAIRS, up);
            at(hi, b, j, STAIRS, down);
            if (b > b0 && b < b1)
                for (int a = lo + 1; a < hi; ++a)
                    at(a, b, j, AIR, 0);
        }
    }
}

// A hipped roof of steps over x0..x1, z0..z1 (eaves included), the lowest
// course at layer `base`: rings of steps rising inwards, outer corner steps
// at the corners (four of them make a point where two by two are left),
// slabs on top, filled with `fill` inside.
void buildHipRoof(Grid &g, int x0, int x1, int z0, int z1, int base, int roof, int fill)
{
    for (int k = 0; x0 + k <= x1 - k && z0 + k <= z1 - k; ++k)
    {
        const int lx = x0 + k, hx = x1 - k, lz = z0 + k, hz = z1 - k, j = base + k;
        for (int bx = lx; bx <= hx; ++bx)
            for (int bz = lz; bz <= hz; ++bz)
            {
                const bool edgeX = bx == lx || bx == hx, edgeZ = bz == lz || bz == hz;
                if (lx == hx || lz == hz)
                    putSlab(g, bx, j, bz, roof); // the top: a row or one block
                else if (edgeX && edgeZ)
                {
                    const int corner = bx == lx ? (bz == lz ? 0 : 3) : (bz == lz ? 1 : 2);
                    putStairs(g, bx, j, bz, corner, roof, false, true);
                }
                else if (edgeZ)
                    putStairs(g, bx, j, bz, bz == lz ? 1 : 3, roof);
                else if (edgeX)
                    putStairs(g, bx, j, bz, bx == lx ? 0 : 2, roof);
                else if (g.get(bx, j, bz) == AIR)
                {
                    g.put(bx, j, bz, fill);
                    g.plant[Grid::index(bx, j, bz)] = NO_PLANT;
                }
            }
    }
}

// The flowers that grow in beds and pots.
const int kBedFlowers[5] = {POPPY, DANDELION, OXEYE, ALLIUM, CORNFLOWER};

// Under about one window pane in three in a wall, outside: a trapdoor for
// a sill with a flower pot on it (a sill under every pane looked too
// busy). The panes are found in the wall's layer
// `paneLayer` between (x0, z0) and (x1, z1) (a straight run along x or
// z); `out` is the way out of the wall (kFacing). They are chosen by
// the distance from the middle of the wall, so the two halves mirror each
// other, as the walls do.
void dressWindows(Grid &g, int x0, int z0, int x1, int z1, int paneLayer, int out)
{
    const int ox = kFacing[out][0], oz = kFacing[out][1];
    const int sx = x1 > x0 ? 1 : (x1 < x0 ? -1 : 0), sz = z1 > z0 ? 1 : (z1 < z0 ? -1 : 0);
    const int length = abs(x1 - x0) + abs(z1 - z0);
    for (int k = 0, bx = x0, bz = z0; k <= length; ++k, bx += sx, bz += sz)
    {
        if (g.get(bx, paneLayer, bz) != WINDOW || g.get(bx, paneLayer - 1, bz) == WINDOW)
            continue;
        const int fromMiddle = abs(2 * k - length);
        const unsigned int h = hash(fromMiddle, paneLayer, 811u);
        if (h % 3u != 0u)
            continue;
        putTrapdoor(g, bx + ox, paneLayer - 1, bz + oz, (out + 2) % 4, true);
        putPot(g, bx + ox, paneLayer, bz + oz, kBedFlowers[(h >> 4) % 5u]);
    }
}

// Flowers along the foot of a building's walls, outside x0..x1, z0..z1,
// here and there, leaving the door's side (doorSide: kFacing, or -1) bare
// so the path reaches it. Chosen by the distance from the building's middle
// lines, so the beds mirror each other.
void flowerBeds(Grid &g, int x0, int z0, int x1, int z1, int floor, int doorSide)
{
    for (int bx = x0 - 1; bx <= x1 + 1; ++bx)
        for (int bz = z0 - 1; bz <= z1 + 1; ++bz)
        {
            const bool ring = bx == x0 - 1 || bx == x1 + 1 || bz == z0 - 1 || bz == z1 + 1;
            const bool corner = (bx == x0 - 1 || bx == x1 + 1) && (bz == z0 - 1 || bz == z1 + 1);
            if (!ring || corner)
                continue;
            const int side = bx == x1 + 1 ? 0 : (bz == z1 + 1 ? 1 : (bx == x0 - 1 ? 2 : 3));
            const unsigned int h = hash(abs(2 * bx - x0 - x1), abs(2 * bz - z0 - z1), 813u);
            if (side == doorSide || h % 5u > 1u || !Grid::inside(bx, floor, bz) ||
                g.get(bx, floor - 1, bz) != GRASS || g.get(bx, floor, bz) != AIR ||
                g.plant[Grid::index(bx, floor, bz)] != NO_PLANT)
                continue;
            g.plant[Grid::index(bx, floor, bz)] =
                static_cast<unsigned char>(kBedFlowers[(h >> 4) % 5u]);
        }
}

// A cottage as in a block game's plains village, shaped first, then
// coloured, then dressed: a cobble footing course, dark oak log posts at
// the corners and in the middle of each wall, glass between the posts two
// blocks high (they do not light up: the lamps do), a band of oak planks
// along the top, an oak door two blocks high in the middle of the front
// under a little roof of a step, a lantern hung under the eaves at each
// front corner; a gable roof of dark oak steps overhanging the walls by a
// block all round, its gable ends of oak planks set back under it, and a
// cobble chimney. Under the windows trapdoor sills, some with a flower pot,
// and flowers at the foot of the walls. The colours: the cobble's grey,
// dark oak, light oak, and the flowers. Walls run an even number of blocks
// between corners (x1 - x0 and z1 - z0 of 4 or 6), so the middle post and
// the windows either side of it are symmetrical.
void buildHouse(Grid &g, int x0, int z0, int x1, int z1, int floor, bool doorOnX)
{
    const int wallTop = floor + 3;
    footing(g, x0, x1, z0, z1, floor);
    fill(g, x0, x1, floor, floor, z0, z1, COBBLE);
    fill(g, x0, x1, floor + 1, wallTop - 1, z0, z1, DARK_OAK_LOG); // the posts
    fill(g, x0, x1, wallTop, wallTop, z0, z1, PLANKS);             // a band under the eaves
    // a room inside, with a plank floor, seen through the glass
    fill(g, x0 + 1, x1 - 1, floor, wallTop, z0 + 1, z1 - 1, AIR);
    fill(g, x0 + 1, x1 - 1, floor - 1, floor - 1, z0 + 1, z1 - 1, PLANKS);
    // posts at the corners and the middle of each wall, glass filling the
    // bays between them, two blocks high
    const int along[2] = {x1 - x0, z1 - z0};
    for (int side = 0; side < 2; ++side)
    {
        const int length = along[side];
        for (int k = 1; k < length; ++k)
        {
            if (k * 2 == length)
                continue; // the middle post
            for (int wall = 0; wall < 2; ++wall)
            {
                const int bx = side == 0 ? x0 + k : (wall == 0 ? x0 : x1);
                const int bz = side == 0 ? (wall == 0 ? z0 : z1) : z0 + k;
                g.put(bx, floor + 1, bz, WINDOW);
                g.put(bx, floor + 2, bz, WINDOW);
            }
        }
    }
    // the door, two blocks high, in the middle of the front wall in place
    // of its middle post, so the front is symmetrical, under a step for a
    // little roof; a lantern hangs under the eaves at each front corner,
    // high over the corner posts, out of the way of the door
    const int dx = doorOnX ? x1 : x0 + along[0] / 2;
    const int dz = doorOnX ? z0 + along[1] / 2 : z1;
    const int out = doorOnX ? 0 : 1; // the front faces +x or +z
    const int ox = kFacing[out][0], oz = kFacing[out][1];
    g.put(dx, floor, dz, DOOR);
    g.put(dx, floor + 1, dz, DOOR);
    g.put(dx, floor + 2, dz, DARK_OAK_LOG);
    putStairs(g, dx + ox, floor + 2, dz + oz, (out + 2) % 4, ROOF);
    for (int side = 0; side < 2; ++side)
    {
        if (doorOnX)
            putLamp(g, x1 + 1, wallTop, side == 0 ? z0 : z1);
        else
            putLamp(g, side == 0 ? x0 : x1, wallTop, z1 + 1);
    }
    // sills and pots under the windows of all four walls
    dressWindows(g, x0, z0, x1, z0, floor + 1, 3);
    dressWindows(g, x0, z1, x1, z1, floor + 1, 1);
    dressWindows(g, x0, z0, x0, z1, floor + 1, 2);
    dressWindows(g, x1, z0, x1, z1, floor + 1, 0);
    flowerBeds(g, x0, z0, x1, z1, floor, out);
    // the roof, along the longer side, a block of eaves all round
    const bool ridgeAlongX = along[0] >= along[1];
    if (ridgeAlongX)
        buildGableRoof(g, x0 - 1, x1 + 1, z0 - 1, z1 + 1, wallTop + 1, true, ROOF, PLANKS);
    else
        buildGableRoof(g, z0 - 1, z1 + 1, x0 - 1, x1 + 1, wallTop + 1, false, ROOF, PLANKS);
    // the chimney, up through the roof at the back, a block above it
    const int cx = ridgeAlongX ? x0 + 1 : (x0 + x1) / 2, cz = ridgeAlongX ? (z0 + z1) / 2 : z0 + 1;
    const int roofRise = ((ridgeAlongX ? along[1] : along[0]) + 2) / 2;
    for (int j = wallTop + 1; j <= wallTop + roofRise + 2; ++j)
    {
        g.put(cx, j, cz, COBBLE);
        g.plant[Grid::index(cx, j, cz)] = NO_PLANT;
    }
}

// A lamp post: on a stone brick foot, an oak fence two high with a lantern
// on it.
void buildLampPost(Grid &g, int bx, int bz, int floor)
{
    g.put(bx, floor, bz, COBBLE);
    putFence(g, bx, floor + 1, bz);
    putFence(g, bx, floor + 2, bz);
    putLamp(g, bx, floor + 3, bz);
}

// A trodden path at ground level (layer `top`): dirt path with gravel here
// and there, never under a building or in the train's way.
void putPath(Grid &g, const Clearance &clear, int bx, int top, int bz, bool edge)
{
    if (!Grid::insideColumn(bx, bz) || clear.blocks(bx, top, bz) || g.get(bx, top + 1, bz) != AIR ||
        isFixture(g.plant[Grid::index(bx, top + 1, bz)]))
        return;
    const int ground = g.get(bx, top, bz);
    if (ground != GRASS && ground != DIRT && ground != GRAVEL && ground != PATH)
        return;
    const unsigned int h = hash(bx, bz, 787u) % 10u;
    g.put(bx, top, bz, (edge && h < 4u) || h == 0u ? GRAVEL : PATH);
    g.plant[Grid::index(bx, top + 1, bz)] = NO_PLANT;
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
    // the lanes: three wide, crossing at the well, out to the edge of the
    // green; the houses' doors are joined to them by paths
    const int laneX = vx(-27), laneZ = vz(-30);
    for (int bx = vx(-37) - 6; bx <= vx(-19) + 6; ++bx)
        for (int w = -1; w <= 1; ++w)
            putPath(g, clear, bx, kVillageTop, laneZ + w, w != 0);
    for (int bz = vz(-37) - 6; bz <= vz(-20) + 6; ++bz)
        for (int w = -1; w <= 1; ++w)
            putPath(g, clear, laneX + w, kVillageTop, bz, w != 0);
    // a path from a door out to the nearer lane: a step out, then straight
    // along whichever way meets a lane sooner, then along it
    auto pathFromDoor = [&](int bx, int bz, int ox, int oz) {
        bx += ox;
        bz += oz;
        for (int step = 0; step < 40; ++step)
        {
            putPath(g, clear, bx, kVillageTop, bz, false);
            if (bx == laneX || bz == laneZ)
                break;
            if (abs(bx - laneX) <= abs(bz - laneZ))
                bx += bx < laneX ? 1 : -1;
            else
                bz += bz < laneZ ? 1 : -1;
        }
    };
    auto house = [&](int x0, int z0, int w, int d, bool doorOnX) {
        if (!roomForBuilding(clear, x0 - 2, x0 + w + 2, floor - 1, floor + 9, z0 - 2, z0 + d + 2))
            return;
        buildHouse(g, x0, z0, x0 + w, z0 + d, floor, doorOnX);
        if (doorOnX)
            pathFromDoor(x0 + w, z0 + d / 2, 1, 0);
        else
            pathFromDoor(x0 + w / 2, z0 + d, 0, 1);
    };

    house(vx(-35), vz(-37), 4, 4, false);
    house(vx(-24), vz(-36), 4, 4, false);
    house(vx(-36), vz(-26), 6, 4, true);
    // the newer cottages further out
    house(cx - 22, cz + 6, 4, 6, true);
    house(cx + 8, cz - 22, 6, 4, false);
    house(cx + 14, cz + 12, 4, 4, false);
    house(cx - 8, cz + 16, 6, 4, false);
    house(cx - 24, cz - 8, 4, 4, true);

    // the church: a cobble nave with oak log posts, tall glass windows and a
    // steep oak roof, and a bell tower at its west end with a lantern for a
    // bell; on the free plot of the green nearest its middle, clear of the
    // train
    // the field, the well and the windmill come later: the church keeps off
    // them, and off the houses already built
    const int fieldX = vx(-25), fieldZ = vz(-27), millX = vx(-22), millZ = vz(-20);
    auto overlaps = [](int a0, int a1, int b0, int b1) { return a0 <= b1 && b0 <= a1; };
    auto plotFree = [&](int x0, int x1, int z0, int z1) {
        if ((overlaps(x0, x1, fieldX - 2, fieldX + 10) &&
             overlaps(z0, z1, fieldZ - 2, fieldZ + 9)) ||
            (overlaps(x0, x1, laneX - 2, laneX + 2) && overlaps(z0, z1, laneZ - 2, laneZ + 2)) ||
            (overlaps(x0, x1, millX - 3, millX + 3) && overlaps(z0, z1, millZ - 3, millZ + 8)))
            return false;
        for (int bx = x0; bx <= x1; ++bx)
            for (int bz = z0; bz <= z1; ++bz)
                if (g.top(bx, bz) > kVillageTop)
                    return false;
        return true;
    };
    // the free plot on the green nearest its middle
    const int greenReach = static_cast<int>((kVillageRadius - 8.0f) / kBlock);
    int churchX = 0, churchZ = 0, churchD = 1 << 30;
    for (int x0 = cx - greenReach; x0 + 9 <= cx + greenReach; ++x0)
        for (int z0 = cz - greenReach; z0 + 6 <= cz + greenReach; ++z0)
        {
            const int x1 = x0 + 9, z1 = z0 + 6;
            const int mx = (x0 + x1) / 2 - cx, mz = (z0 + z1) / 2 - cz;
            const int d = mx * mx + mz * mz;
            // the plot's corner furthest out, squared, in each direction
            auto outmost = [](int lo, int hi) { return lo * lo > hi * hi ? lo * lo : hi * hi; };
            const int reach = outmost(x0 - 3 - cx, x1 + 2 - cx) + outmost(z0 - 2 - cz, z1 + 2 - cz);
            if (d >= churchD || reach > greenReach * greenReach ||
                !roomForBuilding(clear, x0 - 3, x1 + 2, floor - 1, floor + 16, z0 - 1, z1 + 1) ||
                !plotFree(x0 - 3, x1 + 2, z0 - 2, z1 + 2))
                continue;
            churchD = d;
            churchX = x0;
            churchZ = z0;
        }
    if (churchD < (1 << 30))
    {
        // stone bricks on a cobble footing course, dark oak posts, tall
        // windows two wide on sills, a steep roof of dark oak steps, and at
        // the west end a bell tower with a hipped cap; symmetrical: ten
        // long with posts every three, seven wide so the door, the windows
        // over it and the tower stand in the middle of the ends
        const int x0 = churchX, z0 = churchZ, x1 = x0 + 9, z1 = z0 + 6;
        footing(g, x0 - 1, x1, z0, z1, floor);
        fill(g, x0, x1, floor, floor, z0, z1, COBBLE);
        fill(g, x0, x1, floor + 1, floor + 4, z0, z1, STONE_BRICKS);
        fill(g, x0 + 1, x1 - 1, floor, floor + 4, z0 + 1, z1 - 1, AIR); // the inside
        fill(g, x0 + 1, x1 - 1, floor - 1, floor - 1, z0 + 1, z1 - 1, PLANKS);
        for (int bx = x0; bx <= x1; bx += 3)
            for (int j = floor + 1; j <= floor + 4; ++j)
            {
                g.put(bx, j, z0, DARK_OAK_LOG);
                g.put(bx, j, z1, DARK_OAK_LOG);
            }
        for (int bx = x0 + 1; bx < x1; ++bx)
            if ((bx - x0) % 3 != 0)
            {
                g.put(bx, floor + 2, z0, WINDOW);
                g.put(bx, floor + 3, z0, WINDOW);
                g.put(bx, floor + 2, z1, WINDOW);
                g.put(bx, floor + 3, z1, WINDOW);
            }
        dressWindows(g, x0, z0, x1, z0, floor + 2, 3);
        dressWindows(g, x0, z1, x1, z1, floor + 2, 1);
        // the door in the middle of the east end, under a step, a window
        // above it, a lantern on a post either side
        const int mz = (z0 + z1) / 2;
        g.put(x1, floor, mz, DOOR);
        g.put(x1, floor + 1, mz, DOOR);
        g.put(x1, floor + 3, mz, WINDOW);
        putStairs(g, x1 + 1, floor + 2, mz, 2, ROOF);
        for (int side = -1; side <= 1; side += 2)
        {
            putFence(g, x1 + 1, floor, mz + side);
            putLamp(g, x1 + 1, floor + 1, mz + side);
        }
        pathFromDoor(x1, mz, 1, 0);
        flowerBeds(g, x0, z0, x1, z1, floor, 0);
        // the bell tower: stone bricks with dark oak corners, open round the
        // bell (a hung lantern), under a hipped cap
        const int tz = mz - 1;
        fill(g, x0 - 2, x0, floor, floor, tz, tz + 2, COBBLE);
        fill(g, x0 - 2, x0, floor + 1, floor + 10, tz, tz + 2, STONE_BRICKS);
        for (int j = floor + 1; j <= floor + 10; ++j)
            for (int c = 0; c < 4; ++c)
                g.put(c % 2 == 0 ? x0 - 2 : x0, j, c < 2 ? tz : tz + 2, DARK_OAK_LOG);
        // open on all four sides round the bell (a hung lantern), a window
        // in the middle of each outer face below
        g.put(x0 - 1, floor + 9, tz, AIR);
        g.put(x0 - 1, floor + 9, tz + 2, AIR);
        g.put(x0 - 2, floor + 9, tz + 1, AIR);
        g.put(x0, floor + 9, tz + 1, AIR);
        g.put(x0 - 1, floor + 9, tz + 1, AIR);
        putLamp(g, x0 - 1, floor + 9, tz + 1);
        g.put(x0 - 1, floor + 6, tz, WINDOW);
        g.put(x0 - 1, floor + 6, tz + 2, WINDOW);
        g.put(x0 - 2, floor + 6, tz + 1, WINDOW);
        buildHipRoof(g, x0 - 3, x0 + 1, tz - 1, tz + 3, floor + 11, ROOF, STONE_BRICKS);
        // the nave's roof, after the tower, so it stops against it, a
        // window in the middle of the east gable
        buildGableRoof(g, x0 - 1, x1 + 1, z0 - 1, z1 + 1, floor + 5, true, ROOF, STONE_BRICKS);
        g.put(x1, floor + 6, mz, WINDOW);
    }

    // the well at the crossing
    const int wx = vx(-27), wz = vz(-30);
    if (roomForBuilding(clear, wx - 1, wx + 1, floor, floor + 3, wz - 1, wz + 1))
    {
        // a ring of cobble round the water, fence posts at the corners and
        // a little hipped roof of dark oak steps, as in a block game's
        // village well
        fill(g, wx - 1, wx + 1, floor, floor, wz - 1, wz + 1, COBBLE);
        g.put(wx, floor, wz, WATER);
        for (int dx = -1; dx <= 1; dx += 2)
            for (int dz = -1; dz <= 1; dz += 2)
            {
                putFence(g, wx + dx, floor + 1, wz + dz);
                putFence(g, wx + dx, floor + 2, wz + dz);
            }
        buildHipRoof(g, wx - 1, wx + 1, wz - 1, wz + 1, floor + 3, ROOF, ROOF);
    }

    // the wheat field, with a ditch down the middle and hay bales beside it
    const int fx = fieldX, fz = fieldZ;
    for (int bx = fx; bx <= fx + 8; ++bx)
        for (int bz = fz; bz <= fz + 7; ++bz)
        {
            // never under a building
            if (clear.blocks(bx, floor, bz) || g.top(bx, bz) > kVillageTop)
                continue;
            g.put(bx, kVillageTop, bz, bx == fx + 4 ? WATER : FARMLAND);
            if (bx != fx + 4)
                g.plant[Grid::index(bx, floor, bz)] = WHEAT;
        }
    // bordered with oak logs at ground level, as a village farm is
    for (int bx = fx - 1; bx <= fx + 9; ++bx)
        for (int bz = fz - 1; bz <= fz + 8; ++bz)
            if ((bx == fx - 1 || bx == fx + 9 || bz == fz - 1 || bz == fz + 8) &&
                !clear.blocks(bx, kVillageTop, bz) && g.get(bx, floor, bz) == AIR)
            {
                g.put(bx, kVillageTop, bz, OAK_LOG);
                g.plant[Grid::index(bx, floor, bz)] = NO_PLANT;
            }
    g.put(fx - 1, floor, fz + 3, HAY);
    g.put(fx - 1, floor, fz + 4, HAY);
    g.put(fx - 1, floor + 1, fz + 4, HAY);

    // the windmill: a stone tower with a timber top, the sails facing south
    const int mx = millX, mz = millZ;
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
        // a hipped cap of dark oak steps over the timber top
        buildHipRoof(g, mx - 2, mx + 2, mz - 2, mz + 2, floor + 7, ROOF, PLANKS);
        g.put(mx, floor, mz + 1, DOOR);
        g.put(mx, floor + 1, mz + 1, DOOR);
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
        if (roomForBuilding(clear, lamp[0], lamp[0], floor, floor + 3, lamp[1], lamp[1]))
            buildLampPost(g, lamp[0], lamp[1], floor);
}

// A tower: a square shaft of stone bricks on a cobble foot, mossy here and
// there, with narrow windows, a band of upside-down steps standing out
// under the eaves, a hipped roof of dark oak steps overhanging it by a
// block and a lantern on top. x0, z0: its corner; size: its width.
void buildTower(Grid &g, int x0, int z0, int size, int floor, int height)
{
    const int x1 = x0 + size - 1, z1 = z0 + size - 1;
    for (int bx = x0; bx <= x1; ++bx)
        for (int bz = z0; bz <= z1; ++bz)
            for (int j = floor; j < floor + height; ++j)
                g.put(bx, j, bz,
                      j < floor + 2
                          ? COBBLE
                          : (hash(bx * 5 + j, bz, 467u) % 7u == 0 ? MOSSY_BRICKS : STONE_BRICKS));
    // windows in the middle of each face: one where the face is odd, a
    // pair where it is even (halved rounding down, as the castle stands at
    // negative x, where / rounds towards zero and put them a block off)
    for (int j = floor + 3; j < floor + height - 1; j += 3)
        for (int m = floorHalf(x0 + x1); m <= floorHalf(x0 + x1 + 1); ++m)
        {
            const int n = z0 + (m - x0); // the same place along the z faces
            g.put(m, j, z0, WINDOW);
            g.put(m, j, z1, WINDOW);
            g.put(x0, j, n, WINDOW);
            g.put(x1, j, n, WINDOW);
        }
    // a corbel of upside-down steps round the top, the roof on it
    const int top = floor + height;
    for (int bx = x0 - 1; bx <= x1 + 1; ++bx)
        for (int bz = z0 - 1; bz <= z1 + 1; ++bz)
        {
            const bool ring = bx == x0 - 1 || bx == x1 + 1 || bz == z0 - 1 || bz == z1 + 1;
            const bool corner = (bx == x0 - 1 || bx == x1 + 1) && (bz == z0 - 1 || bz == z1 + 1);
            if (!ring || corner)
                continue;
            const int rises = bx == x0 - 1 ? 0 : (bx == x1 + 1 ? 2 : (bz == z0 - 1 ? 1 : 3));
            putStairs(g, bx, top - 1, bz, rises, STONE_BRICKS, true);
        }
    buildHipRoof(g, x0 - 1, x1 + 1, z0 - 1, z1 + 1, top, ROOF, STONE_BRICKS);
    if (size % 2 == 1)
    {
        // an odd tower has a middle: a block at the point, a lantern on it
        const int peak = top + (size + 1) / 2, mx = (x0 + x1) / 2, mz = (z0 + z1) / 2;
        g.put(mx, peak, mz, ROOF);
        g.plant[Grid::index(mx, peak, mz)] = NO_PLANT;
        putLamp(g, mx, peak + 1, mz);
    }
    else
        // an even one comes to a point of four corner steps: a lantern
        // hangs under each corner of the eaves instead
        for (int c = 0; c < 4; ++c)
            putLamp(g, c % 2 == 0 ? x0 - 1 : x1 + 1, top - 1, c < 2 ? z0 - 1 : z1 + 1);
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
            for (int j = floor; j <= wallTop; ++j)
                g.put(bx, j, bz,
                      j == floor
                          ? COBBLE
                          : (hash(bx * 3 + j, bz, 463u) % 6u == 0 ? MOSSY_BRICKS : STONE_BRICKS));
            if ((bx + bz) % 2 == 0)
                g.put(bx, wallTop + 1, bz, STONE_BRICKS);
        }
    // the gate: an opening three wide and three high, lanterns either side
    const int gx = (x0 + x1) / 2, gz = gateNorth ? z1 : z0;
    fill(g, gx - 1, gx + 1, floor, floor + 2, gz, gz, AIR);
    fill(g, gx - 1, gx + 1, floor + 3, floor + 3, gz, gz, DARK_OAK_LOG);
    const int out = gateNorth ? 1 : -1;
    // a lantern on a post either side of the gate
    for (int side = -2; side <= 2; side += 4)
    {
        putFence(g, gx + side, floor, gz + out);
        putFence(g, gx + side, floor + 1, gz + out);
        putLamp(g, gx + side, floor + 2, gz + out);
    }
    // a road three wide out of the gate, following the ground until it
    // falls away, lamp posts in pairs either side of it
    for (int d = 2; d <= 14; ++d)
    {
        const int bz = gz + out * d;
        const int top = Grid::insideColumn(gx, bz) ? g.top(gx, bz) : kMinY - 1;
        if (top < floor - 5 || top > floor)
            break;
        for (int w = -1; w <= 1; ++w)
        {
            // gravel where it crosses the sand, a trodden path elsewhere
            const int t = g.top(gx + w, bz);
            if (g.get(gx + w, t, bz) == SAND && !clear.blocks(gx + w, t, bz) &&
                g.get(gx + w, t + 1, bz) == AIR)
                g.put(gx + w, t, bz, GRAVEL);
            else
                putPath(g, clear, gx + w, t, bz, w != 0);
        }
        if (d % 6 == 0)
            for (int side = -2; side <= 2; side += 4)
            {
                const int lx = gx + side, lt = g.top(lx, bz);
                if (lt == top && g.opaque(lx, lt, bz) &&
                    roomForBuilding(clear, lx, lx, lt + 1, lt + 4, bz, bz) &&
                    g.get(lx, lt + 1, bz) == AIR)
                    buildLampPost(g, lx, bz, lt + 1);
            }
    }

    // corner towers, four wide, rising well above the wall
    buildTower(g, x0 - 1, z0 - 1, 4, floor, 10);
    buildTower(g, x1 - 2, z0 - 1, 4, floor, 10);
    buildTower(g, x0 - 1, z1 - 2, 4, floor, 10);
    buildTower(g, x1 - 2, z1 - 2, 4, floor, 10);

    // the keep at the back of the court, away from the gate: two floors of
    // stone, three of timber between log posts, rows of lit windows
    const int kx0 = x0 + 3, kx1 = x1 - 3;
    const int kz0 = gateNorth ? z0 + 2 : z1 - 7, kz1 = kz0 + 5;
    fill(g, kx0, kx1, floor, floor, kz0, kz1, COBBLE);
    fill(g, kx0, kx1, floor + 1, floor + 2, kz0, kz1, STONE_BRICKS);
    fill(g, kx0, kx1, floor + 3, floor + 8, kz0, kz1, PLANKS);
    // the timber floors jut out over the stone on a row of upside-down steps
    for (int bx = kx0; bx <= kx1; ++bx)
    {
        putStairs(g, bx, floor + 2, kz0 - 1, 1, STONE_BRICKS, true);
        putStairs(g, bx, floor + 2, kz1 + 1, 3, STONE_BRICKS, true);
    }
    // log posts at the ends, a quarter and the middle (the keep is nine
    // long), windows between them, so the front mirrors itself
    const int keepPosts[5] = {0, 2, 4, 6, 8};
    for (int j = floor + 3; j <= floor + 8; ++j)
        for (int p : keepPosts)
        {
            g.put(kx0 + p, j, kz0, OAK_LOG);
            g.put(kx0 + p, j, kz1, OAK_LOG);
        }
    for (int j = floor + 1; j <= floor + 7; j += 3)
    {
        for (int bx = kx0 + 1; bx < kx1; bx += 2)
        {
            g.put(bx, j, kz0, WINDOW);
            g.put(bx, j, kz1, WINDOW);
        }
        // the ends are six deep: a pair of windows in the middle
        for (int m = floorHalf(kz0 + kz1); m <= floorHalf(kz0 + kz1 + 1); ++m)
        {
            g.put(kx0, j, m, WINDOW);
            g.put(kx1, j, m, WINDOW);
        }
    }
    g.put((kx0 + kx1) / 2, floor, gateNorth ? kz1 : kz0, DOOR);
    g.put((kx0 + kx1) / 2, floor + 1, gateNorth ? kz1 : kz0, DOOR);
    // its roof: a gable of dark oak steps along x, the gables of planks
    buildGableRoof(g, kx0 - 1, kx1 + 1, kz0 - 1, kz1 + 1, floor + 9, true, ROOF, PLANKS);

    // the main tower rising out of the keep: five wide, in the middle of
    // it, against its back wall
    const int tx = (kx0 + kx1) / 2 - 2;
    buildTower(g, tx, gateNorth ? kz0 : kz1 - 4, 5, floor, 20);

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
    fill(g, x - 2, x + 2, floor, floor, z - 2, z + 2, STONE_BRICKS);
    // the base's edge: a ring of stone brick steps up to it
    for (int bx = x - 3; bx <= x + 3; ++bx)
        for (int bz = z - 3; bz <= z + 3; ++bz)
            if ((abs(bx - x) == 3) != (abs(bz - z) == 3) && g.get(bx, floor - 1, bz) != AIR)
                putStairs(g, bx, floor, bz,
                          bx == x - 3 ? 0 : (bx == x + 3 ? 2 : (bz == z - 3 ? 1 : 3)),
                          STONE_BRICKS);
    // the banded tower
    for (int j = floor + 1; j <= floor + 13; ++j)
    {
        const int band = ((j - floor - 1) / 2) % 2 == 0 ? CLAY_WHITE : CLAY_RED;
        fill(g, x - 1, x + 1, j, j, z - 1, z + 1, band);
    }
    g.put(x, floor + 1, z + 1, DOOR);
    g.put(x, floor + 2, z + 1, DOOR);
    // windows in the middle of every face, at two heights
    for (int j = floor + 6; j <= floor + 10; j += 4)
    {
        g.put(x, j, z - 1, WINDOW);
        g.put(x, j, z + 1, WINDOW);
        g.put(x - 1, j, z, WINDOW);
        g.put(x + 1, j, z, WINDOW);
    }
    // the gallery on a ring of upside-down steps, with a fence for a rail,
    // the lamp room and the cap
    for (int bx = x - 2; bx <= x + 2; ++bx)
        for (int bz = z - 2; bz <= z + 2; ++bz)
            if ((abs(bx - x) == 2) != (abs(bz - z) == 2))
                putStairs(g, bx, floor + 13, bz,
                          bx == x - 2 ? 0 : (bx == x + 2 ? 2 : (bz == z - 2 ? 1 : 3)), STONE_BRICKS,
                          true);
    fill(g, x - 2, x + 2, floor + 14, floor + 14, z - 2, z + 2, STONE_BRICKS);
    for (int bx = x - 2; bx <= x + 2; ++bx)
        for (int bz = z - 2; bz <= z + 2; ++bz)
            if (abs(bx - x) == 2 || abs(bz - z) == 2)
                putFence(g, bx, floor + 15, bz);
    // the lamp room: glass all round, lanterns inside, one standing and
    // one hanging above it
    fill(g, x - 1, x + 1, floor + 15, floor + 16, z - 1, z + 1, WINDOW);
    g.put(x, floor + 15, z, AIR);
    g.put(x, floor + 16, z, AIR);
    putLamp(g, x, floor + 15, z);
    putLamp(g, x, floor + 16, z);
    buildHipRoof(g, x - 2, x + 2, z - 2, z + 2, floor + 17, ROOF, ROOF);
    // the keeper's cottage on the landward side
    const float len = sqrtf(static_cast<float>(x * x + z * z));
    const int hx = x - static_cast<int>(floorf(static_cast<float>(x) / len * 7.0f + 0.5f)) - 2;
    const int hz = z - static_cast<int>(floorf(static_cast<float>(z) / len * 7.0f + 0.5f)) - 2;
    const int hfloor = g.top(hx + 2, hz + 2) + 1;
    if (hfloor >= 1 &&
        roomForBuilding(clear, hx - 1, hx + 5, hfloor - 1, hfloor + 8, hz - 1, hz + 5))
    {
        buildHouse(g, hx, hz, hx + 4, hz + 3, hfloor, false);
        // a path from the cottage's door to the tower's
        int bx = hx + 2, bz = hz + 4;
        for (int step = 0; step < 40; ++step)
        {
            if (Grid::insideColumn(bx, bz))
                putPath(g, clear, bx, g.top(bx, bz), bz, false);
            if (bx == x && bz == z + 4)
                break;
            if (bz != z + 4)
                bz += bz < z + 4 ? 1 : -1;
            else
                bx += bx < x ? 1 : -1;
        }
    }
}

// A timber footbridge over the pool below the fall, three blocks above the
// water, resting on the gorge's walls at both ends: a deck of planks two
// wide between dark oak beams, a fence rail along both sides and a lantern
// on the rail at each corner, symmetrical; left out if it would not reach
// rock on both sides or the train would pass through it.
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
    if (!roomForBuilding(clear, x0, x1, deck, deck + 3, z0 - 1, z1 + 1))
        return;
    for (int bx = x0; bx <= x1; ++bx)
    {
        for (int bz = z0; bz <= z1; ++bz)
            if (g.get(bx, deck, bz) == AIR || bx == x0 || bx == x1)
                g.put(bx, deck, bz, PLANKS);
        // the beams either side, a rail on them over the open span
        for (int bz = z0 - 1; bz <= z1 + 1; bz += z1 + 2 - z0)
            if (g.get(bx, deck, bz) == AIR)
            {
                g.put(bx, deck, bz, DARK_OAK_LOG);
                putFence(g, bx, deck + 1, bz);
            }
    }
    // a lantern on the rail at each of the four corners of the span
    for (int bx = x0; bx <= x1; ++bx)
        if (bx == x0 + 1 || bx == x1 - 1)
            for (int bz = z0 - 1; bz <= z1 + 1; bz += z1 + 2 - z0)
                if (Grid::inside(bx, deck + 1, bz) &&
                    g.plant[Grid::index(bx, deck + 1, bz)] == FENCE)
                    putLamp(g, bx, deck + 2, bz);
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

// Boulders: a block or two of stone, cobble or mossy cobble with a few
// beside it, half sunk into the ground, here and there on the meadows,
// the crags and more often in the spruce forest and among the bamboo,
// on level ground away from the water, the buildings and the train.
void scatterBoulders(Grid &g, const Clearance &clear)
{
    for (int bx = kMinBX + 3; bx < kMaxBX - 3; ++bx)
        for (int bz = kMinBZ + 3; bz < kMaxBZ - 3; ++bz)
        {
            const int biome = g.biome[Grid::column(bx, bz)];
            const unsigned int h = hash(bx, bz, 817u);
            // on the beaches too, a few more than on the meadows, the sand
            // between them left open
            const int ground = g.get(bx, g.top(bx, bz), bz);
            const bool beach = biome == MEADOW && (ground == SAND || ground == GRAVEL);
            const unsigned int every = beach                    ? 90u
                                       : biome == ALPINE        ? 60u
                                       : biome == BAMBOO_JUNGLE ? 110u
                                       : biome == CRAG          ? 80u
                                       : biome == MEADOW        ? 150u
                                                                : 0u;
            if (every == 0u || h % every != 0u)
                continue;
            const float vx = centerOf(bx) - kVillageX, vz = centerOf(bz) - kVillageZ;
            if (vx * vx + vz * vz < (kVillageRadius + 10.0f) * (kVillageRadius + 10.0f))
                continue;
            const int top = g.top(bx, bz);
            bool fits = beach ? top >= kSeaLayer : top > kSeaLayer;
            for (int dx = -1; dx <= 1 && fits; ++dx)
                for (int dz = -1; dz <= 1 && fits; ++dz)
                {
                    const int t = g.top(bx + dx, bz + dz), b = g.get(bx + dx, t, bz + dz);
                    fits =
                        (b == GRASS || b == PODZOL || b == STONE || b == DIRT || b == SNOWY_DIRT ||
                         b == SNOW || b == COARSE_DIRT || b == SAND || b == GRAVEL) &&
                        abs(t - top) <= 1;
                    for (int j = t + 1; j <= t + 3 && fits; ++j)
                        fits = g.get(bx + dx, j, bz + dz) == AIR &&
                               !isFixture(g.plant[Grid::index(bx + dx, j, bz + dz)]) &&
                               !clear.blocks(bx + dx, j, bz + dz);
                }
            if (!fits)
                continue;
            auto rock = [&](int x, int j, int z) {
                const unsigned int r = hash(x * 3 + j, z, 819u) % 10u;
                int b = STONE;
                if (biome == ALPINE || biome == BAMBOO_JUNGLE)
                    b = r < 6u ? MOSSY : COBBLE;
                else if (biome == MEADOW)
                    b = r < 4u ? STONE : (r < 7u ? COBBLE : MOSSY);
                else
                    b = r < 6u ? STONE : COBBLE;
                g.put(x, j, z, b);
                g.plant[Grid::index(x, j, z)] = NO_PLANT;
            };
            // the middle, sunk a block, one or two high above the ground
            rock(bx, top, bz);
            const int height = 1 + static_cast<int>((h >> 8) % 2u);
            for (int j = 1; j <= height; ++j)
                rock(bx, top + j, bz);
            // some of the four beside it, a block high
            for (int k = 0; k < 4; ++k)
                if ((h >> (12 + k)) & 1u)
                {
                    const int x = bx + kFacing[k][0], z = bz + kFacing[k][1];
                    rock(x, g.top(x, z) + 1, z);
                }
        }
}

// Lanterns in niches and crystals in the walls where the ride runs under a
// roof.
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
        // now and then a crystal grown in the rock; otherwise a lantern in
        // a niche cut a block into the wall, opening on the tunnel, standing
        // on its sill under a roof of rock (a glowing block set in the wall
        // would look like a lamp left on all day)
        const float in = kCarveRadius + 1.6f - kBlock; // the cell in front of it
        const int fx = blockOf(x + dz / step * s * in), fz = blockOf(z - dx / step * s * in);
        const bool opens = g.get(fx, j, fz) == AIR;
        if (hash(bx, bz, 71u) % 3u == 0 || !opens || !g.solid(bx, j - 1, bz) ||
            !g.solid(bx, j + 1, bz))
        {
            g.put(bx, j, bz, CRYSTAL);
            continue;
        }
        g.put(bx, j, bz, AIR);
        putLamp(g, bx, j, bz);
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
            (g.get(p.bx, p.j, p.bz) != AIR && !isGrowth(g.get(p.bx, p.j, p.bz))) ||
            isFixture(g.plant[Grid::index(p.bx, p.j, p.bz)]))
            return false;
    for (const Placed &p : blocks)
        if (g.get(p.bx, p.j, p.bz) == AIR)
        {
            g.put(p.bx, p.j, p.bz, p.block);
            g.plant[Grid::index(p.bx, p.j, p.bz)] = NO_PLANT;
        }
    return true;
}

// A round-crowned tree: oak, birch or an autumn one, as a block game grows
// them, but no two alike: the crown is four or five layers, the outer
// leaves of each layer gone here and there (more of them on the corners
// and on the top), and a tree now and then has a leaf or two sticking out
// beyond the rest, so a wood is not rows of the same green box.
void roundTree(std::vector<Placed> &out, int bx, int base, int bz, int log, int leaves, int trunk,
               bool wide)
{
    for (int k = 0; k < trunk; ++k)
        out.push_back(Placed{bx, base + k, bz, log});
    const unsigned int seed = hash(bx, bz, 71u);
    const int layers = 4 + static_cast<int>(seed % 2u);
    const int crown = base + trunk - 2 - (layers - 4);
    for (int layer = 0; layer < layers; ++layer)
    {
        const int fromTop = layers - 1 - layer;
        const int radius = fromTop >= 2 ? (wide ? 3 : 2) : (fromTop == 1 ? (wide ? 2 : 1) : 1);
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
            {
                if (dx == 0 && dz == 0 && fromTop >= 2)
                    continue; // the trunk
                const bool edge = dx == radius || dx == -radius || dz == radius || dz == -radius;
                const bool corner =
                    (dx == radius || dx == -radius) && (dz == radius || dz == -radius);
                const unsigned int r = hash(bx * 3 + dx, bz * 5 + dz + layer * 17, 73u) % 8u;
                if (corner && (fromTop == 0 || r < 5u))
                    continue;
                if (edge && !corner && radius >= 2 && r < (fromTop == 0 ? 4u : 2u))
                    continue;
                out.push_back(Placed{bx + dx, crown + layer, bz + dz, leaves});
            }
    }
    // a leaf sticking out from the widest layer
    if (seed % 3u == 0u)
    {
        const int r = wide ? 4 : 3;
        const int side = static_cast<int>((seed >> 4) % 4u);
        const int ox = side == 0 ? r : (side == 1 ? -r : 0),
                  oz = side == 2 ? r : (side == 3 ? -r : 0);
        out.push_back(Placed{bx + ox, crown + 1, bz + oz, leaves});
    }
}

// A cherry tree as a block game grows one: a short trunk that splits into
// two or three branches leaning out and up, each ending in a wide, flat
// crown of blossom, its underside hanging down in places, so the grove is
// a pink roof on crooked dark trunks rather than stacked pyramids.
void cherryTree(std::vector<Placed> &out, int bx, int base, int bz, unsigned int h)
{
    const int trunk = 3 + static_cast<int>(h % 2u);
    for (int k = 0; k < trunk; ++k)
        out.push_back(Placed{bx, base + k, bz, DARK_OAK_LOG});
    const int branches = 2 + static_cast<int>((h >> 3) % 2u);
    const float start = static_cast<float>((h >> 5) % 360u) * 0.0174533f;
    for (int b = 0; b < branches; ++b)
    {
        const float a = start + 6.2831853f * static_cast<float>(b) / static_cast<float>(branches) +
                        (static_cast<float>((h >> (8 + b * 3)) % 7u) - 3.0f) * 0.12f;
        const float ux = cosf(a), uz = sinf(a);
        const int length = 2 + static_cast<int>((h >> (12 + b * 2)) % 2u);
        int ex = bx, ey = base + trunk - 1, ez = bz;
        for (int k = 1; k <= length; ++k)
        {
            ex = bx + static_cast<int>(lroundf(ux * static_cast<float>(k) * 1.2f));
            ez = bz + static_cast<int>(lroundf(uz * static_cast<float>(k) * 1.2f));
            ey = base + trunk - 1 + k;
            out.push_back(Placed{ex, ey, ez, DARK_OAK_LOG});
        }
        // the crown at the branch's end: a flat dome, widest a block above it
        for (int dy = -1; dy <= 2; ++dy)
        {
            const float radius = dy == -1 ? 2.6f : (dy == 0 ? 3.6f : (dy == 1 ? 3.1f : 1.9f));
            for (int dx = -4; dx <= 4; ++dx)
                for (int dz = -4; dz <= 4; ++dz)
                {
                    const float d = sqrtf(static_cast<float>(dx * dx + dz * dz)) +
                                    (lattice(ex + dx * 7, ez + dz * 3 + dy, 79u) - 0.5f) * 0.9f;
                    if (d > radius || (dx == 0 && dz == 0 && dy < 1))
                        continue;
                    out.push_back(Placed{ex + dx, ey + dy, ez + dz, LEAVES_CHERRY});
                    // blossom hanging from the underside
                    if (dy == -1 && hash(ex + dx, ez + dz, 81u) % 4u == 0u)
                        out.push_back(Placed{ex + dx, ey - 2, ez + dz, LEAVES_CHERRY});
                }
        }
    }
}

// A big oak, as a block game sometimes grows one: a taller trunk with
// three or four branches reaching out at different heights, each with
// its own ball of leaves, and a crown on top, so the big trees of a wood
// stand out from the small ones.
void bigOak(std::vector<Placed> &out, int bx, int base, int bz, unsigned int h)
{
    const int trunk = 7 + static_cast<int>(h % 3u);
    for (int k = 0; k < trunk; ++k)
        out.push_back(Placed{bx, base + k, bz, OAK_LOG});
    auto ball = [&](int cx, int cy, int cz, float radius) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -3; dx <= 3; ++dx)
                for (int dz = -3; dz <= 3; ++dz)
                {
                    const float d = sqrtf(static_cast<float>(dx * dx + dz * dz) +
                                          1.8f * static_cast<float>(dy * dy)) +
                                    (lattice(cx + dx * 5, cz + dz * 7 + dy, 83u) - 0.5f) * 0.8f;
                    if (d <= radius)
                        out.push_back(Placed{cx + dx, cy + dy, cz + dz, LEAVES_OAK});
                }
    };
    const int branches = 3 + static_cast<int>((h >> 3) % 2u);
    for (int b = 0; b < branches; ++b)
    {
        const float a = static_cast<float>((h >> 5) % 360u) * 0.0174533f +
                        6.2831853f * static_cast<float>(b) / static_cast<float>(branches);
        const int from = base + trunk - 4 + b % 3;
        const int length = 2 + static_cast<int>((h >> (9 + b)) % 2u);
        int ex = bx, ey = from, ez = bz;
        for (int k = 1; k <= length; ++k)
        {
            ex = bx + static_cast<int>(lroundf(cosf(a) * static_cast<float>(k)));
            ez = bz + static_cast<int>(lroundf(sinf(a) * static_cast<float>(k)));
            ey = from + k / 2;
            out.push_back(Placed{ex, ey, ez, OAK_LOG});
        }
        ball(ex, ey + 1, ez, 2.4f);
    }
    ball(bx, base + trunk, bz, 2.9f);
}

// A spruce as a block game grows one: the trunk ends a block under a
// single leaf at the top, and from there down the layers of needles go
// radius 1, 2, 1, 2... (each a square with its corners cut, the lowest a
// little wider on a tall tree), so the trunk never shows inside the crown;
// it is bare only for the block or two above the ground (bare). Where it
// snows the snow settles on it in a thin layer (snowfall()).
void spruce(std::vector<Placed> &out, int bx, int base, int bz, int trunk, int bare)
{
    for (int k = 0; k < trunk; ++k)
        out.push_back(Placed{bx, base + k, bz, SPRUCE_LOG});
    out.push_back(Placed{bx, base + trunk, bz, LEAVES_SPRUCE}); // the tip
    const int lowest = base + bare;
    for (int j = base + trunk - 1, layer = 0; j >= lowest; --j, ++layer)
    {
        int radius = layer % 2 == 0 ? 1 : 2;
        if (j == lowest && trunk >= 8)
            radius = 3; // a wider skirt at the foot of a tall one
        for (int dx = -radius; dx <= radius; ++dx)
            for (int dz = -radius; dz <= radius; ++dz)
            {
                const bool corner = abs(dx) == radius && abs(dz) == radius;
                if ((dx == 0 && dz == 0) || corner ||
                    (radius == 3 && abs(dx) + abs(dz) > 4)) // round off the skirt
                    continue;
                out.push_back(Placed{bx + dx, j, bz + dz, LEAVES_SPRUCE});
            }
    }
}

// A giant spruce, as in a block game's old growth taiga: a trunk two by
// two and very tall, bare for most of its height, under a cone of needles
// that widens a block every two layers down from a cap of four leaves; the
// cone's layers are rounded. bx, bz: the trunk's corner with the lowest x
// and z.
void megaSpruce(std::vector<Placed> &out, int bx, int base, int bz, int height)
{
    for (int k = 0; k < height; ++k)
        for (int t = 0; t < 4; ++t)
            out.push_back(Placed{bx + t % 2, base + k, bz + t / 2, SPRUCE_LOG});
    const int crown = height * 2 / 5 + 2; // layers of needles from the top down
    for (int layer = 0; layer <= crown; ++layer)
    {
        const int j = base + height - layer; // the cap sits on the trunk
        const int radius = layer == 0 ? 0 : (layer + 1) / 2;
        for (int dx = -radius; dx <= 1 + radius; ++dx)
            for (int dz = -radius; dz <= 1 + radius; ++dz)
            {
                const bool trunk = dx >= 0 && dx <= 1 && dz >= 0 && dz <= 1 && layer > 0;
                // distance out from the trunk's edge, for a round layer
                const int ox = dx < 0 ? -dx : (dx > 1 ? dx - 1 : 0);
                const int oz = dz < 0 ? -dz : (dz > 1 ? dz - 1 : 0);
                if (trunk || (radius >= 2 && ox * ox + oz * oz > radius * radius + 1))
                    continue;
                out.push_back(Placed{bx + dx, j, bz + dz, LEAVES_SPRUCE});
            }
    }
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
                b == MUSHROOM_STEM || b == CACTUS || (b >= PLANKS && b <= DOOR) || b == COBBLE)
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
    cherryTree(tree, bestX, level + 2, bestZ, 5u);
    commit(g, clear, tree);
    // the jetty from the south bank
    for (int z = bestZ + radius + 1; z >= bestZ + radius - 5; --z)
    {
        g.put(bestX + 6, level + 1, z, PLANKS);
        if (z % 2 == 0)
            g.put(bestX + 6, level, z, OAK_LOG);
    }
    putLamp(g, bestX + 6, level + 2, bestZ + radius - 5);
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

// A log cabin: spruce log walls on a cobble footing course, a door in the
// middle of the front (the z1 side, or z0 if frontZ0) under a step, a
// lantern under the eaves at each front corner (not in the doorway), a
// window on a trapdoor sill either side of the door and
// two at the back, a gable roof of dark oak steps with spruce plank gables,
// overhanging by a block, and a cobble chimney. Spruce, dark oak and
// cobble. w should be even, so the door has a middle.
void buildCabin(Grid &g, int x0, int z0, int w, int d, int floor, bool frontZ0)
{
    const int x1 = x0 + w, z1 = z0 + d, wallTop = floor + 2;
    footing(g, x0, x1, z0, z1, floor);
    fill(g, x0, x1, floor, floor, z0, z1, COBBLE);
    fill(g, x0, x1, floor + 1, wallTop, z0, z1, SPRUCE_LOG);
    fill(g, x0 + 1, x1 - 1, floor, wallTop, z0 + 1, z1 - 1, AIR);
    fill(g, x0 + 1, x1 - 1, floor - 1, floor - 1, z0 + 1, z1 - 1, SPRUCE_PLANKS);
    const int mx = (x0 + x1) / 2;
    for (int side = -1; side <= 1; side += 2)
    {
        g.put(mx + side, floor + 1, z0, WINDOW);
        g.put(mx + side, floor + 1, z1, WINDOW);
    }
    const int fz = frontZ0 ? z0 : z1, out = frontZ0 ? -1 : 1;
    g.put(mx, floor, fz, DOOR);
    g.put(mx, floor + 1, fz, DOOR);
    putStairs(g, mx, floor + 2, fz + out, frontZ0 ? 1 : 3, ROOF);
    putLamp(g, x0, wallTop, fz + out);
    putLamp(g, x1, wallTop, fz + out);
    dressWindows(g, x0, z0, x1, z0, floor + 1, 3);
    dressWindows(g, x0, z1, x1, z1, floor + 1, 1);
    buildGableRoof(g, x0 - 1, x1 + 1, z0 - 1, z1 + 1, wallTop + 1, true, ROOF, SPRUCE_PLANKS);
    for (int j = wallTop + 1; j <= wallTop + 5; ++j) // the chimney
    {
        g.put(x0 + 1, j, z0 + 1, COBBLE);
        g.plant[Grid::index(x0 + 1, j, z0 + 1)] = NO_PLANT;
    }
}

// A camp in the spruce forest at the foot of the snow mountain, where the
// train passes in sight of it: log cabins round a campfire, lanterns on
// posts and by the doors, warm against the blue of dusk.
// The camp's cabins round its middle: x, z of a corner, and width.
// They mirror each other across the fire, their doors facing it.
const int kCabins[4][3] = {{-8, -8, 4}, {4, -8, 4}, {-8, 4, 4}, {4, 4, 4}};

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
            g.put(x + dx, floor - 1, z + dz, (dx == 0 && dz == 0) ? MAGMA : COBBLE); // embers
    g.put(x - 1, floor, z, SPRUCE_LOG);
    g.put(x + 1, floor, z, SPRUCE_LOG);
    // the cabins round the fire, each where it is clear of the train
    for (const int *c : kCabins)
        if (roomForBuilding(clear, x + c[0] - 1, x + c[0] + c[2] + 1, floor - 1, floor + 12,
                            z + c[1] - 1, z + c[1] + 5 + 1))
            buildCabin(g, x + c[0], z + c[1], c[2], 4, floor, c[1] > 0);
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
            // few of them, and only on level snow: on the slopes they stood
            // out like rows of glass pillars
            if (g.biome[Grid::column(bx, bz)] != ALPINE || hash(bx, bz, 481u) % 240u != 0)
                continue;
            const int top = g.top(bx, bz);
            if (g.get(bx, top, bz) != SNOW || top + 16 > kMaxY)
                continue;
            bool level = true;
            for (int k = 0; k < 4 && level; ++k)
                level = abs(g.top(bx + (k == 0 ? 2 : (k == 1 ? -2 : 0)),
                                  bz + (k == 2 ? 2 : (k == 3 ? -2 : 0))) -
                            top) <= 1;
            if (!level)
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
    // trunks of different heights, so the crowns stand at different levels
    // and the swamp is not one flat green roof
    const int lift = 3, trunk = 4 + static_cast<int>(h % 6u);
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

// A temple lost in the bamboo jungle, where the ride passes in sight of it:
// a stepped pyramid of mossy stone in four tiers, a stair up its south face
// and a little shrine on top round a glowing crystal, hung with vines. It
// stands on the flattest open ground found, filled under with cobble.
void buildJungleTemple(Grid &g, const Clearance &clear, Extras &extras)
{
    const int base = 7;   // the lowest tier's half width
    const int tiers = 4;  // each 3 high, 2 narrower than the one below
    const int stair = 10; // how far south of the middle the stair starts
    int bestX = 0, bestZ = 0, bestFloor = 0, bestScore = 1 << 30;
    for (int bx = kMinBX + 12; bx < kMaxBX - 12; bx += 2)
        for (int bz = kMinBZ + 12; bz < kMaxBZ - 12; bz += 2)
        {
            if (g.biome[Grid::column(bx, bz)] != BAMBOO_JUNGLE)
                continue;
            int low, high;
            if (!openGround(g, bx, bz, base + 1, BAMBOO_JUNGLE, low, high) || high - low > 5)
                continue;
            const int score = (high - low) * 10;
            if (score >= bestScore || !seenFromRide(clear, bx, bz, 12, 42) ||
                !roomForBuilding(clear, bx - base - 1, bx + base + 1, low, high + tiers * 3 + 6,
                                 bz - base - 1, bz + stair + 1))
                continue;
            bestScore = score;
            bestX = bx;
            bestZ = bz;
            bestFloor = low + 1;
        }
    if (bestScore == 1 << 30)
        return;

    const int x = bestX, z = bestZ, floor = bestFloor;
    extras.temple[0] = x;
    extras.temple[1] = z;
    extras.hasTemple = true;
    auto stone = [&](int bx, int j, int bz) {
        const unsigned int h = hash(bx * 5 + j, bz, 761u) % 10u;
        g.put(bx, j, bz, h < 5u ? MOSSY : (h < 9u ? COBBLE : STONE));
    };
    // the tiers, on a cobble footing down to the ground
    for (int dx = -base; dx <= base; ++dx)
        for (int dz = -base; dz <= base; ++dz)
        {
            g.clearAbove(x + dx, floor - 1, z + dz);
            for (int j = floor - 1; j >= floor - 8 && g.get(x + dx, j, z + dz) == AIR; --j)
                g.put(x + dx, j, z + dz, COBBLE);
            const int reach = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            for (int t = 0; t < tiers && reach <= base - 2 * t; ++t)
                for (int k = 0; k < 3; ++k)
                    stone(x + dx, floor + t * 3 + k, z + dz);
        }
    // the stair: three wide, rising one block for every block south, from
    // the ground to the top tier
    const int top = floor + tiers * 3; // the top tier's floor
    for (int i = 0; i < tiers * 3; ++i)
    {
        const int edge = stair - (i * (stair - 1)) / (tiers * 3 - 1);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = 0; dz <= edge; ++dz)
                if (g.get(x + dx, floor + i, z + dz) == AIR)
                    g.put(x + dx, floor + i, z + dz, dx == 0 ? COBBLE : STONE);
    }
    // nothing on the steps that stand out in front of the pyramid
    for (int dz = base + 1; dz <= stair; ++dz)
    {
        int step = 0;
        while (step + 1 < tiers * 3 && stair - ((step + 1) * (stair - 1)) / (tiers * 3 - 1) >= dz)
            ++step;
        for (int dx = -1; dx <= 1; ++dx)
            g.clearAbove(x + dx, floor + step, z + dz);
    }
    // the stair's nosing: where a step stands out in front of the tier and
    // nothing is on it, the block becomes a step rising to the temple
    for (int i = 0; i < tiers * 3; ++i)
    {
        const int edge = stair - (i * (stair - 1)) / (tiers * 3 - 1);
        if (edge <= base - 2 * (i / 3))
            continue; // inside the tier
        for (int dx = -1; dx <= 1; ++dx)
            if (g.get(x + dx, floor + i, z + edge) != AIR &&
                g.get(x + dx, floor + i + 1, z + edge) == AIR)
            {
                g.put(x + dx, floor + i, z + edge, AIR);
                putStairs(g, x + dx, floor + i, z + edge, 3, dx == 0 ? COBBLE : STONE);
            }
    }
    // the shrine: four pillars under a cobble roof, a crystal in the middle
    // and a lantern on each corner of the top tier
    const int topHalf = base - 2 * (tiers - 1);
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dz = -1; dz <= 1; dz += 2)
        {
            for (int k = 0; k < 3; ++k)
                g.put(x + dx, top + k, z + dz, MOSSY);
            putLamp(g, x + dx * topHalf, top, z + dz * topHalf);
        }
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -2; dz <= 2; ++dz)
            if (abs(dx) + abs(dz) < 4)
                g.put(x + dx, top + 3, z + dz, COBBLE);
    g.put(x, top + 4, z, MOSSY);
    g.put(x, top, z, CRYSTAL);
    // vines hanging down the tiers' faces
    for (int t = 0; t < tiers; ++t)
    {
        const int half = base - 2 * t, edgeY = floor + t * 3 + 2;
        for (int a = -half; a <= half; ++a)
            for (int side = 0; side < 4; ++side)
            {
                const int dx = side == 0 ? half + 1 : (side == 1 ? -half - 1 : a);
                const int dz = side == 2 ? half + 1 : (side == 3 ? -half - 1 : a);
                if (hash(x + dx, z + dz * 3 + t, 763u) % 3u != 0)
                    continue;
                const int length = 1 + static_cast<int>(hash(x + dx, z + dz, 765u) % 3u);
                for (int k = 0; k < length; ++k)
                {
                    const int j = edgeY - k;
                    const size_t cell = Grid::index(x + dx, j, z + dz);
                    if (!Grid::inside(x + dx, j, z + dz) || g.block[cell] != AIR ||
                        clear.blocks(x + dx, j, z + dz))
                        break;
                    g.plant[cell] = VINES;
                }
            }
    }
}

// The north-east land's growth: mangroves standing in the swamp, and on
// the hills a bamboo jungle, dense in thickets, with a few tall broadleaf
// trees hung with vines.
// How far round the temple's middle the jungle is cleared, in blocks.
const int kTempleClearing = 17;

void plantNorthEast(Grid &g, const Clearance &clear, const Extras &extras)
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
                if ((ground == MUD || ground == WATER) && h % 34u == 0 && top + 18 < kMaxY &&
                    !crowded(g, bx, top + 1, bz, 9))
                    mangrove(g, clear, bx, top + 1, bz, h >> 4);
                continue;
            }
            if (ground != GRASS || top + 20 > kMaxY)
                continue;
            // a clearing round the temple, its edge ragged
            if (extras.hasTemple)
            {
                const int tx = bx - extras.temple[0], tz = bz - extras.temple[1];
                const int ragged = kTempleClearing + static_cast<int>(h % 4u);
                if (tx * tx + tz * tz < ragged * ragged)
                    continue;
            }
            // thickets of bamboo with grassy clearings between them, where
            // the big trees stand, so from afar the jungle is not one green
            // roof
            const float thicket = valueNoise(cx * 0.04f, cz * 0.04f, 743u);
            if (thicket > 0.44f && h % 4u != 0)
            {
                const int height = 8 + static_cast<int>((h >> 8) % 9u);
                if (bambooCane(g, clear, bx, top + 1, bz, height))
                    caneTops.push_back(Placed{bx, top + height, bz, LEAVES_BAMBOO});
            }
            else if (h % 41u == 0 && !crowded(g, bx, top + 1, bz, 4))
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
    // the giant spruces first, here and there in the spruce forest on level
    // ground (the four trunk columns on the same ground), so the smaller
    // spruces grow round them rather than leaving them no room
    for (int bx = kMinBX + 4; bx < kMaxBX - 5; ++bx)
        for (int bz = kMinBZ + 4; bz < kMaxBZ - 5; ++bz)
        {
            const unsigned int h = hash(bx, bz, 911u);
            if (h % 53u != 0u || g.biome[Grid::column(bx, bz)] != ALPINE)
                continue;
            const int top = g.top(bx, bz);
            const int ground = g.get(bx, top, bz);
            if ((ground != GRASS && ground != SNOWY_DIRT) || g.top(bx + 1, bz) != top ||
                g.top(bx, bz + 1) != top || g.top(bx + 1, bz + 1) != top ||
                !clear.roomForTree(bx, top, bz) || crowded(g, bx, top + 1, bz, 5))
                continue;
            blocks.clear();
            megaSpruce(blocks, bx, top + 1, bz, 14 + static_cast<int>((h >> 8) % 8u));
            commit(g, clear, blocks);
        }
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
                        cherryTree(blocks, bx, base, bz, h >> 6);
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
                        spruce(blocks, bx, base, bz, 6 + static_cast<int>((h >> 8) % 4u),
                               1 + static_cast<int>((h >> 12) % 2u));
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
                            cherryTree(blocks, bx, base, bz, h >> 6);
                        else if (kind < 4u)
                            roundTree(blocks, bx, base, bz, BIRCH_LOG, LEAVES_BIRCH, trunk + 1,
                                      false);
                        else if (kind == 9u && wood > 0.56f)
                            bigOak(blocks, bx, base, bz, h >> 6);
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
    cherryTree(blocks, kFloatX - 1, kFloatTop + 1, kFloatZ + 1, 2u);
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

void flood(const Grid &g, std::vector<unsigned char> &light, CellQueue<Cell> &queue)
{
    while (!queue.empty())
    {
        const Cell c = queue.pop_front();
        const int level = light[Grid::index(c.bx, c.j, c.bz)] - 1;
        if (level <= 0)
            continue;
        for (const int *d : kDirs)
        {
            const int x = c.bx + d[0], j = c.j + d[1], z = c.bz + d[2];
            if (!Grid::inside(x, j, z))
                continue;
            // leaves let light through, dimmed a level more (their faces
            // inside a crown show through the gaps and are lit from it)
            const bool leaf = isLeafBlock(g.get(x, j, z));
            if (g.opaque(x, j, z) && !leaf)
                continue;
            const int here = leaf ? level - 1 : level;
            if (here <= 0)
                continue;
            unsigned char &s = light[Grid::index(x, j, z)];
            if (s < here)
            {
                s = static_cast<unsigned char>(here);
                queue.push_back(Cell{x, j, z});
            }
        }
    }
}

// Sky light pours down every open column and one level less far for every
// block sideways, so cuttings stay bright and tunnels get dark.
void spreadSky(Grid &g)
{
    CellQueue<Cell> queue;
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
    CellQueue<Cell> warm, cool;
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
            for (int j = kMinY; j <= kMaxY; ++j)
            {
                const int block = g.block[Grid::index(bx, j, bz)];
                // as bright as in a block game, its 15 being 14 here: a
                // lantern and lava 15, a campfire's embers (magma) less
                int level = 0;
                bool isCool = false;
                if (g.plant[Grid::index(bx, j, bz)] == LAMP)
                {
                    // the lantern's own cell is lit too
                    unsigned char &own = g.warm[Grid::index(bx, j, bz)];
                    if (own < 14)
                    {
                        own = 14;
                        warm.push_back(Cell{bx, j, bz});
                    }
                    continue;
                }
                if (block == LAVA)
                    level = 14;
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
                    if (!Grid::inside(x, y, z) ||
                        (g.opaque(x, y, z) && !isLeafBlock(g.get(x, y, z))))
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
// The edges of things, as a block game's world has them: lily pads on the
// still water of the lakes and ponds, sugar cane on the banks beside it
// and clay on their beds; podzol on the spruce forest's floor; and coarse
// dirt and gravel where the meadow meets the badlands and the volcano. All
// of it clear of the train.
void dressEdges(Grid &g, const Clearance &clear)
{
    const int sides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int bx = kMinBX + 1; bx < kMaxBX - 1; ++bx)
        for (int bz = kMinBZ + 1; bz < kMaxBZ - 1; ++bz)
        {
            const unsigned int h = hash(bx, bz, 673u);
            const int top = g.top(bx, bz);
            if (top < kMinY || top >= kMaxY - 3)
                continue;
            const int ground = g.get(bx, top, bz);
            const int biome = g.biome[Grid::column(bx, bz)];
            const float cx = centerOf(bx), cz = centerOf(bz);

            // still water above the sea: a pad now and then, clay below
            // (top() counts the water as the top: its bed is under it)
            if (ground == WATER)
            {
                const int surface = top;
                int bed = top;
                while (bed > kMinY && g.get(bx, bed, bz) == WATER)
                    --bed;
                const int floorBlock = g.get(bx, bed, bz);
                if (surface <= kSeaLayer || g.get(bx, surface + 1, bz) != AIR)
                    continue;
                if ((floorBlock == SAND || floorBlock == DIRT || floorBlock == GRAVEL ||
                     floorBlock == GRASS) &&
                    valueNoise(cx * 0.12f, cz * 0.12f, 679u) > 0.5f)
                    g.put(bx, bed, bz, CLAY); // in patches, as a lake bed has it
                const size_t cell = Grid::index(bx, surface, bz);
                // only out on open water (not in a well or a ditch), and not
                // on the hot spring
                int open = 0;
                for (const auto &d : sides)
                    open += g.get(bx + d[0], surface, bz + d[1]) == WATER ? 1 : 0;
                if (g.level[cell] == 0 && open >= 3 && biome != MESA && h % 9u == 0u &&
                    !clear.blocks(bx, surface + 1, bz))
                    g.plant[cell] = LILY_PAD;
                continue;
            }

            // a bank beside still water: sugar cane, one to three high
            const size_t above = Grid::index(bx, top + 1, bz);
            if ((ground == GRASS || ground == SAND) && g.get(bx, top + 1, bz) == AIR &&
                g.plant[above] == NO_PLANT && biome != ALPINE)
            {
                bool shore = false;
                for (const auto &d : sides)
                {
                    const int x = bx + d[0], z = bz + d[1];
                    if (g.get(x, top, z) == WATER && g.get(x, top + 1, z) == AIR &&
                        top > kSeaLayer && g.level[Grid::index(x, top, z)] == 0)
                        shore = true;
                }
                if (shore && h % 4u == 0u)
                {
                    const int height = 1 + static_cast<int>((h >> 8) % 3u);
                    for (int k = 1; k <= height; ++k)
                    {
                        const size_t c = Grid::index(bx, top + k, bz);
                        if (g.get(bx, top + k, bz) != AIR || g.plant[c] != NO_PLANT ||
                            clear.blocks(bx, top + k, bz))
                            break;
                        g.plant[c] = SUGAR_CANE;
                    }
                    continue;
                }
            }

            // the spruce forest's floor
            if (biome == ALPINE && ground == GRASS &&
                valueNoise(cx * 0.06f, cz * 0.06f, 675u) > 0.42f)
            {
                g.put(bx, top, bz, PODZOL);
                continue;
            }

            // the meadow turning to the badlands' or the volcano's ground
            if (ground == GRASS && (biome == MEADOW || biome == ALPINE))
            {
                int closest = 99;
                for (int dx = -4; dx <= 4; ++dx)
                    for (int dz = -4; dz <= 4; ++dz)
                    {
                        if (!Grid::insideColumn(bx + dx, bz + dz))
                            continue;
                        const int other = g.biome[Grid::column(bx + dx, bz + dz)];
                        if (other == MESA || other == VOLCANO)
                        {
                            const int d = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
                            closest = d < closest ? d : closest;
                        }
                    }
                if (closest <= 4 &&
                    valueNoise(cx * 0.2f, cz * 0.2f, 677u) * 5.0f > static_cast<float>(closest))
                    g.put(bx, top, bz, h % 5u == 0u ? GRAVEL : COARSE_DIRT);
            }
        }
}

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

// How far a build has got, for the loading screen, and whether it should
// stop early (the world is being thrown away).
struct BuildProgress
{
    std::atomic<int> *permille = nullptr; // 0..1000
    const std::atomic<bool> *cancel = nullptr;

    // Records that the build has got this far (0..1); false: stop now.
    bool at(float done) const
    {
        if (permille)
            permille->store(static_cast<int>(done * 1000.0f));
        return !(cancel && cancel->load());
    }
};

// Sand under the shallow water at the shore where neither world would
// draw a sea bed: the sea leaves its blocks whose middle is on the island
// to the island, but its blocks are twice the size, so at the inner
// corners of the coast part of such a block is not on the island, and
// there the water looked down into a deep blue hole. The sand stands a
// block under the island's lowest layer of land, well under the sea's
// surface, and goes in last so nothing is set on it.
void fillShoreGaps(Grid &g)
{
    for (int bx = kMinBX; bx < kMaxBX; ++bx)
        for (int bz = kMinBZ; bz < kMaxBZ; ++bz)
        {
            if (onIsland(bx, bz) || !OceanWorld::leftToIsland(centerOf(bx), centerOf(bz)))
                continue;
            for (int j = kMinY; j <= kSeaLayer - 2; ++j)
                if (g.get(bx, j, bz) == AIR && !g.carved[Grid::index(bx, j, bz)])
                    g.put(bx, j, bz, SAND);
        }
}

void buildScenery(Grid &g, Extras &extras, const BuildProgress &progress = BuildProgress())
{
    const std::vector<float> ride = OceanWorld::rideSamples();
    Clearance clear;
    clear.add(ride);

    // the steps in order, with roughly how long each takes (in tenths of a
    // percent of the whole build), so the loading bar moves evenly
    const float total = 1000.0f;
    float done = 0.0f;
    auto step = [&](float share) {
        done += share;
        return progress.at(done / total);
    };

    buildTerrain(g);
    const std::vector<unsigned char> cover = snowCover(g); // before anything is built on it
    if (!step(150))
        return;
    buildLavaLake(g);
    traceLava(g, scaledLine(kLavaSouth, 4).data(), 4, true);
    traceLava(g, scaledLine(kLavaEast, 4).data(), 4, true);
    crustLava(g);
    buildVillage(g, extras, clear);
    buildPond(g, kPondX, kPondZ, kPondRadius);
    buildSpringPool(g, 12.5f, 102.5f, 9.0f); // a hot spring on the badlands
    buildWaterfall(g);
    buildFloatingIsland(g);
    if (!step(100))
        return;

    carveRide(g, ride);
    const std::vector<float> along(ride.begin() + static_cast<long>(ride.size() / 3 * 2),
                                   ride.end());
    lightTunnels(g, along, clear);
    if (!step(100))
        return;
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
    buildJungleTemple(g, clear, extras);
    if (!step(100))
        return;
    buildSwampWater(g, waterZone(ride));
    // after the carving, so the water flows round the ride's room
    spreadWater(g);
    if (!step(100))
        return;

    scatterBoulders(g, clear);
    plantTrees(g, clear, extras);
    plantNorthEast(g, clear, extras);
    if (!step(50))
        return;
    spreadSky(g);
    if (!step(200))
        return;
    plantFlowers(g, clear, extras);
    dressEdges(g, clear);
    snowfall(g, clear, cover);
    if (!step(50))
        return;
    spreadGlow(g);
    placeAnimals(g, clear, extras);
    fillShoreGaps(g);
    step(150);
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
            cherryTree(blocks, tree[0], 0, tree[1], 7u);
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
typedef BlockMesh::Vertex Vertex;   // glow, sway, crystal light, sky light in extra
typedef BlockMesh::Batches Batches; // one batch per texture

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
        // Water and glass are see-through, and leaves have holes: a block
        // shows its face behind them.
        const int beside = g.get(ox, oy, oz);
        if (g.opaque(ox, oy, oz) && !isLeafBlock(beside))
            continue;
        // between two leaf blocks one face does for both sides (faces are
        // not culled), so they do not flicker over each other
        if (isLeafBlock(b) && isLeafBlock(beside) && f.normal[0] + f.normal[1] + f.normal[2] < 0)
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
            // Lava and lantern light, warm: the shader casts it, strongly
            // where it is dark and hardly at all in the sun (as minus the
            // second extra, which a block never uses to sway).
            const float heat = glowFactor(warm / count);

            Vertex v;
            v.pos[0] = static_cast<float>(bx + f.corner[c][0]) * kBlock;
            v.pos[1] = static_cast<float>(j + f.corner[c][1]) * kBlock;
            v.pos[2] = static_cast<float>(bz + f.corner[c][2]) * kBlock;
            for (int k = 0; k < 3; ++k)
                v.normal[k] = static_cast<float>(f.normal[k]);
            const float self = blockGlow(b);
            v.color[0] = v.color[1] = v.color[2] = tint;
            v.uv[0] = f.uv[c][0];
            v.uv[1] = f.uv[c][1];
            v.extra[0] = self;
            v.extra[1] = self > 0.0f ? 0.0f : -heat;
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

// The first extra value of a water vertex (otherwise how much it glows) is
// negative, which marks it as water for the shaders, and says how the
// corner moves: -(0.5 + 1 if it bobs with the open surface + 2 if it sways
// on a fall + how stirred up the surface is there, 0 to 0.4). The shaders
// ripple the surface and move the corners a little, as the sea's waves do
// (AdvancedLighting, PostEffects); a corner's movement depends only on
// where it is, so the faces that share it stay joined.
float waterMark(bool bob, bool sway, float stirred)
{
    return -(0.5f + (bob ? 1.0f : 0.0f) + (sway ? 2.0f : 0.0f) + stirred);
}

// Can a fall's corner at the grid point (gx, gz) sway? Not where it
// touches anything but water and air: in the four cells round its upright
// edge, in the layer it is in, or both layers when it is on the line
// between them (corners there are shared by the faces above and below).
bool swayFree(const Grid &g, int gx, int gz, int layer, bool onLine)
{
    for (int l = onLine ? layer - 1 : layer; l <= layer; ++l)
        for (int dx = -1; dx <= 0; ++dx)
            for (int dz = -1; dz <= 0; ++dz)
            {
                const int b = g.get(gx + dx, l, gz + dz);
                if (b != AIR && b != WATER)
                    return false;
            }
    return true;
}

// How far the open surface at the grid point (gx, gz) of layer j bends
// down, in blocks, where the water pours over an edge: a cell whose water
// falls on below it slopes down to its open side, so the surface curves
// into the fall instead of meeting it at a right angle; the water just
// behind it dips a little too. Every face with a corner there bends it the
// same, so they stay joined.
float lipDrop(const Grid &g, int gx, int gz, int j)
{
    bool pours = false, open = false;
    for (int dx = -1; dx <= 0; ++dx)
        for (int dz = -1; dz <= 0; ++dz)
        {
            const int cx = gx + dx, cz = gz + dz;
            if (g.get(cx, j, cz) == WATER && g.get(cx, j - 1, cz) == WATER &&
                g.level[Grid::index(cx, j - 1, cz)] == WaterFlow::FALLING)
                pours = true;
            if (g.get(cx, j, cz) == AIR)
                open = true;
        }
    return pours ? (open ? 0.55f : 0.15f) : 0.0f;
}

// Which way the water pours over the edge at the grid point (gx, gz) of
// layer j: towards the open cells round it (x, z); false if none is open.
bool lipOutward(const Grid &g, int gx, int gz, int j, float out[2])
{
    out[0] = out[1] = 0.0f;
    for (int dx = -1; dx <= 0; ++dx)
        for (int dz = -1; dz <= 0; ++dz)
            if (g.get(gx + dx, j, gz + dz) == AIR)
            {
                out[0] += static_cast<float>(dx) + 0.5f;
                out[1] += static_cast<float>(dz) + 0.5f;
            }
    const float length = sqrtf(out[0] * out[0] + out[1] * out[1]);
    if (length < 0.01f)
        return false;
    out[0] /= length;
    out[1] /= length;
    return true;
}

// How stirred up the surface is at the grid point (gx, gz) of layer j:
// most right beside falling water landing there, nothing three and a
// half blocks away.
float stirredAt(const Grid &g, int gx, int gz, int j)
{
    float nearest = 1.0e9f;
    for (int cx = gx - 4; cx < gx + 4; ++cx)
        for (int cz = gz - 4; cz < gz + 4; ++cz)
            for (int l = j; l <= j + 1; ++l)
                if (g.get(cx, l, cz) == WATER &&
                    g.level[Grid::index(cx, l, cz)] == WaterFlow::FALLING)
                {
                    const float dx = static_cast<float>(cx) + 0.5f - static_cast<float>(gx);
                    const float dz = static_cast<float>(cz) + 0.5f - static_cast<float>(gz);
                    const float d = sqrtf(dx * dx + dz * dz);
                    nearest = d < nearest ? d : nearest;
                }
    // (not "near": windows.h defines it as nothing)
    const float closeness = 1.0f - nearest / 3.5f;
    return 0.4f * (closeness < 0.0f ? 0.0f : closeness);
}

// The faces of a water block: towards the air, and the part of a side that
// stands above lower water next to it. Lit by the light of the open cell
// each face looks into.
void addWaterFaces(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const float top = waterHeight(g, bx, j, bz);
    const bool open = g.get(bx, j + 1, bz) != WATER; // its top is the surface
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
        v.extra[1] = -glowFactor(static_cast<float>(g.warmAt(lx, ly, lz)));
        v.extra[2] = glowFactor(static_cast<float>(g.coolAt(lx, ly, lz)));
        v.extra[3] = skyFactor(static_cast<float>(g.skyAt(lx, ly, lz)));
        for (int c = 0; c < 4; ++c)
        {
            const float y = f.normal[1] > 0
                                ? top
                                : (f.normal[1] < 0 ? 0.0f : (f.corner[c][1] == 1 ? top : bottom));
            // The corners at the open surface bob, and so do those of a
            // side where it meets the open surface of lower water beside
            // it; the other corners of a side facing the air sway, where
            // they touch nothing but water and air (waterMark).
            const int gx = bx + f.corner[c][0], gz = bz + f.corner[c][2];
            const bool meetsLower =
                f.normal[1] == 0 && y == bottom && bottom > 0.0f && g.get(ox, oy + 1, oz) != WATER;
            const bool bob = (open && y == top) || meetsLower;
            const bool sway = !bob && f.normal[1] == 0 && beside == AIR &&
                              swayFree(g, gx, gz, j + (y >= 1.0f ? 1 : 0), y == 0.0f || y >= 1.0f);
            v.extra[0] = waterMark(bob, sway, bob ? stirredAt(g, gx, gz, j) : 0.0f);
            // the surface bends down where it pours over an edge (lipDrop)
            const float drop = bob ? lipDrop(g, gx, gz, j) : 0.0f;
            // Over the edge itself its normal is halfway between up and out,
            // on the surface and on the face it pours down, so the light and
            // the ripples go smoothly round the bend.
            for (int k = 0; k < 3; ++k)
                v.normal[k] = static_cast<float>(f.normal[k]);
            float outward[2];
            if (drop > 0.3f && lipOutward(g, gx, gz, j, outward) &&
                (f.normal[1] > 0 || static_cast<float>(f.normal[0]) * outward[0] +
                                            static_cast<float>(f.normal[2]) * outward[1] >
                                        0.5f))
            {
                const float s = 1.0f / sqrtf(2.0f);
                v.normal[0] = outward[0] * s;
                v.normal[1] = s;
                v.normal[2] = outward[1] * s;
            }
            v.pos[0] = static_cast<float>(bx + f.corner[c][0]) * kBlock;
            v.pos[1] = (static_cast<float>(j) + y - drop) * kBlock;
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
// the stalk's strip of the picture.
// The leafy top adds a wider spray of leaves square to the crossed one.
void addBambooStalk(bool top, bool leafy, float cx, float y0, float cz, unsigned int h, Vertex v,
                    std::vector<Vertex> &out)
{
    const float half = 0.26f + 0.08f * static_cast<float>((h >> 4) % 4u) / 3.0f;
    const float u0 = 13.2f / 32.0f, u1 = 18.8f / 32.0f;
    // each side faces its own way, lit by the shader from its normal (no
    // shade baked in as well)
    const float normals[4][2] = {{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}};
    const float corners[5][2] = {
        {-half, -half}, {half, -half}, {half, half}, {-half, half}, {-half, -half}};
    // the stalk's picture starts at the top's leaves halfway up the cell
    const float vTop = top ? 0.5f : 0.0f;
    const float y1 = top ? y0 + kBlock * 0.5f : y0 + kBlock;
    for (int f = 0; f < 4; ++f)
    {
        v.normal[0] = normals[f][0];
        v.normal[1] = 0.0f;
        v.normal[2] = normals[f][1];
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
    // the leaves are crossed pictures, lit as facing the sky like the plants
    v.normal[0] = 0.0f;
    v.normal[1] = 1.0f;
    v.normal[2] = 0.0f;
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

// A glass block: its faces towards anything that does not hide them, into
// the see-through batch with the water, so they are blended over what is
// behind them (the dark of a room, the trees beyond); none between two
// panes of glass.
void addGlassFaces(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    std::vector<Vertex> &out = batches[static_cast<size_t>(LandTextures::WINDOW)];
    for (const Face &f : kFaces)
    {
        const int ox = bx + f.normal[0], oy = j + f.normal[1], oz = bz + f.normal[2];
        const int beside = g.get(ox, oy, oz);
        if (beside == WINDOW || (g.opaque(ox, oy, oz) && !isLeafBlock(beside)))
            continue;
        const float sky = skyFactor(static_cast<float>(g.skyAt(ox, oy, oz)));
        // no shading baked in by which way the face looks: the lighting
        // shader does that from the normal, as for every block (baked in as
        // well, it darkened thin parts twice, and more so in linear light)
        const float shade = 0.95f;
        for (int c = 0; c < 4; ++c)
        {
            Vertex v;
            v.pos[0] = static_cast<float>(bx + f.corner[c][0]) * kBlock;
            v.pos[1] = static_cast<float>(j + f.corner[c][1]) * kBlock;
            v.pos[2] = static_cast<float>(bz + f.corner[c][2]) * kBlock;
            for (int k = 0; k < 3; ++k)
                v.normal[k] = static_cast<float>(f.normal[k]);
            v.uv[0] = f.uv[c][0];
            v.uv[1] = f.uv[c][1];
            v.color[0] = v.color[1] = v.color[2] = shade;
            v.extra[0] = 0.0f;
            v.extra[1] = -glowFactor(static_cast<float>(g.warmAt(ox, oy, oz)));
            v.extra[2] = glowFactor(static_cast<float>(g.coolAt(ox, oy, oz)));
            v.extra[3] = sky;
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
    v.extra[1] = -glowFactor(static_cast<float>(g.warm[i]));
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
        v.extra[3] = sky;
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

// A box inside a cell, with the planks' picture at the scale of a block,
// lit by the cell's sky.
void addBox(const Grid &g, int bx, int j, int bz, const float lo[3], const float hi[3],
            Batches &batches, int texture)
{
    const size_t i = Grid::index(bx, j, bz);
    const float sky = skyFactor(static_cast<float>(g.sky[i]));
    const float base[3] = {static_cast<float>(bx) * kBlock, static_cast<float>(j) * kBlock,
                           static_cast<float>(bz) * kBlock};
    std::vector<Vertex> &out = batches[static_cast<size_t>(texture)];
    for (const Face &f : kFaces)
    {
        // no shading baked in by which way the face looks: the lighting
        // shader does that from the normal, as for every block (baked in as
        // well, it darkened thin parts twice, and more so in linear light)
        const float shade = 0.95f;
        for (int c = 0; c < 4; ++c)
        {
            Vertex v;
            float p[3];
            for (int k = 0; k < 3; ++k)
            {
                p[k] = f.corner[c][k] == 1 ? hi[k] : lo[k];
                v.pos[k] = base[k] + p[k];
                v.normal[k] = static_cast<float>(f.normal[k]);
            }
            // the picture in block units across the face, so it is not
            // stretched on a thin post or rail
            const int a = f.normal[0] != 0 ? 2 : 0, b = f.normal[1] != 0 ? 2 : 1;
            v.uv[0] = p[a] / kBlock;
            v.uv[1] = 1.0f - p[b] / kBlock;
            v.color[0] = v.color[1] = v.color[2] = shade;
            v.extra[0] = 0.0f;
            v.extra[1] = -glowFactor(static_cast<float>(g.warm[i]));
            v.extra[2] = glowFactor(static_cast<float>(g.cool[i]));
            v.extra[3] = sky;
            out.push_back(v);
        }
    }
}

// Does a fence reach towards this cell: another fence, or a solid block?
bool fenceJoins(const Grid &g, int bx, int j, int bz)
{
    if (!Grid::inside(bx, j, bz))
        return false;
    return g.plant[Grid::index(bx, j, bz)] == FENCE || g.opaque(bx, j, bz);
}

// An oak fence as in a block game: a square post in the middle of the cell
// and two rails out to each fence or block beside it.
void addFence(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const float h = kBlock, mid = kBlock * 0.5f;
    const float post = kBlock * 0.125f, rail = kBlock * 0.0625f;
    const float postLo[3] = {mid - post, 0.0f, mid - post};
    const float postHi[3] = {mid + post, h, mid + post};
    addBox(g, bx, j, bz, postLo, postHi, batches, LandTextures::PLANKS);
    const int sides[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const float heights[2] = {h * 0.375f, h * 0.75f};
    for (const auto &d : sides)
    {
        if (!fenceJoins(g, bx + d[0], j, bz + d[1]))
            continue;
        for (float y : heights)
        {
            float lo[3] = {mid - rail, y - h * 0.09f, mid - rail};
            float hi[3] = {mid + rail, y + h * 0.09f, mid + rail};
            const int axis = d[0] != 0 ? 0 : 2;
            if ((d[0] != 0 ? d[0] : d[1]) > 0)
                hi[axis] = h;
            else
                lo[axis] = 0.0f;
            addBox(g, bx, j, bz, lo, hi, batches, LandTextures::PLANKS);
        }
    }
}

// A box in a cell, each face's picture taken from part of a texture: for
// each face (kFaces order) the left edge's u, the bottom edge's v, the
// right edge's u and the top edge's v, in sixteenths, v down the picture.
void addPart(const Grid &g, int bx, int j, int bz, const float lo[3], const float hi[3],
             const float uv[6][4], float glow, Batches &batches, int texture)
{
    const size_t i = Grid::index(bx, j, bz);
    const float sky = skyFactor(static_cast<float>(g.sky[i]));
    const float base[3] = {static_cast<float>(bx) * kBlock, static_cast<float>(j) * kBlock,
                           static_cast<float>(bz) * kBlock};
    std::vector<Vertex> &out = batches[static_cast<size_t>(texture)];
    for (int k = 0; k < 6; ++k)
    {
        const Face &f = kFaces[k];
        // no shading baked in by which way the face looks: the lighting
        // shader does that from the normal, as for every block (baked in as
        // well, it darkened thin parts twice, and more so in linear light)
        const float shade = 0.95f;
        for (int c = 0; c < 4; ++c)
        {
            Vertex v;
            for (int a = 0; a < 3; ++a)
            {
                v.pos[a] = base[a] + (f.corner[c][a] == 1 ? hi[a] : lo[a]);
                v.normal[a] = static_cast<float>(f.normal[a]);
            }
            v.uv[0] = (f.uv[c][0] > 0.5f ? uv[k][2] : uv[k][0]) / 16.0f;
            // uv[k]: across from, the bottom's v, across to, the top's v
            v.uv[1] = (f.uv[c][1] > 0.5f ? uv[k][1] : uv[k][3]) / 16.0f;
            v.color[0] = v.color[1] = v.color[2] = shade;
            v.extra[0] = glow;
            v.extra[1] = -glowFactor(static_cast<float>(g.warm[i]));
            v.extra[2] = glowFactor(static_cast<float>(g.cool[i]));
            v.extra[3] = sky;
            out.push_back(v);
        }
    }
}

// A lantern as a block game draws it: a body six sixteenths across and
// seven high with a narrower cap, glowing; on the block below, or hung from
// the block above on a chain (then a sixteenth higher, as it is there).
void addLantern(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const bool standing = g.opaque(bx, j - 1, bz) || (Grid::inside(bx, j - 1, bz) &&
                                                      g.plant[Grid::index(bx, j - 1, bz)] == FENCE);
    const float u = kBlock / 16.0f;
    const float lift = standing ? 0.0f : 1.0f;
    // faces in kFaces order: +x, -x, +y, -y, +z, -z
    const float body[6][4] = {{0, 9, 6, 2},  {0, 9, 6, 2}, {0, 9, 6, 15},
                              {0, 9, 6, 15}, {0, 9, 6, 2}, {0, 9, 6, 2}};
    const float bodyLo[3] = {5 * u, lift * u, 5 * u}, bodyHi[3] = {11 * u, (7 + lift) * u, 11 * u};
    addPart(g, bx, j, bz, bodyLo, bodyHi, body, 1.0f, batches, LandTextures::LANTERN_SMALL);
    const float cap[6][4] = {{1, 2, 5, 0},   {1, 2, 5, 0}, {1, 10, 5, 14},
                             {1, 10, 5, 14}, {1, 2, 5, 0}, {1, 2, 5, 0}};
    const float capLo[3] = {6 * u, (7 + lift) * u, 6 * u},
                capHi[3] = {10 * u, (9 + lift) * u, 10 * u};
    addPart(g, bx, j, bz, capLo, capHi, cap, 0.3f, batches, LandTextures::LANTERN_SMALL);
    if (standing)
        return;
    // the chain, from the cap up to the block above
    const float chain[6][4] = {{0, 16, 3, 0}, {0, 16, 3, 0}, {0, 0, 3, 3},
                               {0, 0, 3, 3},  {0, 16, 3, 0}, {0, 16, 3, 0}};
    const float chainLo[3] = {7.0f * u, (9 + lift) * u, 7.0f * u};
    const float chainHi[3] = {9.0f * u, kBlock, 9.0f * u};
    addPart(g, bx, j, bz, chainLo, chainHi, chain, 0.0f, batches, LandTextures::CHAIN);
}

// A lily pad: one flat leaf just above the still water in its cell, a
// little smaller than the cell and turned a random way.
void addLilyPad(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const size_t i = Grid::index(bx, j, bz);
    const unsigned int h = hash(bx, bz, 671u);
    std::vector<Vertex> &out = batches[static_cast<size_t>(LandTextures::LILY_PAD)];
    Vertex v;
    v.normal[0] = 0.0f;
    v.normal[1] = 1.0f;
    v.normal[2] = 0.0f;
    v.color[0] = v.color[1] = v.color[2] = 0.9f + 0.1f * static_cast<float>(h % 10u) / 10.0f;
    v.extra[0] = 0.0f;
    v.extra[1] = -glowFactor(static_cast<float>(g.warm[i]));
    v.extra[2] = glowFactor(static_cast<float>(g.cool[i]));
    v.extra[3] = skyFactor(static_cast<float>(g.skyAt(bx, j + 1, bz)));
    const float cx = (static_cast<float>(bx) + 0.5f) * kBlock;
    const float cz = (static_cast<float>(bz) + 0.5f) * kBlock;
    const float y = (static_cast<float>(j) + 8.0f / 9.0f) * kBlock + 0.03f;
    const float a = static_cast<float>(h % 360u) * 3.14159265f / 180.0f;
    const float r = kBlock * 0.62f;
    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int c = 0; c < 4; ++c)
    {
        const float ca = a + 0.785398f + 1.570796f * static_cast<float>(c);
        v.pos[0] = cx + cosf(ca) * r;
        v.pos[1] = y;
        v.pos[2] = cz - sinf(ca) * r;
        v.uv[0] = uv[c][0];
        v.uv[1] = uv[c][1];
        out.push_back(v);
    }
}

// A box inside a cell (in block units, 0..1) with the picture at the scale
// of a block, as addBox(), leaving out a face that lies on the cell's side
// against a solid block (nothing sees it).
void addShapeBox(const Grid &g, int bx, int j, int bz, const float lo[3], const float hi[3],
                 Batches &batches, int texture)
{
    const size_t i = Grid::index(bx, j, bz);
    const float sky = skyFactor(static_cast<float>(g.sky[i]));
    std::vector<Vertex> &out = batches[static_cast<size_t>(texture)];
    for (const Face &f : kFaces)
    {
        int axis = 0;
        while (f.normal[axis] == 0)
            ++axis;
        const bool outward = f.normal[axis] > 0 ? hi[axis] >= 1.0f : lo[axis] <= 0.0f;
        if (outward && g.opaque(bx + f.normal[0], j + f.normal[1], bz + f.normal[2]))
            continue;
        for (int c = 0; c < 4; ++c)
        {
            Vertex v;
            float p[3];
            for (int k = 0; k < 3; ++k)
            {
                p[k] = f.corner[c][k] == 1 ? hi[k] : lo[k];
                v.normal[k] = static_cast<float>(f.normal[k]);
            }
            v.pos[0] = (static_cast<float>(bx) + p[0]) * kBlock;
            v.pos[1] = (static_cast<float>(j) + p[1]) * kBlock;
            v.pos[2] = (static_cast<float>(bz) + p[2]) * kBlock;
            const int a = f.normal[0] != 0 ? 2 : 0, b = f.normal[1] != 0 ? 2 : 1;
            v.uv[0] = p[a];
            v.uv[1] = 1.0f - p[b];
            v.color[0] = v.color[1] = v.color[2] = 0.95f;
            v.extra[0] = 0.0f;
            v.extra[1] = -glowFactor(static_cast<float>(g.warm[i]));
            v.extra[2] = glowFactor(static_cast<float>(g.cool[i]));
            v.extra[3] = sky;
            out.push_back(v);
        }
    }
}

// A step, a slab, a trapdoor or a flower pot (see shapeLevel()), drawn
// with the solid blocks, so it is lit and casts shadows like them.
void addShape(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const size_t i = Grid::index(bx, j, bz);
    const int plant = g.plant[i], level = g.level[i];
    const int facing = level & 3;
    const bool high = (level & 4) != 0, corner = (level & 8) != 0;
    const int texture = textureOf(kShapeBlocks[(level >> 4) & 7], 2);
    const float y0 = high ? 0.5f : 0.0f;
    if (plant == SLAB || plant == STAIRS)
    {
        const float lo[3] = {0.0f, y0, 0.0f}, hi[3] = {1.0f, y0 + 0.5f, 1.0f};
        addShapeBox(g, bx, j, bz, lo, hi, batches, texture);
    }
    if (plant == STAIRS)
    {
        // the upper (or, upside down, the lower) half on the side it rises
        // to, or on a corner just the quarter between that way and the next
        float lo[3] = {0.0f, high ? 0.0f : 0.5f, 0.0f}, hi[3] = {1.0f, high ? 0.5f : 1.0f, 1.0f};
        const int ways = corner ? 2 : 1;
        for (int w = 0; w < ways; ++w)
        {
            const int *d = kFacing[(facing + w) % 4];
            if (d[0] > 0)
                lo[0] = 0.5f;
            if (d[0] < 0)
                hi[0] = 0.5f;
            if (d[1] > 0)
                lo[2] = 0.5f;
            if (d[1] < 0)
                hi[2] = 0.5f;
        }
        addShapeBox(g, bx, j, bz, lo, hi, batches, texture);
    }
    else if (plant == TRAPDOOR)
    {
        const float t = 3.0f / 16.0f;
        float lo[3] = {0.0f, 0.0f, 0.0f}, hi[3] = {1.0f, 1.0f, 1.0f};
        if (high)
            lo[1] = 1.0f - t; // closed, at the top of the cell
        else
        {
            const int *d = kFacing[facing]; // open, against that wall
            if (d[0] > 0)
                lo[0] = 1.0f - t;
            if (d[0] < 0)
                hi[0] = t;
            if (d[1] > 0)
                lo[2] = 1.0f - t;
            if (d[1] < 0)
                hi[2] = t;
        }
        addShapeBox(g, bx, j, bz, lo, hi, batches, LandTextures::TRAPDOOR);
    }
    else if (plant == POT)
    {
        // the pot, six sixteenths across and high, and the flower crossed
        // above its earth
        const float lo[3] = {5.0f / 16.0f, 0.0f, 5.0f / 16.0f};
        const float hi[3] = {11.0f / 16.0f, 6.0f / 16.0f, 11.0f / 16.0f};
        addShapeBox(g, bx, j, bz, lo, hi, batches, LandTextures::FLOWER_POT);
        std::vector<Vertex> &out = batches[static_cast<size_t>(plantTexture(level))];
        Vertex v;
        v.normal[0] = 0.0f;
        v.normal[1] = 1.0f;
        v.normal[2] = 0.0f;
        v.color[0] = v.color[1] = v.color[2] = 0.95f;
        v.extra[0] = 0.0f;
        v.extra[1] = -glowFactor(static_cast<float>(g.warm[i]));
        v.extra[2] = glowFactor(static_cast<float>(g.cool[i]));
        v.extra[3] = skyFactor(static_cast<float>(g.sky[i]));
        const float cx = (static_cast<float>(bx) + 0.5f) * kBlock;
        const float cz = (static_cast<float>(bz) + 0.5f) * kBlock;
        const float y = (static_cast<float>(j) + 4.0f / 16.0f) * kBlock, r = 0.3f * kBlock;
        const float size = 0.75f * kBlock;
        for (int q = 0; q < 2; ++q)
        {
            const float sx = q == 0 ? r : -r, sz = r;
            const float pts[4][3] = {{cx - sx, y, cz - sz},
                                     {cx + sx, y, cz + sz},
                                     {cx + sx, y + size, cz + sz},
                                     {cx - sx, y + size, cz - sz}};
            const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            for (int c = 0; c < 4; ++c)
            {
                for (int k = 0; k < 3; ++k)
                    v.pos[k] = pts[c][k];
                v.uv[0] = uv[c][0];
                v.uv[1] = uv[c][1];
                out.push_back(v);
            }
        }
    }
}

// A door as a block game has it: a board three sixteenths thick standing at
// one side of its cell, the bottom or top half of the door's picture by
// whether there is door below. It closes the doorway on the inside, so the
// doorway is set back in the wall: the wall runs along the axis where the
// cells either side are built (wall or window), and the inside is the side
// that gets less sky.
void addDoor(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const bool wallAlongX = g.get(bx - 1, j, bz) != AIR || g.get(bx + 1, j, bz) != AIR;
    const int ax = wallAlongX ? 0 : 1, az = wallAlongX ? 1 : 0; // across the wall
    const int skyPlus = g.skyAt(bx + ax, j, bz + az), skyMinus = g.skyAt(bx - ax, j, bz - az);
    const bool insidePlus = skyPlus < skyMinus;
    const float t = 3.0f / 16.0f;
    float lo[3] = {0.0f, 0.0f, 0.0f}, hi[3] = {1.0f, 1.0f, 1.0f};
    const int axis = wallAlongX ? 2 : 0;
    if (insidePlus)
        lo[axis] = 1.0f - t;
    else
        hi[axis] = t;
    const bool top = g.get(bx, j - 1, bz) == DOOR;
    addShapeBox(g, bx, j, bz, lo, hi, batches, top ? LandTextures::DOOR_TOP : LandTextures::DOOR);
}

// Two quads crossing diagonally, smaller than the cell and off centre.
void addPlant(const Grid &g, int bx, int j, int bz, Batches &batches)
{
    const size_t i = Grid::index(bx, j, bz);
    const int plant = g.plant[i];
    // bamboo and vines stack into columns: a whole cell tall, in the same
    // place in every cell of the column and still, so they join up
    const bool stacked =
        plant == BAMBOO || plant == BAMBOO_TOP || plant == VINES || plant == SUGAR_CANE;
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
    // the warm light, where the plant does not sway (its foot, or all of a
    // plant that never sways)
    const float heat = glowFactor(static_cast<float>(g.warm[i]));

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
            v.extra[1] = corners[c][3] > 0.5f || sway <= 0.0f ? -heat : sway;
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

// A box with the texture bound, its picture at the scale of a block on
// every face, as on the blocks.
void texturedBox(float x0, float y0, float z0, float x1, float y1, float z1)
{
    const float k = 1.0f / kBlock;
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glTexCoord2f(x0 * k, 1.0f - y0 * k);
    glVertex3f(x0, y0, z1);
    glTexCoord2f(x1 * k, 1.0f - y0 * k);
    glVertex3f(x1, y0, z1);
    glTexCoord2f(x1 * k, 1.0f - y1 * k);
    glVertex3f(x1, y1, z1);
    glTexCoord2f(x0 * k, 1.0f - y1 * k);
    glVertex3f(x0, y1, z1);
    glNormal3f(0, 0, -1);
    glTexCoord2f(x1 * k, 1.0f - y0 * k);
    glVertex3f(x1, y0, z0);
    glTexCoord2f(x0 * k, 1.0f - y0 * k);
    glVertex3f(x0, y0, z0);
    glTexCoord2f(x0 * k, 1.0f - y1 * k);
    glVertex3f(x0, y1, z0);
    glTexCoord2f(x1 * k, 1.0f - y1 * k);
    glVertex3f(x1, y1, z0);
    glNormal3f(0, 1, 0);
    glTexCoord2f(x0 * k, z1 * k);
    glVertex3f(x0, y1, z1);
    glTexCoord2f(x1 * k, z1 * k);
    glVertex3f(x1, y1, z1);
    glTexCoord2f(x1 * k, z0 * k);
    glVertex3f(x1, y1, z0);
    glTexCoord2f(x0 * k, z0 * k);
    glVertex3f(x0, y1, z0);
    glNormal3f(0, -1, 0);
    glTexCoord2f(x0 * k, z0 * k);
    glVertex3f(x0, y0, z0);
    glTexCoord2f(x1 * k, z0 * k);
    glVertex3f(x1, y0, z0);
    glTexCoord2f(x1 * k, z1 * k);
    glVertex3f(x1, y0, z1);
    glTexCoord2f(x0 * k, z1 * k);
    glVertex3f(x0, y0, z1);
    glNormal3f(1, 0, 0);
    glTexCoord2f(z1 * k, 1.0f - y0 * k);
    glVertex3f(x1, y0, z1);
    glTexCoord2f(z0 * k, 1.0f - y0 * k);
    glVertex3f(x1, y0, z0);
    glTexCoord2f(z0 * k, 1.0f - y1 * k);
    glVertex3f(x1, y1, z0);
    glTexCoord2f(z1 * k, 1.0f - y1 * k);
    glVertex3f(x1, y1, z1);
    glNormal3f(-1, 0, 0);
    glTexCoord2f(z0 * k, 1.0f - y0 * k);
    glVertex3f(x0, y0, z0);
    glTexCoord2f(z1 * k, 1.0f - y0 * k);
    glVertex3f(x0, y0, z1);
    glTexCoord2f(z1 * k, 1.0f - y1 * k);
    glVertex3f(x0, y1, z1);
    glTexCoord2f(z0 * k, 1.0f - y1 * k);
    glVertex3f(x0, y1, z0);
    glEnd();
}

void colour(int r, int g, int b)
{
    glColor3ub(static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b));
}

// A cloud's box: white, each face as bright as the block game makes that
// face of its clouds (top 1, the sides 0.9 and 0.8, the underside 0.7).
void cloudBox(float x0, float y0, float z0, float x1, float y1, float z1)
{
    const float kWhite[3] = {250.0f / 255.0f, 252.0f / 255.0f, 1.0f};
    auto shade = [&](float k) { glColor3f(kWhite[0] * k, kWhite[1] * k, kWhite[2] * k); };
    glBegin(GL_QUADS);
    shade(0.8f);
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
    shade(1.0f);
    glNormal3f(0, 1, 0);
    glVertex3f(x0, y1, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z0);
    glVertex3f(x0, y1, z0);
    shade(0.7f);
    glNormal3f(0, -1, 0);
    glVertex3f(x0, y0, z0);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x0, y0, z1);
    shade(0.9f);
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

// A sheep's wool, as often as the block game's sheep grow each colour:
// mostly white, now and then light grey, grey, black or brown. Chosen by
// the animal's phase, so a sheep keeps its colour.
const float *woolColour(float phase)
{
    static const float kWool[5][3] = {{1.0f, 1.0f, 1.0f},
                                      {0.62f, 0.62f, 0.6f},
                                      {0.32f, 0.32f, 0.34f},
                                      {0.13f, 0.13f, 0.14f},
                                      {0.5f, 0.34f, 0.22f}};
    const unsigned int h = hash(static_cast<int>(phase * 1000.0f), 0, 451u) % 100u;
    return kWool[h < 82u ? 0 : (h < 87u ? 1 : (h < 92u ? 2 : (h < 97u ? 3 : 4)))];
}

// A hot-air balloon: a stepped envelope of wool in two colours, oak ropes
// and a basket of planks, as blocks a block game has; textures: the texture
// names by LandTextures slot.
void drawBalloon(int woolA, int woolB, const std::vector<unsigned int> &textures)
{
    const float rings[7][2] = {{2.2f, 0.0f}, {4.2f, 1.6f},  {5.6f, 3.6f}, {6.0f, 6.0f},
                               {5.4f, 8.6f}, {4.0f, 10.8f}, {2.2f, 12.4f}};
    glEnable(GL_TEXTURE_2D);
    glColor3f(0.95f, 0.95f, 0.95f);
    for (int i = 0; i < 7; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, textures[static_cast<size_t>(i % 2 == 0 ? woolA : woolB)]);
        const float r = rings[i][0];
        const float top = i < 6 ? rings[i + 1][1] : 13.6f;
        texturedBox(-r, rings[i][1], -r, r, top, r);
    }
    glBindTexture(GL_TEXTURE_2D, textures[LandTextures::OAK_SIDE]);
    for (int k = 0; k < 4; ++k)
    {
        const float x = (k % 2 == 0) ? -1.1f : 1.0f;
        const float z = k < 2 ? -1.1f : 1.0f;
        texturedBox(x, -3.4f, z, x + 0.1f, 0.0f, z + 0.1f);
    }
    glBindTexture(GL_TEXTURE_2D, textures[LandTextures::PLANKS]);
    texturedBox(-1.3f, -5.0f, -1.3f, 1.3f, -3.4f, 1.3f);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    colour(255, 200, 90);
    setGlow(1.2f);
    box(-0.4f, -1.2f, -0.4f, 0.4f, -0.5f, 0.4f); // the burner's flame
    setGlow(0.0f);
}

} // namespace

// ---------------------------------------------------------------------
// LandWorld
// ---------------------------------------------------------------------
// The mesh is cut into chunks of kChunk x kChunk columns, each with its
// own meshes (in the island's Arena) and the box round its blocks, so only
// the chunks in view are drawn (the camera's view, the light's in the
// shadow pass, the mirrored one for the water's reflection).
const int kChunk = 16;

struct LandChunk
{
    BlockMesh::Mesh solid, plant;
    BlockMesh::Mesh water; // see-through, drawn last (drawWater)
    BlockMesh::Mesh glass; // the same, kept apart so the water can be drawn as the sea is
    float lo[3], hi[3];
};

namespace
{
// A chunk the builder has meshed, waiting for its vertex buffers.
struct MeshedChunk
{
    Batches solids, plants, water, glass;
    float lo[3], hi[3];
};
} // namespace

// One of the two islands. The grid and the meshes are made on a thread of
// their own (build()), so the window keeps drawing (the loading screen)
// meanwhile; prepare() turns the meshes into vertex buffers, which only
// the thread that owns the OpenGL context can do.
struct LandWorld::Scene
{
    Grid grid;
    Extras extras;
    bool built = false; // every chunk has its vertex buffers
    // Once built, only the blocks themselves are still looked at (solidAt(),
    // the trestles); the plants, light, water levels and carving went into
    // the meshes, so those layers are let go. Made again (from the save)
    // if the meshes have to be.
    bool trimmed = false;
    std::vector<LandChunk> chunks;
    BlockMesh::Arena arena; // the chunks' vertices, in a few large buffers

    std::thread worker;
    std::atomic<bool> gridReady{false}; // the grid is complete; the worker no longer writes it
    std::atomic<bool> cancel{false};
    std::atomic<bool> fromSave{false}; // the grid is being read from its save
    std::atomic<int> gridPermille{0};
    std::atomic<int> chunksMeshed{0}; // of kChunkCount
    std::mutex lock;                  // guards meshed and meshingDone
    std::vector<MeshedChunk> meshed;
    bool meshingDone = false;

    ~Scene()
    {
        stop();
    }

    // Stops the worker and throws away what it has not handed over.
    void stop()
    {
        cancel = true;
        if (worker.joinable())
            worker.join();
        cancel = false;
        meshed.clear();
        meshingDone = false;
    }
};

const int kChunkCount = ((kSizeX + kChunk - 1) / kChunk) * ((kSizeZ + kChunk - 1) / kChunk);

// The trestles as one mesh in a small arena of their own, made again when
// the track changes (drawSupports()).
struct LandWorld::Supports
{
    BlockMesh::Arena arena{1 << 16};
    BlockMesh::Mesh mesh;
    bool built = false;
};

LandWorld::LandWorld()
{
}

LandWorld::~LandWorld()
{
}

void LandWorld::releaseResources()
{
    // The vertex buffers are gone with the old context; the grids are kept,
    // so only the meshes are made again (a trimmed grid is read again first).
    for (Scene *s : {m_scenery.get(), m_flat.get()})
        if (s)
        {
            s->stop();
            s->chunks.clear();
            s->arena.forget();
            s->built = false;
            s->chunksMeshed = 0;
            if (s->trimmed)
            {
                s->gridReady = false;
                s->gridPermille = 0;
                s->grid = Grid();
                s->trimmed = false;
            }
        }
    m_textures.clear();
    m_mobSkins = Mobs::Skins(); // the names went with the old context
    m_trestles.clear();
    m_supportPath.clear();
    if (m_supports)
    {
        m_supports->arena.forget();
        m_supports->built = false;
    }
}

void LandWorld::unload(bool flat)
{
    std::unique_ptr<Scene> &slot = flat ? m_flat : m_scenery;
    if (!slot)
        return;
    slot->stop();
    slot->arena.release();
    slot.reset();
    if (!flat)
    {
        // the trestles stood on that island's ground
        m_trestles.clear();
        m_supportPath.clear();
        if (m_supports)
        {
            m_supports->arena.release();
            m_supports->built = false;
        }
    }
}

bool LandWorld::loaded(bool flat) const
{
    return (flat ? m_flat : m_scenery) != nullptr;
}

bool LandWorld::solidAt(bool flat, float x, float y, float z) const
{
    const Scene *s = flat ? m_flat.get() : m_scenery.get();
    if (!s || !s->gridReady)
        return false;
    // Only the grid's own blocks: get() takes everything under its lowest
    // layer for stone (so the faces there are not drawn), but under the
    // island's grid out at sea is open water, whose sea bed is the sea's
    // (OceanWorld::solidAt()).
    const int bx = blockOf(x), j = blockOf(y), bz = blockOf(z);
    if (!Grid::inside(bx, j, bz))
        return false;
    const int b = s->grid.get(bx, j, bz);
    return b != AIR && b != WATER;
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

namespace
{
// What a save of this island must have been made by (WorldSave.h): the
// files that shape it, built when they were.
std::string saveKey(bool flat)
{
    return std::string(flat ? "flat " : "island ") + __DATE__ " " __TIME__ + " | " +
           OceanWorld::buildStamp() + " | " + WorldShape::buildStamp() + " | " +
           WaterFlow::buildStamp() + " | " + PixelArt::buildStamp() + " | " +
           std::to_string(kSizeX) + "x" + std::to_string(kSizeY) + "x" + std::to_string(kSizeZ);
}

const char *saveName(bool flat)
{
    return flat ? "flat" : "island";
}

std::vector<std::vector<unsigned char> *> gridLayers(Grid &g)
{
    return {&g.block, &g.plant, &g.sky, &g.warm, &g.cool, &g.carved, &g.level, &g.biome};
}

void saveExtras(WorldSave::Writer &save, const Extras &e)
{
    save.number(static_cast<std::uint32_t>(e.animals.size()));
    for (const Animal &a : e.animals)
    {
        save.raw(&a.x, sizeof(a.x));
        save.raw(&a.z, sizeof(a.z));
        save.raw(&a.kind, sizeof(a.kind));
        save.raw(&a.phase, sizeof(a.phase));
    }
    save.raw(e.mill, sizeof(e.mill));
    save.raw(&e.hasMill, sizeof(e.hasMill));
    save.raw(e.grove, sizeof(e.grove));
    save.raw(&e.hasGrove, sizeof(e.hasGrove));
    save.raw(e.meadow, sizeof(e.meadow));
    save.raw(&e.hasMeadow, sizeof(e.hasMeadow));
    save.raw(e.temple, sizeof(e.temple));
    save.raw(&e.hasTemple, sizeof(e.hasTemple));
}

bool loadExtras(WorldSave::Reader &save, Extras &e)
{
    std::uint32_t count = 0;
    if (!save.number(count) || count > 100000u)
        return false;
    e.animals.resize(count);
    for (Animal &a : e.animals)
        if (!save.raw(&a.x, sizeof(a.x)) || !save.raw(&a.z, sizeof(a.z)) ||
            !save.raw(&a.kind, sizeof(a.kind)) || !save.raw(&a.phase, sizeof(a.phase)))
            return false;
    return save.raw(e.mill, sizeof(e.mill)) && save.raw(&e.hasMill, sizeof(e.hasMill)) &&
           save.raw(e.grove, sizeof(e.grove)) && save.raw(&e.hasGrove, sizeof(e.hasGrove)) &&
           save.raw(e.meadow, sizeof(e.meadow)) && save.raw(&e.hasMeadow, sizeof(e.hasMeadow)) &&
           save.raw(e.temple, sizeof(e.temple)) && save.raw(&e.hasTemple, sizeof(e.hasTemple));
}

bool loadGrid(Grid &g, Extras &e, bool flat)
{
    WorldSave::Reader save;
    if (!save.open(saveName(flat), saveKey(flat)))
        return false;
    for (std::vector<unsigned char> *layer : gridLayers(g))
        if (!save.layer(*layer))
            return false;
    return loadExtras(save, e) && save.done();
}

void saveGrid(Grid &g, const Extras &e, bool flat)
{
    WorldSave::Writer save(saveKey(flat));
    for (std::vector<unsigned char> *layer : gridLayers(g))
        save.layer(*layer);
    saveExtras(save, e);
    save.finish(saveName(flat));
}
} // namespace

// Runs on the worker thread: reads the grid from its save, or makes it and
// saves it, unless it is already there; then meshes it chunk by chunk and
// hands each chunk over.
void LandWorld::buildScene(Scene &s, bool flat)
{
    if (!s.gridReady)
    {
        s.fromSave = true;
        if (loadGrid(s.grid, s.extras, flat))
        {
            s.gridPermille = 1000;
            s.gridReady = true;
        }
        s.fromSave = false;
    }
    if (!s.gridReady)
    {
        s.grid = Grid();
        s.extras = Extras();
        if (flat)
            buildFlat(s.grid);
        else
        {
            BuildProgress progress;
            progress.permille = &s.gridPermille;
            progress.cancel = &s.cancel;
            buildScenery(s.grid, s.extras, progress);
        }
        if (s.cancel)
            return; // half built: made again from the start next time
        saveGrid(s.grid, s.extras, flat);
        s.gridPermille = 1000;
        s.gridReady = true;
    }

    for (int cx = kMinBX; cx < kMaxBX; cx += kChunk)
        for (int cz = kMinBZ; cz < kMaxBZ; cz += kChunk)
        {
            if (s.cancel)
                return;
            MeshedChunk chunk;
            chunk.solids.resize(LandTextures::TEXTURE_COUNT);
            chunk.plants.resize(LandTextures::TEXTURE_COUNT);
            chunk.water.resize(LandTextures::TEXTURE_COUNT);
            chunk.glass.resize(LandTextures::TEXTURE_COUNT);
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
                        {
                            addWaterFaces(s.grid, bx, j, bz, chunk.water);
                            if (s.grid.plant[cell] == LILY_PAD)
                                addLilyPad(s.grid, bx, j, bz, chunk.plants);
                        }
                        else if (b == WINDOW)
                            addGlassFaces(s.grid, bx, j, bz, chunk.glass);
                        else if (b == DOOR)
                            addDoor(s.grid, bx, j, bz, chunk.solids);
                        else if (b != AIR)
                            addBlockFaces(s.grid, bx, j, bz, chunk.solids);
                        else if (s.grid.plant[cell] == SNOW_LAYER)
                            addSnowLayer(s.grid, bx, j, bz, chunk.solids);
                        else if (s.grid.plant[cell] == FENCE)
                            addFence(s.grid, bx, j, bz, chunk.solids);
                        else if (s.grid.plant[cell] == LAMP)
                            addLantern(s.grid, bx, j, bz, chunk.solids);
                        else if (s.grid.plant[cell] >= STAIRS && s.grid.plant[cell] <= POT)
                            addShape(s.grid, bx, j, bz, chunk.solids);
                        else
                            addPlant(s.grid, bx, j, bz, chunk.plants);
                    }
            ++s.chunksMeshed;
            if (high < low)
                continue; // nothing in it
            chunk.lo[0] = static_cast<float>(cx) * kBlock;
            chunk.lo[1] = static_cast<float>(low) * kBlock;
            chunk.lo[2] = static_cast<float>(cz) * kBlock;
            chunk.hi[0] = static_cast<float>(cx + kChunk) * kBlock;
            chunk.hi[1] = static_cast<float>(high + 1) * kBlock + 1.0f; // plants sway a little
            chunk.hi[2] = static_cast<float>(cz + kChunk) * kBlock;
            std::lock_guard<std::mutex> hold(s.lock);
            s.meshed.push_back(std::move(chunk));
        }
    std::lock_guard<std::mutex> hold(s.lock);
    s.meshingDone = true;
}

bool LandWorld::prepare(bool flat, float seconds)
{
    std::unique_ptr<Scene> &slot = flat ? m_flat : m_scenery;
    if (!slot)
        slot.reset(new Scene);
    Scene &s = *slot;
    if (s.built)
        return true;

    if (!s.worker.joinable())
        s.worker = std::thread(&LandWorld::buildScene, std::ref(s), flat);

    if (m_textures.empty())
    {
        m_textures.assign(LandTextures::TEXTURE_COUNT, 0);
        for (int t = 0; t < LandTextures::TEXTURE_COUNT; ++t)
            m_textures[static_cast<size_t>(t)] =
                PixelArt::upload(pictureOf(t), LandTextures::repeatsVertically(t));
        m_texturesStale = false;
    }

    // Turn the chunks meshed so far into vertex buffers, for as long as the
    // caller can wait.
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::microseconds(static_cast<long long>(seconds * 1e6f));
    for (;;)
    {
        std::vector<MeshedChunk> ready;
        bool finished;
        {
            std::lock_guard<std::mutex> hold(s.lock);
            ready.swap(s.meshed);
            finished = s.meshingDone;
        }
        for (size_t i = 0; i < ready.size(); ++i)
        {
            LandChunk chunk;
            for (int k = 0; k < 3; ++k)
            {
                chunk.lo[k] = ready[i].lo[k];
                chunk.hi[k] = ready[i].hi[k];
            }
            chunk.solid = s.arena.compile(ready[i].solids, m_textures);
            chunk.plant = s.arena.compile(ready[i].plants, m_textures);
            chunk.water = s.arena.compile(ready[i].water, m_textures);
            chunk.glass = s.arena.compile(ready[i].glass, m_textures);
            s.chunks.push_back(std::move(chunk));
            if (std::chrono::steady_clock::now() > until && i + 1 < ready.size())
            {
                // out of time: give the rest back for the next frame
                std::lock_guard<std::mutex> hold(s.lock);
                s.meshed.insert(
                    s.meshed.begin(),
                    std::make_move_iterator(ready.begin() + static_cast<std::ptrdiff_t>(i) + 1),
                    std::make_move_iterator(ready.end()));
                return false;
            }
        }
        if (finished && ready.empty())
        {
            s.worker.join();
            s.arena.compact(); // one buffer: one bind a pass
            s.built = true;
            for (std::vector<unsigned char> *layer : {&s.grid.plant, &s.grid.sky, &s.grid.warm,
                                                      &s.grid.cool, &s.grid.carved, &s.grid.level})
                std::vector<unsigned char>().swap(*layer);
            s.trimmed = true;
            return true;
        }
        if (finished)
            continue; // the last chunks were handed over with the flag
        if (std::chrono::steady_clock::now() > until)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

float LandWorld::progress(bool flat) const
{
    const Scene *s = (flat ? m_flat : m_scenery).get();
    if (!s)
        return 0.0f;
    if (s->built)
        return 1.0f;
    // making the grid is most of the work; meshing and the lists the rest
    const float grid = static_cast<float>(s->gridPermille.load()) / 1000.0f;
    const float meshed =
        static_cast<float>(s->chunksMeshed.load()) / static_cast<float>(kChunkCount);
    const float lists = static_cast<float>(s->chunks.size()) / static_cast<float>(kChunkCount);
    return 0.7f * grid + 0.2f * meshed + 0.1f * fminf(1.0f, lists * 1.25f);
}

bool LandWorld::loadingSave(bool flat) const
{
    const Scene *s = (flat ? m_flat : m_scenery).get();
    return s && s->fromSave;
}

LandWorld::Scene &LandWorld::scene(bool flat)
{
    // a caller that cannot wait: finish the build now
    while (!prepare(flat, 1.0f))
    {
    }
    return *(flat ? m_flat : m_scenery);
}

void LandWorld::setTexturePack(std::shared_ptr<const TexturePack::Pack> pack)
{
    m_pack = std::move(pack);
    m_texturesStale = true;
    m_mobsStale = true;
}

void LandWorld::uploadMobSkins()
{
    for (int skin = 0; skin < Mobs::SKIN_COUNT; ++skin)
    {
        Mobs::Picture picture;
        if (!m_pack || !m_pack->entityPicture(Mobs::packNames(skin), Mobs::fits, picture.width,
                                              picture.height, picture.rgba))
            picture = Mobs::builtin(skin);
        Mobs::upload(m_mobSkins, skin, picture);
    }
    m_mobsStale = false;
}

PixelArt::Image LandWorld::pictureOf(int texture) const
{
    PixelArt::Image picture;
    if (m_pack && m_pack->landTexture(texture, picture))
        return picture;
    return LandTextures::make(texture);
}

void LandWorld::draw(bool flat, bool shadowPass, float time)
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    // a new pack: its pictures go into the same texture names, which the
    // meshes bind, so nothing has to be built again
    if (m_texturesStale && !m_textures.empty())
    {
        for (int t = 0; t < LandTextures::TEXTURE_COUNT; ++t)
            PixelArt::uploadInto(m_textures[static_cast<size_t>(t)], pictureOf(t),
                                 LandTextures::repeatsVertically(t));
        m_texturesStale = false;
    }
    const Scene &s = scene(flat);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    const Frustum view = Frustum::current();
    m_chunksDrawn = 0;
    // the fixed pipeline cuts the leaves and plants out here; the shader
    // does it itself, and uses the alpha it writes for something else (how
    // open a surface is to the rain), so the test stays off under it
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program == 0)
    {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
    }
    m_visible.clear();
    for (const LandChunk &c : s.chunks)
        if (view.sees(c.lo, c.hi))
        {
            m_visible.push_back(&c.solid);
            ++m_chunksDrawn;
        }
    s.arena.draw(m_visible, BlockMesh::drawWindow());
    if (!shadowPass)
    {
        // the plants after all the blocks, as before
        m_visible.clear();
        for (const LandChunk &c : s.chunks)
            if (view.sees(c.lo, c.hi))
                m_visible.push_back(&c.plant);
        s.arena.draw(m_visible, BlockMesh::drawWindow());
        if (!flat)
            drawMoving(s, time);
    }
    glDisable(GL_ALPHA_TEST);
    setGlow(0.0f);
    glPopAttrib();
}

void LandWorld::drawWater(bool flat)
{
    drawSeeThrough(flat, true);
}

void LandWorld::drawGlass(bool flat)
{
    drawSeeThrough(flat, false);
}

void LandWorld::drawSeeThrough(bool flat, bool withWater)
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
    m_visible.clear();
    for (const LandChunk &c : s.chunks)
        if (view.sees(c.lo, c.hi))
        {
            if (withWater)
                m_visible.push_back(&c.water);
            m_visible.push_back(&c.glass);
        }
    s.arena.draw(m_visible, BlockMesh::drawWindow());
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    s.arena.draw(m_visible, BlockMesh::drawWindow());
    setGlow(0.0f);
    glPopAttrib();
}

void LandWorld::drawWaterSurface(bool flat)
{
    if (!(flat ? m_flat : m_scenery) || !(flat ? m_flat : m_scenery)->built)
        return;
    const Scene &s = flat ? *m_flat : *m_scenery;
    const Frustum view = Frustum::current();
    m_visible.clear();
    for (const LandChunk &c : s.chunks)
        if (view.sees(c.lo, c.hi))
            m_visible.push_back(&c.water);
    s.arena.draw(m_visible, BlockMesh::drawWindow());
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
        // an oak shaft and frames with white wool sails, as blocks a block
        // game has
        glEnable(GL_TEXTURE_2D);
        glColor3f(0.95f, 0.95f, 0.95f);
        glBindTexture(GL_TEXTURE_2D, m_textures[LandTextures::OAK_SIDE]);
        texturedBox(-0.5f, -0.5f, -3.4f, 0.5f, 0.5f, 0.3f); // the shaft, back to the tower
        glRotatef(time * 22.0f, 0.0f, 0.0f, 1.0f);
        for (int blade = 0; blade < 4; ++blade)
        {
            glPushMatrix();
            glRotatef(90.0f * static_cast<float>(blade), 0.0f, 0.0f, 1.0f);
            glBindTexture(GL_TEXTURE_2D, m_textures[LandTextures::PLANKS]);
            texturedBox(-0.22f, 0.0f, -0.05f, 0.22f, 11.5f, 0.3f);
            glBindTexture(GL_TEXTURE_2D, m_textures[LandTextures::WOOL_WHITE]);
            texturedBox(0.22f, 2.0f, 0.0f, 2.9f, 11.0f, 0.16f);
            glPopMatrix();
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }

    // animals wander around their spot, facing the way they walk
    if (m_mobsStale || m_mobSkins.texture[0] == 0)
        uploadMobSkins();
    glEnable(GL_TEXTURE_2D);
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
        glScalef(kBlock / 16.0f, kBlock / 16.0f, kBlock / 16.0f); // the game's pixels
        Mobs::draw(a.kind, sinf(time * 5.0f + a.phase), m_mobSkins, woolColour(a.phase));
        glPopMatrix();
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);

    // hot-air balloons, bobbing where the ride gets a good look at them
    const float spots[3][3] = {
        {-25.0f, 156.0f, 210.0f}, {375.0f, 93.0f, 240.0f}, {75.0f, 87.0f, -375.0f}};
    const int paints[3][2] = {{LandTextures::WOOL_RED, LandTextures::WOOL_WHITE},
                              {LandTextures::WOOL_BLUE, LandTextures::WOOL_YELLOW},
                              {LandTextures::WOOL_ORANGE, LandTextures::WOOL_PURPLE}};
    for (int i = 0; i < 3; ++i)
    {
        const float fi = static_cast<float>(i);
        glPushMatrix();
        glTranslatef(spots[i][0] + sinf(time * 0.11f + fi * 2.0f) * 9.0f,
                     spots[i][1] + sinf(time * 0.4f + fi) * 2.5f,
                     spots[i][2] + cosf(time * 0.09f + fi) * 9.0f);
        glRotatef(time * 4.0f + fi * 40.0f, 0.0f, 1.0f, 0.0f);
        drawBalloon(paints[i][0], paints[i][1], m_textures);
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
    if (!Grid::insideColumn(bx, bz) || !onIsland(bx, bz) || !m_scenery || !m_scenery->gridReady)
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

// The blocky clouds drifting east, high above the peaks. No glow: they
// are lit as the sky lights them (the caller sets that up, see the header),
// so they are white by day, warm at sunrise and sunset and only grey-blue
// in the moonlight. Each face is shaded as the block game shades its
// clouds, the top brightest and the underside darkest, so the boxes keep
// their shape though the light no longer depends on which way they face.
void LandWorld::drawClouds(bool flat, float time)
{
    if (flat)
        return;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_TEXTURE_2D);
    setGlow(0.0f);
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
            cloudBox(px - w, y, pz - d, px + w, y + 6.0f + static_cast<float>(part % 2) * 4.0f,
                     pz + d);
        }
    }
    glPopAttrib();
}

// Works out the trestles for a track: one every 13 to 19 units along it,
// wherever it is above the water and well above the ground. Each of the
// two posts finds its own ground, so on a slope neither hangs in the air,
// and a post that would pass through the room of another stretch of track
// below is left out (the other one still stands); so are the cross beams.
void LandWorld::planSupports(const std::vector<float> &path)
{
    m_trestles.clear();
    const size_t count = path.size() / 3;
    if (count < 4)
        return;
    const float spacing = 16.0f; // along the track, between trestles, on average
    const float lean = 2.0f;     // how far out a tall post's foot may go
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
    // not quite evenly: each gap 13 to 19 units, from where it starts
    float travelled = spacing, gap = spacing;
    for (size_t n = 0; n < count; ++n)
    {
        const size_t next = (n + 1) % count;
        float ax = path[3 * next] - path[3 * n], az = path[3 * next + 2] - path[3 * n + 2];
        const float length = sqrtf(ax * ax + az * az);
        travelled += length;
        if (travelled < gap || length < 1.0e-4f)
            continue;
        const float x = path[3 * n], y = path[3 * n + 1], z = path[3 * n + 2];
        if (y < OceanWorld::waterLevel() + 0.5f)
            continue; // under water the track needs no posts
        travelled = 0.0f;
        gap = spacing - 3.0f + static_cast<float>(hash(blockOf(x), blockOf(z), 690u) % 7u);
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

namespace
{
// A trestle's own frame: x across the track (to its right), y up (the
// world's height), z along it; (cx, cz) the centre under the track, (c, s)
// the cosine and sine of its yaw, as glRotatef() about +y turned it.
struct TrestleFrame
{
    float cx, cz, c, s;
    float shade; // how light the part being added is: each post and pier its own
    // and its colour on top of that (wet wood with algae on it is greener)
    float tint[3] = {1.0f, 1.0f, 1.0f};
    void place(const float l[3], float out[3]) const
    {
        out[0] = cx + l[0] * c + l[2] * s;
        out[1] = l[1];
        out[2] = cz - l[0] * s + l[2] * c;
    }
    void turn(const float l[3], float out[3]) const
    {
        out[0] = l[0] * c + l[2] * s;
        out[1] = l[1];
        out[2] = -l[0] * s + l[2] * c;
    }
};

// One quad of a trestle, its corners in the trestle's frame and their
// picture coordinates; lit as the open air (no block light, full sky light).
void addTrestleQuad(Batches &batches, int texture, const TrestleFrame &f, const float corner[4][3],
                    const float uv[4][2], const float normal[3], float glow, const float sway[4])
{
    std::vector<Vertex> &out = batches[static_cast<size_t>(texture)];
    float n[3];
    f.turn(normal, n);
    for (int k = 0; k < 4; ++k)
    {
        Vertex v;
        f.place(corner[k], v.pos);
        v.normal[0] = n[0];
        v.normal[1] = n[1];
        v.normal[2] = n[2];
        v.uv[0] = uv[k][0];
        v.uv[1] = uv[k][1];
        v.color[0] = f.shade * f.tint[0];
        v.color[1] = f.shade * f.tint[1];
        v.color[2] = f.shade * f.tint[2];
        v.extra[0] = glow;
        v.extra[1] = sway ? sway[k] : 0.0f;
        v.extra[2] = 0.0f;
        v.extra[3] = 1.0f;
        out.push_back(v);
    }
}

// A box from lo to hi in the trestle's frame. pictures: for each face (+x,
// -x, +y, -y, +z, -z) the left edge's u, the bottom edge's v, the right
// edge's u and the top edge's v in sixteenths, as addPart() takes them; null
// for the picture at a block's scale, as on the blocks.
void addTrestleBox(Batches &batches, int texture, const TrestleFrame &f, const float lo[3],
                   const float hi[3], float glow, const float (*pictures)[4] = nullptr)
{
    // Most of the island's side pictures do not repeat upwards (their top
    // row has to stay on top), so a box taller than a block is made of
    // boxes a block high at most, each showing its picture whole; drawn as
    // one, the picture's last row was stretched down the rest of it.
    if (!pictures && hi[1] - lo[1] > kBlock + 1e-3f)
    {
        for (float y = hi[1]; y > lo[1] + 1e-3f; y -= kBlock)
        {
            const float pieceLo[3] = {lo[0], fmaxf(lo[1], y - kBlock), lo[2]};
            const float pieceHi[3] = {hi[0], y, hi[2]};
            addTrestleBox(batches, texture, f, pieceLo, pieceHi, glow);
        }
        return;
    }
    const float x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
    // corners going round each face as seen from outside it, from its
    // bottom left
    const float corners[6][4][3] = {
        {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}, // +x
        {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}, // -x
        {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}, // +y
        {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}, // -y
        {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}, // +z
        {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}, // -z
    };
    const float normals[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                 {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    const float k = 1.0f / kBlock;
    // the face's width and height, for the picture at a block's scale
    const float sizes[6][2] = {{z1 - z0, y1 - y0}, {z1 - z0, y1 - y0}, {x1 - x0, z1 - z0},
                               {x1 - x0, z1 - z0}, {x1 - x0, y1 - y0}, {x1 - x0, y1 - y0}};
    for (int face = 0; face < 6; ++face)
    {
        float u0, v0, u1, v1; // left, bottom, right, top
        if (pictures)
        {
            u0 = pictures[face][0] / 16.0f;
            v0 = pictures[face][1] / 16.0f;
            u1 = pictures[face][2] / 16.0f;
            v1 = pictures[face][3] / 16.0f;
        }
        else
        {
            u0 = 0.0f;
            u1 = sizes[face][0] * k;
            v0 = sizes[face][1] * k;
            v1 = 0.0f;
        }
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        addTrestleQuad(batches, texture, f, corners[face], uv, normals[face], glow, nullptr);
    }
}

// A beam of square-ish section from a to b in the trestle's frame, `half`
// across and `deep` front to back, the picture at a block's scale along it:
// the posts, which lean, and the braces, which run aslant.
void addTrestleBeam(Batches &batches, int texture, const TrestleFrame &f, const float a[3],
                    const float b[3], float half, float deep)
{
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length < 1e-4f)
        return;
    // pieces a block long at most, each with its picture whole (as boxes)
    if (length > kBlock + 1e-3f)
    {
        const int pieces = static_cast<int>(ceilf(length / kBlock));
        for (int i = 0; i < pieces; ++i)
        {
            const float t0 = static_cast<float>(i) / static_cast<float>(pieces);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(pieces);
            const float p0[3] = {a[0] + d[0] * t0, a[1] + d[1] * t0, a[2] + d[2] * t0};
            const float p1[3] = {a[0] + d[0] * t1, a[1] + d[1] * t1, a[2] + d[2] * t1};
            addTrestleBeam(batches, texture, f, p0, p1, half, deep);
        }
        return;
    }
    for (float &x : d)
        x /= length;
    // across: level and square to the beam (x for an upright post);
    // deep: square to both
    float across[3] = {d[2], 0.0f, -d[0]};
    float level = sqrtf(across[0] * across[0] + across[2] * across[2]);
    if (level < 1e-3f)
    {
        across[0] = 1.0f;
        across[2] = 0.0f;
        level = 1.0f;
    }
    across[0] /= level;
    across[2] /= level;
    const float depth[3] = {d[1] * across[2] - d[2] * across[1],
                            d[2] * across[0] - d[0] * across[2],
                            d[0] * across[1] - d[1] * across[0]};
    auto at = [&](const float from[3], float sa, float sd, float out[3]) {
        for (int i = 0; i < 3; ++i)
            out[i] = from[i] + across[i] * half * sa + depth[i] * deep * sd;
    };
    const float k = 1.0f / kBlock;
    const float sides[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}}; // going round
    for (int i = 0; i < 4; ++i)
    {
        const float *p = sides[i], *q = sides[(i + 1) % 4];
        float corner[4][3];
        at(a, p[0], p[1], corner[0]);
        at(a, q[0], q[1], corner[1]);
        at(b, q[0], q[1], corner[2]);
        at(b, p[0], p[1], corner[3]);
        const float width = (i % 2 == 0 ? 2.0f * half : 2.0f * deep) * k;
        const float uv[4][2] = {{0, length * k}, {width, length * k}, {width, 0}, {0, 0}};
        float normal[3];
        for (int c = 0; c < 3; ++c)
            normal[c] = across[c] * (p[0] + q[0]) * 0.5f + depth[c] * (p[1] + q[1]) * 0.5f;
        const float nl =
            sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        for (float &x : normal)
            x /= nl;
        addTrestleQuad(batches, texture, f, corner, uv, normal, 0.0f, nullptr);
    }
    // the ends
    for (int e = 0; e < 2; ++e)
    {
        const float *end = e == 0 ? a : b;
        float corner[4][3];
        at(end, 1, 1, corner[0]);
        at(end, -1, 1, corner[1]);
        at(end, -1, -1, corner[2]);
        at(end, 1, -1, corner[3]);
        const float uv[4][2] = {
            {0, 0}, {2 * half * k, 0}, {2 * half * k, 2 * deep * k}, {0, 2 * deep * k}};
        float normal[3] = {d[0], d[1], d[2]};
        if (e == 0)
            for (float &x : normal)
                x = -x;
        addTrestleQuad(batches, texture, f, corner, uv, normal, 0.0f, nullptr);
    }
}

// How much of the vine picture goes across one strand (its first two
// strands of leaves), and so how much longer than wide a piece showing the
// picture's whole height is.
const float kVineSlice = 0.25f;
const float kVinePieces = 1.0f / kVineSlice;

// A strand of vines hanging from y down to y - drop at (x, z), about `width`
// across at the top and narrower towards its end. It is two strips of the
// vine picture crossed at right angles, so it shows from every side, and it
// wanders a little from side to side and to and fro on its way down instead
// of hanging as a flat, straight ribbon; the lower it hangs, the more it
// sways, and a long strand swings further. seed: where it hangs, so it looks
// the same every time.
void addTrestleVines(Batches &batches, const TrestleFrame &frame, float x, float y, float z,
                     float drop, float width, unsigned int seed)
{
    // a little darker than the wood beside them, as leaves in the shade of
    // the trestle (lit as brightly they stood out, almost glowing green)
    TrestleFrame f = frame;
    f.shade = frame.shade * 0.72f;
    // The picture's first two strands (a quarter of it across, where the
    // whole picture's thin strands vanished on a narrow strand) across it,
    // in pieces down it with the texels square (the picture does not repeat
    // upwards).
    // (a sway of a tenth of a unit or so: more swung them through the
    // beams and posts beside them)
    const float swing = fminf(0.1f, 0.03f + 0.012f * drop);
    float ox = 0.0f, oz = 0.0f; // where the last piece ended, off (x, z)
    int n = 0;
    for (float top = 0.0f; top < drop - 1e-3f; ++n)
    {
        auto dice = [&](unsigned int k) {
            return static_cast<float>(hash(n, static_cast<int>(k), seed) % 1000u) / 1000.0f;
        };
        const float w = width * (1.0f - 0.45f * top / drop);
        const float piece = kVinePieces * w;
        const float bottom = fminf(drop, top + piece);
        const float v = (bottom - top) / piece;
        float nx = ox + (dice(1) - 0.5f) * 0.35f * w, nz = oz + (dice(2) - 0.5f) * 0.25f * w;
        nx = fmaxf(-0.6f, fminf(0.6f, nx));
        nz = fmaxf(-0.12f, fminf(0.12f, nz));
        const float swayTop = swing * top / drop, swayBottom = swing * bottom / drop;
        const float sway[4] = {swayBottom, swayBottom, swayTop, swayTop}; // bottom corners first
        const float h = 0.5f * w;
        for (int plane = 0; plane < 2; ++plane)
            for (int side = 0; side < 2; ++side)
            {
                const float e = side == 0 ? h : -h;
                // plane 0 faces along z, plane 1 along x
                const float ex = plane == 0 ? e : 0.0f, ez = plane == 1 ? e : 0.0f;
                const float corner[4][3] = {{x + nx - ex, y - bottom, z + nz - ez},
                                            {x + nx + ex, y - bottom, z + nz + ez},
                                            {x + ox + ex, y - top, z + oz + ez},
                                            {x + ox - ex, y - top, z + oz - ez}};
                const float uv[4][2] = {{0, v}, {kVineSlice, v}, {kVineSlice, 0}, {0, 0}};
                const float facing = side == 0 ? 1.0f : -1.0f;
                const float normal[3] = {plane == 1 ? -facing : 0.0f, 0.0f,
                                         plane == 0 ? facing : 0.0f};
                addTrestleQuad(batches, LandTextures::VINES, f, corner, uv, normal, 0.0f, sway);
            }
        ox = nx;
        oz = nz;
        top = bottom;
    }
}

// A clump of leaves over (lo, hi): three to five small blocks of leaves of
// different sizes scattered through it, some hanging lower than others, not
// one box, and as dark as the island's trees in the shade of their own
// leaves. seed: where it is.
void addLeafClump(Batches &batches, int texture, const TrestleFrame &frame, const float lo[3],
                  const float hi[3], unsigned int seed)
{
    TrestleFrame f = frame;
    f.shade = 0.68f;
    const int parts = 3 + static_cast<int>(hash(0, 1, seed) % 3u);
    for (int i = 0; i < parts; ++i)
    {
        float a[3], b[3];
        for (int k = 0; k < 3; ++k)
        {
            const float size = hi[k] - lo[k];
            const float r0 = static_cast<float>(hash(i, 10 + k, seed) % 1000u) / 1000.0f;
            const float r1 = static_cast<float>(hash(i, 20 + k, seed) % 1000u) / 1000.0f;
            // each block takes 30-60 % of the clump each way, somewhere in it
            const float part = size * (0.3f + 0.3f * r0);
            a[k] = lo[k] + (size - part) * r1;
            b[k] = a[k] + part;
        }
        addTrestleBox(batches, texture, f, a, b, 0.0f);
    }
}

// The leaves the vines grow from, by the land round about (-1: out at sea).
int trestleLeavesIn(int biome)
{
    switch (biome)
    {
    case MANGROVE:
        return LandTextures::LEAVES_MANGROVE;
    case ALPINE:
        return LandTextures::LEAVES_SPRUCE;
    case VOLCANO:
    case MESA:
        return LandTextures::LEAVES_AUTUMN;
    default:
        return LandTextures::LEAVES_OAK;
    }
}

// A small plant standing on (x, y, z): its picture twice, crossed, as the
// island draws its plants, `size` high and turned by `turn` (radians); its
// top sways a little in the wind (which also marks it as a plant to the
// lighting, so the sheen of water leaves it alone).
void addTrestlePlant(Batches &batches, int texture, const TrestleFrame &f, float x, float y,
                     float z, float size, float turn)
{
    const float half = 0.5f * size;
    const float sway[4] = {0.0f, 0.0f, 0.03f, 0.03f}; // bottom corners first
    for (int plane = 0; plane < 2; ++plane)
    {
        const float a = turn + 1.5708f * static_cast<float>(plane);
        const float cx = cosf(a), cz = sinf(a);
        for (int side = 0; side < 2; ++side)
        {
            const float e = side == 0 ? half : -half;
            const float corner[4][3] = {{x - cx * e, y, z - cz * e},
                                        {x + cx * e, y, z + cz * e},
                                        {x + cx * e, y + size, z + cz * e},
                                        {x - cx * e, y + size, z - cz * e}};
            const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            const float facing = side == 0 ? 1.0f : -1.0f;
            const float normal[3] = {-cz * facing, 0.0f, cx * facing};
            addTrestleQuad(batches, texture, f, corner, uv, normal, 0.0f, sway);
        }
    }
}

// A vine climbing a post from (x0, y0) up to (x1, y1) (the post leans),
// the post `half` across each way from its middle: it winds round it,
// each piece on the next of its four faces, so it climbs in a spiral
// instead of sticking to one face; each piece a little to one side of the
// face, its own width (about `width`), and here and there one left out.
// seed: where the post is, so it looks the same every time.
void addTrestleClimber(Batches &batches, const TrestleFrame &frame, float x0, float y0, float x1,
                       float y1, float half, float width, unsigned int seed)
{
    TrestleFrame f = frame; // darker, as the hanging vines
    f.shade = frame.shade * 0.72f;
    // the picture's first two strands across it, as hanging vines take them
    const float piece = kVinePieces * width * 0.6f;
    const float out = half + 0.02f; // just off the post's faces
    const int start = static_cast<int>(hash(0, 9, seed) % 4u);
    int n = 0;
    for (float a = y0; a < y1 - 1e-3f; a += piece, ++n)
    {
        auto dice = [&](unsigned int k) {
            return static_cast<float>(hash(n, static_cast<int>(k), seed) % 1000u) / 1000.0f;
        };
        if (n > 0 && dice(1) < 0.15f)
            continue;
        // a little overlap with the next piece, round the corner
        const float b = fminf(y1, a + piece * 1.15f);
        const float xa = x0 + (x1 - x0) * (a - y0) / (y1 - y0);
        const float xb = x0 + (x1 - x0) * (b - y0) / (y1 - y0);
        const float h = fminf(0.5f * width * (0.65f + 0.5f * dice(3)), out);
        const float shift = (dice(2) - 0.5f) * 2.0f * fmaxf(out - h, 0.0f);
        const float v = (b - a) / (kVinePieces * 2.0f * h);
        // on this face and round the corner onto the next, so it wraps
        for (int wrap = 0; wrap < 2; ++wrap)
        {
            const int face = (start + n + wrap) % 4; // +z, +x, -z, -x
            const float sign = face < 2 ? 1.0f : -1.0f;
            const bool alongX = face % 2 == 0; // the piece lies along x on a z face
            for (int side = 0; side < 2; ++side)
            {
                const float w = side == 0 ? h : -h;
                float corner[4][3];
                if (alongX)
                {
                    const float z = sign * out;
                    const float c[4][3] = {{xa + shift - w, a, z},
                                           {xa + shift + w, a, z},
                                           {xb + shift + w, b, z},
                                           {xb + shift - w, b, z}};
                    for (int i = 0; i < 4; ++i)
                        for (int k = 0; k < 3; ++k)
                            corner[i][k] = c[i][k];
                }
                else
                {
                    const float c[4][3] = {{xa + sign * out, a, shift - w},
                                           {xa + sign * out, a, shift + w},
                                           {xb + sign * out, b, shift + w},
                                           {xb + sign * out, b, shift - w}};
                    for (int i = 0; i < 4; ++i)
                        for (int k = 0; k < 3; ++k)
                            corner[i][k] = c[i][k];
                }
                const float uv[4][2] = {{0, v}, {kVineSlice, v}, {kVineSlice, 0}, {0, 0}};
                const float facing = side == 0 ? sign : -sign;
                const float normal[3] = {alongX ? 0.0f : facing, 0.0f, alongX ? facing : 0.0f};
                // (a sway too small to see: it marks the leaves as a plant, so the
                // water's sheen and the rain's film leave them alone)
                const float still[4] = {1e-4f, 1e-4f, 1e-4f, 1e-4f};
                addTrestleQuad(batches, LandTextures::VINES, f, corner, uv, normal, 0.0f, still);
            }
        }
    }
}

// A lantern hung on a chain chainLength long under (x, y, z), as
// addLantern() draws one in a block: a glowing body six sixteenths across
// and seven high, a narrower cap.
void addTrestleLantern(Batches &batches, const TrestleFrame &f, float x, float y, float z,
                       float chainLength)
{
    const float u = kBlock / 16.0f;
    const float chain[6][4] = {{0, 16, 3, 0}, {0, 16, 3, 0}, {0, 0, 3, 3},
                               {0, 0, 3, 3},  {0, 16, 3, 0}, {0, 16, 3, 0}};
    const float chainLo[3] = {x - u, y - chainLength, z - u};
    const float chainHi[3] = {x + u, y, z + u};
    addTrestleBox(batches, LandTextures::CHAIN, f, chainLo, chainHi, 0.0f, chain);
    const float capTop = y - chainLength;
    const float cap[6][4] = {{1, 2, 5, 0},   {1, 2, 5, 0}, {1, 10, 5, 14},
                             {1, 10, 5, 14}, {1, 2, 5, 0}, {1, 2, 5, 0}};
    const float capLo[3] = {x - 2 * u, capTop - 2 * u, z - 2 * u};
    const float capHi[3] = {x + 2 * u, capTop, z + 2 * u};
    addTrestleBox(batches, LandTextures::LANTERN_SMALL, f, capLo, capHi, 0.3f, cap);
    const float body[6][4] = {{0, 9, 6, 2},  {0, 9, 6, 2}, {0, 9, 6, 15},
                              {0, 9, 6, 15}, {0, 9, 6, 2}, {0, 9, 6, 2}};
    const float bodyLo[3] = {x - 3 * u, capTop - 9 * u, z - 3 * u};
    const float bodyHi[3] = {x + 3 * u, capTop - 2 * u, z + 3 * u};
    addTrestleBox(batches, LandTextures::LANTERN_SMALL, f, bodyLo, bodyHi, 1.0f, body);
}
} // namespace

// The trestles' mesh, made from m_trestles. Each is dressed for where it
// stands, and no two alike: the posts are of the wood that grows round it
// (spruce in the snow, mangrove in the swamp, dark timber by the volcano, in
// the badlands and the bamboo hills, oak elsewhere), each a shade lighter or
// darker, under a cap of planks; cross beams with a single brace between
// each two, turning each way in turn (knee braces where there is only one
// post); the posts going straight into the ground or the sea bed, and snow
// on the beams in the mountains; lanterns on chains of different lengths
// here and there (never two trestles running, sometimes one on each side);
// vines grown over most of them as over a jungle tree, hanging from the
// beams in threads and curtains of all lengths and climbing the posts, a lot
// in the swamp and the bamboo hills; and vines hanging off the ends of the
// sleepers here and there where the track is up in the air. The dice are thrown from where each trestle or
// stretch of track is, so it looks the same every time, and editing the
// track elsewhere does not change it.
void LandWorld::buildSupportMesh()
{
    namespace T = LandTextures;
    Batches batches(T::TEXTURE_COUNT);
    const float water = OceanWorld::waterLevel();
    const Grid &g = m_scenery->grid;
    // How far a vine hanging from y at (lx, lz) in frame fr may reach down:
    // to a little above the ground or the water, and nowhere near a stretch
    // of track further down (a train goes over it, about six high).
    auto vineRoom = [&](const TrestleFrame &fr, float lx, float lz, float y) {
        const float l[3] = {lx, 0.0f, lz};
        float w[3];
        fr.place(l, w);
        float most = y - (fmaxf(postGround(w[0], w[2], y), water) + 0.6f);
        for (size_t i = 0; i + 2 < m_supportPath.size(); i += 3)
        {
            const float px = m_supportPath[i], py = m_supportPath[i + 1], pz = m_supportPath[i + 2];
            if (py < y - 1.0f && fabsf(px - w[0]) < 3.0f && fabsf(pz - w[2]) < 3.0f)
                most = fminf(most, y - (py + 6.0f));
        }
        return most;
    };
    bool litBefore = false; // the trestle before has a lantern
    for (size_t n = 0; n < m_trestles.size(); ++n)
    {
        const Trestle &t = m_trestles[n];
        const int px = static_cast<int>(floorf(t.x * 2.0f)),
                  pz = static_cast<int>(floorf(t.z * 2.0f));
        auto roll = [&](int k) {
            return static_cast<float>(hash(px, pz, 700u + static_cast<unsigned int>(k)) % 1000u) /
                   1000.0f;
        };
        const float yaw = t.yaw / 57.29578f;
        TrestleFrame f = {t.x, t.z, cosf(yaw), sinf(yaw), 0.95f};
        const int bx = blockOf(t.x), bz = blockOf(t.z);
        const int biome = Grid::insideColumn(bx, bz) && onIsland(bx, bz)
                              ? g.biome[Grid::column(bx, bz)]
                              : -1; // out at sea
        int wood = T::OAK_SIDE, planks = T::PLANKS;
        if (biome == ALPINE)
        {
            wood = T::SPRUCE_SIDE;
            planks = T::SPRUCE_PLANKS;
        }
        else if (biome == MANGROVE)
        {
            wood = T::MANGROVE_SIDE;
            planks = T::SPRUCE_PLANKS;
        }
        else if (biome == BAMBOO_JUNGLE)
        {
            wood = T::DARK_OAK_SIDE;
            planks = T::SPRUCE_PLANKS;
        }
        else if (biome == VOLCANO || biome == MESA)
        {
            wood = T::DARK_OAK_SIDE;
            planks = T::SPRUCE_PLANKS;
        }
        const bool overgrown = biome == MANGROVE || biome == BAMBOO_JUNGLE;
        const bool snowy = biome == ALPINE;
        const bool both = t.post[0] && t.post[1];
        const float post = 0.25f + 0.07f * roll(1);

        // Where each post goes into the ground or the sea bed (a little below
        // it, so no gap shows on a slope), and the ground or the water at its
        // foot: what a lantern hangs clear of, what the beams stop above and
        // where a climbing vine starts. No stone round the posts: footings
        // turned to the track in a world of blocks looked put on.
        float footY[2], baseY[2];
        auto postX = [&](int side, float h) {
            const float topX = side == 0 ? -1.3f : 1.3f;
            const float ground = t.ground[side];
            return t.foot[side] + (topX - t.foot[side]) * (h - ground) / (t.top - ground);
        };
        for (int side = 0; side < 2; ++side)
        {
            footY[side] = t.ground[side] - 0.6f;
            baseY[side] = fmaxf(t.ground[side], water);
        }
        // the posts, leaning in from the ground to the cap
        for (int side = 0; side < 2; ++side)
        {
            if (!t.post[side])
                continue;
            const float dry = 0.84f + 0.14f * roll(30 + side);
            const float a[3] = {postX(side, footY[side]), footY[side], 0.0f};
            const float b[3] = {side == 0 ? -1.3f : 1.3f, t.top - 0.45f, 0.0f};
            if (t.ground[side] >= water)
            {
                f.shade = dry;
                addTrestleBeam(batches, wood, f, a, b, post, post);
                continue;
            }
            // Standing in the water, the post is soaked: dark and green with
            // algae under the surface and a little above it where the waves
            // wash over it, drying in a band above that and dry further up.
            const float wet = water + 0.5f + 0.4f * roll(34 + side);
            const float damp = wet + 0.45f + 0.25f * roll(36 + side);
            // In bands up the post, all of them the post itself (nothing
            // stands proud of it): soaked under the water, a dark line of
            // scum and weed at the surface, soaked again up to where the
            // waves reach, a thin pale tide mark there, then drying in three
            // steps and dry.
            struct Band
            {
                float top, shade, r, g, b;
            };
            const float dryHeight = damp - wet;
            const Band bands[] = {
                {water - 0.05f, 0.45f, 0.84f, 0.98f, 0.82f},
                {water + 0.1f, 0.33f, 0.8f, 1.0f, 0.7f},
                {wet - 0.03f, 0.45f, 0.84f, 0.98f, 0.82f},
                {wet + 0.03f, 0.8f, 1.0f, 0.97f, 0.88f},
                {wet + 0.03f + dryHeight / 3.0f, 0.6f, 0.9f, 0.99f, 0.87f},
                {wet + 0.03f + dryHeight * 2.0f / 3.0f, 0.76f, 0.95f, 0.99f, 0.92f},
                {damp, 0.92f, 0.98f, 1.0f, 0.97f},
            };
            float from = footY[side];
            for (const Band &band : bands)
            {
                if (band.top <= from + 1e-3f)
                    continue;
                const float lo[3] = {postX(side, from), from, 0.0f};
                const float hi[3] = {postX(side, band.top), band.top, 0.0f};
                f.shade = dry * band.shade;
                f.tint[0] = band.r;
                f.tint[1] = band.g;
                f.tint[2] = band.b;
                addTrestleBeam(batches, wood, f, lo, hi, post, post);
                from = band.top;
            }
            f.shade = dry;
            f.tint[0] = f.tint[1] = f.tint[2] = 1.0f;
            const float d[3] = {postX(side, from), from, 0.0f};
            addTrestleBeam(batches, wood, f, d, b, post, post);
        }
        // lanterns here and there: never on two trestles running, on one
        // side or now and then both, where a post is tall enough for one to
        // hang clear of the ground
        bool lit[2] = {false, false};
        if (!litBefore && roll(40) < 0.38f)
        {
            const int side = roll(41) < 0.5f ? 0 : 1;
            lit[side] = true;
            if (roll(42) < 0.15f)
                lit[1 - side] = true;
        }
        for (int side = 0; side < 2; ++side)
            lit[side] = lit[side] && t.post[side] && t.top - baseY[side] > 4.0f;
        litBefore = lit[0] || lit[1];
        // the cap under the sleepers, reaching out over a lantern
        f.shade = 0.88f + 0.1f * roll(43);
        {
            const float lo[3] = {lit[0] ? -2.65f : -1.9f, t.top - 0.5f, -0.32f};
            const float hi[3] = {lit[1] ? 2.65f : 1.9f, t.top, 0.32f};
            addTrestleBox(batches, planks, f, lo, hi, 0.0f);
        }
        f.shade = 0.95f;
        for (int side = 0; side < 2; ++side)
            if (lit[side])
                addTrestleLantern(batches, f, side == 0 ? -2.3f : 2.3f, t.top - 0.5f, 0.0f,
                                  0.3f + 0.9f * roll(44 + side));
        // cross beams down to above the higher ground or the water, and
        // between each two a single brace, turning each way in turn (a
        // zigzag up the trestle)
        f.shade = 0.86f + 0.1f * roll(50);
        std::vector<float> levels = {t.top - 0.5f};
        if (both)
        {
            const float low = fmaxf(baseY[0], baseY[1]) + 1.0f;
            const float step = 5.0f + 1.5f * roll(51);
            for (float h = t.top - 0.5f - step; h > low; h -= step)
                levels.push_back(h);
            for (size_t i = 1; i < levels.size(); ++i)
            {
                const float h = levels[i];
                const float lo[3] = {postX(0, h) - post, h - 0.4f, -0.2f};
                const float hi[3] = {postX(1, h) + post, h, 0.2f};
                addTrestleBox(batches, planks, f, lo, hi, 0.0f);
                if (snowy)
                {
                    const float snowLo[3] = {lo[0], h, -0.22f};
                    const float snowHi[3] = {hi[0], h + 0.08f, 0.22f};
                    addTrestleBox(batches, T::SNOW, f, snowLo, snowHi, 0.0f);
                }
            }
            // ... and the last one from the lowest beam (or the cap, on a
            // low trestle) down to just above the ground or the water, so no
            // trestle stands on two bare legs
            const float bottom = low - 0.4f;
            for (size_t i = 0; i < levels.size(); ++i)
            {
                const float up = levels[i] - 0.2f;
                const float down = i + 1 < levels.size() ? levels[i + 1] - 0.2f : bottom;
                if (up - down < 1.8f)
                    continue;
                // in the posts' own plane and narrower than them, its ends
                // inside them, so it does not stand out from the posts' faces
                // seen from the side (in front of them it stuck out)
                const int k = static_cast<int>(i % 2);
                const float a[3] = {postX(k, up), up, 0.0f};
                const float b[3] = {postX(1 - k, down), down, 0.0f};
                addTrestleBeam(batches, planks, f, a, b, 0.6f * post, 0.07f);
            }
        }
        // knee braces from the post up to the cap where there is only one
        if (!both)
            for (int side = 0; side < 2; ++side)
            {
                if (!t.post[side] || t.top - baseY[side] < 3.0f)
                    continue;
                const float in = side == 0 ? 1.0f : -1.0f;
                const float h = t.top - 0.5f;
                const float a[3] = {postX(side, h - 1.8f), h - 1.8f, 0.0f};
                const float b[3] = {postX(side, h) + in * 1.2f, h - 0.1f, 0.0f};
                addTrestleBeam(batches, planks, f, a, b, 0.55f * post, 0.07f);
            }
        // Vines, here and there, where the jungle and the swamp grow over
        // everything: hanging from the cap and the cross beams, mostly short,
        // now and then climbing a post from the ground or the water. None
        // reaches the ground, the water or a stretch of track below.
        // (only there: elsewhere each land dresses its trestles its own way,
        // below)
        const int leafTexture = trestleLeavesIn(biome);
        if (overgrown && roll(60) < 0.6f)
        {
            const int strands =
                overgrown ? 1 + static_cast<int>(roll(61) * 3.0f) : 1 + (roll(61) < 0.3f ? 1 : 0);
            for (int k = 0; k < strands; ++k)
            {
                // between the posts (they lean out below, so it stays clear of
                // them), on the face of the beams away from the braces, so a
                // strand swaying in the wind goes through neither
                const float x = -0.7f + 1.4f * roll(62 + k);
                const size_t at =
                    static_cast<size_t>(roll(72 + k) * static_cast<float>(levels.size()));
                // (no std::min here: <windows.h> makes min a macro in this file)
                const float level = levels[at < levels.size() ? at : levels.size() - 1];
                const float y = level - 0.45f;
                const float z = -0.5f;
                // the leaves grown over the beam that the strands hang from
                // (only where the jungle and the swamp grow over everything:
                // elsewhere a clump on a bare beam looked stuck on)
                if (overgrown && roll(242 + k) < 0.85f)
                {
                    const float hx = 0.5f + 0.7f * roll(252 + k);
                    // (over the face the strands hang from)
                    const float lo[3] = {x - hx, level - 0.62f - 0.2f * roll(262 + k), -0.66f};
                    const float hi[3] = {x + hx, fminf(level + 0.16f, t.top - 0.36f), 0.3f};
                    addLeafClump(batches, leafTexture, f, lo, hi, hash(px * 3 + k, pz, 790u));
                }
                // a bunch: one, two or three strands side by side, each its
                // own length and width
                const int bunch = roll(92 + k) < 0.2f ? 2 : 1;
                for (int b = 0; b < bunch; ++b)
                {
                    const float r = roll(102 + 10 * k + b);
                    const float drop = overgrown ? 0.6f + 3.4f * r * r : 0.5f + 2.0f * r * r;
                    const float width = 0.3f + 0.5f * roll(152 + 10 * k + b);
                    const float sx =
                        fmaxf(-0.85f, fminf(0.85f, x + (static_cast<float>(b) -
                                                        0.5f * static_cast<float>(bunch - 1)) *
                                                           (0.25f + 0.25f * roll(202 + k))));
                    const float most = vineRoom(f, sx, z, y);
                    if (most >= 0.4f)
                        addTrestleVines(batches, f, sx, y, z, fminf(drop, most), width,
                                        hash(px * 13 + k, pz * 7 + b, 780u));
                }
            }
            // climbing the posts from the ground or the water, part of the
            // way up or nearly to the cap; in the jungle up both
            for (int side = 0; side < 2; ++side)
            {
                if (!t.post[side] || roll(232 + side) >= (overgrown ? 0.3f : 0.08f))
                    continue;
                const float y0 = baseY[side];
                const float tall = t.top - 0.6f - y0;
                const float y1 = y0 + tall * (0.2f + 0.35f * roll(234 + side));
                if (y1 > y0 + 0.5f)
                {
                    addTrestleClimber(batches, f, postX(side, y0), y0, postX(side, y1), y1, post,
                                      post * (1.0f + 0.5f * roll(238 + side)),
                                      hash(px, pz, 760u + static_cast<unsigned int>(side)));
                }
            }
        }
        // Elsewhere each land dresses its trestles its own way: grass and
        // flowers round the posts' feet in the meadows, ferns on the crags,
        // dead bushes in the badlands and by the volcano, icicles under the
        // cap and the beams in the snow. Out at sea the posts are soaked
        // (above).
        f.shade = 0.92f;
        if (biome == ALPINE)
        {
            // icicles: a thin piece of ice and a thinner one under it
            for (size_t i = 0; i < levels.size(); ++i)
            {
                // the cap's underside is at levels[0], a beam's 0.4 under its top
                const float under = levels[i] - (i == 0 ? 0.0f : 0.4f);
                const float span =
                    i == 0 ? 1.7f : fabsf(postX(1, levels[i]) - postX(0, levels[i])) * 0.5f - post;
                const int count = 2 + static_cast<int>(roll(310 + static_cast<int>(i)) * 4.0f);
                for (int k = 0; k < count; ++k)
                {
                    const int c = 320 + 10 * static_cast<int>(i) + k;
                    const float x = (roll(c) * 2.0f - 1.0f) * span;
                    const float z = (roll(c + 400) - 0.5f) * 0.3f;
                    const float len = 0.25f + 0.6f * roll(c + 800) * roll(c + 800);
                    const float lo1[3] = {x - 0.07f, under - len * 0.55f, z - 0.07f};
                    const float hi1[3] = {x + 0.07f, under, z + 0.07f};
                    addTrestleBox(batches, T::ICE, f, lo1, hi1, 0.0f);
                    const float lo2[3] = {x - 0.035f, under - len, z - 0.035f};
                    const float hi2[3] = {x + 0.035f, under - len * 0.55f, z + 0.035f};
                    addTrestleBox(batches, T::ICE, f, lo2, hi2, 0.0f);
                }
            }
        }
        else if (biome == MEADOW || biome == CRAG || biome == MESA || biome == VOLCANO)
        {
            const float chance = biome == MEADOW ? 0.75f : (biome == CRAG ? 0.6f : 0.5f);
            for (int side = 0; side < 2; ++side)
            {
                if (!t.post[side] || t.ground[side] < water + 0.2f || roll(340 + side) >= chance)
                    continue;
                const float ground = t.ground[side];
                const float foot = postX(side, ground);
                const int count = biome == MEADOW ? 2 + static_cast<int>(roll(342 + side) * 3.0f)
                                                  : 1 + (roll(342 + side) < 0.4f ? 1 : 0);
                for (int k = 0; k < count; ++k)
                {
                    const int c = 350 + 20 * side + k;
                    float dx = (roll(c) - 0.5f) * 1.6f, dz = (roll(c + 5) - 0.5f) * 1.6f;
                    // not inside the post
                    if (fabsf(dx) < post + 0.15f && fabsf(dz) < post + 0.15f)
                        dx = (dx < 0.0f ? -1.0f : 1.0f) * (post + 0.15f + 0.2f * roll(c + 9));
                    int texture = T::TALL_GRASS;
                    float size = 1.2f + 0.6f * roll(c + 11);
                    if (biome == MEADOW)
                    {
                        const float pick = roll(c + 13);
                        if (pick > 0.5f)
                        {
                            static const int kFlowers[5] = {T::POPPY, T::DANDELION, T::CORNFLOWER,
                                                            T::OXEYE, T::ALLIUM};
                            texture = kFlowers[static_cast<int>(roll(c + 15) * 4.99f)];
                            size = 1.0f + 0.4f * roll(c + 11);
                        }
                    }
                    else if (biome == CRAG)
                        texture = T::FERN;
                    else
                    {
                        texture = T::DEAD_BUSH;
                        size = 1.0f + 0.5f * roll(c + 11);
                    }
                    addTrestlePlant(batches, texture, f, foot + dx, ground, dz, size,
                                    3.1416f * roll(c + 17));
                }
            }
        }
    }

    // Where the track is up in the air, vines hanging from the ends of the
    // sleepers here and there, long and short, more where more grows round
    // about; none reaching the ground, the water or the track below. (Leaves
    // grown over the sleepers' ends stood out beside the track.)
    const size_t points = m_supportPath.size() / 3;
    for (size_t n = 0; n + 1 < points; n += 2)
    {
        const float x = m_supportPath[3 * n], y = m_supportPath[3 * n + 1],
                    z = m_supportPath[3 * n + 2];
        const int px = static_cast<int>(floorf(x * 2.0f)), pz = static_cast<int>(floorf(z * 2.0f));
        auto roll = [&](int k) {
            return static_cast<float>(hash(px, pz, 900u + static_cast<unsigned int>(k)) % 1000u) /
                   1000.0f;
        };
        const int bx = blockOf(x), bz = blockOf(z);
        const int biome =
            Grid::insideColumn(bx, bz) && onIsland(bx, bz) ? g.biome[Grid::column(bx, bz)] : -1;
        const bool overgrown = biome == MANGROVE || biome == BAMBOO_JUNGLE;
        if (!overgrown || roll(0) >= 0.12f) // only where the jungle and the swamp grow
            continue;
        const float dx = m_supportPath[3 * (n + 1)] - x, dz = m_supportPath[3 * (n + 1) + 2] - z;
        const float along = sqrtf(dx * dx + dz * dz);
        if (along < 1e-4f)
            continue;
        // a frame with x along the track and z across it, so the strands
        // hang off one end of a sleeper
        const float c = dz / along, s = dx / along; // across is (c, -s) in the world
        const TrestleFrame f = {x, z, s, -c, 0.95f};
        const float out = roll(2) < 0.5f ? 1.0f : -1.0f;
        const float under = y - 0.18f; // the sleepers' underside
        const int strands = 1;
        for (int k = 0; k < strands; ++k)
        {
            const float r = roll(10 + k);
            const float drop = overgrown ? 0.5f + 3.0f * r * r : 0.5f + 1.8f * r * r;
            const float sx = (roll(20 + k) - 0.5f) * 0.8f;
            const float across = out * (1.8f + 0.1f * roll(30 + k));
            const float most = vineRoom(f, sx, across, under);
            if (most >= 0.4f)
                addTrestleVines(batches, f, sx, under, across, fminf(drop, most),
                                0.3f + 0.5f * roll(40 + k), hash(px + k, pz, 910u));
        }
    }
    m_supports->arena.release();
    m_supports->mesh = m_supports->arena.compile(batches, m_textures);
    m_supports->built = true;
}

void LandWorld::drawSupports(const std::vector<float> &path, bool shadowPass)
{
    if (!m_scenery || !m_scenery->built || m_textures.empty())
        return;
    if (!m_supports)
        m_supports = std::make_unique<Supports>();
    if (path != m_supportPath)
    {
        planSupports(path);
        m_supports->built = false;
    }
    if (!m_supports->built)
        buildSupportMesh();

    // drawn alike into the shadow maps: the vines' cut-out needs the texture
    // there too
    (void)shadowPass;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    // the vines are cut out: by the fixed pipeline here, by the shader itself
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program == 0)
    {
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
    }
    m_supports->arena.draw({&m_supports->mesh});
    setGlow(0.0f);
    glPopAttrib();
}
