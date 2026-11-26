// Checks adaptive subdivision of the track (src/TrackSampling.*): every straight piece stays
// within the error and turn limits of the real curve, bends get more samples than straights,
// the samples cover the closed loop in order, a Linear track keeps its corners, and the track
// mesh switches between adaptive and fixed rail samples.
#include "Track.H"
#include "TrackRenderer.H"
#include "TrackSampling.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void expect(bool ok, const char *name, const char *what)
{
    if (!ok)
    {
        std::printf("%s: %s FAILED\n", name, what);
        ++failures;
    }
}

Pnt3f minus(const Pnt3f &a, const Pnt3f &b)
{
    return Pnt3f(a.x - b.x, a.y - b.y, a.z - b.z);
}

float dot(const Pnt3f &a, const Pnt3f &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(const Pnt3f &v)
{
    return std::sqrt(dot(v, v));
}

float distanceToChord(const Pnt3f &p, const Pnt3f &a, const Pnt3f &b)
{
    const Pnt3f chord = minus(b, a), offset = minus(p, a);
    const float c2 = dot(chord, chord);
    float t = c2 > 0.0f ? dot(offset, chord) / c2 : 0.0f;
    t = std::fmax(0.0f, std::fmin(1.0f, t));
    return length(minus(offset, chord * t));
}

float angleDegrees(const Pnt3f &a, const Pnt3f &b)
{
    const float c = dot(a, b) / (length(a) * length(b));
    return std::acos(std::fmax(-1.0f, std::fmin(1.0f, c))) * 180.0f / 3.14159265f;
}

// A stadium: two long straights joined by two tight half circles, flat.
CTrack stadium()
{
    CTrack track;
    track.points.clear();
    const float halfStraight = 60.0f, radius = 12.0f;
    for (int i = 0; i <= 6; ++i) // straight along +x at z = -radius
        track.points.push_back(
            ControlPoint(Pnt3f(-halfStraight + 2.0f * halfStraight * i / 6.0f, 5.0f, -radius)));
    for (int i = 1; i < 6; ++i) // bend round the +x end
    {
        const float a = -1.5707963f + 3.14159265f * i / 6.0f;
        track.points.push_back(
            ControlPoint(Pnt3f(halfStraight + radius * std::cos(a), 5.0f, radius * std::sin(a))));
    }
    for (int i = 0; i <= 6; ++i) // back along -x at z = +radius
        track.points.push_back(
            ControlPoint(Pnt3f(halfStraight - 2.0f * halfStraight * i / 6.0f, 5.0f, radius)));
    for (int i = 1; i < 6; ++i) // bend round the -x end
    {
        const float a = 1.5707963f + 3.14159265f * i / 6.0f;
        track.points.push_back(
            ControlPoint(Pnt3f(-halfStraight + radius * std::cos(a), 5.0f, radius * std::sin(a))));
    }
    return track;
}

// The samples run from u = 0 to the end of the loop in order, their distances add up their
// pieces, and the total is close to the curve's length measured finely.
void checkCoverage(const char *name, const CTrack &track, TrackSplineType type,
                   const std::vector<TrackArcSample> &samples)
{
    const float period = static_cast<float>(track.points.size());
    expect(samples.size() >= 2, name, "has samples");
    if (samples.size() < 2)
        return;
    expect(samples.front().u == 0.0f && samples.back().u == period, name, "u = 0 to the end");
    bool ordered = true, added = true, onCurve = true;
    for (size_t i = 1; i < samples.size(); ++i)
    {
        ordered &= samples[i].u > samples[i - 1].u;
        const float piece = length(minus(samples[i].position, samples[i - 1].position));
        added &= std::fabs(samples[i].distance - samples[i - 1].distance - piece) < 1e-3f;
        onCurve &=
            length(minus(samples[i].position, track.evaluatePosition(samples[i].u, type))) < 1e-4f;
    }
    expect(ordered, name, "u keeps going up");
    expect(added, name, "distances add up the pieces");
    expect(onCurve, name, "samples on the curve");

    const std::vector<TrackArcSample> fine = sampleTrackUniform(track, type, 400);
    const float exact = fine.back().distance;
    const float total = samples.back().distance;
    expect(std::fabs(total - exact) <= 0.002f * exact, name, "length within 0.2%");
}

// Measure every piece against the curve: how far the curve strays from the piece, and how
// far it turns along it (from just inside one end to just inside the other). Pieces already
// halved as often as allowed are left out: a cusp may turn sharply in the shortest piece.
void measurePieces(const CTrack &track, TrackSplineType type,
                   const std::vector<TrackArcSample> &samples, float &worstError, float &worstTurn,
                   int &shortest)
{
    worstError = 0.0f;
    worstTurn = 0.0f;
    shortest = 0;
    const float smallest = std::ldexp(1.0f, -TrackSubdivision().maxDepth) * 1.01f;
    for (size_t i = 1; i < samples.size(); ++i)
    {
        const float a = samples[i - 1].u, b = samples[i].u;
        if (b - a <= smallest)
        {
            ++shortest;
            continue;
        }
        const Pnt3f &pa = samples[i - 1].position, &pb = samples[i].position;
        const int steps = 32;
        for (int k = 1; k < steps; ++k)
        {
            const Pnt3f p = track.evaluatePosition(a + (b - a) * k / steps, type);
            worstError = std::fmax(worstError, distanceToChord(p, pa, pb));
        }
        const float h = (b - a) / 64.0f;
        const Pnt3f startDir = minus(track.evaluatePosition(a + h, type), pa);
        const Pnt3f endDir = minus(pb, track.evaluatePosition(b - h, type));
        worstTurn = std::fmax(worstTurn, angleDegrees(startDir, endDir));
    }
}

void checkSmooth(const char *name, const CTrack &track, TrackSplineType type)
{
    const TrackSubdivision limits;
    const std::vector<TrackArcSample> samples = sampleTrackAdaptive(track, type, limits);
    checkCoverage(name, track, type, samples);

    float worstError = 0.0f, worstTurn = 0.0f;
    int shortest = 0;
    measurePieces(track, type, samples, worstError, worstTurn, shortest);
    // The tests inside a piece look at its quarter points, so the true worst may be a little
    // more than the limit; well under twice it.
    expect(worstError <= 1.5f * limits.maxError, name, "pieces close to the curve");
    expect(worstTurn <= 1.5f * limits.maxTurnDegrees, name, "pieces turn little");

    const int uniform =
        static_cast<int>(track.points.size()) * TrackMesh::kUniformSamplesPerSegment + 1;
    std::printf("%s: %d adaptive samples (fixed %d), worst %.4f units, %.2f degrees, %d "
                "shortest pieces\n",
                name, static_cast<int>(samples.size()), uniform, static_cast<double>(worstError),
                static_cast<double>(worstTurn), shortest);
}

// On the stadium the bends get many more samples per unit of length than the straights.
void checkDensity()
{
    const char *name = "stadium";
    const CTrack track = stadium();
    const std::vector<TrackArcSample> samples = sampleTrackAdaptive(track, TRACK_SPLINE_BSPLINE);
    float straightLength = 0.0f, bendLength = 0.0f;
    int straightPieces = 0, bendPieces = 0;
    for (size_t i = 1; i < samples.size(); ++i)
    {
        const Pnt3f mid = (samples[i].position + samples[i - 1].position) * 0.5f;
        const float piece = samples[i].distance - samples[i - 1].distance;
        // the straights well clear of the bends
        if (std::fabs(mid.x) < 40.0f)
        {
            straightLength += piece;
            ++straightPieces;
        }
        else if (std::fabs(mid.x) > 62.0f)
        {
            bendLength += piece;
            ++bendPieces;
        }
    }
    const float straightDensity = straightPieces / straightLength;
    const float bendDensity = bendPieces / bendLength;
    std::printf("%s: %.3f samples a unit on the straights, %.3f in the bends\n", name,
                static_cast<double>(straightDensity), static_cast<double>(bendDensity));
    expect(bendDensity > 5.0f * straightDensity, name, "bends denser than straights");
    const int uniform =
        static_cast<int>(track.points.size()) * TrackMesh::kUniformSamplesPerSegment + 1;
    expect(static_cast<int>(samples.size()) * 3 < uniform, name, "far fewer than the fixed way");
}

// Linear: one piece per segment plus a close sample either side of each corner; every control
// point is a sample, and the stretches between are not cut up.
void checkLinear()
{
    const char *name = "linear square";
    CTrack track;
    track.points.clear();
    track.points.push_back(ControlPoint(Pnt3f(-30, 5, -30)));
    track.points.push_back(ControlPoint(Pnt3f(30, 5, -30)));
    track.points.push_back(ControlPoint(Pnt3f(30, 5, 30)));
    track.points.push_back(ControlPoint(Pnt3f(-30, 5, 30)));
    const std::vector<TrackArcSample> samples = sampleTrackAdaptive(track, TRACK_SPLINE_LINEAR);
    checkCoverage(name, track, TRACK_SPLINE_LINEAR, samples);
    // 4 corners and the end, with a sample just before and after each corner
    expect(samples.size() == 13, name, "corners and their neighbours only");
    int corners = 0;
    for (const TrackArcSample &s : samples)
        if (s.u == std::floor(s.u) && s.u < 4.0f)
        {
            const Pnt3f &p = track.points[static_cast<size_t>(s.u)].pos;
            corners += length(minus(s.position, p)) < 1e-4f ? 1 : 0;
        }
    expect(corners == 4, name, "every control point is a sample");
    const float h = 1.0f / TrackMesh::kUniformSamplesPerSegment;
    expect(std::fabs(samples[1].u - h) < 1e-5f && std::fabs(samples[11].u - (4.0f - h)) < 1e-5f,
           name, "close samples either side of the corner at the seam");
}

// The mesh takes its rails from adaptive or fixed samples, and switching rebuilds it.
void checkMesh()
{
    const char *name = "mesh";
    const CTrack track = stadium();
    TrackMesh mesh;
    expect(mesh.update(track, TRACK_SPLINE_BSPLINE, true), name, "first update builds");
    const size_t adaptive = mesh.railSamples().size();
    expect(mesh.railStripVertexCount() == static_cast<int>(adaptive) * 2, name,
           "a vertex pair per adaptive sample");
    expect(!mesh.update(track, TRACK_SPLINE_BSPLINE, true), name, "same keeps it");
    expect(mesh.update(track, TRACK_SPLINE_BSPLINE, false), name, "switching off rebuilds");
    const size_t fixed = mesh.railSamples().size();
    expect(static_cast<int>(fixed) == mesh.uniformSampleCount(), name, "fixed samples when off");
    expect(mesh.railStripVertexCount() == static_cast<int>(fixed) * 2, name,
           "a vertex pair per fixed sample");
    expect(mesh.update(track, TRACK_SPLINE_BSPLINE, true), name, "switching on rebuilds");
    expect(mesh.railSamples().size() == adaptive, name, "back to the adaptive samples");
    std::printf("%s: rails from %d adaptive or %d fixed samples\n", name,
                static_cast<int>(adaptive), static_cast<int>(fixed));
}
} // namespace

