/************************************************************************
     File:        TrainModel.h

     Comment:     The roller coaster train, as a block game would have it:
                  a furnace minecart in front (its fire lit, glowing) and
                  five minecarts behind it, each following the track on
                  its own, with a player sitting in each as players sit
                  in a minecart. Everything is in the game's pixels, a
                  sixteenth of the island's blocks, so the carts and the
                  riders are the size they would be in that world, and
                  their pictures are laid out the game's way: a texture
                  pack's player skins, minecart and furnace fit them (the
                  built-in ones are drawn in the same layout). The riders
                  react to the ride: hands forward over the rim on the
                  climbs, arms up on the drops (a third of them keep
                  holding on), leaning out on the curves, looking round
                  and now and then waving on the level. Drawn in
                  immediate mode, so it works with the fixed pipeline and
                  with the lighting shader (the furnace uses its glow).
*************************************************************************/
#pragma once

#include <memory>
#include <vector>

#include "Track.H"

namespace TexturePack
{
class Pack;
}

namespace TrainModel
{
// Draws the train with its first car at track.trainU. In the shadow pass
// only the shapes are drawn, without colours. `time` (seconds) animates
// the riders.
void draw(const CTrack &track, TrackSplineType splineType, bool shadowPass, float time);

// Uses a texture pack's player skins, minecart and furnace where it has
// them (nullptr: all built in). Takes effect at the next draw().
void setTexturePack(std::shared_ptr<const TexturePack::Pack> pack);

// Forgets the texture names, after the GL context they lived in is gone.
void releaseTextures();

// From the front of the lead car to the middle of the last car,
// along the track.
float length();

// How high above the rails the riders' heads reach (world units).
float riderTop();

// Where each vehicle is (x, y, z of each, at its wheels), the furnace cart
// first, as draw() puts them.
void vehiclePositions(const CTrack &track, TrackSplineType splineType, std::vector<float> &out);

// The track parameter `distance` further back along the track from u.
float parameterBehind(const CTrack &track, float u, float distance, TrackSplineType splineType);
} // namespace TrainModel
