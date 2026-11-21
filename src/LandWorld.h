/************************************************************************
     File:        LandWorld.h

     Comment:     The island the ride tours, built from 2.5 unit blocks
                  like a sandbox game world:

                  * a snow mountain the ride spirals up on a ledge, with
                    a spruce forest on its lower slopes and a stream that
                    steps down its west side into the sea in columns of
                    still, see-through water blocks,
                  * badlands with banded clay and a canyon cut down to
                    sea level, so the sea (and its reflections) fills it;
                    a pond on the plateau spills into it as a waterfall,
                  * a volcano with a lava lake in its crater and lava
                    rivers running down to the shore,
                  * rocky crags with ore in the stone and a mine tunnel
                    lit by lanterns and crystals,
                  * meadows with oak, birch and cherry trees, flowers and
                    tall grass, a pond, giant mushrooms, a village with a
                    windmill and a wheat field, and grazing animals,
                  * a floating island with a waterfall, blocky clouds and
                    hot-air balloons.

                  The blocks are carved away around the demo ride
                  (OceanWorld::demoTrack()) for all three spline types,
                  which gives cuttings, ledges and tunnels; see
                  rideObstructions(). Where the track is in the air,
                  drawSupports() stands it on wooden trestles.

                  Built once into display lists with the same baked
                  lighting as OceanWorld: sky light that fades in tunnels,
                  the glow of lava, lanterns and crystals, and ambient
                  occlusion in the corners. Everything that moves (windmill,
                  animals, clouds, balloons) is drawn every frame from
                  boxes.

                  The flat variant is the plain grass island the track
                  editor works on: level ground and a few trees.

*************************************************************************/
#pragma once

#include <memory>
#include <vector>

#include "Mobs.h"
#include "PixelArt.h"

namespace TexturePack
{
class Pack;
}

class LandWorld
{
  public:
    LandWorld();
    ~LandWorld();

    // Call when the OpenGL context was recreated: the old names are gone.
    // The blocks are kept; only their display lists are made again.
    void releaseResources();

    // Gets the island ready to draw without holding up the window: the
    // first call starts building it on a thread of its own (the scenery
    // takes a second or two, many more in a Debug build); each call then
    // turns what is built into display lists for up to the given seconds.
    // True once it is all ready. draw() builds whatever is left at once.
    bool prepare(bool flat, float seconds);

    // How far the island is from ready, 0..1, for a loading screen.
    float progress(bool flat) const;

    // True while the island is being read from its save (WorldSave.h)
    // rather than generated.
    bool loadingSave(bool flat) const;

    // Draws the island. flat: the level editor island instead of the
    // scenery. shadowPass: only the solid blocks, for the shadow map.
    // time (seconds) moves the water, the animals and the clouds.
    void draw(bool flat, bool shadowPass, float time);

    // The see-through water blocks (streams, falls, ponds). Call after
    // everything else is drawn, outside the shadow pass: they are blended
    // over what is behind them.
    void drawWater(bool flat);

    // Stands the track on wooden trestles, one every 16 units along it
    // wherever it is above the water and well above the ground; off the
    // island the posts go down to the sea bed. path: points along the whole
    // track about a unit apart, x, y, z for each. The trestles are worked
    // out again only when the path changes.
    void drawSupports(const std::vector<float> &path, bool shadowPass);

    // How many chunks of the island the last draw() drew (the others were
    // out of view).
    int chunksDrawn() const
    {
        return m_chunksDrawn;
    }

    // Swaps the island's textures for a texture pack's, where it has them
    // (nullptr: all built in). Takes effect at the next draw().
    void setTexturePack(std::shared_ptr<const TexturePack::Pack> pack);

    // A box around the blocks that cast shadows.
    static void shadowBounds(float lo[3], float hi[3]);

    // Is there a block (not water) at (x, y, z) of the scenery's or the flat
    // island's grid? False until that grid is complete. For keeping the
    // Chase camera out of the rock.
    bool solidAt(bool flat, float x, float y, float z) const;

    // For tests: how many of the given points (x, y, z for each, a path the
    // train follows) have a block or a plant within the train's reach. The
    // demo ride gives 0 for every spline type.
    static int rideObstructions(const std::vector<float> &path);

    // For tests: how many water blocks lie near the given path (x, y, z for
    // each point): within 6 units sideways, from 6 below to 10 above, so
    // beside, under or over the track. The demo ride gives 0.
    static int waterNearRide(const std::vector<float> &path);

  private:
    struct Trestle
    {
        float x, z, yaw; // centre under the track, and which way it runs
        float top;       // the bottom of the sleepers
        float foot[2];   // across the track, where each post meets the ground
        float ground[2]; // and how high that ground is
        bool post[2];    // a post would pass through other track: left out
    };
    float postGround(float x, float z, float from) const;
    void planSupports(const std::vector<float> &path);
    std::vector<Trestle> m_trestles;
    std::vector<float> m_supportPath;

    struct Scene;
    static void buildScene(Scene &scene, bool flat);
    Scene &scene(bool flat);
    void drawMoving(const Scene &scene, float time);

    std::unique_ptr<Scene> m_scenery;
    std::unique_ptr<Scene> m_flat;
    // the pack's picture for a texture, or the built-in one
    PixelArt::Image pictureOf(int texture) const;

    std::vector<unsigned int> m_textures;
    std::shared_ptr<const TexturePack::Pack> m_pack;
    bool m_texturesStale = false;
    // the animals' pictures, uploaded when first drawn and for a new pack
    Mobs::Skins m_mobSkins;
    bool m_mobsStale = true;
    void uploadMobSkins();
    int m_chunksDrawn = 0;
};
