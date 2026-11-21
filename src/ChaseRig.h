/************************************************************************
     File:        ChaseRig.h

     Comment:     Where the Chase camera sits, as plain numbers: a third
                  person view from behind and above the train that keeps
                  the whole train and the track ahead in view, the last
                  rider low in the picture. Worked out from the train's
                  length, the riders' height and the field of view, so it
                  follows the model rather than numbers made for one size
                  of it; where a tunnel or the rock leaves less room above
                  the track, fit() brings the camera lower and nearer while
                  keeping the rider low in the picture as far as it can.
                  TrainView places it on the track and keeps it out of the
                  blocks; ChaseRigCheck tests the numbers.

*************************************************************************/
#pragma once

namespace ChaseRig
{

// Seen from the side, with the middle of the last cart at the origin on
// the track and the train ahead along +x: the camera is `behind` units back
// along the track and `height` above it, and looks at the point
// `lookAhead` ahead of the last cart, `lookHeight` above the track.
struct Placement
{
    float behind;
    float height;
    float lookAhead;
    float lookHeight;
};

// The train: from the front of its first car to the middle of its last,
// along the track, and the height above the rails of the tallest rider's
// head (both in world units); the vertical field of view in degrees.
struct Train
{
    float length;
    float riderTop;
};

// How far below the middle of the picture the last rider's head shows, as
// a part of half the picture's height (0 at the middle, 1 at the bottom
// edge, negative above the middle).
float headBelowCentre(const Placement &p, const Train &train, float fovY);

// How far below the middle of the picture the front of the train shows, in
// the same measure: above -1 and below 1 means it is in the picture.
float frontBelowCentre(const Placement &p, const Train &train, float fovY);

// Where the camera goes with room to spare above the track: the last
// rider's head about halfway between the middle of the picture and its
// bottom edge, the train ahead below the middle with the track beyond it.
Placement plan(const Train &train, float fovY);

// The same with the camera no higher than `maxHeight` above the track (a
// tunnel's roof, an overhang): lower, and nearer so the rider still shows
// below the middle where that is possible; never nearer than `minBehind`.
Placement fit(const Placement &open, float maxHeight, float minBehind, const Train &train,
              float fovY);

// One step of following a target smoothly: moves `current` towards
// `target` by the share of the way that `rate` (per second) covers in `dt`
// seconds, never past it.
float approach(float current, float target, float dt, float rate);

} // namespace ChaseRig
