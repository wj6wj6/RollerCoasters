/************************************************************************
     File:        LandWaterMotion.h

     Comment:     GLSL shared by the lighting shader (the island's water as
                  textured blocks) and the water surface shader (the same
                  water drawn as the sea is), so both move it the same way.

                  * landWaterMotion(): how far a corner of the island's
                    water moves, from the mark LandWorld gives it (its
                    first extra value, see waterMark() there): the open
                    surface bobs a little, only down so it never rises
                    into the block above, and more where falling water
                    stirs it up; the sides of a fall that face the air
                    sway a little. The movement depends only on where the
                    corner is, so the faces that share it stay joined.

                  Pasted into the vertex shaders after "#version 120".

*************************************************************************/
#pragma once

const char *const kLandWaterMotionGlsl = R"GLSL(
// mark: the vertex's first extra value (water when below -0.25); p: where
// it is; t: the time. stirred: 0 to 0.4, how stirred up the surface is.
vec3 landWaterMotion(float mark, vec3 p, float t, out float stirred)
{
    stirred = 0.0;
    vec3 d = vec3(0.0);
    if (mark > -0.25)
        return d;
    float v = -mark;
    float kind = floor(v - 0.5 + 0.001); // 1: bobs, 2: sways
    stirred = clamp(v - 0.5 - kind, 0.0, 0.4);
    if (mod(kind, 2.0) > 0.5) {
        float bob = sin(p.x * 0.42 + t * 1.3) * cos(p.z * 0.37 - t * 1.05);
        bob = 0.6 * bob + 0.4 * sin(t * 0.7 + (p.x + p.z) * 0.08);
        float churn = 0.5 + 0.5 * sin(t * 6.0 + p.x * 1.7 + p.z * 1.3);
        d.y = 0.08 * (bob - 1.0) - stirred * 0.2 * churn;
    }
    if (kind > 1.5) {
        d.x = 0.12 * sin(p.z * 0.9 + p.y * 0.7 + t * 3.1);
        d.z = 0.12 * cos(p.x * 0.8 - p.y * 0.6 + t * 2.7);
    }
    return d;
}
)GLSL";
