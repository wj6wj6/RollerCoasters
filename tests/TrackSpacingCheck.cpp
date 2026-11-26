// Checks that the arc-length table places rail sleepers at even spacing along the track,
// even when the control points are spread very unevenly, and that the cached track
// mesh (TrackMesh) is laid out as the renderer draws it and is only worked out again
// when the track changes.
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

static Pnt3f vertexPosition(const TrackVertex &v)
{
    return Pnt3f(v.position[0], v.position[1], v.position[2]);
}

static bool checkMesh(const char *name, CTrack track, TrackSplineType type)
{
    bool ok = true;
    auto expect = [&](bool condition, const char *what) {
        if (!condition)
        {
            std::printf("%s mesh: %s FAILED\n", name, what);
            ok = false;
        }
    };

    TrackMesh mesh;
    expect(mesh.update(track, type), "first update builds");
    expect(!mesh.update(track, type), "same track keeps the mesh");

    const std::vector<TrackVertex> &v = mesh.vertices();
    const int sleepers = mesh.sleeperVertexCount();
    const int strip = mesh.railStripVertexCount();
    expect(sleepers > 0 && sleepers % 24 == 0, "six quads per sleeper");
    expect(strip > 0 && strip % 2 == 0, "rail strips hold vertex pairs");
    const int plates = mesh.plateVertexCount();
    expect(plates == sleepers * TrackMesh::kPlateBoxes,
           "a tie plate and two spikes under each rail on every sleeper");
    expect(static_cast<int>(v.size()) == sleepers + plates + TrackMesh::kRailStrips * strip,
           "sleepers, then the plates, then the rail strips");

    // Unit normals; every sleeper sits on the track (its box is centred
    // 0.03 below the centre line, between its bottom and top).
    for (const TrackVertex &vertex : v)
    {
        const float n =
            std::sqrt(vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] +
                      vertex.normal[2] * vertex.normal[2]);
        if (std::fabs(n - 1.0f) > 1e-3f)
        {
            expect(false, "unit normals");
            break;
        }
    }
    TrackArcTable table;
    table.build(track, type, 48);
    const int count = sleepers / 24;
    const float spacing = table.totalLength() / static_cast<float>(count);
    for (int i = 0; i < count; ++i)
    {
        Pnt3f centre(0, 0, 0);
        for (int k = 0; k < 24; ++k)
            centre = centre + vertexPosition(v[static_cast<size_t>(i * 24 + k)]) * (1.0f / 24.0f);
        const float u = table.parameterAtDistance(spacing * static_cast<float>(i));
        Pnt3f side, up;
        makeTrackFrame(track.evaluateTangent(u, type), side, up);
        const Pnt3f expected = track.evaluatePosition(u, type) + up * -0.03f;
        if (distanceBetween(centre, expected) > 1e-3f)
        {
            expect(false, "sleepers on the track");
            break;
        }
    }
    // The first rail strip, after the plates, starts at the left edge of
    // the left rail's head, on top, at u = 0 (1.1 out, the head 0.13 across
    // each way, its top 0.46 up).
    {
        Pnt3f side, up;
        makeTrackFrame(track.evaluateTangent(0.0f, type), side, up);
        const Pnt3f expected = track.evaluatePosition(0.0f, type) + side * -1.23f + up * 0.46f;
        expect(distanceBetween(vertexPosition(v[static_cast<size_t>(sleepers + plates)]),
                               expected) < 1e-3f,
               "left rail top corner");
    }

    // Moving a point or changing the spline works the mesh out again.
    track.points[1].pos.y += 2.0f;
    expect(mesh.update(track, type), "moved point rebuilds");
    expect(!mesh.update(track, type), "then keeps it");
    const TrackSplineType other =
        type == TRACK_SPLINE_LINEAR ? TRACK_SPLINE_BSPLINE : TRACK_SPLINE_LINEAR;
    expect(mesh.update(track, other), "spline change rebuilds");
    track.points.push_back(ControlPoint(Pnt3f(0, 9, 0)));
    expect(mesh.update(track, other), "added point rebuilds");

    std::printf("%s mesh: %d sleepers, %d vertices -> %s\n", name, count,
                static_cast<int>(v.size()), ok ? "ok" : "FAILED");
    return ok;
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

    ok &= checkMesh("default B-spline", defaultTrack, TRACK_SPLINE_BSPLINE);
    ok &= checkMesh("uneven cardinal", uneven, TRACK_SPLINE_CARDINAL);

    std::printf(ok ? "TrackSpacingCheck passed\n" : "TrackSpacingCheck FAILED\n");
    return ok ? 0 : 1;
}
