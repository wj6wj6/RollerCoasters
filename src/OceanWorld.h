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
                  textures are pixel art generated in code: the blocks
                  32 x 32 in the island's style (LandTextures), the
                  plants 16 x 16.

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

#include <memory>
#include <vector>

namespace TexturePack
{
class Pack;
}

class OceanWorld
{
  public:
    OceanWorld();
    ~OceanWorld();

    // Call when the OpenGL context was recreated: the old names are gone.
    void releaseResources();

    // Gets the sea floor ready to draw without holding up the window: the
    // first call starts building it on a thread of its own (or reading it
    // from its save, WorldSave.h); the call after it is built makes the
    // display lists. True once it is all ready. draw() waits for whatever
    // is left.
    bool prepare();

    // How far the sea floor is from ready, 0..1, for a loading screen.
    float progress() const;

    // Draws the sea floor and everything on it. shadowPass: only the solid
    // blocks, for the shadow map. time (seconds) moves the fish.
    void draw(bool shadowPass, float time);

    // Swaps the sea floor's textures for a texture pack's, where it has
    // them (nullptr: all built in). Takes effect at the next draw().
    void setTexturePack(std::shared_ptr<const TexturePack::Pack> pack);

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

    // When this file was compiled, for the key of a saved world
    // (WorldSave.h).
    static const char *buildStamp();

    // Height of the water surface.
    static float waterLevel();

    // Is there a block of the sea floor at (x, y, z)? False until it is
    // built. For keeping the Chase camera out of the rock.
    bool solidAt(float x, float y, float z) const;

    // Height of the terraced sea bed at (x, z), off the island: where the
    // posts of a track over the sea stand.
    static float seaFloor(float x, float z);

    // Is the sea bed at (x, z) left for the island to draw? The sea's
    // blocks are twice the island's, so near the shore one of them can be
    // left to the island while part of it is not on the island.
    static bool leftToIsland(float x, float z);

    // For tests: how many of the given points (x, y, z for each, a path the
    // train follows) have a block or a plant within the train's reach. The
    // demo ride gives 0 for every spline type.
    static int rideObstructions(const std::vector<float> &path);

  private:
    void drawSeaLife(float time);

    std::vector<unsigned char> m_solid; // per block of the grid: 1 where it is solid
    bool m_built;
    struct Chunk
    {
        unsigned int solid = 0, plant = 0;
        float lo[3], hi[3];
    };
    std::vector<Chunk> m_chunks; // drawn only when in view
    unsigned int m_farList;      // the sea bed beyond the grid, always drawn
    std::vector<unsigned int> m_textures;
    std::shared_ptr<const TexturePack::Pack> m_pack;
    bool m_texturesStale = false;
    void uploadTextures(bool fresh);
    struct Build;
    std::unique_ptr<Build> m_build; // while it is being built
};
