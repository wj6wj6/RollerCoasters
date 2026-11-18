/************************************************************************
     File:        WorldShape.cpp

     Comment:     See WorldShape.h.

*************************************************************************/

#include "WorldShape.h"

#include <math.h>

namespace
{

float lattice(int x, int z, unsigned int seed)
{
    unsigned int h = static_cast<unsigned int>(x) * 374761393u +
                     static_cast<unsigned int>(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffu) / 65535.0f;
}

// Smooth noise in 0 .. 1.
float smoothNoise(float x, float z, unsigned int seed)
{
    const float fx = floorf(x), fz = floorf(z);
    const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    float tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a =
        lattice(ix, iz, seed) + (lattice(ix + 1, iz, seed) - lattice(ix, iz, seed)) * tx;
    const float b = lattice(ix, iz + 1, seed) +
                    (lattice(ix + 1, iz + 1, seed) - lattice(ix, iz + 1, seed)) * tx;
    return a + (b - a) * tz;
}

} // namespace

namespace WorldShape
{

float inland(float x, float z)
{
    // the rounded square: |x|^4 + |z|^4 = half^4 on its edge
    const float ax = fabsf(x) / kIslandHalf, az = fabsf(z) / kIslandHalf;
    const float r = sqrtf(sqrtf(ax * ax * ax * ax + az * az * az * az)); // 1 on the edge
    float d = (1.0f - r) * kIslandHalf;
    // bays and headlands, then smaller coves
    d += (smoothNoise(x * 0.009f, z * 0.009f, 701u) - 0.5f) * 2.0f * kCoastWander * 0.75f;
    d += (smoothNoise(x * 0.035f, z * 0.035f, 703u) - 0.5f) * 2.0f * kCoastWander * 0.25f;
    return d;
}

} // namespace WorldShape
