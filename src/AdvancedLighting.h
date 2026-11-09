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
                  * A time-of-day sun that moves across the sky, with a
                    dim moon at night.
                  * A spotlight headlight attached to the front of the
                    train.

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

    // Train frame used for the headlight.
    bool hasTrain = false;
    float trainPosition[3] = {0.0f, 0.0f, 0.0f};
    float trainForward[3] = {0.0f, 0.0f, 1.0f};
    float trainUp[3] = {0.0f, 1.0f, 0.0f};

    // World space box around everything that casts a shadow.
    float casterMin[3] = {-100.0f, 0.0f, -100.0f};
    float casterMax[3] = {100.0f, 30.0f, 100.0f};
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

    // Shadow pass. Between these two calls the caller draws every shadow caster
    // with colours off. Both matrix stacks, the viewport and the framebuffer are
    // restored by endShadowPass().
    void beginShadowPass(const LightingFrame& frame);
    void endShadowPass();

    // Main pass. Call begin() with the camera view matrix on GL_MODELVIEW (right
    // after setting up the projection), draw the scene, then call end().
    void begin(const LightingFrame& frame);
    void end();

    // Lines and other pieces drawn with GL_LIGHTING off can ask to keep their
    // flat colour. Does nothing when the shader is not active.
    void setUnlit(bool unlit);

    bool active() const
    {
        return m_active;
    }

  private:
    void computeLight(const LightingFrame& frame);
    void releaseFramebuffer();

    bool m_triedInit;
    bool m_ok;
    bool m_active;
    bool m_shadowMapValid;

    unsigned int m_program;
    unsigned int m_shadowFbo;
    unsigned int m_shadowTexture;
    int m_shadowSize;
    int m_previousFbo;

    int m_locInvView;
    int m_locLightMatrix;
    int m_locShadowMap;
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
    int m_locUnlit;

    // Results of computeLight(), kept between the shadow and the main pass.
    float m_lightDir[3];
    float m_lightColor[3];
    float m_skyAmbient[3];
    float m_groundAmbient[3];
    float m_fogColor[3];
    float m_lightView[16];
    float m_lightProj[16];
    float m_lightMatrix[16];
    float m_normalOffset;
    float m_depthBias;
};
