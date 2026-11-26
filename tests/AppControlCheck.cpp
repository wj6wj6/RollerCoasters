// Checks the command line the verification scripts drive: parsing and its
// errors, the track file check against the real CTrack reader, the PNG writer
// read back by the program's own PNG decoder, the drawn-frame test, and the
// doctor's folder checks in an isolated folder.
#include "AppControl.h"
#include "TexturePack.h"
#include "Track.H"
#include "Utilities/Utf8File.H"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

bool parses(std::vector<const char *> args, AppControl::Options &options, std::string &error)
{
    args.insert(args.begin(), "RollerCoasters");
    error.clear();
    return AppControl::parse(static_cast<int>(args.size()), args.data(), options, error);
}

bool refused(std::vector<const char *> args)
{
    AppControl::Options options;
    std::string error;
    return !parses(args, options, error) && !error.empty();
}

void writeText(const fs::path &path, const char *text)
{
    FILE *fp = openUtf8(path.u8string().c_str(), "w");
    if (fp)
    {
        std::fputs(text, fp);
        std::fclose(fp);
    }
}

std::vector<unsigned char> readFile(const fs::path &path)
{
    std::vector<unsigned char> bytes;
    FILE *fp = openUtf8(path.u8string().c_str(), "rb");
    if (!fp)
        return bytes;
    unsigned char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, fp)) > 0)
        bytes.insert(bytes.end(), buffer, buffer + n);
    std::fclose(fp);
    return bytes;
}

// A picture with something in it: a gradient with a few boxes.
std::vector<unsigned char> scene(int width, int height)
{
    std::vector<unsigned char> rgb(static_cast<size_t>(width * height * 3));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            unsigned char *p = &rgb[static_cast<size_t>((y * width + x) * 3)];
            const bool box = (x / 20 + y / 20) % 3 == 0;
            p[0] = static_cast<unsigned char>(box ? 200 : x * 255 / width);
            p[1] = static_cast<unsigned char>(box ? 60 : y * 255 / height);
            p[2] = static_cast<unsigned char>(box ? 30 : 120);
        }
    return rgb;
}
} // namespace

