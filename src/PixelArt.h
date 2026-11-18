/************************************************************************
     File:        PixelArt.h

     Comment:     Helpers for the square block textures that are drawn in
                  code (blocks at 32 x 32, the sea floor's plants at 16 x 16): a
                  repeatable per-texel hash and an uploader that keeps the
                  pixels crisp up close and mip-maps them far away.

*************************************************************************/
#pragma once

#include <vector>

namespace PixelArt
{

const int kSize = 16;     // the sea floor's textures
const int kFineSize = 32; // the island's textures

// RGBA, size x size (a power of two), row 0 at the top of the block.
typedef std::vector<unsigned char> Image;

// Small integer hash so every texel gets a repeatable random value.
unsigned int hash(int x, int y, unsigned int seed);

// A fully transparent image.
Image blank(int size = kSize);

// The width (and height) of an image.
int sizeOf(const Image &pixels);

// Writes one texel; the colour is clamped to 0..255. Outside the image it
// does nothing.
void set(Image &pixels, int x, int y, int r, int g, int b, int a = 255);

// Uploads with box-filtered mip levels: nearest when close so the pixels stay
// crisp, mip-mapped far away so the blocks do not shimmer. repeatT: tile
// vertically as well (side textures clamp so their top row stays on top).
unsigned int upload(Image pixels, bool repeatT);

// The same into a texture that already has a name, replacing what it held
// (display lists that bind the name then draw the new picture). The new
// picture may be a different size.
void uploadInto(unsigned int texture, Image pixels, bool repeatT);

} // namespace PixelArt
