/************************************************************************
     File:        TrainModel.h

     Comment:     The roller coaster train: three blocky cars coupled
                  together, each following the track on its own, with
                  wheels, seats, lap bars, block-figure riders with their
                  arms in the air, head lamps on the first car and tail lamps on the
                  last. Built from boxes and drawn in immediate mode, so
                  it works with the fixed pipeline and with the lighting
                  shader (the lamps use its glow input).
*************************************************************************/
#pragma once

#include "Track.H"

namespace TrainModel
{
// Draws the train with its first car at track.trainU. In the shadow pass
// only the shapes are drawn, without colours. `time` (seconds) animates
// the riders.
void draw(const CTrack &track, TrackSplineType splineType, bool shadowPass, float time);
} // namespace TrainModel
