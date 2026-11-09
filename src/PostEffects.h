/************************************************************************
     File:        PostEffects.h

     Comment:     Shader-pack style effects on top of AdvancedLighting.

                  The scene is rendered into an HDR (16-bit float) frame
                  buffer and finished by full screen passes:

                  * Sky: gradient, sun and moon discs, stars at night and
                    drifting procedural clouds.
                  * Water: an ocean around the island with animated wave
                    normals, planar reflection (the scene is rendered a
                    second time, mirrored), refraction of the sea bed,
                    depth-based colour absorption, Fresnel, sun glints,
                    shadows and shore foam.
                  * SSAO: screen space ambient occlusion from the depth
                    buffer darkens creases and contact areas.
                  * Bloom: bright pixels glow.
                  * God rays: light shafts streaming from the sun past
                    the trees and the train.
                  * ACES filmic tone mapping and a soft vignette.

                  renderAdvancedFrame() runs the whole frame. The caller
                  only supplies how to draw the ground and the objects,
                  so the existing drawing code is reused unchanged.

*************************************************************************/
#pragma once

#include "AdvancedLighting.h"

struct EffectToggles
{
    bool water = true;
    bool bloom = true; // bloom and god rays
    bool ssao = true;
};

// What renderAdvancedFrame() needs to draw the scene.
class SceneCallbacks
{
  public:
    virtual ~SceneCallbacks()
    {
    }
    // The island's ground (the floor).
    virtual void drawGround() = 0;
    // Everything on the ground. shadowPass: draw without setting colours.
    virtual void drawObjects(bool shadowPass) = 0;
};

class PostEffects
{
  public:
    PostEffects();

    void invalidate();
    bool init();

    // Height of the sea and half the width of the square island.
    float waterLevel;
    float islandHalfSize;

    // --- steps used by renderAdvancedFrame(), in order ---
    void beginFrame(int width, int height);
    void beginReflection();
    void endReflection();
    void beginScene();
    void drawSky(const SkyState &sky, float time);
    void drawSeabed();
    void applySSAO();
    void drawWater(const AdvancedLighting &lighting, float time, bool orthographic);
    void finish(const SkyState &sky, bool bloom);

  private:
    struct Target
    {
        unsigned int fbo = 0;
        unsigned int color = 0;
        unsigned int depth = 0; // texture, or renderbuffer when depthIsRenderbuffer
        bool depthIsRenderbuffer = false;
        int width = 0;
        int height = 0;
    };

    void createTarget(Target &t, int width, int height, bool withDepth, bool depthRenderbuffer);
    void releaseTarget(Target &t);
    void releaseTargets();
    void bindTarget(const Target &t);
    void fullscreenQuad();
    void blur(Target &src, Target &tmp, float radius);

    bool m_triedInit;
    bool m_ok;
    int m_width;
    int m_height;
    int m_outputFbo;
    int m_outputViewport[4];

    Target m_scene;      // HDR colour + depth of the main view
    Target m_copy;       // copy of m_scene before the water (refraction)
    Target m_reflection; // mirrored view, half size
    Target m_halfA;      // AO, then bloom
    Target m_halfB;      // blur temporary
    Target m_rays;       // god rays

    unsigned int m_skyProgram;
    unsigned int m_waterProgram;
    unsigned int m_ssaoProgram;
    unsigned int m_aoApplyProgram;
    unsigned int m_brightProgram;
    unsigned int m_blurProgram;
    unsigned int m_raysProgram;
    unsigned int m_compositeProgram;

    // Camera of the main view, captured in beginScene().
    float m_view[16];
    float m_proj[16];
    float m_invProj[16];
};

// Draws one frame with shadows, the lighting shader and the post effects.
// Expects the camera projection on GL_PROJECTION and the view on GL_MODELVIEW.
// When the post effects are all off (or unavailable) it draws straight to the
// window like before.
void renderAdvancedFrame(AdvancedLighting &lighting, PostEffects &effects, LightingFrame frame,
                         const EffectToggles &toggles, int width, int height,
                         SceneCallbacks &scene);
