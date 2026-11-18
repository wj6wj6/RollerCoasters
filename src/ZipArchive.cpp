/************************************************************************
     File:        ZipArchive.cpp

     Comment:     See ZipArchive.h. The decoder follows RFC 1951 the plain
                  way: canonical Huffman codes decoded a bit at a time from
                  their code-length counts.

*************************************************************************/

#include <cstddef>

#include "ZipArchive.h"

namespace ZipArchive
{

namespace
{

// Reads the stream's bits, least significant first.
struct BitReader
{
    const unsigned char *data;
    size_t size;
    size_t pos = 0;
    unsigned int buffer = 0;
    int count = 0;
    bool overrun = false;

    BitReader(const unsigned char *d, size_t s) : data(d), size(s)
    {
    }
    unsigned int bits(int need)
    {
        while (count < need)
        {
            if (pos >= size)
            {
                overrun = true;
                return 0;
            }
            buffer |= static_cast<unsigned int>(data[pos++]) << count;
            count += 8;
        }
        const unsigned int value = buffer & ((1u << need) - 1u);
        buffer >>= need;
        count -= need;
        return value;
    }
    void toByte()
    {
        buffer = 0;
        count = 0;
    }
};

const int kMaxBits = 15;

// A canonical Huffman code: how many codes of each length, and the symbols
// in code order.
struct Huffman
{
    short counts[kMaxBits + 1];
    short symbols[288];
};

// Builds the code from each symbol's code length; false if the lengths
// over-subscribe the code (an incomplete code is allowed, as in RFC 1951's
// single distance code).
bool build(Huffman &h, const unsigned char *lengths, int n)
{
    for (int len = 0; len <= kMaxBits; ++len)
        h.counts[len] = 0;
    for (int s = 0; s < n; ++s)
        ++h.counts[lengths[s]];
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        left = left * 2 - h.counts[len];
        if (left < 0)
            return false;
    }
    short offsets[kMaxBits + 1];
    offsets[1] = 0;
    for (int len = 1; len < kMaxBits; ++len)
        offsets[len + 1] = static_cast<short>(offsets[len] + h.counts[len]);
    for (int s = 0; s < n; ++s)
        if (lengths[s] != 0)
            h.symbols[offsets[lengths[s]]++] = static_cast<short>(s);
    return true;
}

// The next symbol, or -1 if the bits match no code.
int decode(BitReader &in, const Huffman &h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        code |= static_cast<int>(in.bits(1));
        if (in.overrun)
            return -1;
        const int count = h.counts[len];
        if (code - count < first)
            return h.symbols[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

const short kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistanceBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                 33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// One block's symbols, until its end code.
bool codes(BitReader &in, Bytes &out, const Huffman &lengths, const Huffman &distances)
{
    while (true)
    {
        const int symbol = decode(in, lengths);
        if (symbol < 0)
            return false;
        if (symbol < 256)
            out.push_back(static_cast<unsigned char>(symbol));
        else if (symbol == 256)
            return true;
        else
        {
            const int l = symbol - 257;
            if (l >= 29)
                return false;
            const size_t length = static_cast<size_t>(kLengthBase[l]) + in.bits(kLengthExtra[l]);
            const int d = decode(in, distances);
            if (d < 0 || d >= 30)
                return false;
            const size_t distance =
                static_cast<size_t>(kDistanceBase[d]) + in.bits(kDistanceExtra[d]);
            if (in.overrun || distance > out.size())
                return false;
            const size_t from = out.size() - distance;
            for (size_t k = 0; k < length; ++k)
                out.push_back(out[from + k]); // may overlap what it copies
        }
    }
}

bool fixedBlock(BitReader &in, Bytes &out)
{
    static Huffman lengths, distances;
    static bool built = false;
    if (!built)
    {
        unsigned char l[288];
        for (int s = 0; s < 288; ++s)
            l[s] = static_cast<unsigned char>(s < 144 ? 8 : (s < 256 ? 9 : (s < 280 ? 7 : 8)));
        build(lengths, l, 288);
        unsigned char d[30];
        for (int s = 0; s < 30; ++s)
            d[s] = 5;
        build(distances, d, 30);
        built = true;
    }
    return codes(in, out, lengths, distances);
}

bool dynamicBlock(BitReader &in, Bytes &out)
{
    const int nLengths = static_cast<int>(in.bits(5)) + 257;
    const int nDistances = static_cast<int>(in.bits(5)) + 1;
    const int nCodes = static_cast<int>(in.bits(4)) + 4;
    if (in.overrun || nLengths > 286 || nDistances > 30)
        return false;
    static const unsigned char order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                            11, 4,  12, 3, 13, 2, 14, 1, 15};
    unsigned char lengths[320] = {0};
    for (int k = 0; k < nCodes; ++k)
        lengths[order[k]] = static_cast<unsigned char>(in.bits(3));
    Huffman codeCode;
    if (in.overrun || !build(codeCode, lengths, 19))
        return false;
    int k = 0;
    while (k < nLengths + nDistances)
    {
        const int symbol = decode(in, codeCode);
        if (symbol < 0)
            return false;
        if (symbol < 16)
        {
            lengths[k++] = static_cast<unsigned char>(symbol);
            continue;
        }
        unsigned char repeat = 0;
        int times = 0;
        if (symbol == 16)
        {
            if (k == 0)
                return false;
            repeat = lengths[k - 1];
            times = 3 + static_cast<int>(in.bits(2));
        }
        else if (symbol == 17)
            times = 3 + static_cast<int>(in.bits(3));
        else
            times = 11 + static_cast<int>(in.bits(7));
        if (in.overrun || k + times > nLengths + nDistances)
            return false;
        while (times-- > 0)
            lengths[k++] = repeat;
    }
    if (lengths[256] == 0)
        return false; // no end code
    Huffman literals, distances;
    if (!build(literals, lengths, nLengths) || !build(distances, lengths + nLengths, nDistances))
        return false;
    return codes(in, out, literals, distances);
}

unsigned int read16(const Bytes &b, size_t at)
{
    return static_cast<unsigned int>(b[at]) | (static_cast<unsigned int>(b[at + 1]) << 8);
}

unsigned int read32(const Bytes &b, size_t at)
{
    return read16(b, at) | (read16(b, at + 2) << 16);
}

} // namespace

bool inflate(const unsigned char *data, size_t size, Bytes &out)
{
    BitReader in(data, size);
    bool last = false;
    while (!last)
    {
        last = in.bits(1) != 0;
        const unsigned int type = in.bits(2);
        if (in.overrun)
            return false;
        bool ok = false;
        if (type == 0)
        {
            // stored: byte aligned, a length and its complement
            in.toByte();
            if (in.pos + 4 > size)
                return false;
            const size_t length =
                static_cast<size_t>(data[in.pos]) | (static_cast<size_t>(data[in.pos + 1]) << 8);
            const size_t check = static_cast<size_t>(data[in.pos + 2]) |
                                 (static_cast<size_t>(data[in.pos + 3]) << 8);
            in.pos += 4;
            if ((length ^ 0xffffu) != check || in.pos + length > size)
                return false;
            out.insert(out.end(), data + in.pos, data + in.pos + length);
            in.pos += length;
            ok = true;
        }
        else if (type == 1)
            ok = fixedBlock(in, out);
        else if (type == 2)
            ok = dynamicBlock(in, out);
        if (!ok)
            return false;
    }
    return true;
}

bool Archive::open(Bytes bytes)
{
    m_bytes.swap(bytes);
    m_entries.clear();
    const Bytes &b = m_bytes;
    // the end of central directory record, searched for from the end (it
    // may be followed by a comment of up to 64 KB)
    if (b.size() < 22)
        return false;
    size_t end = b.size() - 22;
    const size_t stop = end > 65557 ? end - 65557 : 0;
    while (read32(b, end) != 0x06054b50u)
    {
        if (end == stop)
            return false;
        --end;
    }
    const size_t count = read16(b, end + 10);
    size_t at = read32(b, end + 16);
    for (size_t k = 0; k < count; ++k)
    {
        if (at + 46 > b.size() || read32(b, at) != 0x02014b50u)
            return false;
        const unsigned int flags = read16(b, at + 8);
        Entry e;
        e.method = static_cast<int>(read16(b, at + 10));
        e.packed = read32(b, at + 20);
        e.size = read32(b, at + 24);
        const size_t nameLength = read16(b, at + 28);
        const size_t extra = read16(b, at + 30), comment = read16(b, at + 32);
        e.offset = read32(b, at + 42);
        if (at + 46 + nameLength > b.size())
            return false;
        std::string name(reinterpret_cast<const char *>(&b[at + 46]), nameLength);
        for (char &c : name)
            if (c == '\\')
                c = '/'; // some tools write Windows separators
        const bool folder = !name.empty() && name.back() == '/';
        if (!folder && (flags & 1u) == 0) // encrypted entries are left out
            m_entries[name] = e;
        at += 46 + nameLength + extra + comment;
    }
    return true;
}

bool Archive::has(const std::string &path) const
{
    return m_entries.count(path) != 0;
}

bool Archive::read(const std::string &path, Bytes &out) const
{
    out.clear();
    const auto it = m_entries.find(path);
    if (it == m_entries.end())
        return false;
    const Entry &e = it->second;
    const Bytes &b = m_bytes;
    if (e.offset + 30 > b.size() || read32(b, e.offset) != 0x04034b50u)
        return false;
    const size_t start = e.offset + 30 + read16(b, e.offset + 26) + read16(b, e.offset + 28);
    if (start + e.packed > b.size())
        return false;
    if (e.method == 0)
    {
        out.assign(b.begin() + static_cast<std::ptrdiff_t>(start),
                   b.begin() + static_cast<std::ptrdiff_t>(start + e.packed));
        return true;
    }
    if (e.method != 8)
        return false;
    out.reserve(e.size);
    return inflate(&b[start], e.packed, out) && out.size() == e.size;
}

std::vector<std::string> Archive::paths() const
{
    std::vector<std::string> all;
    for (const auto &e : m_entries)
        all.push_back(e.first);
    return all;
}

} // namespace ZipArchive
