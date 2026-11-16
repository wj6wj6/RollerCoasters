#include "TwoFingerGesture.H"

#include <cmath>

static float smoothstep(float low, float high, float value)
{
    float t = (value - low) / (high - low);
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    return t * t * (3 - 2 * t);
}

void TwoFingerGesture::start(float centerX, float centerY, float spread)
{
    startX = x = centerX;
    startY = y = centerY;
    startSpread = spreadNow = spread;
    moveAverage = pinchAverage = 0;
    panActive = zoomActive = false;
}

bool TwoFingerGesture::update(float centerX, float centerY, float spread, float &dx, float &dy,
                              float &scale)
{
    dx = dy = 0;
    scale = 1;
    // Low-pass the samples so sensor noise does not reach the camera.
    const float nextX = x + (centerX - x) * kSmoothing;
    const float nextY = y + (centerY - y) * kSmoothing;
    const float nextSpread = spreadNow + (spread - spreadNow) * kSmoothing;
    const float moveX = nextX - x;
    const float moveY = nextY - y;
    // Counted in full: a pinch feels as large as moving by the spread change.
    const float pinch = std::fabs(nextSpread - spreadNow);
    const float ratio = (nextSpread > 0 && spreadNow > 0) ? nextSpread / spreadNow : 1;
    x = nextX;
    y = nextY;
    spreadNow = nextSpread;

    // Each motion starts only after a clear, deliberate amount of it.
    if (!panActive && std::hypot(x - startX, y - startY) > kPanSlop)
        panActive = true;
    if (!zoomActive && startSpread > 0 && spreadNow > 0 &&
        std::fabs(std::log(spreadNow / startSpread)) > kZoomSlop)
        zoomActive = true;

    // Compare recent finger travel of both motions; the weaker one fades out.
    moveAverage = moveAverage * .8f + std::hypot(moveX, moveY) * .2f;
    pinchAverage = pinchAverage * .8f + pinch * .2f;
    const float total = moveAverage + pinchAverage;
    const float moveShare = total > 0 ? moveAverage / total : .5f;
    const float panWeight = panActive ? smoothstep(kDominanceLow, kDominanceHigh, moveShare) : 0;
    const float zoomWeight =
        zoomActive ? smoothstep(kDominanceLow, kDominanceHigh, 1 - moveShare) : 0;

    dx = moveX * panWeight;
    dy = moveY * panWeight;
    scale = std::pow(ratio, zoomWeight);
    return dx != 0 || dy != 0 || scale != 1;
}
