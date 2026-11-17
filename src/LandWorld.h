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

class LandWorld
{
  public:
    LandWorld();
    ~LandWorld();

    // Call when the OpenGL context was recreated: the old names are gone.
    void releaseResources();

    // Draws the island. flat: the level editor island instead of the
    // scenery. shadowPass: only the solid blocks, for the shadow map.
    // time (seconds) moves the water, the animals and the clouds.
    void draw(bool flat, bool shadowPass, float time);

    // The see-through water blocks (streams, falls, ponds). Call after
    // everything else is drawn, outside the shadow pass: they are blended
    // over what is behind them.
    void drawWater(bool flat);

    // Stands the track on wooden trestles. rail: points along the track,
    // about evenly spaced, x, y, z for each. Only where the ground of the
    // scenery is well below the track and nothing else passes underneath.
    void drawSupports(const std::vector<float> &rail, bool shadowPass);

    // A box around the blocks that cast shadows.
    static void shadowBounds(float lo[3], float hi[3]);

    // For tests: how many of the given points (x, y, z for each, a path the
    // train follows) have a block or a plant within the train's reach. The
    // demo ride gives 0 for every spline type.
    static int rideObstructions(const std::vector<float> &path);

    // For tests: how many water blocks lie near the given path (x, y, z for
    // each point): within 6 units sideways, from 6 below to 10 above, so
    // beside, under or over the track. The demo ride gives 0.
    static int waterNearRide(const std::vector<float> &path);

  private:
    struct Scene;
    Scene &scene(bool flat);
    void drawMoving(const Scene &scene, float time);

    std::unique_ptr<Scene> m_scenery;
    std::unique_ptr<Scene> m_flat;
    std::vector<unsigned int> m_textures;
};
