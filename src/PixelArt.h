/************************************************************************
     File:        PixelArt.h

     Comment:     Helpers for the 16 x 16 block textures that are drawn in
                  code (LandWorld, OceanWorld): a repeatable per-texel
                  hash and an uploader that keeps the pixels crisp up close
                  and mip-maps them far away.

*************************************************************************/
#pragma once

#include <vector>

namespace PixelArt
{

const int kSize = 16;

// RGBA, kSize x kSize, row 0 at the top of the block.
typedef std::vector<unsigned char> Image;

// Small integer hash so every texel gets a repeatable random value.
unsigned int hash(int x, int y, unsigned int seed);

// A fully transparent image.
Image blank();

// Writes one texel; the colour is clamped to 0..255.
void set(Image &pixels, int x, int y, int r, int g, int b, int a = 255);

// Uploads with box-filtered mip levels: nearest when close so the pixels stay
// crisp, mip-mapped far away so the blocks do not shimmer. repeatT: tile
// vertically as well (side textures clamp so their top row stays on top).
unsigned int upload(Image pixels, bool repeatT);

} // namespace PixelArt
