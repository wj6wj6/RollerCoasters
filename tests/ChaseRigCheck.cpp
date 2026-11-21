// Checks where the Chase camera goes (ChaseRig.h), for trains of several lengths and riders of
// several heights and for the fields of view the FOV slider allows: with room to spare the last
// rider's head shows in the lower half of the picture, about halfway down, and the front of the
// train is in the picture; the camera stands further back for a longer train and higher for
// taller riders; under a low roof it stays at or under the roof and comes no nearer than allowed,
// the head still below the middle where the roof leaves room for that; following moves towards
// the target without passing it.
#include "ChaseRig.h"

#include <cmath>
#include <cstdio>

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

    std::printf("%s\n", failures == 0 ? "ChaseRigCheck passed" : "ChaseRigCheck FAILED");
    return failures == 0 ? 0 : 1;
}
