/************************************************************************
     File:        TrainModel.h

     Comment:     The roller coaster train: a blocky steam locomotive
                  (red boiler, brass bands, head lamp, green cab with a
                  driver) pulling five wooden mine carts, each following
                  the track on its own. One block-figure rider sits in
                  each cart with their arms in the air, clear of the
                  walls; the last cart has tail lamps. Built from boxes
                  and drawn in immediate mode, so it works with the fixed
                  pipeline and with the lighting shader (the lamps use its
                  glow input).
*************************************************************************/
#pragma once

#include "Track.H"

namespace TrainModel
{
// Draws the train with its first car at track.trainU. In the shadow pass
// only the shapes are drawn, without colours. `time` (seconds) animates
// the riders.
void draw(const CTrack &track, TrackSplineType splineType, bool shadowPass, float time);

// From the front of the locomotive's frame to the middle of the last cart,
// along the track.
float length();

// The track parameter `distance` further back along the track from u.
float parameterBehind(const CTrack &track, float u, float distance, TrackSplineType splineType);
} // namespace TrainModel
