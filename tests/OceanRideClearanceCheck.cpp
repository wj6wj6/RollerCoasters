// Checks that nothing in the sea-floor scenery touches the underwater demo ride: no block and
// no plant may come within reach of the train anywhere along it, for any of the spline types.
// The path is sampled with the real CTrack, independently of the code that carves the scenery.
#include "OceanWorld.h"
#include "Track.H"

#include <cstdio>
#include <vector>

int main()
{
    CTrack track;
    const std::vector<float> xyz = OceanWorld::demoTrack();
    track.points.clear();
    for (size_t i = 0; i + 2 < xyz.size(); i += 3)
        track.points.push_back(ControlPoint(Pnt3f(xyz[i], xyz[i + 1], xyz[i + 2])));

    const struct
    {
        const char *name;
        TrackSplineType type;
    } splines[] = {{"linear", TRACK_SPLINE_LINEAR},
                   {"cardinal", TRACK_SPLINE_CARDINAL},
                   {"b-spline", TRACK_SPLINE_BSPLINE}};

    bool clear = true;
    for (const auto &spline : splines)
    {
        const int samplesPerSegment = 200;
        const int count = static_cast<int>(track.points.size()) * samplesPerSegment;
        std::vector<float> path;
        path.reserve(static_cast<size_t>(count) * 3);
        for (int i = 0; i < count; ++i)
        {
            const Pnt3f p = track.evaluatePosition(
                static_cast<float>(i) / static_cast<float>(samplesPerSegment), spline.type);
            path.push_back(p.x);
            path.push_back(p.y);
            path.push_back(p.z);
        }
        const int blocked = OceanWorld::rideObstructions(path);
        std::printf("%s: %d of %d points obstructed -> %s\n", spline.name, blocked, count,
                    blocked == 0 ? "ok" : "BLOCKED");
        clear = clear && blocked == 0;
    }

    // A path straight through the sea bed must be reported, or the check proves nothing.
    std::vector<float> buried;
    for (int i = 0; i < 50; ++i)
    {
        buried.push_back(200.0f + static_cast<float>(i));
        buried.push_back(-44.0f);
        buried.push_back(250.0f);
    }
    const int buriedHits = OceanWorld::rideObstructions(buried);
    std::printf("buried path: %d of 50 points obstructed -> %s\n", buriedHits,
                buriedHits == 50 ? "ok" : "NOT DETECTED");

    return (clear && buriedHits == 50) ? 0 : 1;
}
