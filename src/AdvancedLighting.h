/************************************************************************
     File:        AdvancedLighting.h

     Comment:     Optional high quality lighting for the TrainView.

                  * Shadow mapping: the scene is rendered once from the
                    sun (or moon) into a depth texture, and every pixel
                    then checks that texture to know whether it is lit.
                    Percentage-closer filtering softens the edges.
                  * Per-pixel Blinn-Phong lighting with a sky/ground
                    hemisphere ambient term, gamma correction and
                    distance fog.
                  * A time-of-day sun that moves across the sky, orange
                    and golden near the horizon, and a cool blue moon
                    with its ambient light at night.
                  * A spotlight headlight attached to the front of the
                    train.
                  * Below the water level: sunlight absorbed with depth
                    (red first) and focused into moving caustics.
                  * Optional per-vertex data in texture unit 1's
                    coordinate: glow, sway, crystal light and sky light
                    (OceanWorld bakes these; other code leaves the
                    default, which changes nothing). Texels with alpha
                    below 0.5 are cut out.

                  The geometry is still drawn with the fixed function
                  (glBegin/glEnd) calls used everywhere else. The GLSL
                  1.20 shaders read the built-in gl_Vertex, gl_Normal and
                  gl_Color attributes, so no drawing code has to change.
                  If the shaders or the framebuffer cannot be created,
                  init() returns false and the caller keeps the original
                  rendering path.

*************************************************************************/
#pragma once

// All the inputs that change from frame to frame.
struct LightingFrame
{
    float timeOfDay = 15.0f; // hours, 0 to 24
    bool shadows = true;
    bool headlight = true;
    bool orthographic = false; // the top view: no fog
    bool hdr = false;          // write linear HDR colour for the post effects
    float clipBelowY = -1e9f;  // discard anything below this height (reflections)
    float time = 0.0f;         // seconds: swaying plants, caustics

    // Below this height things are under water: caustics, blue light.
    float waterLevel = -1e9f;
    bool cameraUnderwater = false; // fog is then drawn by PostEffects

    float rain = 0.0f;    // 0 dry sky, 1 pouring: clouds close in, the sun dims
    float wetness = 0.0f; // 0 dry, 1 soaked: surfaces darken and turn glossy

    // Train frame used for the headlight.
    bool hasTrain = false;
    float trainPosition[3] = {0.0f, 0.0f, 0.0f};
    float trainForward[3] = {0.0f, 0.0f, 1.0f};
    float trainUp[3] = {0.0f, 1.0f, 0.0f};

    // World space box around everything that casts a shadow.
    float casterMin[3] = {-100.0f, 0.0f, -100.0f};
    float casterMax[3] = {100.0f, 30.0f, 100.0f};

    // Where the camera looks: when set, the shadow map covers the ground
    // within kShadowReach of this point instead of the whole caster box,
    // so its texels stay small however large the world is.
    bool hasFocus = false;
    float focus[3] = {0.0f, 0.0f, 0.0f};
};

// How far from the focus the shadow map reaches (world units).
const float kShadowReach = 150.0f;
// ... and the second, coarser one round it, which carries the shadows on
// out to most of the island, so the far mountains and woods keep theirs.
const float kFarShadowReach = 600.0f;

// Sun, moon and sky colours for one time of day (linear colour space).
struct SkyState
{
    float sunDir[3];   // towards the sun, may be below the horizon
    float moonDir[3];  // towards the moon
    float lightDir[3]; // the one that casts shadows right now
    float lightColor[3];
    float zenith[3];
    float horizon[3];
    float day;      // 0 at night, 1 in daylight
    float dusk;     // 1 near sunrise and sunset
    float overcast; // 0 clear, 1 rain clouds
};

class AdvancedLighting
{
  public:
    AdvancedLighting();

    // Call when the OpenGL context was recreated: the old object names are gone.
    void invalidate();

    // Creates the shaders and the shadow framebuffer the first time it is called
    // with a current context. Returns false if this GPU cannot run them.
    bool init();

    // The sky colour for the given time of day, in display (gamma) space.
    static void skyColor(float timeOfDay, float rgb[3]);

