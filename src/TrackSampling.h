/************************************************************************
     File:        TrackSampling.h

     Comment:     Where along a closed track to take the points that the
                  rails are built from.

                  The fixed way takes the same number of points in every
                  control-point segment, so a long straight gets as many as
                  a hairpin. Adaptive subdivision starts from one piece per
                  segment and halves a piece again only while it is not yet
                  straight enough: while its middle strays from the straight
                  line between its ends by more than maxError, or the
                  direction of travel turns by more than maxTurnDegrees
                  across it. Bends end up with many short pieces and
                  straights with a few long ones.

                  No OpenGL here: tests/TrackSubdivisionCheck.cpp checks it.
*************************************************************************/
#pragma once

#include <vector>

#include "Track.H"

// One point along the track.
struct TrackArcSample
{
    float u;        // spline parameter
    float distance; // arc length from u = 0 to here (sum of the straight pieces)
    Pnt3f position; // curve position at u
};

// How straight a piece must be before adaptive subdivision stops halving it.
struct TrackSubdivision
{
    // The rails are 0.24 wide, so 0.02 is a twelfth of a rail: out of sight.
    // On the Grand Tour (B-spline) these take about 1700 samples instead of
    // the fixed way's 2977, and turn less in the tightest bends.
    float maxError = 0.02f;      // world units the middle of a piece may stray from its chord
    float maxTurnDegrees = 2.0f; // the most the direction may turn across one piece
    int maxDepth = 9;            // halvings of one segment at most (512 pieces)
};

// samplesPerSegment points per control-point segment, evenly in u. The first
// sample is u = 0 and the last the end of the loop (the same place).
std::vector<TrackArcSample> sampleTrackUniform(const CTrack &track, TrackSplineType splineType,
                                               int samplesPerSegment);

// Points where the track needs them (see the comment at the top). Every
// control-point segment starts with a sample at its start, so the corners of
// a Linear track are kept exactly. First and last sample as above.
std::vector<TrackArcSample> sampleTrackAdaptive(
    const CTrack &track, TrackSplineType splineType,
    const TrackSubdivision &limits = TrackSubdivision());
