/************************************************************************
     File:        TrackSampling.cpp

     Comment:     Fixed and adaptive sampling of a closed track. See
                  TrackSampling.h.
*************************************************************************/

#include "TrackSampling.h"

#include <math.h>

namespace
{

const float kPi = 3.14159265358979f;
// Close samples on either side of a corner, as far apart as the fixed
// sampling's (TrackMesh::kUniformSamplesPerSegment).
const int kCornerSamplesPerSegment = 48;

Pnt3f subtract(const Pnt3f &a, const Pnt3f &b)
{
    return Pnt3f(a.x - b.x, a.y - b.y, a.z - b.z);
}

float dot(const Pnt3f &a, const Pnt3f &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(const Pnt3f &v)
{
    return sqrtf(dot(v, v));
}

// Distance from p to the straight piece from a to b.
float distanceToChord(const Pnt3f &p, const Pnt3f &a, const Pnt3f &b)
{
    const Pnt3f chord = subtract(b, a);
    const Pnt3f offset = subtract(p, a);
    const float chordSquared = dot(chord, chord);
    float t = chordSquared > 0.0f ? dot(offset, chord) / chordSquared : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return length(subtract(offset, chord * t));
}

// Angle in radians between two directions; 0 when either has no length.
float angleBetween(const Pnt3f &a, const Pnt3f &b)
{
    const float la = length(a), lb = length(b);
    if (la <= 1e-6f || lb <= 1e-6f)
        return 0.0f;
    float c = dot(a, b) / (la * lb);
    c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    return acosf(c);
}

void appendSample(std::vector<TrackArcSample> &out, float u, const Pnt3f &position)
{
    TrackArcSample sample;
    sample.u = u;
    sample.position = position;
    sample.distance =
        out.empty() ? 0.0f : out.back().distance + length(subtract(position, out.back().position));
    out.push_back(sample);
}

struct Subdivider
{
    const CTrack &track;
    TrackSplineType type;
    float maxError;
    float maxTurn; // radians
    int maxDepth;
    std::vector<TrackArcSample> &out;

    // The piece from a to b, whose start is already in `out`: adds the samples
    // inside it and its end.
    void piece(float a, float b, const Pnt3f &pa, const Pnt3f &pb, int depth)
    {
        if (depth < maxDepth)
        {
            // Four quarters of the piece. The curve strays from the chord
            // furthest somewhere in the piece; the three inner points catch
            // that even on an S bend, whose middle may sit on the chord.
            const float step = (b - a) * 0.25f;
            const Pnt3f q1 = track.evaluatePosition(a + step, type);
            const Pnt3f mid = track.evaluatePosition(a + 2.0f * step, type);
            const Pnt3f q3 = track.evaluatePosition(a + 3.0f * step, type);
            const float error =
                fmaxf(distanceToChord(mid, pa, pb),
                      fmaxf(distanceToChord(q1, pa, pb), distanceToChord(q3, pa, pb)));
            // How far the direction turns: the four quarter chords point the
            // way the curve goes at 1/8, 3/8, 5/8 and 7/8 of the piece, so
            // they see three quarters of the turn. Adding the turns up
            // (instead of comparing the two ends) counts an S bend too.
            const Pnt3f c0 = subtract(q1, pa), c1 = subtract(mid, q1), c2 = subtract(q3, mid),
                        c3 = subtract(pb, q3);
            const float turn =
                (angleBetween(c0, c1) + angleBetween(c1, c2) + angleBetween(c2, c3)) *
                (4.0f / 3.0f);
            if (error > maxError || turn > maxTurn)
            {
                const float m = a + 2.0f * step;
                piece(a, m, pa, mid, depth + 1);
                piece(m, b, mid, pb, depth + 1);
                return;
            }
        }
        appendSample(out, b, pb);
    }
};

} // namespace

std::vector<TrackArcSample> sampleTrackUniform(const CTrack &track, TrackSplineType splineType,
                                               int samplesPerSegment)
{
    std::vector<TrackArcSample> samples;
    const int pointCount = static_cast<int>(track.points.size());
    if (pointCount < 4 || samplesPerSegment < 1)
        return samples;

    const int sampleCount = pointCount * samplesPerSegment;
    samples.reserve(static_cast<size_t>(sampleCount) + 1);
    for (int i = 0; i <= sampleCount; ++i)
    {
        const float u = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
        appendSample(samples, u, track.evaluatePosition(u, splineType));
    }
    return samples;
}

std::vector<TrackArcSample> sampleTrackAdaptive(const CTrack &track, TrackSplineType splineType,
                                                const TrackSubdivision &limits)
{
    std::vector<TrackArcSample> samples;
    const int pointCount = static_cast<int>(track.points.size());
    if (pointCount < 4)
        return samples;

    Subdivider subdivider{track,
                          splineType,
                          fmaxf(limits.maxError, 1e-5f),
                          fmaxf(limits.maxTurnDegrees, 0.01f) * kPi / 180.0f,
                          limits.maxDepth < 0 ? 0 : limits.maxDepth,
                          samples};
    Pnt3f start = track.evaluatePosition(0.0f, splineType);
    appendSample(samples, 0.0f, start);
    for (int segment = 0; segment < pointCount; ++segment)
    {
        const float a = static_cast<float>(segment);
        const float b = static_cast<float>(segment + 1);
        const Pnt3f end = track.evaluatePosition(b, splineType);
        subdivider.piece(a, b, start, end, 0);
        start = end;
    }

    // Corners (a Linear track's control points): no piece is short enough
    // to round them, and the rails' frame there points half way between the
    // two directions. Close samples on either side keep that mitred frame to
    // a short stretch, as the fixed sampling does, instead of skewing the
    // whole straight next to it.
    const size_t n = samples.size(); // the last is the first again
    std::vector<bool> corner(n, false);
    bool anyCorner = false;
    for (size_t i = 0; i + 1 < n; ++i)
    {
        if (samples[i].u != floorf(samples[i].u))
            continue;
        const Pnt3f &before = samples[i > 0 ? i - 1 : n - 2].position;
        const Pnt3f &after = samples[i + 1].position;
        const Pnt3f &here = samples[i].position;
        if (angleBetween(subtract(here, before), subtract(after, here)) > 2.0f * subdivider.maxTurn)
            corner[i] = anyCorner = true;
    }
    if (anyCorner)
    {
        const float h = 1.0f / static_cast<float>(kCornerSamplesPerSegment);
        std::vector<TrackArcSample> withCorners;
        withCorners.reserve(n + 8);
        auto add = [&](float u) {
            appendSample(withCorners, u, track.evaluatePosition(u, splineType));
        };
        for (size_t i = 0; i < n; ++i)
        {
            const float u = samples[i].u;
            // just before this sample: it is a corner (the end is the start)
            const bool cornerHere = corner[i] || (i + 1 == n && corner[0]);
            if (i > 0 && cornerHere && u - h > samples[i - 1].u)
                add(u - h);
            add(u);
            if (i + 1 < n && corner[i] && u + h < samples[i + 1].u)
                add(u + h);
        }
        samples.swap(withCorners);
    }
    return samples;
}