int main()
{
    CTrack defaultTrack;
    checkSmooth("default B-spline", defaultTrack, TRACK_SPLINE_BSPLINE);
    checkSmooth("default cardinal", defaultTrack, TRACK_SPLINE_CARDINAL);
    const CTrack oval = stadium();
    checkSmooth("stadium B-spline", oval, TRACK_SPLINE_BSPLINE);
    checkSmooth("stadium cardinal", oval, TRACK_SPLINE_CARDINAL);

    // a steep, twisting climb with bunched-up points next to a long stretch
    CTrack uneven;
    uneven.points.clear();
    uneven.points.push_back(ControlPoint(Pnt3f(60, 5, 0)));
    uneven.points.push_back(ControlPoint(Pnt3f(62, 6, 4)));
    uneven.points.push_back(ControlPoint(Pnt3f(60, 30, 8)));
    uneven.points.push_back(ControlPoint(Pnt3f(-60, 20, 40)));
    uneven.points.push_back(ControlPoint(Pnt3f(-50, 5, -50)));
    checkSmooth("uneven B-spline", uneven, TRACK_SPLINE_BSPLINE);
    checkSmooth("uneven cardinal", uneven, TRACK_SPLINE_CARDINAL);

    checkDensity();
    checkLinear();
    checkMesh();

    std::printf(failures == 0 ? "TrackSubdivisionCheck passed\n"
                              : "TrackSubdivisionCheck FAILED\n");
    return failures == 0 ? 0 : 1;
}
