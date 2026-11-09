#include "Track.H"
#include <windows.h>
#include <cmath>
#include <cstdio>

// Exercise the production serializer and parser against an isolated real file.
// Explicit checks also run in Release, where assert() would be disabled.
int main()
{
    char directory[MAX_PATH];
    char filename[MAX_PATH];
    const DWORD length = GetTempPathA(MAX_PATH, directory);
    if (length == 0 || length >= MAX_PATH || !GetTempFileNameA(directory, "rct", 0, filename))
        return 1;

    CTrack original;
    original.points[0].pos = Pnt3f(12.5f, -3.25f, 99.0f);
    original.points[0].orient = Pnt3f(1, 0, 0);
    original.points.push_back(ControlPoint(Pnt3f(-7, 2, 4), Pnt3f(0, 0, 1)));
    original.writePoints(filename);

    CTrack restored;
    restored.trainU = 2.5f;
    restored.readPoints(filename);
    bool pass = restored.points.size() == original.points.size() && restored.trainU == 0;
    if (pass)
    {
        for (size_t i = 0; i < original.points.size(); ++i)
        {
            const auto &a = original.points[i];
            const auto &b = restored.points[i];
            pass = pass && std::fabs(a.pos.x - b.pos.x) < .0001f &&
                   std::fabs(a.pos.y - b.pos.y) < .0001f && std::fabs(a.pos.z - b.pos.z) < .0001f &&
                   std::fabs(a.orient.x - b.orient.x) < .0001f &&
                   std::fabs(a.orient.y - b.orient.y) < .0001f &&
                   std::fabs(a.orient.z - b.orient.z) < .0001f;
        }
    }
    if (!DeleteFileA(filename))
        pass = false;
    std::printf("%s: track positions, orientations and reset survive a real file round trip\n",
                pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
