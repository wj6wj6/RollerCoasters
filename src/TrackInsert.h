/************************************************************************
     File:        TrackInsert.h

     Comment:     Adding a control point where the user clicks on the
                  track (the Add Pt button, Shift + D, Alt + D in Fly).

                  pick() finds the spot on the track nearest the mouse,
                  as a track parameter u. insertAt() puts a new control
                  point in there, between the two points around u, so
                  that the track still passes through the spot clicked:
                  the shape barely changes and the new point is where
                  the user expected it. Its tilt is blended from the two
                  points around it, and the train keeps its place on the
                  track.

                  None of this needs OpenGL or a window, so it is tested
                  in tests/TrackInsertCheck.cpp.
*************************************************************************/
#pragma once

#include "Track.H"
#include "TransformGizmo.h"

namespace TrackInsert
{
// How close (as a fraction of a segment) a new point may come to the
// points around it; a click nearer than that goes in at this distance.
constexpr float kEdgeMargin = 0.05f;

// The control point that, put in at track parameter u, keeps the track
// passing through where it is at u now.
ControlPoint pointAt(const CTrack &track, float u, TrackSplineType splineType);

// Puts that point into the track between the two points around u and
// moves the train so it stays where it was. Returns the new point's
// index, or -1 when the track has fewer than 4 points.
int insertAt(CTrack &track, float u, TrackSplineType splineType);

// Where the train is after a point goes in at u, in a loop that had
// `count` points: the segment u was on is split in two at u, and the
// segments after it move up by one.
float trainAfterInsert(float trainU, float u, int count);

// The track parameter of the spot on the track nearest the mouse on
// screen, if one is within `maxPixels`.
bool pick(const CTrack &track, TrackSplineType splineType, const TransformGizmo::Camera &camera,
          int mouseX, int mouseY, float maxPixels, float &u);
} // namespace TrackInsert
