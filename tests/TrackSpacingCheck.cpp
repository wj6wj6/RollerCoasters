// Checks that the arc-length table places rail sleepers at even spacing along the track,
// even when the control points are spread very unevenly.
#include "Track.H"
#include "TrackRenderer.H"

#include <cmath>
#include <cstdio>

static float distanceBetween(const Pnt3f &a, const Pnt3f &b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Arc length between two parameters, measured independently with many small chords.
static float arcLengthBetween(const CTrack &track, TrackSplineType type, float u0, float u1)
{
    if (u1 < u0)
        u1 += static_cast<float>(track.points.size());
    const int steps = 400;
    float length = 0.0f;
    Pnt3f previous = track.evaluatePosition(u0, type);
    for (int i = 1; i <= steps; ++i)
    {
        const Pnt3f current = track.evaluatePosition(
            u0 + (u1 - u0) * static_cast<float>(i) / static_cast<float>(steps), type);
        length += distanceBetween(previous, current);
        previous = current;
    }
    return length;
}

static bool checkTrack(const char *name, const CTrack &track, TrackSplineType type)
{
    TrackArcTable table;
    table.build(track, type, 48);
    const float total = table.totalLength();
    const int count = 120;
    const float spacing = total / static_cast<float>(count);

    float shortest = 1e9f, longest = 0.0f;
    float previous = table.parameterAtDistance(0.0f);
    for (int i = 1; i <= count; ++i)
    {
        const float current = table.parameterAtDistance(spacing * static_cast<float>(i));
        const float gap = arcLengthBetween(track, type, previous, current);
        shortest = std::fmin(shortest, gap);
        longest = std::fmax(longest, gap);
        previous = current;
    }

    // The table is piecewise linear, so allow 2% between the shortest and longest gap.
    const bool even = (longest - shortest) <= 0.02f * spacing;
    std::printf("%s: length %.2f, spacing %.3f, gaps %.3f..%.3f -> %s\n", name, total, spacing,
                shortest, longest, even ? "ok" : "UNEVEN");
    return even;
}

int main()
{
    bool ok = true;

    CTrack defaultTrack;
    ok &= checkTrack("default B-spline", defaultTrack, TRACK_SPLINE_BSPLINE);
    ok &= checkTrack("default cardinal", defaultTrack, TRACK_SPLINE_CARDINAL);

    // Bunched-up points next to a long stretch: spacing by parameter u would be very uneven.
    CTrack uneven;
    uneven.points.clear();
    uneven.points.push_back(ControlPoint(Pnt3f(60, 5, 0)));
    uneven.points.push_back(ControlPoint(Pnt3f(62, 6, 4)));
    uneven.points.push_back(ControlPoint(Pnt3f(60, 7, 8)));
    uneven.points.push_back(ControlPoint(Pnt3f(-60, 20, 40)));
    uneven.points.push_back(ControlPoint(Pnt3f(-50, 5, -50)));
    ok &= checkTrack("uneven B-spline", uneven, TRACK_SPLINE_BSPLINE);
    ok &= checkTrack("uneven linear", uneven, TRACK_SPLINE_LINEAR);

    std::printf(ok ? "TrackSpacingCheck passed\n" : "TrackSpacingCheck FAILED\n");
    return ok ? 0 : 1;
}