int main()
{
    using AppControl::Camera;
    using AppControl::Mode;
    using AppControl::Switch;

    AppControl::Options options;
    std::string error;

    check(parses({}, options, error) && options.mode == Mode::Gui, "no arguments opens the window");
    check(parses({"--version"}, options, error) && options.mode == Mode::Version, "--version");
    check(parses({"--doctor"}, options, error) && options.mode == Mode::Doctor, "--doctor");
    check(parses({"--screenshot", "out.png", "--camera", "fly",  "--scenery", "off",
                  "--light",      "on",      "--rain",   "on",   "--time",    "18.5",
                  "--u",          "3",       "--frames", "8",    "--size",    "800x600",
                  "--track",      "t.txt",   "--pack",   "p.zip"},
                 options, error) &&
              options.mode == Mode::Screenshot && options.output == "out.png" &&
              options.camera == Camera::Fly && options.scenery == Switch::Off &&
              options.light == Switch::On && options.rain == Switch::On &&
              options.water == Switch::Keep && options.time == 18.5f && options.trainU == 3.0f &&
              options.frames == 8 && options.width == 800 && options.height == 600 &&
              options.track == "t.txt" && options.texturePack == "p.zip",
          "--screenshot with every scene option");
    check(parses({"--benchmark", "perf.txt", "--camera", "train"}, options, error) &&
              options.mode == Mode::Benchmark && options.output == "perf.txt" &&
              options.frames == AppControl::kBenchmarkFrames && options.camera == Camera::Train,
          "--benchmark measures 300 frames unless told");
    check(parses({"--frames", "50", "--benchmark", "perf.txt"}, options, error) &&
              options.mode == Mode::Benchmark && options.frames == 50,
          "--benchmark takes --frames");
    check(parses({"--screenshot", "a.png"}, options, error) && options.frames == 5,
          "--screenshot still draws 5 frames");
    check(parses({"--track", "t.txt"}, options, error) && options.mode == Mode::Gui &&
              options.track == "t.txt",
          "scene options also work when the window stays open");

    check(
        parses({"--screenshot", "a.png", "--adaptive", "off", "--samples", "on"}, options, error) &&
            options.adaptive == Switch::Off && options.samples == Switch::On,
        "--adaptive and --samples switch the track's subdivision and its samples");
    check(parses({"--track", "t.txt"}, options, error) && options.adaptive == Switch::Keep &&
              options.samples == Switch::Keep,
          "the track's subdivision is left alone unless told");
    check(parses({"--screenshot", "a.png", "--menu", "track"}, options, error) &&
              options.menu == AppControl::Menu::Track,
          "--menu opens a game menu screen for the screenshot");
    check(refused({"--menu", "inventory"}), "an unknown menu screen is refused");
    check(parses({"--screenshot", "a.png", "--waves", "equation"}, options, error) &&
              options.waves == 2,
          "--waves picks World > Waves");
    check(parses({"--screenshot", "a.png"}, options, error) && options.waves < 0,
          "the waves are left alone unless told");
    check(refused({"--waves", "tsunami"}), "an unknown wave model is refused");
    check(refused({"--frobnicate"}), "an unknown option is refused");
    check(refused({"stray"}), "a stray argument is refused");
    check(refused({"--screenshot"}), "an option without its value is refused");
    check(refused({"--camera", "side"}), "an unknown camera is refused");
    check(refused({"--scenery", "yes"}), "a switch takes on or off");
    check(refused({"--time", "25"}) && refused({"--time", "noon"}), "the hour is 0 to 24");
    check(refused({"--frames", "0"}) && refused({"--frames", "2.5"}), "frames are 1 to 600");
    check(refused({"--size", "800"}) && refused({"--size", "10x10"}), "the size is WxH, not tiny");
    check(refused({"--doctor", "--screenshot", "a.png"}), "one mode at a time");
    check(refused({"--screenshot", "a.png", "--benchmark", "b.txt"}), "screenshot or benchmark");
    check(refused({"--benchmark"}), "--benchmark needs a file");
    check(refused({"--version", "--track", "t.txt"}), "--version takes nothing else");
    check(std::string(AppControl::usage()).find("--doctor") != std::string::npos,
          "the usage lists --doctor");

    const fs::path folder = fs::temp_directory_path() / "rollercoasters-appcontrol-check";
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    fs::create_directories(folder);

    // track files: what the check accepts, the real reader loads
    const fs::path good = folder / "good.txt";
    writeText(good, "4\n0 5 0\n10 5 0 0 1 0\n10 5 10\n0 5 10\n");
    check(AppControl::checkTrackFile(good.u8string(), error), "a well formed track passes");
    {
        CTrack track;
        std::string readError;
        check(track.readPoints(good.u8string().c_str(), readError) && track.points.size() == 4,
              "and CTrack::readPoints() loads all of it");
    }
    struct Bad
    {
        const char *name;
        const char *text;
        const char *what;
    } bad[] = {
        {"empty.txt", "", "an empty file fails"},
        {"three.txt", "3\n0 0 0\n1 0 0\n2 0 0\n", "fewer than 4 points fails"},
        {"short.txt", "5\n0 0 0\n1 0 0\n2 0 0\n3 0 0\n", "fewer lines than the count fails"},
        {"words.txt", "4\n0 0 0\n1 0\n2 0 0\n3 0 0\n", "a line without x y z fails"},
    };
    for (const Bad &b : bad)
    {
        writeText(folder / b.name, b.text);
        check(!AppControl::checkTrackFile((folder / b.name).u8string(), error) && !error.empty(),
              b.what);
        CTrack track;
        std::string readError;
        check(!track.readPoints((folder / b.name).u8string().c_str(), readError) &&
                  !readError.empty(),
              "and CTrack::readPoints() refuses it too");
    }
    check(!AppControl::checkTrackFile((folder / "missing.txt").u8string(), error),
          "a missing file fails");

    // the PNG writer, read back by the decoder texture packs use; 200x150 RGB
    // is more than one stored deflate block
    const int width = 200, height = 150;
    const std::vector<unsigned char> rgb = scene(width, height);
    const fs::path png = folder / "shot.png";
    check(AppControl::writePng(png.u8string(), rgb, width, height, error), "the PNG is written");
    {
        int w = 0, h = 0;
        std::vector<unsigned char> rgba;
        const bool decoded = TexturePack::decodePng(readFile(png), w, h, rgba);
        bool same = decoded && w == width && h == height;
        for (size_t i = 0; same && i < static_cast<size_t>(width * height); ++i)
            same = rgba[i * 4] == rgb[i * 3] && rgba[i * 4 + 1] == rgb[i * 3 + 1] &&
                   rgba[i * 4 + 2] == rgb[i * 3 + 2] && rgba[i * 4 + 3] == 255;
        check(same, "the PNG decodes to the same pixels, top row first");
    }
    check(!AppControl::writePng((folder / "none" / "x.png").u8string(), rgb, width, height, error),
          "a PNG that cannot be written fails");

    // the drawn-frame test
    const std::vector<unsigned char> cleared(static_cast<size_t>(width * height * 3), 77);
    check(!AppControl::looksDrawn(AppControl::measure(cleared, width, height)),
          "a cleared window does not look drawn");
    std::vector<unsigned char> speck = cleared;
    for (size_t i = 0; i < 300; ++i)
        speck[i] = static_cast<unsigned char>(i);
    check(!AppControl::looksDrawn(AppControl::measure(speck, width, height)),
          "a few odd pixels do not make a drawn frame");
    const AppControl::ImageStats drawn = AppControl::measure(rgb, width, height);
    check(AppControl::looksDrawn(drawn) && drawn.width == width && drawn.colours >= 16,
          "a scene looks drawn");
    check(AppControl::describe(drawn).find("200x150") == 0, "the description starts with the size");

    // the report and the doctor's folder checks
    check(AppControl::report({{"a", true, "fine"}}, stdout) == AppControl::kPass &&
              AppControl::report({{"a", true, "fine"}, {"b", false, "broken"}}, stdout) ==
                  AppControl::kFail,
          "one failed check fails the report");
    const fs::path home = fs::current_path();
    fs::current_path(folder);
    std::vector<AppControl::Check> checks = AppControl::environmentChecks();
    check(!checks.empty() && checks[0].name == "track folder" && !checks[0].ok,
          "without TrackFiles the doctor fails");
    fs::create_directories(folder / "TrackFiles");
    checks = AppControl::environmentChecks();
    check(!checks.empty() && checks[0].ok && !fs::exists(folder / "TrackFiles" / ".doctor-probe"),
          "with TrackFiles it passes and leaves nothing behind");
    fs::current_path(home);

    fs::remove_all(folder, ignored);
    std::printf("%s\n", failures == 0 ? "all ok" : "some checks FAILED");
    return failures == 0 ? 0 : 1;
}
