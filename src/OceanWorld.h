/************************************************************************
     File:        OceanWorld.h

     Comment:     The blocky sea floor around the island and everything on
                  it, for the underwater ride:

                  * terraced sand and stone sea bed that drops from a
                    shallow shelf by the island to deep water,
                  * a coral reef, a kelp forest, sea grass and starfish,
                  * the ruins of a sunken colonnade with two gates,
                  * a wrecked wooden boat,
                  * a rocky islet with a long tunnel through it, lit only
                    by glowing crystals,
                  * small schools of fish.

                  The world is a grid of 5 x 5 x 5 blocks. It is built once
                  into display lists, with Minecraft-like smooth lighting
                  baked into the vertices: sky light fades inside the
                  tunnel and under overhangs, crystals light their
                  surroundings, and corners get ambient occlusion. All
                  textures are 16 x 16 pixel art generated in code.

                  The extra lighting data goes to the shader through
                  texture unit 1's coordinate (see AdvancedLighting):
                  x = glow, y = sway, z = crystal light, w = sky light.

                  demoTrack() returns a ride that dives off the island,
                  passes the reef, the kelp, the ruins and the wreck, runs
                  through the tunnel and climbs back out. The tunnel and
                  the gates are carved around that ride.

*************************************************************************/
#pragma once

#include <vector>

class OceanWorld
{
  public:
    OceanWorld();

    // Call when the OpenGL context was recreated: the old names are gone.
    void releaseResources();

    // Draws the sea floor and everything on it. shadowPass: only the solid
    // blocks, for the shadow map. time (seconds) moves the fish.
    void draw(bool shadowPass, float time);

    // Where bubbles rise from: x, y, z for each source.
    static const std::vector<float> &bubbleSources();

    // A box around the blocks that cast shadows (the islet, ruins and wreck).
    static void shadowBounds(float lo[3], float hi[3]);

    // Control points of the demo ride: x, y, z for each point, in order.
    static std::vector<float> demoTrack();

    // Height of the water surface.
    static float waterLevel();

  private:
    void build();
    void drawFish(float time);

    bool m_built;
    unsigned int m_solidList;
    unsigned int m_plantList;
    std::vector<unsigned int> m_textures;
};
