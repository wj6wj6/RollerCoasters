#include "Track.H"
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <windows.h>
#include <cmath>
#include <cstdio>

// Exercise the production serializer and parser against isolated real files.
// Explicit checks also run in Release, where assert() would be disabled.

namespace
{
int alertsShown = 0;

// readPoints() reports problems with the real fl_alert(), which runs its own
// modal loop. This timer runs inside that loop, counts the dialog and closes it.
void closeAlert(void *)
{
    if (Fl_Window *modal = Fl::modal())
    {
        modal->hide();
        ++alertsShown;
    }
    Fl::repeat_timeout(0.02, closeAlert);
}

bool writeText(const char *filename, const char *text)
{
    FILE *fp = nullptr;
    if (fopen_s(&fp, filename, "w") != 0 || !fp)
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
// stay as they were, and exactly one alert is shown.
bool rejects(const char *filename, const char *text, const char *what)
{
    CTrack track;
    track.points[1].pos = Pnt3f(1, 2, 3);
    track.points.push_back(ControlPoint(Pnt3f(4, 5, 6), Pnt3f(0, 0, 1)));
    track.trainU = 1.5f;
    const CTrack before = track;

    alertsShown = 0;
    bool pass = writeText(filename, text);
    if (pass)
    {
        track.readPoints(filename);
        pass = samePoints(track, before) && track.trainU == 1.5f && alertsShown == 1;
    }
    return report(pass, what);
}
} // namespace

int main()
{
    char directory[MAX_PATH];
    char filename[MAX_PATH];
    const DWORD length = GetTempPathA(MAX_PATH, directory);
    if (length == 0 || length >= MAX_PATH || !GetTempFileNameA(directory, "rct", 0, filename))
        return 1;

    Fl::add_timeout(0.02, closeAlert);
    bool pass = true;

    {
        CTrack original;
        original.points[0].pos = Pnt3f(12.5f, -3.25f, 99.0f);
        original.points[0].orient = Pnt3f(1, 0, 0);
        original.points.push_back(ControlPoint(Pnt3f(-7, 2, 4), Pnt3f(0, 0, 1)));
        original.writePoints(filename);

        CTrack restored;
        restored.trainU = 2.5f;
        alertsShown = 0;
        restored.readPoints(filename);
        pass &= report(samePoints(original, restored) && restored.trainU == 0 && alertsShown == 0,
                       "track positions, orientations and reset survive a real file round trip");
    }

    {
        // Older files list only x y z; orientation defaults to straight up.
        CTrack track;
        alertsShown = 0;
        bool ok = writeText(filename, "4\n1 2 3\n4 5 6\n7 8 9\n10 11 12 # comment\n");
        if (ok)
        {
            track.readPoints(filename);
            ok = track.points.size() == 4 && alertsShown == 0 &&
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

    Fl::remove_timeout(closeAlert);
    if (!DeleteFileA(filename))
        pass = report(false, "temporary track file removed");
    return pass ? 0 : 1;
}
