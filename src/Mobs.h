/************************************************************************
     File:        Mobs.h

     Comment:     The island's grazing animals, a sheep, a pig and a cow,
                  built as a block game builds them: boxes measured in the
                  game's pixels (16 to a block), each box's faces cut from
                  one picture laid out the game's way (a box w wide, h tall
                  and d deep at (u, v) has its top and bottom in the row
                  from v, d tall, and its four sides in the row under it,
                  h tall). So a texture pack's entity pictures fit them as
                  they are (TexturePack::Pack::entityPicture()); without a
                  pack the built-in pictures here, in the same layout, are
                  used.

*************************************************************************/
#pragma once

#include <vector>

namespace Mobs
{

enum Kind
{
    SHEEP,
    PIG,
    COW,
    KIND_COUNT
};

// The pictures the animals are drawn with.
enum Skin
{
    SHEEP_SKIN, // its face, legs and the body under the wool
    SHEEP_WOOL, // the wool over them, see-through where there is none
    PIG_SKIN,
    COW_SKIN,
    SKIN_COUNT
};

// A picture as it comes (64 x 32 in the game's own size, or a multiple;
// some packs make it 64 x 64 with the same layout in its upper half), RGBA,
// row 0 at the top.
struct Picture
{
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
};

// The built-in picture of a skin, 64 x 32.
Picture builtin(int skin);

// The pack files a skin is read from, under the pack's
// assets/minecraft/textures/entity/, best first, '|' between them: the
// newer games' names before the older ones'.
const char *packNames(int skin);

// Can a picture of this size be laid out as the skins are (at least 64
// wide, a multiple of 64, and half as tall or as tall)?
bool fits(int width, int height);

// One box of a model: where it is, in the game's pixels with y up from
// the ground and the animal facing +z, and where its faces are in the
// picture. A lying box is the body of a four-legged animal: modelled
// standing up (h tall) and tipped forward, so in the picture its belly
// and back are the front and back faces, h long.
struct Box
{
    float x0, y0, z0, x1, y1, z1;
    int u, v, w, h, d;
    bool lying;
    int skin;
    int leg; // 0 for none, else which leg (1-4) it is or is wool round
};

// The boxes of an animal (with its wool, for the sheep).
std::vector<Box> model(int kind);

// The skins as uploaded: a texture name for each, and how tall its picture
// is in the game's pixels (32, or 64 for a picture laid out 64 x 64).
struct Skins
{
    unsigned int texture[SKIN_COUNT] = {};
    int rows[SKIN_COUNT] = {};
};

// Uploads a picture (nearest texel, no mip levels) as a skin, into the
// texture name it has or a new one.
void upload(Skins &skins, int skin, const Picture &picture);

// Draws an animal standing on y = 0 facing +z, one unit to the game's
// pixel; stride (-1 to 1) swings the legs. wool: the colour the sheep's
// wool is dyed (1, 1, 1 for white).
void draw(int kind, float stride, const Skins &skins, const float wool[3]);

} // namespace Mobs
