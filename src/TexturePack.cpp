/************************************************************************
     File:        TexturePack.cpp

     Comment:     See TexturePack.h.

*************************************************************************/

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // keep std::min / std::max usable
#endif
#include <windows.h>
#endif

#include <stb_image.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "LandTextures.h"
#include "TexturePack.h"

namespace fs = std::filesystem;

namespace TexturePack
{

namespace
{

// How a block game colours a grey picture: grass, the common foliage and
// the trees with their own leaf colour.
enum Tint
{
    NONE = 0,
    GRASS,
    FOLIAGE,
    BIRCH,
    SPRUCE,
    MANGROVE,
    // for a block that stands in for another, named after "@" in the table:
    // the picture is made grey first, so it takes the colour whatever it was
    AUTUMN,
    CHERRY,
    LIME,
    WATER,
    SEA,
    LILY // a block game's lily pads are this green
};

void tintColour(int which, int &r, int &g, int &b)
{
    static const int colours[12][3] = {{255, 255, 255}, {145, 189, 89},  {119, 171, 47},
                                       {128, 167, 85},  {97, 153, 97},   {146, 198, 72},
                                       {232, 132, 44},  {246, 172, 208}, {150, 204, 84},
                                       {63, 118, 228},  {52, 140, 190},  {32, 128, 48}};
    r = colours[which][0];
    g = colours[which][1];
    b = colours[which][2];
}

// One of the island's textures: its name in this program's own layout, and
// the block pictures of a resource pack that stand for it, best first,
// '|' between them (newer names, then the older packs' names, then blocks
// that can stand in, "@" and the colour they are given), so that with a
// full pack every block on the island is one of the pack's. Empty: no
// block picture fits, the built-in one stays unless a pack of this
// program's own layout has it. The bamboo is put together from the
// pack's bamboo pictures (bambooPicture()).
struct Source
{
    int texture;
    const char *native;
    const char *blocks;
    int tint;
};

using T = LandTextures::Texture;

const Source kSources[] = {
    {T::GRASS_TOP, "grass_top", "grass_block_top|grass_top", GRASS},
    {T::GRASS_SIDE, "grass_side", "grass_block_side|grass_side", NONE},
    {T::DIRT, "dirt", "dirt", NONE},
    {T::STONE, "stone", "stone", NONE},
    {T::COBBLE, "cobble", "cobblestone", NONE},
    {T::MOSSY, "mossy", "mossy_cobblestone|cobblestone_mossy", NONE},
    {T::SAND, "sand", "sand", NONE},
    {T::GRAVEL, "gravel", "gravel", NONE},
    {T::SNOW, "snow", "snow", NONE},
    {T::SNOW_SIDE, "snow_side", "grass_block_snow|grass_side_snowed", NONE},
    {T::BASALT, "basalt", "basalt_side|blackstone|andesite|stone_andesite", NONE},
    {T::MAGMA, "magma", "magma|netherrack", NONE},
    {T::LAVA, "lava", "lava_still", NONE},
    {T::CLAY_RED, "clay_red", "red_terracotta|hardened_clay_stained_red", NONE},
    {T::CLAY_ORANGE, "clay_orange", "orange_terracotta|hardened_clay_stained_orange", NONE},
    {T::CLAY_YELLOW, "clay_yellow", "yellow_terracotta|hardened_clay_stained_yellow", NONE},
    {T::CLAY_WHITE, "clay_white", "white_terracotta|hardened_clay_stained_white", NONE},
    {T::CLAY_BROWN, "clay_brown", "brown_terracotta|hardened_clay_stained_brown", NONE},
    {T::CLAY_GREY, "clay_grey", "light_gray_terracotta|hardened_clay_stained_silver", NONE},
    {T::RED_SAND, "red_sand", "red_sand", NONE},
    {T::OAK_SIDE, "oak_side", "oak_log|log_oak", NONE},
    {T::OAK_TOP, "oak_top", "oak_log_top|log_oak_top", NONE},
    {T::BIRCH_SIDE, "birch_side", "birch_log|log_birch", NONE},
    {T::BIRCH_TOP, "birch_top", "birch_log_top|log_birch_top", NONE},
    {T::SPRUCE_SIDE, "spruce_side", "spruce_log|log_spruce", NONE},
    {T::SPRUCE_TOP, "spruce_top", "spruce_log_top|log_spruce_top", NONE},
    {T::LEAVES_OAK, "leaves_oak", "oak_leaves|leaves_oak", FOLIAGE},
    {T::LEAVES_BIRCH, "leaves_birch", "birch_leaves|leaves_birch", BIRCH},
    {T::LEAVES_SPRUCE, "leaves_spruce", "spruce_leaves|leaves_spruce", SPRUCE},
    {T::LEAVES_CHERRY, "leaves_cherry", "cherry_leaves|oak_leaves@cherry|leaves_oak@cherry", NONE},
    {T::LEAVES_AUTUMN, "leaves_autumn", "oak_leaves@autumn|leaves_oak@autumn", NONE},
    {T::PLANKS, "planks", "oak_planks|planks_oak", NONE},
    // the roofs of the castle, towers and lighthouse: dark wood
    {T::ROOF, "roof", "dark_oak_planks|planks_big_oak|spruce_planks|planks_spruce", NONE},
    {T::WINDOW, "window", "glass", NONE},
    {T::DOOR, "door", "oak_door_bottom|door_wood_lower", NONE},
    // the full glowing block: only the mine's lamps set in the rock
    {T::CRYSTAL, "crystal", "sea_lantern|amethyst_block|glowstone", NONE},
    {T::ORE_COAL, "ore_coal", "coal_ore", NONE},
    {T::ORE_IRON, "ore_iron", "iron_ore", NONE},
    {T::ORE_GOLD, "ore_gold", "gold_ore", NONE},
    {T::ORE_DIAMOND, "ore_diamond", "diamond_ore", NONE},
    {T::CACTUS_SIDE, "cactus_side", "cactus_side", NONE},
    {T::CACTUS_TOP, "cactus_top", "cactus_top", NONE},
    {T::MUSHROOM_CAP, "mushroom_cap", "red_mushroom_block|mushroom_block_skin_red", NONE},
    {T::MUSHROOM_STEM, "mushroom_stem", "mushroom_stem|mushroom_block_skin_stem", NONE},
    {T::HAY_SIDE, "hay_side", "hay_block_side", NONE},
    {T::HAY_TOP, "hay_top", "hay_block_top", NONE},
    {T::FARMLAND, "farmland", "farmland|farmland_dry", NONE},
    // the pack's water, tinted, as see-through as the built-in water
    {T::WATER, "water", "water_still@water", NONE},
    {T::SEA_WATER, "sea_water", "water_still@sea", NONE},
    {T::TALL_GRASS, "tall_grass", "short_grass|grass|tallgrass", GRASS},
    {T::FERN, "fern", "fern", GRASS},
    {T::POPPY, "poppy", "poppy|flower_rose", NONE},
    {T::DANDELION, "dandelion", "dandelion|flower_dandelion", NONE},
    {T::CORNFLOWER, "cornflower", "cornflower|blue_orchid|flower_blue_orchid", NONE},
    {T::TULIP, "tulip", "red_tulip|flower_tulip_red", NONE},
    {T::DEAD_BUSH, "dead_bush", "dead_bush|deadbush", NONE},
    {T::WHEAT, "wheat", "wheat_stage7|wheat_stage_7", NONE},
    {T::PETALS, "petals", "pink_petals|pink_tulip|flower_tulip_pink", NONE},
    {T::OXEYE, "oxeye", "oxeye_daisy|flower_oxeye_daisy", NONE},
    {T::ALLIUM, "allium", "allium|flower_allium", NONE},
    {T::TULIP_ORANGE, "tulip_orange", "orange_tulip|flower_tulip_orange", NONE},
    {T::BAMBOO, "bamboo", "", NONE},
    {T::BAMBOO_TOP, "bamboo_top", "", NONE},
    {T::VINES, "vines", "vine", FOLIAGE},
    {T::ICE, "ice", "packed_ice|ice_packed|ice", NONE},
    {T::MUD, "mud", "mud|coarse_dirt|dirt", NONE},
    {T::MANGROVE_SIDE, "mangrove_side", "mangrove_log|jungle_log|log_jungle", NONE},
    {T::MANGROVE_TOP, "mangrove_top", "mangrove_log_top|jungle_log_top|log_jungle_top", NONE},
    {T::LEAVES_MANGROVE, "leaves_mangrove",
     "mangrove_leaves|jungle_leaves@mangrove|oak_leaves@mangrove|leaves_oak@mangrove", MANGROVE},
    {T::LEAVES_BAMBOO, "leaves_bamboo", "azalea_leaves|oak_leaves@lime|leaves_oak@lime", NONE},
    {T::PATH_TOP, "path_top", "dirt_path_top|grass_path_top|coarse_dirt", NONE},
    {T::PATH_SIDE, "path_side", "dirt_path_side|grass_path_side|dirt", NONE},
    {T::SPRUCE_PLANKS, "spruce_planks", "spruce_planks|planks_spruce", NONE},
    // the lanterns of the buildings: the picture as the game lays it out
    {T::LANTERN_SMALL, "lantern_small", "lantern", NONE},
    {T::CHAIN, "chain", "chain|iron_chain", NONE},
    {T::WOOL_WHITE, "wool_white", "white_wool|wool_colored_white", NONE},
    {T::WOOL_RED, "wool_red", "red_wool|wool_colored_red", NONE},
    {T::WOOL_BLUE, "wool_blue", "blue_wool|wool_colored_blue", NONE},
    {T::WOOL_YELLOW, "wool_yellow", "yellow_wool|wool_colored_yellow", NONE},
    {T::WOOL_ORANGE, "wool_orange", "orange_wool|wool_colored_orange", NONE},
    {T::WOOL_PURPLE, "wool_purple", "purple_wool|wool_colored_purple", NONE},
    {T::LILY_PAD, "lily_pad", "lily_pad|waterlily", LILY},
    {T::SUGAR_CANE, "sugar_cane", "sugar_cane|reeds", GRASS},
    {T::CLAY, "clay", "clay", NONE},
    {T::PODZOL_TOP, "podzol_top", "podzol_top|dirt_podzol_top", NONE},
    {T::PODZOL_SIDE, "podzol_side", "podzol_side|dirt_podzol_side", NONE},
    {T::COARSE_DIRT, "coarse_dirt", "coarse_dirt|dirt", NONE},
    {T::STONE_BRICKS, "stone_bricks", "stone_bricks|stonebrick", NONE},
    {T::MOSSY_BRICKS, "mossy_bricks", "mossy_stone_bricks|stonebrick_mossy|mossy_cobblestone",
     NONE},
    {T::DARK_OAK_SIDE, "dark_oak_side", "dark_oak_log|log_big_oak|spruce_log|log_spruce", NONE},
    {T::DARK_OAK_TOP, "dark_oak_top",
     "dark_oak_log_top|log_big_oak_top|spruce_log_top|log_spruce_top", NONE},
    {T::TRAPDOOR, "trapdoor", "spruce_trapdoor|dark_oak_trapdoor|trapdoor", NONE},
    {T::FLOWER_POT, "flower_pot", "flower_pot", NONE},
    {T::DOOR_TOP, "door_top", "oak_door_top|door_wood_upper", NONE},
};
static_assert(sizeof(kSources) / sizeof(kSources[0]) == LandTextures::TEXTURE_COUNT,
              "every island texture needs a row");

// A block picture's name and the colour it stands in with ("oak_leaves@autumn"),
// or NONE.
void candidate(const std::string &entry, std::string &name, int &colour)
{
    static const char *const names[] = {"",       "grass",    "foliage", "birch",
                                        "spruce", "mangrove", "autumn",  "cherry",
                                        "lime",   "water",    "sea",     "lily"};
    const size_t at = entry.find('@');
    name = entry.substr(0, at);
    colour = NONE;
    if (at != std::string::npos)
        for (int k = 1; k < 12; ++k)
            if (entry.compare(at + 1, std::string::npos, names[k]) == 0)
                colour = k;
}

// Greys a picture by its brightness, keeping its see-through parts.
void desaturate(PixelArt::Image &image)
{
    for (size_t i = 0; i + 3 < image.size(); i += 4)
    {
        const int grey = (image[i] * 77 + image[i + 1] * 150 + image[i + 2] * 29) / 256;
        image[i] = image[i + 1] = image[i + 2] = static_cast<unsigned char>(grey);
    }
}

const Source *sourceOf(int texture)
{
    for (const Source &s : kSources)
        if (s.texture == texture)
            return &s;
    return nullptr;
}

// Plants are crossed pictures that keep their see-through parts; the
// leaves are solid blocks, their holes filled darker.
bool isPlant(int texture)
{
    return (texture >= T::TALL_GRASS && texture <= T::VINES) || texture == T::LANTERN_SMALL ||
           texture == T::CHAIN || texture == T::LILY_PAD || texture == T::SUGAR_CANE ||
           texture == T::TRAPDOOR || texture == T::FLOWER_POT || texture == T::DOOR ||
           texture == T::DOOR_TOP;
}

bool isLeaves(int texture)
{
    return (texture >= T::LEAVES_OAK && texture <= T::LEAVES_AUTUMN) ||
           texture == T::LEAVES_MANGROVE || texture == T::LEAVES_BAMBOO;
}

std::vector<std::string> split(const char *names)
{
    std::vector<std::string> out;
    std::string current;
    for (const char *c = names; *c != '\0'; ++c)
        if (*c == '|')
        {
            out.push_back(current);
            current.clear();
        }
        else
            current += *c;
    if (!current.empty())
        out.push_back(current);
    return out;
}

std::string lower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

fs::path pathOf(const std::string &utf8)
{
    return fs::u8path(utf8);
}

std::string utf8Of(const fs::path &p)
{
    return p.u8string();
}

bool readWhole(const fs::path &p, ZipArchive::Bytes &out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// The files that mark a pack's root, one of which a pack must have there.
const char *const kRootMarks[] = {"pack.mcmeta", "assets/minecraft/textures/", "textures/"};

// Where a resource pack keeps its block pictures: newer packs, older ones.
const char *const kBlockFolders[] = {"assets/minecraft/textures/block/",
                                     "assets/minecraft/textures/blocks/"};

int sizeOfSide(const PixelArt::Image &image)
{
    return PixelArt::sizeOf(image);
}

} // namespace

std::vector<std::string> searchFolders()
{
    std::vector<std::string> folders;
    std::error_code error;
#ifdef _WIN32
    wchar_t module[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        folders.push_back(utf8Of(fs::path(module).parent_path() / "texturepacks"));
#else
    const fs::path self = fs::read_symlink("/proc/self/exe", error);
    if (!error)
        folders.push_back(utf8Of(self.parent_path() / "texturepacks"));
#endif
    const fs::path here = fs::current_path(error);
    if (!error)
    {
        folders.push_back(utf8Of(here / "texturepacks"));
        folders.push_back(utf8Of(here / "data" / "texturepacks"));
    }
    // the same folder reached two ways is searched once
    std::vector<std::string> unique;
    for (const std::string &f : folders)
    {
        bool seen = false;
        for (const std::string &u : unique)
            seen = seen || fs::equivalent(pathOf(f), pathOf(u), error) || f == u;
        if (!seen)
            unique.push_back(f);
    }
    return unique;
}

std::vector<Found> findIn(const std::string &folder)
{
    std::vector<Found> packs;
    std::error_code error;
    for (fs::directory_iterator it(pathOf(folder), error), end; !error && it != end;
         it.increment(error))
    {
        const fs::path p = it->path();
        const bool zip = lower(utf8Of(p.extension())) == ".zip";
        if (!zip && !it->is_directory(error))
            continue;
        packs.push_back(Found{utf8Of(zip ? p.stem() : p.filename()), utf8Of(p)});
    }
    std::sort(packs.begin(), packs.end(),
              [](const Found &a, const Found &b) { return lower(a.name) < lower(b.name); });
    return packs;
}

std::vector<Found> findAll()
{
    std::vector<Found> all;
    for (const std::string &folder : searchFolders())
    {
        const std::vector<Found> here = findIn(folder);
        all.insert(all.end(), here.begin(), here.end());
    }
    return all;
}

const char *nativeName(int texture)
{
    const Source *s = sourceOf(texture);
    return s ? s->native : "";
}

bool Pack::open(const std::string &path)
{
    m_folder.clear();
    m_zip.reset();
    m_prefix.clear();
    m_coverage = 0;
    std::error_code error;
    fs::path p = pathOf(path);
    if (lower(utf8Of(p.filename())) == "pack.mcmeta")
        p = p.parent_path();
    if (lower(utf8Of(p.extension())) == ".zip")
    {
        ZipArchive::Bytes bytes;
        std::unique_ptr<ZipArchive::Archive> zip(new ZipArchive::Archive);
        if (!readWhole(p, bytes) || !zip->open(bytes))
            return false;
        // the root: where the first mark is found nearest the top
        size_t best = std::string::npos;
        for (const std::string &entry : zip->paths())
            for (const char *mark : kRootMarks)
            {
                const size_t at = entry.find(mark);
                if (at == std::string::npos || (at != 0 && entry[at - 1] != '/'))
                    continue;
                if (best == std::string::npos || at < best)
                {
                    best = at;
                    m_prefix = entry.substr(0, at);
                }
            }
        if (best == std::string::npos)
            return false;
        m_zip = std::move(zip);
        m_name = utf8Of(p.stem());
    }
    else
    {
        if (!fs::is_directory(p, error))
            return false;
        // the root: this folder, or the one folder inside it that is one
        auto isRoot = [&](const fs::path &folder) {
            for (const char *mark : kRootMarks)
                if (fs::exists(folder / pathOf(mark), error))
                    return true;
            return false;
        };
        fs::path root = p;
        if (!isRoot(root))
        {
            int roots = 0;
            for (fs::directory_iterator it(p, error), end; !error && it != end; it.increment(error))
                if (it->is_directory(error) && isRoot(it->path()))
                {
                    root = it->path();
                    ++roots;
                }
            if (roots != 1)
                return false;
        }
        m_folder = utf8Of(root);
        m_name = utf8Of(p.filename());
    }
    for (int t = 0; t < LandTextures::TEXTURE_COUNT; ++t)
        if (provides(t))
            ++m_coverage;
    return m_coverage > 0;
}

bool Pack::provides(int texture) const
{
    const Source *s = sourceOf(texture);
    if (!s)
        return false;
    if (hasFile(std::string("textures/") + s->native + ".png"))
        return true;
    std::vector<std::string> blocks = split(s->blocks);
    if (texture == T::BAMBOO || texture == T::BAMBOO_TOP)
        blocks = {"bamboo_block", "bamboo_stalk"};
    for (const std::string &entry : blocks)
    {
        std::string name;
        int colour;
        candidate(entry, name, colour);
        for (const char *folder : kBlockFolders)
            if (hasFile(folder + name + ".png"))
                return true;
    }
    return false;
}

bool Pack::blockPicture(const std::string &name, PixelArt::Image &out) const
{
    for (const char *folder : kBlockFolders)
        if (picture(folder + name + ".png", out))
            return true;
    return false;
}

bool Pack::bambooPicture(bool top, PixelArt::Image &out) const
{
    // The stalks are drawn from the strip in the middle of the picture
    // (columns 13 to 18 of 32), a top's leaves from its upper half: the
    // pack's bamboo block (or stalk) is squeezed into the strip, its leaves
    // spread over the upper half round it.
    PixelArt::Image stalk, leaves;
    if (!blockPicture("bamboo_block", stalk) && !blockPicture("bamboo_stalk", stalk))
        return false;
    const bool hasLeaves =
        blockPicture(top ? "bamboo_large_leaves" : "bamboo_small_leaves", leaves);
    const int side = PixelArt::sizeOf(stalk) < 16 ? 16 : PixelArt::sizeOf(stalk);
    const int stalkSide = PixelArt::sizeOf(stalk);
    const int c0 = 13 * side / 32, c1 = 19 * side / 32, from = top ? side / 2 : 0;
    out = PixelArt::blank(side);
    for (int y = from; y < side; ++y)
        for (int x = c0; x < c1; ++x)
        {
            const size_t i = static_cast<size_t>((y * side + x) * 4);
            const size_t j = static_cast<size_t>(
                ((y * stalkSide / side) * stalkSide + (x - c0) * stalkSide / (c1 - c0)) * 4);
            for (int c = 0; c < 3; ++c)
                out[i + static_cast<size_t>(c)] = stalk[j + static_cast<size_t>(c)];
            out[i + 3] = 255;
        }
    if (hasLeaves)
    {
        const int leafSide = PixelArt::sizeOf(leaves);
        for (int y = 0; y < side / 2; ++y)
            for (int x = 0; x < side; ++x)
            {
                if (!top && x >= c0 && x < c1)
                    continue; // a stalk's own strip stays stalk
                const size_t j = static_cast<size_t>(
                    (((y * 2) * leafSide / side) * leafSide + x * leafSide / side) * 4);
                if (leaves[j + 3] < 128)
                    continue;
                const size_t i = static_cast<size_t>((y * side + x) * 4);
                for (int c = 0; c < 4; ++c)
                    out[i + static_cast<size_t>(c)] = leaves[j + static_cast<size_t>(c)];
            }
    }
    return true;
}

bool Pack::readFile(const std::string &relative, ZipArchive::Bytes &out) const
{
    if (m_zip)
        return m_zip->read(m_prefix + relative, out);
    return !m_folder.empty() && readWhole(pathOf(m_folder) / pathOf(relative), out);
}

bool Pack::hasFile(const std::string &relative) const
{
    if (m_zip)
        return m_zip->has(m_prefix + relative);
    std::error_code error;
    return !m_folder.empty() && fs::is_regular_file(pathOf(m_folder) / pathOf(relative), error);
}

bool Pack::picture(const std::string &relative, PixelArt::Image &out) const
{
    ZipArchive::Bytes file;
    if (!hasFile(relative) || !readFile(relative, file))
        return false;
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
    if (!decodePng(file, width, height, rgba))
        return false;
    out = firstFrame(rgba, width, height);
    return true;
}

bool Pack::entityPicture(const std::string &names, bool (*fits)(int width, int height), int &width,
                         int &height, std::vector<unsigned char> &rgba) const
{
    for (const std::string &name : split(names.c_str()))
    {
        const std::string relative = "assets/minecraft/textures/entity/" + name + ".png";
        ZipArchive::Bytes file;
        if (hasFile(relative) && readFile(relative, file) && decodePng(file, width, height, rgba) &&
            fits(width, height))
            return true;
    }
    return false;
}

bool Pack::standIn(const std::string &blocks, Use use, PixelArt::Image &out) const
{
    for (const std::string &entry : split(blocks.c_str()))
    {
        std::string block;
        int colour;
        candidate(entry, block, colour);
        for (const char *folder : kBlockFolders)
        {
            if (!picture(folder + block + ".png", out))
                continue;
            // a grass side's green fringe is a grey overlay, tinted
            PixelArt::Image fringe;
            if (picture(std::string(folder) + block + "_overlay.png", fringe))
            {
                int r, g, b;
                tintColour(GRASS, r, g, b);
                tint(fringe, r, g, b);
                overlay(out, fringe);
            }
            int r, g, b;
            if (colour != NONE)
            {
                desaturate(out);
                tintColour(colour, r, g, b);
                tint(out, r, g, b);
            }
            else if (use == Use::GRASS && isGrey(out))
            {
                tintColour(GRASS, r, g, b);
                tint(out, r, g, b);
            }
            if (use != Use::PLANT)
                fillHoles(out, 1.0f);
            return true;
        }
    }
    return false;
}

bool Pack::hasAny(const std::string &blocks) const
{
    for (const std::string &entry : split(blocks.c_str()))
    {
        std::string block;
        int colour;
        candidate(entry, block, colour);
        for (const char *folder : kBlockFolders)
            if (hasFile(folder + block + ".png"))
                return true;
    }
    return false;
}

bool Pack::landTexture(int texture, PixelArt::Image &out) const
{
    const Source *s = sourceOf(texture);
    if (!s)
        return false;
    // this program's own layout first: taken as it is
    bool tinted = false;
    int standIn = NONE; // the colour a block standing in for another takes
    if (!picture(std::string("textures/") + s->native + ".png", out))
    {
        if (texture == T::BAMBOO || texture == T::BAMBOO_TOP)
            return bambooPicture(texture == T::BAMBOO_TOP, out);
        bool found = false;
        for (const std::string &entry : split(s->blocks))
            for (const char *folder : kBlockFolders)
            {
                std::string block;
                int colour;
                candidate(entry, block, colour);
                if (!found && picture(folder + block + ".png", out))
                {
                    found = true;
                    standIn = colour;
                    // the grass side's green fringe is a grey overlay, tinted
                    PixelArt::Image fringe;
                    if (texture == T::GRASS_SIDE &&
                        picture(std::string(folder) + block + "_overlay.png", fringe))
                    {
                        int r, g, b;
                        tintColour(GRASS, r, g, b);
                        tint(fringe, r, g, b);
                        overlay(out, fringe);
                    }
                }
            }
        if (!found)
            return false;
        tinted = s->tint != NONE;
    }
    if (standIn != NONE)
    {
        desaturate(out);
        int r, g, b;
        tintColour(standIn, r, g, b);
        tint(out, r, g, b);
    }
    else if (tinted && isGrey(out))
    {
        int r, g, b;
        tintColour(s->tint, r, g, b);
        tint(out, r, g, b);
    }
    if (texture == T::WATER || texture == T::SEA_WATER)
    {
        // as see-through as the built-in water, whatever the pack's is
        const PixelArt::Image own = LandTextures::make(texture);
        long alpha = 0;
        for (size_t i = 3; i < own.size(); i += 4)
            alpha += own[i];
        const unsigned char a =
            static_cast<unsigned char>(alpha / static_cast<long>(own.size() / 4));
        for (size_t i = 3; i < out.size(); i += 4)
            out[i] = a;
    }
    else if (texture == T::WINDOW)
    {
        // the pack's glass as it is: blended over what is behind it, its
        // clear parts (less than half opaque) cut out
    }
    else if (isLeaves(texture))
        bleedHoles(out); // cut out, as fancy leaves are
    else if (!isPlant(texture))
        fillHoles(out, isLeaves(texture) ? 0.55f : 1.0f);
    return true;
}

bool decodePng(const ZipArchive::Bytes &file, int &width, int &height,
               std::vector<unsigned char> &rgba)
{
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (file.size() < 8 || file.size() > 64u * 1024u * 1024u ||
        !std::equal(signature, signature + 8, file.begin()))
        return false;
    // stb_image (include/stb-2.30) turns any PNG (palette, grey, 16 bit,
    // with or without alpha) into 8 bit RGBA; a broken one is left out.
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
                                                  &width, &height, &channels, 4);
    if (!pixels)
        return false;
    if (width <= 0 || height <= 0)
    {
        stbi_image_free(pixels);
        return false;
    }
    rgba.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    stbi_image_free(pixels);
    return true;
}

PixelArt::Image firstFrame(const std::vector<unsigned char> &rgba, int width, int height)
{
    // an animated strip's frames are squares one under another
    const int frame = width < height ? width : height;
    int side = 1;
    while (side * 2 <= frame && side < 256)
        side *= 2;
    PixelArt::Image out = PixelArt::blank(side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
        {
            const int sx = x * frame / side, sy = y * frame / side;
            for (int c = 0; c < 4; ++c)
                out[static_cast<size_t>((y * side + x) * 4 + c)] =
                    rgba[(static_cast<size_t>(sy) * static_cast<size_t>(width) +
                          static_cast<size_t>(sx)) *
                             4u +
                         static_cast<size_t>(c)];
        }
    return out;
}

bool isGrey(const PixelArt::Image &image)
{
    long spread = 0, opaque = 0;
    for (size_t i = 0; i + 3 < image.size(); i += 4)
    {
        if (image[i + 3] < 128)
            continue;
        const int hi = std::max({image[i], image[i + 1], image[i + 2]});
        const int lo = std::min({image[i], image[i + 1], image[i + 2]});
        spread += hi - lo;
        ++opaque;
    }
    return opaque > 0 && spread < opaque * 12;
}

void tint(PixelArt::Image &image, int r, int g, int b)
{
    const int by[3] = {r, g, b};
    for (size_t i = 0; i + 3 < image.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            image[i + static_cast<size_t>(c)] =
                static_cast<unsigned char>(image[i + static_cast<size_t>(c)] * by[c] / 255);
}

void overlay(PixelArt::Image &image, const PixelArt::Image &over)
{
    const int side = sizeOfSide(image), overSide = sizeOfSide(over);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
        {
            const size_t i = static_cast<size_t>((y * side + x) * 4);
            const size_t j =
                static_cast<size_t>(((y * overSide / side) * overSide + x * overSide / side) * 4);
            const int a = over[j + 3];
            for (int c = 0; c < 3; ++c)
                image[i + static_cast<size_t>(c)] =
                    static_cast<unsigned char>((image[i + static_cast<size_t>(c)] * (255 - a) +
                                                over[j + static_cast<size_t>(c)] * a) /
                                               255);
            image[i + 3] = static_cast<unsigned char>(std::max<int>(image[i + 3], a));
        }
}

void bleedHoles(PixelArt::Image &image)
{
    long sum[3] = {0, 0, 0}, count = 0;
    for (size_t i = 0; i + 3 < image.size(); i += 4)
        if (image[i + 3] >= 128)
        {
            for (int c = 0; c < 3; ++c)
                sum[c] += image[i + static_cast<size_t>(c)];
            ++count;
        }
    if (count == 0)
        return;
    for (size_t i = 0; i + 3 < image.size(); i += 4)
        if (image[i + 3] < 128)
            for (int c = 0; c < 3; ++c)
                image[i + static_cast<size_t>(c)] = static_cast<unsigned char>(sum[c] / count);
}

void fillHoles(PixelArt::Image &image, float shade)
{
    long sum[3] = {0, 0, 0}, count = 0;
    for (size_t i = 0; i + 3 < image.size(); i += 4)
        if (image[i + 3] >= 128)
        {
            for (int c = 0; c < 3; ++c)
                sum[c] += image[i + static_cast<size_t>(c)];
            ++count;
        }
    unsigned char fill[3] = {128, 128, 128};
    if (count > 0)
        for (int c = 0; c < 3; ++c)
            fill[c] = static_cast<unsigned char>(static_cast<float>(sum[c] / count) * shade);
    for (size_t i = 0; i + 3 < image.size(); i += 4)
    {
        if (image[i + 3] < 128)
            for (int c = 0; c < 3; ++c)
                image[i + static_cast<size_t>(c)] = fill[c];
        image[i + 3] = 255;
    }
}

} // namespace TexturePack
