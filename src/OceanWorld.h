/************************************************************************
     File:        OceanWorld.h

     Comment:     The blocky sea floor around the island and everything on
                  it, for the underwater ride:

                  * terraced sand and stone sea bed that drops from a
                    shallow shelf by the island to deep water,
                  * rocky outcrops, arches and spires,
                  * coral reefs with branching coral trees and giant clams,
                    a kelp forest, sea grass, anemones and starfish,
                  * the ruins of a sunken colonnade with lamp-lit gates
                    and a stepped temple,
                  * a wrecked ship with gold in its hold,
                  * a rocky islet with a long tunnel through it, lit only
                    by glowing crystals,
                  * ring gates where the ride dives and where it surfaces,
                  * sea life: schools of fish, sharks, turtles, rays, a
                    whale and glowing jellyfish.

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
                  through the tunnel and climbs back out, and then tours
                  the island (LandWorld). Blocks are carved away around
                  that ride and plants keep clear of it, for all three
                  spline types (see rideObstructions()).

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
    // The underwater part comes first, then the part on the island.
    static std::vector<float> demoTrack();

    // The control point where the demo ride comes back ashore.
    static int landStart();

    // Closely spaced points along the demo ride, x, y, z for each: the whole
    // loop as a linear, a cardinal and a B-spline curve, one after another
    // (a third of the points each). LandWorld shapes the island around them.
    static std::vector<float> rideSamples();

    // Height of the water surface.
    static float waterLevel();

    // For tests: how many of the given points (x, y, z for each, a path the
    // train follows) have a block or a plant within the train's reach. The
    // demo ride gives 0 for every spline type.
    static int rideObstructions(const std::vector<float> &path);

  private:
    void build();
    void drawSeaLife(float time);

    bool m_built;
    unsigned int m_solidList;
    unsigned int m_plantList;
    std::vector<unsigned int> m_textures;
};