    // Works out the sun, sky and shadow camera for this frame. Call it first.
    void prepare(const LightingFrame &frame);

    const SkyState &sky() const
    {
        return m_sky;
    }
    unsigned int shadowTexture() const
    {
        return m_shadowTexture;
    }
    const float *lightMatrix() const
    {
        return m_lightMatrix;
    }
    bool shadowMapValid() const
    {
        return m_shadowMapValid;
    }
    unsigned int farShadowTexture() const
    {
        return m_farTexture;
    }
    const float *farMatrix() const
    {
        return m_farMatrix;
    }
    bool farShadowValid() const
    {
        return m_farValid;
    }

    // The train headlight as set up by the last begin(): is it on, where
    // it is and which way it points.
    bool headlightOn() const
    {
        return m_headOn;
    }
    const float *headlightPosition() const
    {
        return m_headPos;
    }
    const float *headlightDirection() const
    {
        return m_headDir;
    }

    // Shadow pass, once for each cascade (0: the fine map round the camera,
    // 1: the coarse one out to kFarShadowReach; shadowCascades() says how
    // many this frame needs). Between these two calls the caller draws every
    // shadow caster with colours off. Both matrix stacks, the viewport and
    // the framebuffer are restored by endShadowPass().
    int shadowCascades() const
    {
        return m_farUsed ? 2 : 1;
    }
    void beginShadowPass(int cascade = 0);
    void endShadowPass();

    // Main pass. Call begin() with the camera view matrix on GL_MODELVIEW (right
    // after setting up the projection), draw the scene, then call end().
    void begin(const LightingFrame &frame);
    void end();

    // While see-through surfaces (the island's water and glass) are drawn:
    // their alpha blends them. Otherwise the alpha of a solid surface carries
    // how open it is to the rain, for the wet ground pass (PostEffects).
    // Does nothing when the shader is not active.
    void setTranslucent(bool translucent);

    bool active() const
    {
        return m_active;
    }

  private:
    void releaseFramebuffer();

    bool m_triedInit;
    bool m_ok;
    bool m_active;
    bool m_shadowMapValid;
    bool m_farUsed;  // this frame has the coarse cascade
    bool m_farValid; // ... and it has been drawn
    int m_cascade;   // the one being drawn

    unsigned int m_program;
    unsigned int m_shadowFbo;
    unsigned int m_shadowTexture;
    int m_shadowSize;
    unsigned int m_farFbo;
    unsigned int m_farTexture;
    int m_farSize;
    int m_previousFbo;

    int m_locInvView;
    int m_locLightMatrix;
    int m_locShadowMap;
    int m_locFarMatrix;
    int m_locFarShadowMap;
    int m_locFarOn;
    int m_locFarTexel;
    int m_locFarNormalOffset;
    int m_locFarDepthBias;
    int m_locShadowsOn;
    int m_locShadowTexel;
    int m_locNormalOffset;
    int m_locDepthBias;
    int m_locLightDir;
    int m_locLightColor;
    int m_locSkyAmbient;
    int m_locGroundAmbient;
    int m_locFogColor;
    int m_locFogDensity;
    int m_locHeadlightOn;
    int m_locHeadPos;
    int m_locHeadDir;
    int m_locHeadColor;
    int m_locHdr;
    int m_locClipY;
    int m_locTime;
    int m_locView;
    int m_locTexture;
    int m_locWaterLevel;
    int m_locBlockLightColor;
    int m_locWetness;
    int m_locTranslucent;
    int m_locDaylight;
    float m_daylight = 1.0f; // 1 in full day, 0 at night: how much the block lights wash out

    // Results of prepare(), kept between the shadow and the main passes.
    SkyState m_sky;
    float m_skyAmbient[3];
    float m_groundAmbient[3];
    float m_fogColor[3];
    float m_lightView[16];
    float m_lightProj[16];
    float m_farView[16];
    float m_farProj[16];
    float m_farMatrix[16];
    float m_farNormalOffset = 0.0f;
    float m_farDepthBias = 0.0f;
    float m_lightMatrix[16];
    float m_normalOffset;
    float m_depthBias;
    bool m_headOn;
    float m_headPos[3];
    float m_headDir[3];
};
