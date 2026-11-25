// Checks where the Chase camera goes (ChaseRig.h), for trains of several lengths and riders of
// several heights and for the fields of view the FOV slider allows: with room to spare the last
// rider's head shows in the lower half of the picture, about halfway down, and the front of the
// train is in the picture; the camera stands further back for a longer train and higher for
// taller riders; under a low roof it stays at or under the roof and comes no nearer than allowed,
// the head still below the middle where the roof leaves room for that; following moves towards
// the target without passing it; turning round the train keeps the camera as far from the middle
// of the train, turns the view the way it is dragged and never tips it over; riding through a
// tunnel and under a low overhang, the follower moves the camera without jumps, comes down before
// a roof instead of under it, stays out of the rock and no nearer than the riders' height and a
// half, and goes back up once out.
#include "ChaseRig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}
} // namespace

int main()
{
    const float lengths[] = {12.0f, 23.0f, 40.0f};
    const float tops[] = {3.0f, 5.35f, 8.0f};
    const float fovs[] = {50.0f, 70.0f, 100.0f};
    bool framed = true, front = true, inPicture = true;
    for (float length : lengths)
        for (float top : tops)
            for (float fov : fovs)
            {
                const ChaseRig::Train train = {length, top};
                const ChaseRig::Placement p = ChaseRig::plan(train, fov);
                const float head = ChaseRig::headBelowCentre(p, train, fov);
                framed = framed && head > 0.5f && head < 0.65f && p.height > top;
                const float f = ChaseRig::frontBelowCentre(p, train, fov);
                front = front && f > -1.0f && f < 1.0f;
                inPicture = inPicture && p.behind > 0.0f && p.lookAhead > length;
            }
    expect(framed, "with room the last rider's head is about halfway down the lower half");
    expect(front, "the front of the train is in the picture");
    expect(inPicture, "the camera is behind the train and looks beyond its front");

    const ChaseRig::Train shortTrain = {12.0f, 5.35f}, longTrain = {40.0f, 5.35f};
    expect(ChaseRig::plan(longTrain, 70.0f).behind > ChaseRig::plan(shortTrain, 70.0f).behind,
           "a longer train: the camera stands further back");
    const ChaseRig::Train small = {23.0f, 3.0f}, tall = {23.0f, 8.0f};
    expect(ChaseRig::plan(tall, 70.0f).height > ChaseRig::plan(small, 70.0f).height,
           "taller riders: the camera stands higher");

    // under a roof
    const ChaseRig::Train train = {23.0f, 5.35f};
    const ChaseRig::Placement open = ChaseRig::plan(train, 70.0f);
    const ChaseRig::Placement same = ChaseRig::fit(open, open.height + 5.0f, 3.0f, train, 70.0f);
    expect(same.height == open.height && same.behind == open.behind,
           "with room above, fitting changes nothing");
    bool under = true, notNearer = true, stillLow = true;
    for (float roof = 1.5f; roof < open.height; roof += 0.5f)
    {
        const ChaseRig::Placement p = ChaseRig::fit(open, roof, 3.0f, train, 70.0f);
        under = under && p.height <= roof + 1e-4f;
        notNearer = notNearer && p.behind >= 3.0f - 1e-4f && p.behind <= open.behind + 1e-4f;
        // where the roof is a block and a half over the riders' heads, the head stays below
        // the middle of the picture
        if (roof >= train.riderTop + 1.5f)
            stillLow = stillLow && ChaseRig::headBelowCentre(p, train, 70.0f) > 0.0f;
    }
    expect(under, "under a roof the camera stays at or under it");
    expect(notNearer, "it comes nearer, but no nearer than allowed");
    expect(stillLow, "with a little room over the riders the head still shows below the middle");

    // following
    float x = 0.0f;
    bool towards = true;
    for (int i = 0; i < 200; ++i)
    {
        const float next = ChaseRig::approach(x, 10.0f, 1.0f / 60.0f, 3.0f);
        towards = towards && next >= x && next <= 10.0f;
        x = next;
    }
    expect(towards && std::fabs(x - 10.0f) < 0.1f,
           "following moves towards the target and gets there");
    expect(ChaseRig::approach(4.0f, 10.0f, 0.0f, 3.0f) == 4.0f, "no time, no move");
    expect(ChaseRig::approach(4.0f, 10.0f, 100.0f, 3.0f) <= 10.0f,
           "a long step never passes the target");

    // turning round the train: it runs along -z, so its right is +x; the camera is behind and
    // above the middle and looks at a point beyond the front
    const ChaseRig::Point pivot = {0.0f, 2.0f, 0.0f};
    const ChaseRig::Point eye0 = {0.0f, 8.0f, 12.0f}, look0 = {0.0f, 1.0f, -30.0f};
    auto reach = [&](const ChaseRig::Point &e) {
        return std::sqrt((e.x - pivot.x) * (e.x - pivot.x) + (e.y - pivot.y) * (e.y - pivot.y) +
                         (e.z - pivot.z) * (e.z - pivot.z));
    };
    auto tilt = [](const ChaseRig::Point &e, const ChaseRig::Point &l) {
        const float dx = l.x - e.x, dy = l.y - e.y, dz = l.z - e.z;
        return std::atan2(dy, std::sqrt(dx * dx + dz * dz)) * 57.2957795f;
    };
    ChaseRig::Point eye = eye0, look = look0;
    ChaseRig::orbit(pivot, eye, look, 0.0f, 0.0f);
    expect(eye.x == eye0.x && eye.y == eye0.y && eye.z == eye0.z && look.z == look0.z,
           "not turned, the camera stays where it was");
    eye = eye0, look = look0;
    ChaseRig::orbit(pivot, eye, look, 30.0f, 0.0f);
    expect(look.x - eye.x > 0.0f && eye.x < 0.0f,
           "turned right: the view looks right, the camera goes round to the train's left");
    expect(std::fabs(eye.y - eye0.y) < 1e-3f, "turning sideways keeps the height");
    eye = eye0, look = look0;
    ChaseRig::orbit(pivot, eye, look, 180.0f, 0.0f);
    expect(eye.z < pivot.z && look.z > eye.z,
           "half way round: in front of the train, looking back");
    eye = eye0, look = look0;
    ChaseRig::orbit(pivot, eye, look, 0.0f, 20.0f);
    expect(std::fabs(tilt(eye, look) - (tilt(eye0, look0) + 20.0f)) < 0.01f && eye.y < eye0.y,
           "dragged up: the view tilts up and the camera goes down");
    bool sameReach = true, bounded = true;
    const float reach0 = reach(eye0);
    for (float yaw = -180.0f; yaw <= 180.0f; yaw += 45.0f)
        for (float pitch = -200.0f; pitch <= 200.0f; pitch += 50.0f)
        {
            eye = eye0, look = look0;
            ChaseRig::orbit(pivot, eye, look, yaw, pitch);
            sameReach = sameReach && std::fabs(reach(eye) - reach0) < 1e-3f;
            const float t = tilt(eye, look);
            bounded =
                bounded && t <= ChaseRig::kMaxTilt + 0.01f && t >= -ChaseRig::kMaxTilt - 0.01f;
        }
    expect(sameReach, "turning keeps the camera as far from the middle of the train");
    expect(bounded, "the view never tilts past the limit, so the picture never turns over");

    // Riding a straight track along +x through a tunnel (roof 8 above the rails from x = 200 to
    // 260) and under a short ledge (6 above, x = 400 to 405), 20 units a second at 60 frames a
    // second.
    {
        const ChaseRig::Train ride = {23.0f, 5.35f};
        const float fov = 70.0f;
        const ChaseRig::Placement roomyPlace = ChaseRig::plan(ride, fov);
        auto roofAt = [](float x) {
            if (x >= 200.0f && x <= 260.0f)
                return 8.0f;
            if (x >= 400.0f && x <= 405.0f)
                return 6.0f;
            return 1e9f;
        };
        const ChaseRig::Solid solid = [&](const ChaseRig::Point &p) {
            return p.y >= roofAt(p.x) && p.y < 30.0f;
        };
        const float spacing = 0.25f;
        ChaseRig::Follower follower;
        std::vector<ChaseRig::TrackSample> samples;
        const float speed = 20.0f, dt = 1.0f / 60.0f;
        float lastHeight = -1.0f, lastBehind = -1.0f;
        float worstRise = 0.0f, worstFall = 0.0f, worstAlong = 0.0f, nearest = 1e9f;
        float lastStep = 0.0f, lastAlongStep = 0.0f, worstJolt = 0.0f;
        float clearance = 1e9f, heightAtTunnel = -1.0f, heightAtLedge = -1.0f, lastLow = 0.0f;
        bool inRock = false;
        for (float trainFront = 0.0f; trainFront < 600.0f; trainFront += speed * dt)
        {
            samples.clear();
            for (float back = -ChaseRig::Follower::kAhead;
                 back <= ride.length + roomyPlace.behind + 1.0f; back += spacing)
                samples.push_back({back, {trainFront - back, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}});
            const ChaseRig::Point head = {trainFront - ride.length, ride.riderTop, 0.0f};
            const ChaseRig::Follow f = follower.update(samples, solid, ride, fov, dt, head);
            if (lastHeight >= 0.0f)
            {
                worstRise = std::max(worstRise, f.height - lastHeight);
                worstFall = std::max(worstFall, lastHeight - f.height);
                worstAlong = std::max(worstAlong, std::fabs(f.behind - lastBehind));
                // how much the speed changes from one frame to the next: a jolt
                const float step = f.height - lastHeight, alongStep = f.behind - lastBehind;
                worstJolt = std::max(worstJolt, std::max(std::fabs(step - lastStep),
                                                         std::fabs(alongStep - lastAlongStep)));
                lastStep = step;
                lastAlongStep = alongStep;
            }
            lastHeight = f.height;
            lastBehind = f.behind;
            nearest = std::min(nearest, f.behind);
            inRock = inRock || solid(f.eye);
            clearance = std::min(clearance, roofAt(f.eye.x) - f.eye.y);
            const float camera = f.eye.x;
            if (heightAtTunnel < 0.0f && camera >= 200.0f)
                heightAtTunnel = f.height;
            if (heightAtLedge < 0.0f && camera >= 400.0f)
                heightAtLedge = f.height;
            if (camera > 405.0f && camera < 600.0f - ride.length)
                lastLow = f.height;
        }
        std::printf("ride: worst rise %.3f, fall %.3f, along %.3f a frame, jolt %.4f; nearest "
                    "%.2f; clearance %.2f; at the tunnel %.2f, at the ledge %.2f, after %.2f\n",
                    worstRise, worstFall, worstAlong, worstJolt, nearest, clearance, heightAtTunnel,
                    heightAtLedge, lastLow);
        // 8 units a second is 0.133 a frame
        expect(worstRise < 0.14f && worstFall < 0.14f && worstAlong < 0.14f,
               "riding in and out of a tunnel and under a ledge, the camera never jumps");
        expect(worstJolt < 0.02f, "nor does its speed: it starts and stops gently");
        expect(!inRock && clearance >= 1.0f,
               "the camera stays out of the rock, with room to spare");
        expect(heightAtTunnel <= 8.0f - 1.5f + 0.01f && heightAtLedge <= 6.0f - 1.5f + 0.01f,
               "it is already down when it reaches a roof, not pushed down under it");
        expect(nearest >= ChaseRig::Follower::kNearestRiders * ride.riderTop - 0.01f,
               "never nearer the last cart than the riders' height and a half");
        expect(std::fabs(lastLow - roomyPlace.height) < 0.05f,
               "back up once out from under the roof");
    }

    std::printf("%s\n", failures == 0 ? "ChaseRigCheck passed" : "ChaseRigCheck FAILED");
    return failures == 0 ? 0 : 1;
}
