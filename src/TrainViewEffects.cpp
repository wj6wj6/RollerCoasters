/************************************************************************
     File:        TrainViewEffects.cpp

     Comment:     The TrainView side of the shader lighting: reads the
                  widgets, describes the train for the headlight and hands
                  the floor and drawStuff() to renderAdvancedFrame().

*************************************************************************/

#include <math.h>
#include <chrono>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include "TrainView.H"
#include "TrainWindow.H"
#include "Utilities/3DUtils.h"

namespace
{

// Lets renderAdvancedFrame() draw the floor and everything on it.
class TrainViewScene : public SceneCallbacks
{
  public:
    explicit TrainViewScene(TrainView *view) : m_view(view)
    {
    }
    void drawGround() override
    {
        setupFloor();
        m_view->drawGround();
    }
    void drawObjects(bool shadowPass) override
    {
        if (!shadowPass)
            setupObjects();
        else
            m_view->drawGround(true); // the mountains cast shadows too
        m_view->drawStuff(shadowPass);
    }
    void drawUnderwater(bool shadowPass) override
    {
        if (!shadowPass)
            setupFloor();
        m_view->drawUnderwater(shadowPass);
    }

  private:
    TrainView *m_view;
};

float secondsSinceStart()
{
    static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    const std::chrono::duration<float> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

// Same side/up construction the train uses: side is horizontal, up is
// perpendicular to the direction of travel.
void trainUpVector(const Pnt3f &forward, Pnt3f &up)
{
    Pnt3f side(forward.z, 0.0f, -forward.x);
    float length = sqrtf(side.x * side.x + side.z * side.z);
    if (length < 0.0001f)
    {
        side = Pnt3f(-forward.y, forward.x, 0.0f);
        length = sqrtf(side.x * side.x + side.y * side.y);
    }
    if (length > 0.0001f)
        side = side * (1.0f / length);
    up = Pnt3f(forward.y * side.z - forward.z * side.y, forward.z * side.x - forward.x * side.z,
               forward.x * side.y - forward.y * side.x);
    up.normalize();
}

} // namespace

//************************************************************************
//
// * The 200 x 200 island: the scenery, or level grass for editing a track
//========================================================================
bool TrainView::sceneryOn() const
{
    return tw->sceneryButton->value() != 0;
}

void TrainView::drawGround(bool shadowPass)
{
    m_land.draw(!sceneryOn(), shadowPass, secondsSinceStart());
}

//************************************************************************
//
// * Trestles under the track, one every 9 units along it
//========================================================================
void TrainView::drawSupports(bool shadowPass)
{
    const TrackSplineType splineType = static_cast<TrackSplineType>(tw->splineBrowser->value());
    const float period = static_cast<float>(m_pTrack->points.size());
    std::vector<float> rail;
    Pnt3f last = m_pTrack->evaluatePosition(0.0f, splineType);
    float travelled = 9.0f;
    for (float u = 0.0f; u < period; u += 0.02f)
    {
        const Pnt3f p = m_pTrack->evaluatePosition(u, splineType);
        const float dx = p.x - last.x, dy = p.y - last.y, dz = p.z - last.z;
        travelled += sqrtf(dx * dx + dy * dy + dz * dz);
        last = p;
        if (travelled < 9.0f)
            continue;
        travelled = 0.0f;
        rail.push_back(p.x);
        rail.push_back(p.y);
        rail.push_back(p.z);
    }
    m_land.drawSupports(rail, shadowPass);
}

//************************************************************************
//
// * The sea floor around the island, with the fish moving
//========================================================================
float TrainView::animationTime() const
{
    return secondsSinceStart();
}

void TrainView::drawUnderwater(bool shadowPass)
{
    m_ocean.draw(shadowPass, secondsSinceStart());
}

//************************************************************************
//
// * Is the shader path switched on and does this GPU support it?
//========================================================================
bool TrainView::advancedLightingOn()
{
    return tw->lightingButton->value() && lighting.init();
}

//************************************************************************
//
// * Draw the frame with shadow maps, per-pixel lighting and post effects
//========================================================================
void TrainView::drawAdvanced()
{
    LightingFrame frame;
    fillLightingFrame(frame);

    EffectToggles toggles;
    toggles.water = tw->waterButton->value() != 0;
    toggles.bloom = tw->bloomButton->value() != 0;
    toggles.ssao = tw->ssaoButton->value() != 0;

    if (effects.bubbleSources.empty())
        effects.bubbleSources = OceanWorld::bubbleSources();
    effects.waterLevel = OceanWorld::waterLevel();

    TrainViewScene scene(this);
    renderAdvancedFrame(lighting, effects, frame, toggles, w(), h(), scene);
}

//************************************************************************
//
// * Gather what the lighting needs for this frame
//========================================================================
void TrainView::fillLightingFrame(LightingFrame &frame)
{
    frame.timeOfDay = static_cast<float>(tw->timeOfDay->value());
    frame.shadows = tw->shadowButton->value() != 0;
    frame.headlight = tw->headlightButton->value() != 0;
    frame.orthographic = tw->topCam->value() != 0;
    frame.time = secondsSinceStart();

    // Rain sets in within a couple of seconds; the ground takes longer to
    // get wet and much longer to dry again.
    const float elapsed = m_weatherTime < 0.0f ? 0.0f : fminf(frame.time - m_weatherTime, 0.5f);
    m_weatherTime = frame.time;
    const bool raining = tw->rainButton->value() != 0;
    m_rain = fmaxf(0.0f, fminf(1.0f, m_rain + (raining ? elapsed : -elapsed) / 2.0f));
    m_wetness = fmaxf(0.0f, fminf(1.0f, m_wetness + (raining ? elapsed / 6.0f : -elapsed / 25.0f)));
    frame.rain = m_rain;
    frame.wetness = m_wetness;

    // Start with a box that holds the island (the scenery's peaks, or the
    // editor island's trees) and grow it to include every control point.
    float lo[3] = {-102.0f, 0.0f, -102.0f};
    float hi[3] = {102.0f, 24.0f, 102.0f};
    if (sceneryOn())
        LandWorld::shadowBounds(lo, hi);
    if (tw->waterButton->value())
    {
        // the islet, ruins and wreck too
        float seaLo[3], seaHi[3];
        OceanWorld::shadowBounds(seaLo, seaHi);
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = fminf(lo[k], seaLo[k]);
            hi[k] = fmaxf(hi[k], seaHi[k]);
        }
    }
    for (size_t i = 0; i < m_pTrack->points.size(); ++i)
    {
        const Pnt3f &p = m_pTrack->points[i].pos;
        const float c[3] = {p.x, p.y, p.z};
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = fminf(lo[k], c[k] - 6.0f);
            hi[k] = fmaxf(hi[k], c[k] + 6.0f);
        }
    }
    for (int k = 0; k < 3; ++k)
    {
        frame.casterMin[k] = lo[k];
        frame.casterMax[k] = hi[k];
    }

    frame.hasTrain = m_pTrack->points.size() >= 4;
    if (frame.hasTrain)
    {
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->splineBrowser->value());
        const Pnt3f position = m_pTrack->evaluatePosition(m_pTrack->trainU, splineType);
        Pnt3f forward = m_pTrack->evaluateTangent(m_pTrack->trainU, splineType);
        forward.normalize();
        Pnt3f up;
        trainUpVector(forward, up);
        frame.trainPosition[0] = position.x;
        frame.trainPosition[1] = position.y;
        frame.trainPosition[2] = position.z;
        frame.trainForward[0] = forward.x;
        frame.trainForward[1] = forward.y;
        frame.trainForward[2] = forward.z;
        frame.trainUp[0] = up.x;
        frame.trainUp[1] = up.y;
        frame.trainUp[2] = up.z;
    }
}
