/************************************************************************
     File:        TrainViewEffects.cpp

     Comment:     The TrainView side of the shader lighting: reads the
                  widgets, describes the train for the headlight and hands
                  the floor and drawStuff() to renderAdvancedFrame().

*************************************************************************/

#include <math.h>
#include <stdio.h>
#include <chrono>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <FL/Fl.H>
#include <FL/gl.h>

#include "LandTextures.h"
#include "PixelArt.h"
#include "TrainView.H"
#include "TrainWindow.H"
#include "Utilities/3DUtils.h"
#include "Utilities/Platform.H"
#include "TexturePack.h"
#include "WorldShape.h"

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
// The Top view frames the whole scenery, both the main island and the
// north-east land across the strait, or the editor's island. The scenery
// spans the same range along x as along -z (the north-east land is on the
// diagonal), from the main island's far shore to the land grid's edge.
namespace
{
const float kSceneryLow = -WorldShape::kIslandReach;
const float kSceneryHigh = WorldShape::kNorthEastX + WorldShape::kNorthEastRadius + 20.0f;
} // namespace

// Half the shorter side of the Top view.
float TrainView::topViewHalf() const
{
    return sceneryOn() ? (kSceneryHigh - kSceneryLow) * 0.5f : 110.0f;
}

// The ground the Top view centres on.
void TrainView::topViewCentre(float &x, float &z) const
{
    const float middle = sceneryOn() ? (kSceneryHigh + kSceneryLow) * 0.5f : 0.0f;
    x = middle;
    z = -middle;
}

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
// * Trestles under the track (LandWorld::drawSupports)
//========================================================================
void TrainView::drawSupports(bool shadowPass)
{
    // Points along the whole track about a unit apart; worked out again only
    // when the track or the spline type changes.
    const int splineType = tw->splineBrowser->value();
    bool same = splineType == m_supportSpline && m_pTrack->points.size() == m_supportPoints.size();
    for (size_t i = 0; same && i < m_supportPoints.size(); ++i)
    {
        const Pnt3f &a = m_pTrack->points[i].pos, &b = m_supportPoints[i];
        same = a.x == b.x && a.y == b.y && a.z == b.z;
    }
    if (!same)
    {
        m_supportSpline = splineType;
        m_supportPoints.clear();
        for (const ControlPoint &p : m_pTrack->points)
            m_supportPoints.push_back(p.pos);
        m_supportPath.clear();
        const TrackSplineType type = static_cast<TrackSplineType>(splineType);
        const float period = static_cast<float>(m_pTrack->points.size());
        Pnt3f last = m_pTrack->evaluatePosition(0.0f, type);
        float travelled = 1.0f;
        for (float u = 0.0f; u < period; u += 0.01f)
        {
            const Pnt3f p = m_pTrack->evaluatePosition(u, type);
            const float dx = p.x - last.x, dy = p.y - last.y, dz = p.z - last.z;
            travelled += sqrtf(dx * dx + dy * dy + dz * dz);
            last = p;
            if (travelled < 1.0f)
                continue;
            travelled = 0.0f;
            m_supportPath.push_back(p.x);
            m_supportPath.push_back(p.y);
            m_supportPath.push_back(p.z);
        }
    }
    m_land.drawSupports(m_supportPath, shadowPass);
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

//************************************************************************
//
// * A texture pack (nullptr: the built-in pictures) for the island, the
//   sea floor and the track: its sleepers in the pack's spruce planks and
//   its rails in iron, as a block game's rails are wood and iron
//========================================================================
void TrainView::setTexturePack(std::shared_ptr<const TexturePack::Pack> pack)
//========================================================================
{
    PixelArt::Image wood, iron;
    if (pack)
    {
        using Use = TexturePack::Pack::Use;
        if (!pack->standIn("spruce_planks|planks_spruce|oak_planks|planks_oak", Use::SOLID, wood))
            wood.clear();
        if (!pack->standIn("iron_block", Use::SOLID, iron))
            iron.clear();
    }
    m_trackRenderer.setPictures(wood, iron);
    m_ocean.setTexturePack(pack);
    m_land.setTexturePack(std::move(pack));
}

//************************************************************************
//
// * Build the world without holding up the window
//========================================================================
bool TrainView::worldReady()
{
    // The sea floor builds alongside the island, so it is there when Water
    // is turned on; it is waited for only when it is drawn.
    const bool oceanReady = m_ocean.prepare();
    const bool needOcean = tw->waterButton->value() && tw->lightingButton->value();
    const bool flat = !sceneryOn();
    // about two frames' worth of display lists at a time, so the bar moves
    const bool landReady = m_land.prepare(flat, 0.03f);
    if (landReady && (oceanReady || !needOcean))
        return true;

    float done = m_land.progress(flat);
    // as a sandbox game says it: reading the save, making a new world, or
    // turning the blocks into something to draw
    const char *stage = m_land.loadingSave(flat)
                            ? "Loading world"
                            : (done < 0.7f ? "Generating terrain" : "Building terrain");
    if (needOcean)
    {
        done = 0.85f * done + 0.15f * m_ocean.progress();
        if (landReady)
            stage = "Building the sea floor";
    }
    drawLoadingScreen(done, stage);
    if (!platform::hasTimeout(redrawSoon, this))
        platform::addTimeout(1.0 / 30.0, redrawSoon, this);
    return false;
}

void TrainView::redrawSoon(void *view)
{
    static_cast<TrainView *>(view)->redraw();
}

//************************************************************************
//
// * A loading screen like a sandbox game's: dark dirt, the stage and a bar
//========================================================================
void TrainView::drawLoadingScreen(float done, const char *stage)
{
    glUseProgram(0);
    glViewport(0, 0, w(), h());
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    const float width = static_cast<float>(w()), height = static_cast<float>(h());
    glOrtho(0.0, static_cast<double>(width), 0.0, static_cast<double>(height), -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // the dirt, darkened, in tiles of 64 pixels
    if (m_loadingTexture == 0)
        m_loadingTexture = PixelArt::upload(LandTextures::make(LandTextures::DIRT), true);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_loadingTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    const float tile = 64.0f;
    glColor3f(0.25f, 0.25f, 0.25f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(0.0f, 0.0f);
    glTexCoord2f(width / tile, 0.0f);
    glVertex2f(width, 0.0f);
    glTexCoord2f(width / tile, height / tile);
    glVertex2f(width, height);
    glTexCoord2f(0.0f, height / tile);
    glVertex2f(0.0f, height);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);

    // the stage, with a shadow, over a grey bar that fills with green
    const float middleX = width * 0.5f, middleY = height * 0.5f;
    gl_font(FL_HELVETICA_BOLD, 20);
    const float textWidth = static_cast<float>(gl_width(stage));
    glColor3f(0.25f, 0.25f, 0.25f);
    gl_draw(stage, middleX - textWidth * 0.5f + 2.0f, middleY + 18.0f);
    glColor3f(1.0f, 1.0f, 1.0f);
    gl_draw(stage, middleX - textWidth * 0.5f, middleY + 20.0f);

    const float barWidth = fminf(400.0f, width * 0.6f), barHeight = 6.0f;
    const float left = middleX - barWidth * 0.5f, top = middleY - 10.0f;
    const float filled = fmaxf(0.0f, fminf(1.0f, done));
    glColor3ub(128, 128, 128);
    glRectf(left, top - barHeight, left + barWidth, top);
    glColor3ub(128, 255, 128);
    glRectf(left, top - barHeight, left + barWidth * filled, top);

    char percent[16];
    snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(filled * 100.0f));
    gl_font(FL_HELVETICA, 14);
    const float percentWidth = static_cast<float>(gl_width(percent));
    glColor3f(0.8f, 0.8f, 0.8f);
    gl_draw(percent, middleX - percentWidth * 0.5f, top - barHeight - 22.0f);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}
