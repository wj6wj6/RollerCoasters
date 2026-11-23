/************************************************************************
     File:        AppControl.cpp

     Comment:     See AppControl.h.

*************************************************************************/

#include "AppControl.h"

#include "TexturePack.h"
#include "Utilities/Utf8File.H"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_set>

namespace AppControl
{
namespace
{
bool readNumber(const char *text, float lo, float hi, float &value)
{
    char *end = nullptr;
    const double read = std::strtod(text, &end);
    if (end == text || *end != '\0' || !(read >= lo && read <= hi))
        return false;
    value = static_cast<float>(read);
    return true;
}

bool readSwitch(const char *text, Switch &value)
{
    if (std::strcmp(text, "on") == 0)
        value = Switch::On;
    else if (std::strcmp(text, "off") == 0)
        value = Switch::Off;
    else
        return false;
    return true;
}

bool readCamera(const char *text, Camera &value)
{
    static const struct
    {
        const char *name;
        Camera camera;
    } kCameras[] = {{"world", Camera::World},
                    {"train", Camera::Train},
                    {"chase", Camera::Chase},
                    {"top", Camera::Top},
                    {"fly", Camera::Fly}};
    for (const auto &c : kCameras)
        if (std::strcmp(text, c.name) == 0)
        {
            value = c.camera;
            return true;
        }
    return false;
}

bool readSize(const char *text, int &width, int &height)
{
    char *end = nullptr;
    const long w = std::strtol(text, &end, 10);
    if (end == text || *end != 'x')
        return false;
    const char *rest = end + 1;
    const long h = std::strtol(rest, &end, 10);
    if (end == rest || *end != '\0' || w < 320 || h < 240 || w > 7680 || h > 4320)
        return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

// Splits a line into words the way CTrack::readPoints() does.
int countWords(const char *line)
{
    int words = 0;
    bool inWord = false;
    for (const char *c = line; *c; ++c)
    {
        const bool space = *c == ' ' || *c == '\t' || *c == '\n' || *c == '\r';
        if (!space && !inWord)
            ++words;
        inWord = !space;
    }
    return words;
}

void put32(std::vector<unsigned char> &out, uint32_t v)
{
    out.push_back(static_cast<unsigned char>(v >> 24));
    out.push_back(static_cast<unsigned char>(v >> 16));
    out.push_back(static_cast<unsigned char>(v >> 8));
    out.push_back(static_cast<unsigned char>(v));
}

uint32_t crc32(const unsigned char *data, size_t size, uint32_t crc = 0)
{
    static uint32_t table[256];
    static bool made = false;
    if (!made)
    {
        for (uint32_t n = 0; n < 256; ++n)
        {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        made = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void putChunk(std::vector<unsigned char> &out, const char type[4],
              const std::vector<unsigned char> &data)
{
    put32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc32(&out[start], out.size() - start));
}
} // namespace

const char *usage()
{
    return "Usage: RollerCoasters [--version | --help | --doctor | --screenshot <file.png>\n"
           "                       | --benchmark <report.txt>]\n"
           "                      [--track <file>] [--pack <zip or folder>]\n"
           "                      [--camera world|train|chase|top|fly]\n"
           "                      [--scenery on|off] [--light on|off] [--water on|off]\n"
           "                      [--rain on|off] [--shadows on|off]\n"
           "                      [--time <0-24>] [--u <train position>]\n"
           "                      [--frames <1-600>] [--size <width>x<height>]\n"
           "Exit codes: 0 passed, 1 a check failed, 2 wrong command line.\n";
}

bool parse(int argc, const char *const *argv, Options &options, std::string &error)
{
    options = Options();
    bool sceneOptions = false;
    bool framesGiven = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : nullptr;
        auto needsValue = [&]() {
            if (!value)
                error = arg + " needs a value";
            return value != nullptr;
        };
        auto setMode = [&](Mode mode) {
            if (options.mode != Mode::Gui)
            {
                error = "only one of --version, --help, --doctor, --screenshot and --benchmark";
                return false;
            }
            options.mode = mode;
            return true;
        };

        if (arg == "--version")
        {
            if (!setMode(Mode::Version))
                return false;
            continue;
        }
        if (arg == "--help" || arg == "-h")
        {
            if (!setMode(Mode::Help))
                return false;
            continue;
        }
        if (arg == "--doctor")
        {
            if (!setMode(Mode::Doctor))
                return false;
            continue;
        }
        static const char *const kValueOptions[] = {
            "--screenshot", "--benchmark", "--track",  "--pack", "--camera",
            "--scenery",    "--light",     "--water",  "--rain", "--shadows",
            "--time",       "--u",         "--frames", "--size"};
        bool known = false;
        for (const char *name : kValueOptions)
            known = known || arg == name;
        if (!known)
        {
            error = (arg.rfind("--", 0) == 0 ? "unknown option: " : "unexpected argument: ") + arg;
            return false;
        }
        if (!needsValue())
            return false;
        ++i;
        bool ok = true;
        if (arg == "--screenshot")
        {
            if (!setMode(Mode::Screenshot))
                return false;
            options.output = value;
            ok = !options.output.empty();
        }
        else if (arg == "--benchmark")
        {
            if (!setMode(Mode::Benchmark))
                return false;
            options.output = value;
            ok = !options.output.empty();
        }
        else if (arg == "--track")
            options.track = value;
        else if (arg == "--pack")
            options.texturePack = value;
        else if (arg == "--camera")
            ok = readCamera(value, options.camera);
        else if (arg == "--scenery")
            ok = readSwitch(value, options.scenery);
        else if (arg == "--light")
            ok = readSwitch(value, options.light);
        else if (arg == "--water")
            ok = readSwitch(value, options.water);
        else if (arg == "--rain")
            ok = readSwitch(value, options.rain);
        else if (arg == "--shadows")
            ok = readSwitch(value, options.shadows);
        else if (arg == "--time")
            ok = readNumber(value, 0.0f, 24.0f, options.time);
        else if (arg == "--u")
            ok = readNumber(value, 0.0f, 65535.0f, options.trainU);
        else if (arg == "--frames")
        {
            float frames = 0;
            ok = readNumber(value, 1.0f, 600.0f, frames) &&
                 frames == static_cast<float>(static_cast<int>(frames));
            options.frames = static_cast<int>(frames);
            framesGiven = true;
        }
        else if (arg == "--size")
            ok = readSize(value, options.width, options.height);
        if (!ok)
        {
            error = "bad value for " + arg + ": " + value;
            return false;
        }
        if (arg != "--screenshot" && arg != "--benchmark")
            sceneOptions = true;
    }
    if (options.mode == Mode::Benchmark && !framesGiven)
        options.frames = kBenchmarkFrames;
    if (sceneOptions && (options.mode == Mode::Version || options.mode == Mode::Help))
    {
        error = "--version and --help take no other options";
        return false;
    }
    return true;
}

bool checkTrackFile(const std::string &path, std::string &error)
{
    FILE *fp = openUtf8(path.c_str(), "r");
    if (!fp)
    {
        error = "cannot open " + path;
        return false;
    }
    char buf[512];
    bool ok = false;
    if (!std::fgets(buf, sizeof buf, fp))
        error = "empty file, the first line must be the number of points";
    else
    {
        const long count = std::strtol(buf, nullptr, 10);
        if (count < 4 || count > 65535)
            error = "the point count must be 4 to 65535, not " + std::to_string(count);
        else
        {
            long lines = 0;
            ok = true;
            while (ok && lines < count && std::fgets(buf, sizeof buf, fp))
            {
                ++lines;
                const int words = countWords(buf);
                if (words < 3)
                {
                    error = "line " + std::to_string(lines + 1) + " needs x y z";
                    ok = false;
                }
            }
            if (ok && lines < count)
            {
                error = "the file says " + std::to_string(count) + " points but has " +
                        std::to_string(lines);
                ok = false;
            }
        }
    }
    std::fclose(fp);
    return ok;
}

std::vector<Check> environmentChecks()
{
    std::vector<Check> checks;

    // Load and Save start in TrackFiles beside the program (main() moves there).
    const std::string probe = "TrackFiles/.doctor-probe";
    FILE *fp = openUtf8(probe.c_str(), "w");
    const bool writable = fp && std::fputs("ok\n", fp) >= 0;
    if (fp)
        std::fclose(fp);
    std::remove(probe.c_str());
    checks.push_back({"track folder", writable,
                      writable ? "TrackFiles can be written" : "cannot write TrackFiles"});

    const std::vector<TexturePack::Found> packs = TexturePack::findAll();
    checks.push_back(
        {"texture pack folders", true,
         std::to_string(packs.size()) + " pack(s) in " + TexturePack::searchFolders().front()});
    for (const TexturePack::Found &found : packs)
    {
        TexturePack::Pack pack;
        const bool opened = pack.open(found.path);
        checks.push_back({"texture pack " + found.name, opened,
                          opened ? std::to_string(pack.coverage()) + " textures"
                                 : "no usable textures in " + found.path});
    }
    return checks;
}

int report(const std::vector<Check> &checks, FILE *out)
{
    int result = kPass;
    for (const Check &check : checks)
    {
        std::fprintf(out, "%s %s: %s\n", check.ok ? "PASS" : "FAIL", check.name.c_str(),
                     check.detail.c_str());
        if (!check.ok)
            result = kFail;
    }
    std::fflush(out);
    return result;
}

ImageStats measure(const std::vector<unsigned char> &rgb, int width, int height)
{
    ImageStats stats;
    stats.width = width;
    stats.height = height;
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (width <= 0 || height <= 0 || rgb.size() < pixels * 3)
        return stats;

    std::map<uint32_t, size_t> exact;
    std::unordered_set<uint32_t> quantised;
    double luma = 0;
    for (size_t i = 0; i < pixels; ++i)
    {
        const unsigned char r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        ++exact[(uint32_t(r) << 16) | (uint32_t(g) << 8) | b];
        quantised.insert((uint32_t(r >> 4) << 8) | (uint32_t(g >> 4) << 4) | uint32_t(b >> 4));
        luma += 0.299 * r + 0.587 * g + 0.114 * b;
    }
    uint32_t common = 0;
    size_t most = 0;
    for (const auto &entry : exact)
        if (entry.second > most)
        {
            most = entry.second;
            common = entry.first;
        }
    const int cr = static_cast<int>(common >> 16), cg = static_cast<int>((common >> 8) & 0xFF),
              cb = static_cast<int>(common & 0xFF);
    size_t unlike = 0;
    for (size_t i = 0; i < pixels; ++i)
        if (std::abs(rgb[i * 3] - cr) > 8 || std::abs(rgb[i * 3 + 1] - cg) > 8 ||
            std::abs(rgb[i * 3 + 2] - cb) > 8)
            ++unlike;
    stats.coverage = static_cast<double>(unlike) / static_cast<double>(pixels);
    stats.colours = static_cast<int>(quantised.size());
    stats.meanLuma = luma / static_cast<double>(pixels);
    return stats;
}

bool looksDrawn(const ImageStats &stats)
{
    return stats.width > 0 && stats.coverage >= 0.2 && stats.colours >= 16 &&
           stats.meanLuma > 4.0 && stats.meanLuma < 251.0;
}

std::string describe(const ImageStats &stats)
{
    char text[160];
    std::snprintf(text, sizeof text,
                  "%dx%d, %.0f%% unlike the background, %d colours, mean luma %.0f", stats.width,
                  stats.height, stats.coverage * 100.0, stats.colours, stats.meanLuma);
    return text;
}

bool writePng(const std::string &path, const std::vector<unsigned char> &rgb, int width, int height,
              std::string &error)
{
    const size_t row = static_cast<size_t>(width) * 3;
    if (width <= 0 || height <= 0 || rgb.size() < row * static_cast<size_t>(height))
    {
        error = "no picture to write";
        return false;
    }

    // The rows, each after filter byte 0, in stored (uncompressed) deflate
    // blocks of at most 65535 bytes, with the zlib header and Adler-32.
    std::vector<unsigned char> raw;
    raw.reserve((row + 1) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y)
    {
        raw.push_back(0);
        const unsigned char *start = &rgb[row * static_cast<size_t>(y)];
        raw.insert(raw.end(), start, start + row);
    }
    std::vector<unsigned char> zlib = {0x78, 0x01};
    size_t at = 0;
    do
    {
        const size_t n = raw.size() - at < 65535 ? raw.size() - at : 65535;
        zlib.push_back(static_cast<unsigned char>(at + n == raw.size() ? 1 : 0));
        zlib.push_back(static_cast<unsigned char>(n & 0xFF));
        zlib.push_back(static_cast<unsigned char>(n >> 8));
        zlib.push_back(static_cast<unsigned char>(~n & 0xFF));
        zlib.push_back(static_cast<unsigned char>((~n >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                    raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
    } while (at < raw.size());
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw)
    {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    put32(zlib, (b << 16) | a);

    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<unsigned char> header;
    put32(header, static_cast<uint32_t>(width));
    put32(header, static_cast<uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8 bits, RGB, deflate, no filter, no interlace
    putChunk(png, "IHDR", header);
    putChunk(png, "IDAT", zlib);
    putChunk(png, "IEND", {});

    FILE *fp = openUtf8(path.c_str(), "wb");
    if (!fp)
    {
        error = "cannot write " + path;
        return false;
    }
    const bool written = std::fwrite(png.data(), 1, png.size(), fp) == png.size();
    const bool closed = std::fclose(fp) == 0;
    if (!written || !closed)
    {
        error = "cannot write " + path;
        return false;
    }
    return true;
}
} // namespace AppControl
