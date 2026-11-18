/************************************************************************
     File:        LandTextures.h

     Comment:     The 32 x 32 pixel textures of the island's blocks and
                  plants (LandWorld), drawn in code like the others, so
                  there are no image files to load. A texture pack the
                  user picks can stand in for any of them (TexturePack).

*************************************************************************/
#pragma once

#include "PixelArt.h"

namespace LandTextures
{

enum Texture
{
    GRASS_TOP = 0,
    GRASS_SIDE,
    DIRT,
    STONE,
    COBBLE,
    MOSSY,
    SAND,
    GRAVEL,
    SNOW,
    SNOW_SIDE, // dirt with a snow cap
    BASALT,
    MAGMA,
    LAVA,
    CLAY_RED, // the bands of the badlands
    CLAY_ORANGE,
    CLAY_YELLOW,
    CLAY_WHITE,
    CLAY_BROWN,
    CLAY_GREY,
    RED_SAND,
    OAK_SIDE,
    OAK_TOP,
    BIRCH_SIDE,
    BIRCH_TOP,
    SPRUCE_SIDE,
    SPRUCE_TOP,
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
    CACTUS_SIDE,
    CACTUS_TOP,
    MUSHROOM_CAP,
    MUSHROOM_STEM,
    HAY_SIDE,
    HAY_TOP,
    FARMLAND,
    WATER,     // still and see-through
    SEA_WATER, // the same where it runs into the sea, in the sea's colours
    // cut-out plants
    TALL_GRASS,
    FERN,
    POPPY,
    DANDELION,
    CORNFLOWER,
    TULIP,
    DEAD_BUSH,
    WHEAT,
    PETALS, // fallen cherry blossom on the ground
    OXEYE,  // white daisies
    ALLIUM, // round purple heads
    TULIP_ORANGE,
    BAMBOO,     // a stalk, the whole height of the cell: they stack
    BAMBOO_TOP, // the leafy top of a stalk
    VINES,      // hanging from the mangroves' crowns
    // more blocks (after the plants, which keep their order)
    ICE,          // packed ice of the ice spikes
    PUMPKIN_FACE, // a carved pumpkin lantern's face, lit
    PUMPKIN_TOP,
    MUD, // the mangrove swamp's ground
    MANGROVE_SIDE,
    MANGROVE_TOP,
    LEAVES_MANGROVE,
    LEAVES_BAMBOO, // the pale canopy over a bamboo thicket
    PATH_TOP,      // a trodden dirt path, as in a village
    PATH_SIDE,
    SPRUCE_PLANKS, // dark planks: the taiga cabins' roofs
    LANTERN_SMALL, // a lantern as a block game draws it: the parts laid out as
                   // its lantern picture lays them out (see addLantern())
    CHAIN,         // what a hanging lantern hangs from
    TEXTURE_COUNT
};

// The pixels of one texture.
PixelArt::Image make(int texture);

// Does this texture tile vertically as well?
bool repeatsVertically(int texture);

// The same style for the sea floor's blocks (OceanWorld), in its colours.
PixelArt::Image sand(int r, int g, int b, unsigned int seed);
PixelArt::Image stone(int r, int g, int b, unsigned int seed);
PixelArt::Image planks(int r, int g, int b, bool nails);
PixelArt::Image bark(int r, int g, int b, unsigned int seed);
PixelArt::Image crystal();
PixelArt::Image lantern();
PixelArt::Image stoneBricks(int r, int g, int b, bool algae, unsigned int seed);
PixelArt::Image coral(int r, int g, int b, unsigned int seed);
PixelArt::Image gold();
PixelArt::Image lapis();
PixelArt::Image marble();
PixelArt::Image pillar();

} // namespace LandTextures
