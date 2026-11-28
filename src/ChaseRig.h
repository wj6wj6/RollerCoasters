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
                  Follower moves it along with the train without jumps:
                  it looks along the stretch of track the camera is about
                  to pass over, so it comes down before a tunnel's roof or
                  an overhang instead of being pushed down under it, and
                  never nearer than the riders' height and a half. Dragging
                  turns it round the train (orbit()). TrainView samples the
                  track and answers where the blocks are. ChaseRigCheck
                  tests the numbers.

*************************************************************************/
#pragma once

#include <functional>
#include <vector>

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

// A point or a direction in the world (+y up).
struct Point
{
    float x, y, z;
};

// The view never tilts closer than this to straight up or down, so the
// picture does not turn over (gluLookAt with +y up).
const float kMaxTilt = 80.0f;

// Turning the Chase camera round the train, as a block game's third person
// view turns round the player: the camera and the point it looks at swing
// together round `pivot`, first `yawDegrees` about the vertical (positive:
// the view turns right, so the camera goes round to the train's left), then
// `pitchDegrees` about the view's level side axis (positive: the view looks
// up and the camera goes down). The pitch is cut short where the view would
// tilt past kMaxTilt. With both 0, nothing moves.
void orbit(const Point &pivot, Point &eye, Point &look, float yawDegrees, float pitchDegrees);

// A point of the track `back` units behind the front of the train along it
// (negative: ahead of the train), and the track's up there.
struct TrackSample
{
    float back;
    Point base;
    Point up;
};

// Is there a block at this point? (TrainView: the island's or the sea
// floor's, as they are drawn.)
using Solid = std::function<bool(const Point &)>;

// How high above base, along up, the camera can go: a block and a half
// short of the first block over it, no higher than upTo, and never lower
// than kLowest even when a block is right there.
const float kLowest = 1.0f;
float room(const Solid &solid, const Point &base, const Point &up, float upTo);

// Where the track is `back` behind the front of the train, between the
// samples (sorted by back, nearest the front first); the first or the last
// sample beyond them.
void sampleAt(const std::vector<TrackSample> &track, float back, Point &base, Point &up);

// The Chase camera as it follows the train: `behind` the middle of the last
// cart along the track, `height` above it, and the eye there.
struct Follow
{
    float behind;
    float height;
    Point eye;
};

// Follows the train from one frame to the next without jumps:
//  * It looks at the room over the track the camera is about to pass over
//    (from where it is up to the front of the train and beyond), each low
//    stretch counting for less the further ahead it is: allowed height =
//    room + kDescentSlope x distance. So it starts down well before a roof
//    and is low enough by the time it gets there, and nothing ever has to
//    push it down at once.
//  * Where the roof is low it comes nearer (fit()), but never nearer than
//    kNearestRiders riders' heights, so the view does not end up on the
//    back of the last cart.
//  * It moves like a critically damped spring, so it starts and stops
//    gently rather than with a jolt: up again slowly once out from under
//    the roof, nearer or further back smoothly, never faster than 8 units a
//    second.
// The faster the train, the further ahead it looks and the more gradually
// it lets a roof bring it down, so that however fast the roof comes it
// never has to come down faster than kDescentRate (and aims to take no
// more than kAimRate): at full speed it would otherwise reach a tunnel
// before it was down and be pushed down at the last moment.
// `track` must reach from aheadFor(speed) ahead of the train's front to the
// last cart plus the open placement's distance behind it, every
// spacingFor(speed) or closer.
class Follower
{
  public:
    // how far ahead of the train's front the samples should reach at least
    static constexpr float kAhead = 30.0f;
    // how much a low roof counts less for each unit further along the track
    // (at most: less when the train is fast)
    static constexpr float kDescentSlope = 0.35f;
    // the fastest the limit over the camera comes down, and the camera
    // aims to (units a second), however fast the train
    static constexpr float kDescentRate = 6.0f;
    static constexpr float kAimRate = 3.5f;
    // how far ahead to look, and how far apart the samples may be, for a
    // train going `speed` units a second
    static float aheadFor(float speed);
    static float spacingFor(float speed);
    // never nearer the last cart than this many riders' heights
    static constexpr float kNearestRiders = 1.5f;

    // The next update goes straight to the place, as when the Chase camera
    // is chosen.
    void reset()
    {
        m_valid = false;
    }
    // dt: seconds since the last update (a negative or a long one jumps
    // straight to the place). lastHead: the last rider's head; a block
    // between it and the camera brings the camera nearer. speed: how fast
    // the train goes along the track, units a second.
    Follow update(const std::vector<TrackSample> &track, const Solid &solid, const Train &train,
                  float fovY, float dt, const Point &lastHead, float speed);

  private:
    bool m_valid = false;
    float m_behind = 0.0f;
    float m_height = 0.0f;
    float m_behindSpeed = 0.0f; // units a second, carried from one update to the next
    float m_heightSpeed = 0.0f;
};

} // namespace ChaseRig
