#include "TwoFingerGesture.H"
#include <cmath>
#include <cstdio>
#include <cstdlib>

struct Result
{
    float moved = 0; // total orbit movement, touchpad widths
    float zoom = 1;  // total scale
};

// Feeds frames of a two-finger gesture: centre drifts by (vx, vy) per frame,
// spread grows by growth per frame, with deterministic sensor jitter.
static Result run(float vx, float vy, float growth, float jitter, int frames = 120)
{
    TwoFingerGesture gesture;
    float cx = .5f, cy = .5f, spread = .3f;
    gesture.start(cx, cy, spread);
    Result result;
    std::srand(7);
    for (int i = 0; i < frames; ++i)
    {
        cx += vx;
        cy += vy;
        spread *= 1 + growth;
        const float n1 = ((float)std::rand() / RAND_MAX - .5f) * 2 * jitter;
        const float n2 = ((float)std::rand() / RAND_MAX - .5f) * 2 * jitter;
        const float n3 = ((float)std::rand() / RAND_MAX - .5f) * 2 * jitter;
        float dx, dy, scale;
        if (gesture.update(cx + n1, cy + n2, spread + n3, dx, dy, scale))
        {
            result.moved += std::hypot(dx, dy);
            result.zoom *= scale;
        }
    }
    return result;
}

static bool check(bool pass, const char *what, const Result &r)
{
    std::printf("%s: %s (moved %.4f pad widths, zoom x%.3f)\n", pass ? "PASS" : "FAIL", what,
                r.moved, r.zoom);
    return pass;
}

int main()
{
    const float jitter = .0015f; // about 0.15 mm on a 100 mm pad
    bool ok = true;
    Result still = run(0, 0, 0, jitter);
    ok &=
        check(still.moved == 0 && still.zoom == 1, "resting fingers do not move the camera", still);
    Result pinch = run(.0004f, 0, .006f, jitter); // pinch with slight drift
    ok &= check(pinch.zoom > 1.8f && pinch.moved < .01f, "a pinch zooms without wobbling the view",
                pinch);
    Result swipe = run(.004f, .001f, .0003f, jitter); // swipe with slight spread change
    ok &= check(swipe.moved > .4f && std::fabs(swipe.zoom - 1) < .02f,
                "a swipe orbits without breathing the zoom", swipe);
    Result both = run(.0025f, 0, .006f, jitter); // deliberate move and pinch together
    ok &=
        check(both.moved > .2f && both.zoom > 1.8f, "a clear move and pinch apply together", both);
    return ok ? 0 : 1;
}
