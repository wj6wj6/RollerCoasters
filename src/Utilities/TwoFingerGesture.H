// Turns two touchpad fingers into camera motion: their shared movement
// orbits, their spread zooms. Sensor jitter is smoothed away, each motion
// must pass a small threshold before it starts, and while one motion clearly
// dominates the other is suppressed, so a pinch does not wobble the view and
// a swipe does not breathe the zoom. Both still apply when both are clear.
#pragma once

class TwoFingerGesture
{
  public:
    // Positions and spread in touchpad widths (0..1 across the pad).
    void start(float centerX, float centerY, float spread);
    // Returns true when (dx, dy) in touchpad widths or scale != 1 should apply.
    bool update(float centerX, float centerY, float spread, float &dx, float &dy, float &scale);

    // Tuning, in touchpad widths unless noted.
    static constexpr float kSmoothing = .5f;     // share of each new sample kept
    static constexpr float kPanSlop = .025f;     // net movement before orbit starts
    static constexpr float kZoomSlop = .06f;     // |log spread change| before zoom starts
    static constexpr float kDominanceLow = .3f;  // below this share: no motion
    static constexpr float kDominanceHigh = .5f; // above this share: full motion

  private:
    float startX = 0, startY = 0, startSpread = 0;
    float x = 0, y = 0, spreadNow = 0;
    float moveAverage = 0, pinchAverage = 0;
    bool panActive = false, zoomActive = false;
};
