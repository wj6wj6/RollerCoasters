#include "Track.H"
#include "Utilities/Utf8File.H"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

// Exercise the production serializer and parser against isolated real files.
// Explicit checks also run in Release, where assert() would be disabled.

namespace
{
bool writeText(const char *filename, const char *text)
{
    FILE *fp = openUtf8(filename, "w");
    if (!fp)
        return false;
    const bool ok = std::fputs(text, fp) >= 0;
    return std::fclose(fp) == 0 && ok;
}

bool samePos(const Pnt3f &a, const Pnt3f &b)
{
    return std::fabs(a.x - b.x) < .0001f && std::fabs(a.y - b.y) < .0001f &&
           std::fabs(a.z - b.z) < .0001f;
}

bool samePoints(const CTrack &a, const CTrack &b)
{
    if (a.points.size() != b.points.size())
        return false;
    for (size_t i = 0; i < a.points.size(); ++i)
    {
        if (!samePos(a.points[i].pos, b.points[i].pos) ||
            !samePos(a.points[i].orient, b.points[i].orient))
            return false;
    }
    return true;
}

bool report(bool pass, const char *what)
{
    std::printf("%s: %s\n", pass ? "PASS" : "FAIL", what);
    return pass;
}

// A file readPoints() must refuse: the track, train position and point count
// stay as they were, and it says what is wrong (the menu shows that).
bool rejects(const char *filename, const char *text, const char *what)
{
    CTrack track;
    track.points[1].pos = Pnt3f(1, 2, 3);
    track.points.push_back(ControlPoint(Pnt3f(4, 5, 6), Pnt3f(0, 0, 1)));
    track.trainU = 1.5f;
    const CTrack before = track;

    bool pass = writeText(filename, text);
    if (pass)
    {
        std::string error;
        pass = !track.readPoints(filename, error) && !error.empty() && samePoints(track, before) &&
               track.trainU == 1.5f;
        std::printf("      (%s)\n", error.c_str());
    }
    return report(pass, what);
}
} // namespace

int main()
{
    // Paths are UTF-8, as in the program: the temporary folder may have a
    // Chinese user name in it.
    namespace fs = std::filesystem;
    std::error_code fsError;
    const fs::path folder = fs::temp_directory_path(fsError) / "rollercoasters-track-check";
    fs::remove_all(folder, fsError);
    if (!fs::create_directories(folder, fsError))
        return 1;
    const std::string path = (folder / "track.txt").u8string();
    const char *filename = path.c_str();

    bool pass = true;

    {
        CTrack original;
        original.points[0].pos = Pnt3f(12.5f, -3.25f, 99.0f);
        original.points[0].orient = Pnt3f(1, 0, 0);
        original.points.push_back(ControlPoint(Pnt3f(-7, 2, 4), Pnt3f(0, 0, 1)));
        std::string error;
        const bool written = original.writePoints(filename, error);

        CTrack restored;
        restored.trainU = 2.5f;
        const bool read = restored.readPoints(filename, error);
        pass &= report(written && read && error.empty() && samePoints(original, restored) &&
                           restored.trainU == 0,
                       "track positions, orientations and reset survive a real file round trip");
    }

    {
        // Older files list only x y z; orientation defaults to straight up.
        CTrack track;
        bool ok = writeText(filename, "4\n1 2 3\n4 5 6\n7 8 9\n10 11 12 # comment\n");
        if (ok)
        {
            std::string error;
            ok = track.readPoints(filename, error) && track.points.size() == 4 &&
                 samePos(track.points[3].pos, Pnt3f(10, 11, 12)) &&
                 samePos(track.points[0].orient, Pnt3f(0, 1, 0));
        }
        pass &= report(ok, "old files with only x y z per line still load");
    }

    pass &= rejects(filename, "5\n1 2 3\n4 5 6\n",
                    "a file with fewer point lines than declared leaves the track unchanged");
    pass &= rejects(filename, "4\n1 2 3\n4 5 6\n7 8\n10 11 12\n",
                    "a line without x y z leaves the track unchanged");
    pass &= rejects(filename, "4\n1 2 3\n\n7 8 9\n10 11 12\n",
                    "a blank point line leaves the track unchanged");
    pass &= rejects(filename, "3\n1 2 3\n4 5 6\n7 8 9\n",
                    "an illegal point count leaves the track unchanged");
    pass &= rejects(filename, "", "an empty file leaves the track unchanged");
    {
        CTrack track;
        std::string error;
        const bool refused = !track.readPoints("no such folder/no such track.txt", error) &&
                             !error.empty() && track.points.size() == 4;
        pass &= report(refused, "a missing file is refused with a message");
        error.clear();
        pass &= report(!track.writePoints("no such folder/track.txt", error) && !error.empty(),
                       "saving where there is no folder fails with a message");
    }

    {
        // a track named in Chinese (UTF-8 bytes for "roller coaster"), as one
        // dropped on the window or listed in TrackFiles is
        const std::string chinese =
            (folder / fs::u8path("\xe9\x9b\xb2\xe9\x9c\x84\xe9\xa3\x9b\xe8\xbb\x8a.txt"))
                .u8string();
        CTrack original;
        original.points[1].pos = Pnt3f(3, 4, 5);
        CTrack restored;
        std::string error;
        const bool ok = original.writePoints(chinese.c_str(), error) &&
                        restored.readPoints(chinese.c_str(), error) &&
                        samePoints(original, restored);
        pass &= report(ok, "a track with a Chinese file name saves and loads");
    }

    if (fs::remove_all(folder, fsError) == 0 || fsError)
        pass = report(false, "temporary track files removed");
    return pass ? 0 : 1;
}
