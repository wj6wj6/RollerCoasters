/************************************************************************
     File:        ChaseRig.cpp

     Comment:     See ChaseRig.h.

*************************************************************************/

#include "ChaseRig.h"

#include <algorithm>
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

namespace
{
Point scale(const Point &v, float k)
{
    return {v.x * k, v.y * k, v.z * k};
}
Point lerp(const Point &a, const Point &b, float t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}
// a block and a half of air kept between the camera and a block over it
const float kMargin = 1.5f;
// Where the camera aims to be under a roof comes down more gently than the
// limit (kDescentSlope) and is all the way down kSettle units before the
// roof, so the camera, following it smoothly, has caught up by then and the
// limit itself never has to stop it.
const float kAimSlope = 0.2f;
const float kSettle = 20.0f;
// ... and a little under the limit, which a spring that is still settling
// would otherwise just touch
const float kAimBelow = 0.3f;
// how long (seconds, about) the camera takes to get where it aims, and how
// fast it may go: along the track, down, and up once out from under a roof
const float kAlongTime = 0.8f;
const float kFallTime = 0.3f;
const float kRiseTime = 1.2f;
const float kMaxSpeed = 8.0f;

// Moves `current` towards `target` like a critically damped spring, so it
// starts and stops gently (its speed never jumps) and does not overshoot;
// speed carries over from one call to the next.
float smoothDamp(float current, float target, float &speed, float smoothTime, float dt)
{
    const float omega = 2.0f / smoothTime;
    const float x = omega * dt;
    const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float most = kMaxSpeed * smoothTime;
    const float change = std::min(most, std::max(-most, current - target));
    const float aim = current - change;
    const float pull = (speed + omega * change) * dt;
    speed = (speed - omega * pull) * decay;
    float next = aim + (change + pull) * decay;
    if ((target > current) == (next > target))
    {
        next = target;
        speed = 0.0f;
    }
    return next;
}
} // namespace

float room(const Solid &solid, const Point &base, const Point &up, float upTo)
{
    for (float h = 1.0f; h <= upTo + kMargin; h += 0.5f)
        if (solid(add(base, scale(up, h))))
            return std::max(kLowest, h - kMargin);
    return upTo;
}

void sampleAt(const std::vector<TrackSample> &track, float back, Point &base, Point &up)
{
    if (track.empty())
    {
        base = {0.0f, 0.0f, 0.0f};
        up = {0.0f, 1.0f, 0.0f};
        return;
    }
    if (back <= track.front().back || track.size() == 1)
    {
        base = track.front().base;
        up = track.front().up;
        return;
    }
    if (back >= track.back().back)
    {
        base = track.back().base;
        up = track.back().up;
        return;
    }
    const auto after = std::lower_bound(
        track.begin(), track.end(), back,
        [](const TrackSample &sample, float value) { return sample.back < value; });
    const TrackSample &b = *after;
    const TrackSample &a = *(after - 1);
    const float span = b.back - a.back;
    const float t = span > 1e-6f ? (back - a.back) / span : 0.0f;
    base = lerp(a.base, b.base, t);
    up = lerp(a.up, b.up, t);
    const float length = sqrtf(dot(up, up));
    up = length > 1e-6f ? scale(up, 1.0f / length) : Point{0.0f, 1.0f, 0.0f};
}

float Follower::aheadFor(float speed)
{
    // long enough to come all the way down at kAimRate before the roof
    return std::min(800.0f, std::max(kAhead, kSettle + std::max(speed, 0.0f) * 2.5f));
}

float Follower::spacingFor(float speed)
{
    return std::max(0.25f, std::max(speed, 0.0f) / 240.0f);
}

Follow Follower::update(const std::vector<TrackSample> &track, const Solid &solid,
                        const Train &train, float fovY, float dt, const Point &lastHead,
                        float speed)
{
    // A roof x units ahead is x / speed seconds away: each unit ahead lets
    // the camera be no more than rate / speed higher, so it never has to
    // come down faster than that rate (and settles in time, earlier).
    const float pace = std::max(speed, 1.0f);
    const float descentSlope = std::min(kDescentSlope, kDescentRate / pace);
    const float aimSlope = std::min(kAimSlope, kAimRate / pace);
    const float settle = kSettle + 0.6f * std::max(speed, 0.0f);
    const Placement open = plan(train, fovY);
    const float nearest = std::min(open.behind, kNearestRiders * train.riderTop);
    std::vector<float> rooms(track.size());
    for (size_t k = 0; k < track.size(); ++k)
        rooms[k] = room(solid, track[k].base, track[k].up, open.height);
    // The highest the camera may be `behind` the last cart: under every
    // roof between it and the front of the train and beyond, each the less
    // the further along it is, so the limit comes down gradually as a roof
    // draws near and never all at once. The samples just behind the camera
    // count too, for the block right over it. `slope`, `settle` and `below`
    // give the limit itself, or the gentler, earlier, lower height the
    // camera aims for.
    const float spacing = track.size() > 1 ? track[1].back - track[0].back : 0.0f;
    auto ceiling = [&](float behind, float slope, float settle, float below) {
        const float at = train.length + behind;
        float limit = open.height;
        for (size_t k = 0; k < track.size() && track[k].back <= at + spacing; ++k)
        {
            const float ahead = std::max(0.0f, at - track[k].back - settle);
            const float roof = rooms[k] < open.height ? rooms[k] - below : rooms[k];
            limit = std::min(limit, roof + slope * ahead);
        }
        return std::max(kLowest, limit);
    };
    auto limit = [&](float behind) { return ceiling(behind, descentSlope, 0.0f, 0.0f); };
    auto aimFor = [&](float behind) { return ceiling(behind, aimSlope, settle, kAimBelow); };

    const float from = m_valid ? m_behind : open.behind;
    Placement want = fit(open, aimFor(from), nearest, train, fovY);
    // and a clear line to the last rider (a bend in a tunnel), or nearer
    for (int pass = 0; pass < 4 && want.behind > nearest; ++pass)
    {
        Point base, up;
        sampleAt(track, train.length + want.behind, base, up);
        const Point eye = add(base, scale(up, std::min(want.height, limit(want.behind))));
        bool clear = true;
        for (int k = 1; k < 12 && clear; ++k)
            clear = !solid(lerp(eye, lastHead, static_cast<float>(k) / 12.0f));
        if (clear)
            break;
        want.behind = std::max(nearest, want.behind * 0.7f);
    }

    if (!m_valid || dt <= 0.0f || dt > 1.0f)
    {
        m_behind = want.behind;
        m_height = std::min(want.height, aimFor(m_behind));
        m_behindSpeed = 0.0f;
        m_heightSpeed = 0.0f;
        m_valid = true;
    }
    else
    {
        m_behind = smoothDamp(m_behind, want.behind, m_behindSpeed, kAlongTime, dt);
        const float target = std::min(want.height, aimFor(m_behind));
        m_height = smoothDamp(m_height, target, m_heightSpeed,
                              target > m_height ? kRiseTime : kFallTime, dt);
    }
    // the limit only ever comes down gradually, so keeping under it is no
    // jump (and the camera, aiming lower and earlier, is under it anyway)
    const float most = limit(m_behind);
    if (m_height > most)
    {
        m_height = most;
        m_heightSpeed = std::min(m_heightSpeed, 0.0f);
    }
    Point base, up;
    sampleAt(track, train.length + m_behind, base, up);
    return {m_behind, m_height, add(base, scale(up, m_height))};
}

} // namespace ChaseRig
