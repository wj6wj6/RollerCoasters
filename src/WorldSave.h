/************************************************************************
     File:        WorldSave.h

     Comment:     Saves of the generated worlds, as a sandbox game keeps
                  its world in a save instead of generating it every time
                  it starts. LandWorld and OceanWorld write their blocks
                  and baked light to data\saves\ (beside the exe) the first
                  time they make them, and read them back on later starts.

                  A save holds a key naming what made it: the build time of
                  every source file that shapes that world. Rebuilding any
                  of them gives a new key, so the save no longer matches and
                  the world is generated (and saved) again. Deleting
                  data\saves\ does the same.

                  The block layers are run-length encoded: most of the
                  world is air or stone in long runs. A save that is
                  missing, cut short or from another build is ignored.

*************************************************************************/
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace WorldSave
{

const char kFolder[] = "data/saves";
const std::uint32_t kFormat = 1;
const char kMagic[8] = {'R', 'C', 'W', 'O', 'R', 'L', 'D', '1'};
const char kEnd[4] = {'E', 'N', 'D', '!'};

// Where the save with this name goes, e.g. "island" -> data/saves/island.world.
inline std::string path(const char *name)
{
    return std::string(kFolder) + "/" + name + ".world";
}

// Collects a save in memory; finish() writes it in one go.
class Writer
{
  public:
    explicit Writer(const std::string &key)
    {
        raw(kMagic, sizeof(kMagic));
        number(kFormat);
        number(static_cast<std::uint32_t>(key.size()));
        raw(key.data(), key.size());
    }

    void raw(const void *data, size_t size)
    {
        const unsigned char *bytes = static_cast<const unsigned char *>(data);
        m_out.insert(m_out.end(), bytes, bytes + size);
    }

    void number(std::uint32_t value)
    {
        raw(&value, sizeof(value));
    }

    // A block layer: its size, then runs of one value (value, run length).
    void layer(const std::vector<unsigned char> &cells)
    {
        number(static_cast<std::uint32_t>(cells.size()));
        const unsigned char *p = cells.data();
        const size_t count = cells.size();
        size_t i = 0;
        while (i < count)
        {
            const unsigned char value = p[i];
            size_t run = 1;
            while (i + run < count && p[i + run] == value && run < 0xffffffffu)
                ++run;
            m_out.push_back(value);
            number(static_cast<std::uint32_t>(run));
            i += run;
        }
    }

    // Writes the save under a temporary name and then puts it in place, so
    // a save cut short (the program closed meanwhile) is never read.
    bool finish(const char *name)
    {
        raw(kEnd, sizeof(kEnd));
        std::error_code error;
        std::filesystem::create_directories(kFolder, error);
        const std::string target = path(name);
        const std::string temporary = target + ".tmp";
        FILE *file = nullptr;
#ifdef _MSC_VER
        if (fopen_s(&file, temporary.c_str(), "wb") != 0)
            file = nullptr;
#else
        file = fopen(temporary.c_str(), "wb");
#endif
        if (!file)
            return false;
        const bool written = fwrite(m_out.data(), 1, m_out.size(), file) == m_out.size();
        const bool closed = fclose(file) == 0;
        if (!written || !closed)
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        std::filesystem::rename(temporary, target, error);
        return !error;
    }

  private:
    std::vector<unsigned char> m_out;
};

// Reads a save back. Every read returns false once anything does not fit,
// and the caller then generates the world instead.
class Reader
{
  public:
    // False when there is no save with this name or it was made by another
    // build (another key).
    bool open(const char *name, const std::string &key)
    {
        FILE *file = nullptr;
        const std::string from = path(name);
#ifdef _MSC_VER
        if (fopen_s(&file, from.c_str(), "rb") != 0)
            file = nullptr;
#else
        file = fopen(from.c_str(), "rb");
#endif
        if (!file)
            return false;
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(from, error);
        if (!error && size > sizeof(kMagic) + sizeof(kEnd))
        {
            m_in.resize(static_cast<size_t>(size));
            if (fread(m_in.data(), 1, m_in.size(), file) != m_in.size())
                m_in.clear();
        }
        fclose(file);
        if (m_in.size() < sizeof(kMagic) + sizeof(kEnd) ||
            std::memcmp(m_in.data() + m_in.size() - sizeof(kEnd), kEnd, sizeof(kEnd)) != 0)
            return false;
        m_end = m_in.size() - sizeof(kEnd);

        char magic[sizeof(kMagic)];
        std::uint32_t format = 0, keySize = 0;
        if (!raw(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0 ||
            !number(format) || format != kFormat || !number(keySize) || keySize != key.size())
            return false;
        std::string stored(keySize, '\0');
        return raw(&stored[0], keySize) && stored == key;
    }

    bool raw(void *data, size_t size)
    {
        if (m_at + size > m_end)
            return false;
        std::memcpy(data, m_in.data() + m_at, size);
        m_at += size;
        return true;
    }

    bool number(std::uint32_t &value)
    {
        return raw(&value, sizeof(value));
    }

    // A layer written by Writer::layer(); it must have the size cells has.
    bool layer(std::vector<unsigned char> &cells)
    {
        std::uint32_t count = 0;
        if (!number(count) || count != cells.size())
            return false;
        unsigned char *p = cells.data();
        size_t i = 0;
        while (i < count)
        {
            unsigned char value = 0;
            std::uint32_t run = 0;
            if (!raw(&value, 1) || !number(run) || run == 0 || i + run > count)
                return false;
            std::memset(p + i, value, run);
            i += run;
        }
        return true;
    }

    // Everything was read, up to the end mark.
    bool done() const
    {
        return m_at == m_end;
    }

  private:
    std::vector<unsigned char> m_in;
    size_t m_at = 0, m_end = 0;
};

} // namespace WorldSave
