// Turns two touchpad fingers into camera motion: their shared movement
// orbits, their spread zooms. A 1-euro filter removes sensor jitter while the
// fingers are slow and adds almost no lag while they move fast. Each motion
// must pass a small threshold before it starts, and while one motion clearly
// dominates the other is suppressed: orbit wins ties, zoom needs a clear pinch.
#pragma once

class TwoFingerGesture
{
  public:
    // Positions and spread in touchpad widths (0..1 across the pad).
    void start(float centerX, float centerY, float spread);
    // seconds: time since the previous sample. Returns true when (dx, dy) in
    // touchpad widths or scale != 1 should apply.
    bool update(float centerX, float centerY, float spread, float seconds, float &dx, float &dy,
                float &scale);

    // Tuning, in touchpad widths and seconds.
    static constexpr float kCenterMinCutoff = 1.5f; // Hz while still: removes jitter
    static constexpr float kCenterBeta = 8.0f;      // faster fingers, less smoothing
    static constexpr float kSpreadMinCutoff = 1.0f; // the spread is the noisier signal
    static constexpr float kSpreadBeta = 6.0f;
    static constexpr float kSpeedCutoff = 8.0f;   // Hz, how fast the speed estimate reacts
    static constexpr float kPanSlop = .012f;      // net movement before orbit starts
    static constexpr float kZoomSlop = .1f;       // |log spread change| before zoom starts
    static constexpr float kPanShareLow = .2f;    // movement share for no orbit ...
    static constexpr float kPanShareHigh = .4f;   // ... and for full orbit
    static constexpr float kZoomShareLow = .4f;   // pinch share for no zoom ...
    static constexpr float kZoomShareHigh = .65f; // ... and for full zoom

  private:
    struct OneEuro
    {
        float value = 0, speed = 0;
        void reset(float sample);
        float filter(float sample, float seconds, float minCutoff, float beta);
    };
    OneEuro x, y, spread;
    float startX = 0, startY = 0, startSpread = 0;
    float moveAverage = 0, pinchAverage = 0;
    bool panActive = false, zoomActive = false;
};
