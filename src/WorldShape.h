/************************************************************************
     File:        WorldShape.h

     Comment:     The size and outline of the world, shared by the island
                  (LandWorld), the sea floor (OceanWorld) and the demo
                  ride, so the coast is the same for all of them.

                  The world was first laid out on a 200 unit square island
                  with the ride circling it under the sea; everything in it
                  is now placed at kScale times its first distance from the
                  middle (heights above the sea at kLift times), which
                  gives every landmark room to be built at a larger size.

                  The coast is a rounded square (superellipse) whose
                  shore wanders in and out with noise, giving bays and
                  headlands. inland() is the distance from the shore,
                  positive on land. No OpenGL here, so it can be tested
                  on its own.

*************************************************************************/
#pragma once

namespace WorldShape
{

const float kScale = 2.5f; // horizontal distances, from the first layout
const float kLift = 1.5f;  // heights above the sea

const float kIslandHalf = 250.0f; // half the width of the rounded square
const float kCoastWander = 24.0f; // how far the shore wanders in or out
// No land lies further than this from the middle along x or z.
const float kIslandReach = kIslandHalf + kCoastWander + 4.0f;

// The north-east land across a strait from the main island, where the
// sea has room (the ride's undersea part keeps to the other sides): a
// rounded blob whose shore wanders like the main island's.
const float kNorthEastX = 360.0f, kNorthEastZ = -360.0f;
const float kNorthEastRadius = 150.0f;

// Distance from the shore at (x, z): positive on land, negative at sea.
// Roughly in world units near the coast. Either land counts.
float inland(float x, float z);

// The same for the main island alone, and for the north-east land alone.
float inlandMain(float x, float z);
float inlandNorthEast(float x, float z);

// A distance of the first layout, scaled.
inline float scaled(float v)
{
    return v * kScale;
}

// A height of the first layout (above the sea), lifted.
inline float lifted(float y)
{
    return y > 0.0f ? y * kLift : y;
}

} // namespace WorldShape
