/************************************************************************
     File:        UnderwaterOptics.h

     Comment:     GLSL shared by the lighting shader, the water surface and
                  the underwater fog pass, so all three agree on how light
                  behaves below the surface.

                  * caustics(): the bright, moving web of light that the
                    waves focus onto everything under water. It is the
                    edge pattern of a slowly drifting cell (Voronoi) noise.
                  * sunThroughWater(): how much sunlight is left after
                    travelling down to a point. Red is absorbed first, so
                    deep water turns blue.

                  Pasted into the shaders after "#version 120".

*************************************************************************/
#pragma once

const char *const kUnderwaterOpticsGlsl = R"GLSL(
// Absorption per world unit of water (red, green, blue).
const vec3 kWaterAbsorb = vec3(0.050, 0.024, 0.018);

vec2 opticsHash(vec2 p)
{
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

// One layer of the caustic web: bright where two moving cells meet.
float causticLayer(vec2 p, float t)
{
    p += vec2(sin(p.y * 0.9 + t * 0.7), sin(p.x * 0.8 - t * 0.6)) * 0.3;
    vec2 i = floor(p);
    vec2 f = fract(p);
    float d1 = 8.0;
    float d2 = 8.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = 0.5 + 0.42 * sin(t * 0.8 + 6.2831 * opticsHash(i + g));
            float d = length(g + o - f);
            if (d < d1) {
                d2 = d1;
                d1 = d;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    return 1.0 - smoothstep(0.0, 0.28, d2 - d1);
}

// Caustic brightness for a point below the surface. The pattern is looked up
// where the sun ray through the point crossed the surface, so it slides with
// the sun. Averages roughly 1.
float caustics(vec3 p, vec3 lightDir, float waterLevel, float t)
{
    float down = max(waterLevel - p.y, 0.0);
    vec2 s = p.xz + lightDir.xz / max(lightDir.y, 0.2) * down;
    float c = causticLayer(s * 0.11, t) * 0.65 + causticLayer(s * 0.19 + 3.7, t * 1.3) * 0.35;
    return 0.35 + c * c * 2.6;
}

// Fraction of sunlight that reaches a point at this depth.
vec3 sunThroughWater(float depth, vec3 lightDir)
{
    return exp(-kWaterAbsorb * (max(depth, 0.0) / max(lightDir.y, 0.2)));
}
)GLSL";
