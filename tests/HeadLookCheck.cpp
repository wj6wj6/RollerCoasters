#include "HeadLook.H"
#include <cmath>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

static bool approx(float a, float b)
{
    return std::fabs(a - b) <= 1e-4f;
}

static bool same(const Pnt3f &a, const Pnt3f &b)
{
    return approx(a.x, b.x) && approx(a.y, b.y) && approx(a.z, b.z);
}

static float dot(const Pnt3f &a, const Pnt3f &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

int main()
{
    // A train heading +Z on flat ground: makeTrackFrame() gives side = +X
    // (the rider's left) and up = +Y.
    const Pnt3f forward(0, 0, 1);
    const Pnt3f side(1, 0, 0);
    const Pnt3f up(0, 1, 0);
    const Pnt3f right(-1, 0, 0);
    Pnt3f direction, viewUp;

    HeadLook head;
    check(head.centered(), "starts looking ahead");
    head.apply(forward, side, up, direction, viewUp);
    check(same(direction, forward) && same(viewUp, up), "centred view is the train's view");

    head.turn(90, 0);
    head.apply(forward, side, up, direction, viewUp);
    check(same(direction, right) && same(viewUp, up), "positive yaw looks to the right");

    head.reset();
    head.turn(0, 30);
    head.apply(forward, side, up, direction, viewUp);
    check(direction.y > 0 && approx(direction.y, std::sin(30 * 3.14159265f / 180)),
          "positive pitch looks up");
    check(approx(dot(direction, viewUp), 0) && approx(dot(viewUp, viewUp), 1),
          "view up stays perpendicular and unit length");
    check(viewUp.y > 0, "view stays upright");

    // Pitch stops short of straight up and down.
    head.turn(0, 500);
    check(approx(head.pitch(), HeadLook::maxPitch), "pitch stops looking up");
    head.turn(0, -1000);
    check(approx(head.pitch(), -HeadLook::maxPitch), "pitch stops looking down");

    // Yaw goes all the way round and wraps.
    head.reset();
    head.turn(170, 0);
    head.turn(20, 0);
    check(approx(head.yaw(), -170), "yaw wraps past 180");
    head.turn(-20, 0);
    check(approx(head.yaw(), 170), "yaw wraps back");
    head.reset();
    head.turn(180, 0);
    head.apply(forward, side, up, direction, viewUp);
    check(same(direction, Pnt3f(0, 0, -1)), "can look backward");

    // Drags turn the way the mouse moves; touchpads move the scene with the fingers.
    head.reset();
    head.drag(40, -20);
    check(head.yaw() > 0 && head.pitch() > 0, "drag right and up looks right and up");
    head.reset();
    head.touchpadGesture(40, -20);
    check(head.yaw() < 0 && head.pitch() < 0, "fingers right and up pull the scene along");
    head.reset();
    head.scroll(1, -1);
    check(approx(head.yaw(), HeadLook::scrollDegrees) &&
              approx(head.pitch(), HeadLook::scrollDegrees),
          "touchpad scroll looks like the Fly camera");

    // The head turns with the train: a train climbing a slope looks along it.
    head.reset();
    const float s = std::sqrt(.5f);
    const Pnt3f climbing(0, s, s);
    const Pnt3f climbingUp(0, s, -s);
    head.apply(climbing, side, climbingUp, direction, viewUp);
    check(same(direction, climbing) && same(viewUp, climbingUp), "head follows the train's frame");
    head.turn(90, 0);
    head.apply(climbing, side, climbingUp, direction, viewUp);
    check(same(direction, right), "looking sideways on a slope");

    if (failures == 0)
        std::printf("HeadLookCheck passed\n");
    return failures == 0 ? 0 : 1;
}
