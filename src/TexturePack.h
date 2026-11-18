/************************************************************************
     File:        TexturePack.h

     Comment:     Texture packs the user downloads or makes, swapped in for
                  the island's built-in textures (LandTextures) while the
                  program runs. A pack is a folder or a .zip archive. Two
                  layouts are read:
                  * this program's own: textures/<name>.png, one for each
                    of the island's textures, named as in nativeName();
                  * the resource pack layout block games use:
                    assets/minecraft/textures/block/<block>.png (or
                    textures/blocks/ in older packs); the files are matched
                    to the island's textures by block name (see the table
                    in TexturePack.cpp). Grey grass and leaf pictures are
                    tinted green as those games do.
                  A pack may sit one folder down in its archive. Pictures
                  are cut to their first frame (animated strips), made
                  square at a power of two up to 256, and solid blocks are
                  filled in where the picture is see-through. A texture the
                  pack lacks, or one this program draws differently (the
                  water, the bamboo stalks), keeps its built-in picture.
                  No pack's files are part of this program: packs are read
                  from where the user puts them (searchFolders()).

*************************************************************************/
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PixelArt.h"
#include "ZipArchive.h"

namespace TexturePack
{

// A pack found on disk: its name (the folder's or archive's) and where it
// is, as UTF-8.
struct Found
{
    std::string name;
    std::string path;
};

// Where packs are looked for: texturepacks/ beside the program (the build
// and install make it), and texturepacks/ and data/texturepacks/ in the
// working folder, which main() makes the program's own; a folder reached
// two ways is listed once.
std::vector<std::string> searchFolders();

// The packs in those folders: every .zip and every folder, sorted by name.
std::vector<Found> findAll();
std::vector<Found> findIn(const std::string &folder);

// The island texture's file name in this program's own layout
// ("grass_top" for LandTextures::GRASS_TOP).
const char *nativeName(int texture);

class Pack
{
  public:
    // Opens a pack: a folder, a .zip archive, or a pack.mcmeta file inside
    // a folder. False if it holds no texture this program can use.
    bool open(const std::string &path);
    const std::string &name() const
    {
        return m_name;
    }
    // How many of the island's textures the pack replaces.
    int coverage() const
    {
        return m_coverage;
    }
    // The pack's picture for one of the island's textures, ready to
    // upload; false when the built-in one is to be used.
    bool landTexture(int texture, PixelArt::Image &out) const;

    // Does the pack have a picture for this texture (without reading it)?
    bool provides(int texture) const;

  private:
    bool readFile(const std::string &relative, ZipArchive::Bytes &out) const;
    bool hasFile(const std::string &relative) const;
    bool picture(const std::string &relative, PixelArt::Image &out) const;

    std::string m_name;
    std::string m_folder; // a folder pack's root, or empty
    std::unique_ptr<ZipArchive::Archive> m_zip;
    std::string m_prefix; // the root inside the archive, ending in '/'
    int m_coverage = 0;
};

// The steps a pack's pictures go through, here for the tests.
// Decodes a PNG file to RGBA rows (top row first); false if it is not one.
bool decodePng(const ZipArchive::Bytes &file, int &width, int &height,
               std::vector<unsigned char> &rgba);
// The first frame (the top square of a strip), made square at a power of
// two from 1 to 256, nearest texel.
PixelArt::Image firstFrame(const std::vector<unsigned char> &rgba, int width, int height);
// Is it grey (a picture the game tints)?
bool isGrey(const PixelArt::Image &image);
// Multiplies the colour by r, g, b (0..255).
void tint(PixelArt::Image &image, int r, int g, int b);
// Draws `over` on top of `image` by its alpha (same size, or scaled).
void overlay(PixelArt::Image &image, const PixelArt::Image &over);
// Fills the see-through texels with the picture's average colour, darker
// by `shade`, so a solid block has no holes.
void fillHoles(PixelArt::Image &image, float shade);

} // namespace TexturePack
