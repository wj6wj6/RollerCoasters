/************************************************************************
     File:        ZipArchive.h

     Comment:     Reads the files in a .zip archive, as texture packs are
                  usually shared: the stored and the deflated (RFC 1951)
                  entries, which is what zip tools write. The decoder is
                  our own, small and only as fast as loading a few hundred
                  small images needs, so no third-party library is added.
                  No encryption, no ZIP64, no split archives.

*************************************************************************/
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ZipArchive
{

typedef std::vector<unsigned char> Bytes;

// Decodes raw DEFLATE data (no zlib or gzip header). False if it is cut
// short or not valid; out then holds what was decoded so far.
bool inflate(const unsigned char *data, size_t size, Bytes &out);

// The entries of an archive read into memory, by their path inside it
// ("assets/minecraft/textures/block/stone.png"); folders are left out.
class Archive
{
  public:
    // Reads the archive's directory; false if it is not a zip archive
    // this can read.
    bool open(Bytes bytes);
    bool has(const std::string &path) const;
    // The entry's contents; false if there is none or it cannot be
    // decoded.
    bool read(const std::string &path, Bytes &out) const;
    // Every entry's path.
    std::vector<std::string> paths() const;

  private:
    struct Entry
    {
        size_t offset;       // of its local header
        size_t packed, size; // compressed and decoded sizes
        int method;          // 0 stored, 8 deflated
    };
    Bytes m_bytes;
    std::map<std::string, Entry> m_entries;
};

} // namespace ZipArchive
