/************************************************************************
     File:        LandTextures.h

     Comment:     The 16 x 16 pixel textures of the island's blocks and
                  plants (LandWorld), drawn in code like the others, so
                  there are no image files to load.

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
    TEXTURE_COUNT
};

// The pixels of one texture.
PixelArt::Image make(int texture);

// Does this texture tile vertically as well?
bool repeatsVertically(int texture);

} // namespace LandTextures
