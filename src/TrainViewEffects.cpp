/************************************************************************
     File:        TrainViewEffects.cpp

     Comment:     The TrainView side of the shader lighting: reads the
                  widgets, describes the train for the headlight and hands
                  the floor and drawStuff() to renderAdvancedFrame().

*************************************************************************/

#include <math.h>
#include <chrono>

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
        m_view->drawStuff(shadowPass);
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
// * The 200 x 200 grass island (5 x 5 blocks) with dirt sides
//========================================================================
void TrainView::drawGround()
{
    m_ground.draw(200.0f, 40, -4.0f);
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
    frame.wind = true;

    // The trees stand near the edge of the ground; start with a box that
    // holds them and grow it to include every control point.
    float lo[3] = {-92.0f, 0.0f, -92.0f};
    float hi[3] = {92.0f, 24.0f, 92.0f};
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
