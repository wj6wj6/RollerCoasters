/************************************************************************
     File:        TrainModel.h

     Comment:     The roller coaster train: a classic coaster train in the
                  block style, a lead car with a stepped nose, a chrome
                  bumper, a head lamp and a gold star, and five cars
                  behind it, each following the track on its own: deep
                  red bodies with a cream band and gold trim on dark
                  running gear, black padded seats with headrests and a
                  chrome lap bar; the last car has tail lamps. Two
                  block-figure riders sit side by side in each car and
                  react to the ride: hands on the bar on the climbs, arms
                  up on the drops (a third of them keep holding on),
                  leaning out on the curves, looking round on the level
                  and now and then waving. Built from boxes and drawn in
                  immediate mode, so it works with the fixed pipeline and
                  with the lighting shader (the lamps use its glow input).
*************************************************************************/
#pragma once

#include "Track.H"

namespace TrainModel
{
// Draws the train with its first car at track.trainU. In the shadow pass
// only the shapes are drawn, without colours. `time` (seconds) animates
// the riders.
void draw(const CTrack &track, TrackSplineType splineType, bool shadowPass, float time);

// From the front of the lead car to the middle of the last car,
// along the track.
float length();

// The track parameter `distance` further back along the track from u.
float parameterBehind(const CTrack &track, float u, float distance, TrackSplineType splineType);
} // namespace TrainModel
