/************************************************************************
     File:        ChaseRig.cpp

     Comment:     See ChaseRig.h.

*************************************************************************/

#include "ChaseRig.h"

#include <math.h>

namespace ChaseRig
{
namespace
{
const float kDegrees = 57.2957795f;
// where the last rider's head is aimed for: halfway down the lower half
const float kHeadTarget = 0.55f;
// it must at least stay in the lower part of the picture
const float kHeadLowest = 0.25f;

// The angle below the camera's line of sight of a point d ahead of the
// camera and y above the track, as a part of half the field of view.
float belowCentre(const Placement &p, float d, float y, float fovY)
{
    const float sight = atan2f(p.lookHeight - p.height, p.behind + p.lookAhead);
    const float toPoint = atan2f(y - p.height, d);
    return (sight - toPoint) * kDegrees / (0.5f * fovY);
}
} // namespace

float headBelowCentre(const Placement &p, const Train &train, float fovY)
{
    return belowCentre(p, p.behind, train.riderTop, fovY);
}

float frontBelowCentre(const Placement &p, const Train &train, float fovY)
{
    return belowCentre(p, p.behind + train.length, train.riderTop, fovY);
}

Placement plan(const Train &train, float fovY)
{
    Placement p;
    // back from the last cart by a little more than a rider is tall and a
    // share of the train, looking at a point beyond its front, low down
    p.behind = 1.8f * train.riderTop + 0.2f * train.length;
    p.lookAhead = train.length + 0.5f * train.length + 6.0f;
    p.lookHeight = 0.6f * train.riderTop;
    // then up until the last rider's head is where it is aimed for (the
    // higher the camera, the lower in the picture the head); if no sensible
    // height does it (a very wide view), a little nearer and again
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        float lo = train.riderTop, hi = 4.0f * train.riderTop + p.behind;
        p.height = hi;
        if (headBelowCentre(p, train, fovY) >= kHeadTarget)
        {
            for (int i = 0; i < 40; ++i)
            {
                p.height = 0.5f * (lo + hi);
                if (headBelowCentre(p, train, fovY) < kHeadTarget)
                    lo = p.height;
                else
                    hi = p.height;
            }
            p.height = hi;
            break;
        }
        p.behind *= 0.85f;
    }
    return p;
}

Placement fit(const Placement &open, float maxHeight, float minBehind, const Train &train,
              float fovY)
{
    if (open.height <= maxHeight)
        return open;
    Placement p = open;
    p.height = maxHeight;
    // nearer, until the head is at least in the lower part of the picture
    // again (the nearer, the lower it shows), or as near as allowed
    if (headBelowCentre(p, train, fovY) >= kHeadLowest)
        return p;
    float lo = minBehind, hi = open.behind;
    for (int i = 0; i < 40; ++i)
    {
        p.behind = 0.5f * (lo + hi);
        if (headBelowCentre(p, train, fovY) >= kHeadLowest)
            lo = p.behind;
        else
            hi = p.behind;
    }
    p.behind = lo;
    return p;
}

float approach(float current, float target, float dt, float rate)
{
    if (dt <= 0.0f)
        return current;
    const float share = 1.0f - expf(-rate * dt);
    return current + (target - current) * share;
}

namespace
{
Point sub(const Point &a, const Point &b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Point add(const Point &a, const Point &b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Point cross(const Point &a, const Point &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(const Point &a, const Point &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
// v turned by `radians` round the unit axis k (right-handed)
Point rotate(const Point &v, const Point &k, float radians)
{
    const float c = cosf(radians), s = sinf(radians);
    const Point kv = cross(k, v);
    const float along = dot(k, v) * (1.0f - c);
    return {v.x * c + kv.x * s + k.x * along, v.y * c + kv.y * s + k.y * along,
            v.z * c + kv.z * s + k.z * along};
}
} // namespace

void orbit(const Point &pivot, Point &eye, Point &look, float yawDegrees, float pitchDegrees)
{
    Point fromPivot = sub(eye, pivot);
    Point toLook = sub(look, pivot);
    // round the vertical: turning about +y by a positive angle turns the
    // view to the left, so the other way
    const Point vertical = {0.0f, 1.0f, 0.0f};
    fromPivot = rotate(fromPivot, vertical, -yawDegrees / kDegrees);
    toLook = rotate(toLook, vertical, -yawDegrees / kDegrees);
    // then about the view's level side axis, keeping the tilt within bounds
    const Point sight = sub(toLook, fromPivot);
    const float level = sqrtf(sight.x * sight.x + sight.z * sight.z);
    if (level > 1e-4f && pitchDegrees != 0.0f)
    {
        const float tilt = atan2f(sight.y, level) * kDegrees;
        float pitch = pitchDegrees;
        if (tilt + pitch > kMaxTilt)
            pitch = kMaxTilt - tilt;
        if (tilt + pitch < -kMaxTilt)
            pitch = -kMaxTilt - tilt;
        // sight x up points to the right; turning about it lifts the sight
        const Point right = {-sight.z / level, 0.0f, sight.x / level};
        fromPivot = rotate(fromPivot, right, pitch / kDegrees);
        toLook = rotate(toLook, right, pitch / kDegrees);
    }
    eye = add(pivot, fromPivot);
    look = add(pivot, toLook);
}

} // namespace ChaseRig
