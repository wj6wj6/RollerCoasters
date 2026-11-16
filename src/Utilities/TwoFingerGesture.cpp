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

static float blend(float cutoff, float seconds)
{
    const float tau = 1.0f / (2.0f * 3.14159265f * cutoff);
    return 1.0f / (1.0f + tau / seconds);
}

void TwoFingerGesture::OneEuro::reset(float sample)
{
    value = sample;
    speed = 0;
}

// Casiez et al., "1 Euro Filter" (CHI 2012): the cutoff rises with speed.
float TwoFingerGesture::OneEuro::filter(float sample, float seconds, float minCutoff, float beta)
{
    const float rawSpeed = (sample - value) / seconds;
    speed += (rawSpeed - speed) * blend(kSpeedCutoff, seconds);
    const float cutoff = minCutoff + beta * std::fabs(speed);
    value += (sample - value) * blend(cutoff, seconds);
    return value;
}

void TwoFingerGesture::start(float centerX, float centerY, float spreadNow)
{
    x.reset(centerX);
    y.reset(centerY);
    spread.reset(spreadNow);
    startX = centerX;
    startY = centerY;
    startSpread = spreadNow;
    moveAverage = pinchAverage = 0;
    panActive = zoomActive = false;
}

bool TwoFingerGesture::update(float centerX, float centerY, float spreadNow, float seconds,
                              float &dx, float &dy, float &scale)
{
    dx = dy = 0;
    scale = 1;
    // Reports arrive about every 8 ms; queued ones can look simultaneous, so
    // keep odd gaps from upsetting the filter.
    if (!(seconds > .004f))
        seconds = .004f;
    if (seconds > .05f)
        seconds = .05f;
    const float lastX = x.value, lastY = y.value, lastSpread = spread.value;
    x.filter(centerX, seconds, kCenterMinCutoff, kCenterBeta);
    y.filter(centerY, seconds, kCenterMinCutoff, kCenterBeta);
    spread.filter(spreadNow, seconds, kSpreadMinCutoff, kSpreadBeta);
    const float moveX = x.value - lastX;
    const float moveY = y.value - lastY;
    // Counted in full: a pinch feels as large as moving by the spread change.
    const float pinch = std::fabs(spread.value - lastSpread);
    const float ratio = (spread.value > 0 && lastSpread > 0) ? spread.value / lastSpread : 1;

    // Each motion starts only after a clear, deliberate amount of it.
    if (!panActive && std::hypot(x.value - startX, y.value - startY) > kPanSlop)
        panActive = true;
    if (!zoomActive && startSpread > 0 && spread.value > 0 &&
        std::fabs(std::log(spread.value / startSpread)) > kZoomSlop)
        zoomActive = true;

    // Compare recent travel of both motions; the weaker one fades out.
    moveAverage = moveAverage * .7f + std::hypot(moveX, moveY) * .3f;
    pinchAverage = pinchAverage * .7f + pinch * .3f;
    const float total = moveAverage + pinchAverage;
    const float moveShare = total > 0 ? moveAverage / total : .5f;
    // Orbit wins ties; zoom only joins in when the pinch is at least as large.
    const float panWeight = panActive ? smoothstep(kPanShareLow, kPanShareHigh, moveShare) : 0;
    const float zoomWeight =
        zoomActive ? smoothstep(kZoomShareLow, kZoomShareHigh, 1 - moveShare) : 0;

    dx = moveX * panWeight;
    dy = moveY * panWeight;
    scale = std::pow(ratio, zoomWeight);
    return dx != 0 || dy != 0 || scale != 1;
}
