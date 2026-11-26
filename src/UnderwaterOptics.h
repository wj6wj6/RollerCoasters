/************************************************************************
     File:        UnderwaterOptics.h

     Comment:     GLSL shared by the lighting shader, the water surface and
                  the underwater fog pass, so all three agree on how light
                  behaves below the surface.

                  * caustics(): the bright, moving web of light that the
                    waves focus onto everything under water. It is the
                    edge pattern of a slowly drifting cell (Voronoi) noise,
                    sharp just under the surface and softer deeper down.
                  * sunThroughWater(): how much sunlight is left after
                    travelling down to a point. Red is absorbed first, so
                    deep water turns blue.
                  * openWater(): the colour of the water itself along a
                    line of sight: turquoise towards the surface, deep
                    blue below, with a glow around the sun.

                  The water is clear, like a tropical lagoon: things
                  stay visible for a few hundred units and fade into
                  blue rather than into the dark.

                  Pasted into the shaders after "#version 120".

*************************************************************************/
#pragma once

#include <math.h>

// Sunlight that enters the water is bent towards straight down (Snell's law,
// water 1.33): the direction towards the sun as seen under water. Under the
// surface the light shafts, the shadows, the caustics and the sun seen
// through the waves all use it. A light below the horizon is left as it is.
inline void lightDirInWater(const float light[3], float out[3])
{
    if (light[1] <= 0.0f)
    {
        for (int k = 0; k < 3; ++k)
            out[k] = light[k];
        return;
    }
    const float length = sqrtf(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    const float inv = length > 0.0f ? 1.0f / length : 0.0f;
    out[0] = light[0] * inv / 1.33f;
    out[2] = light[2] * inv / 1.33f;
    out[1] = sqrtf(fmaxf(1.0f - out[0] * out[0] - out[2] * out[2], 0.0f));
}

const char *const kUnderwaterOpticsGlsl = R"GLSL(
// Sunlight lost per world unit of depth (red, green, blue).
const vec3 kWaterAbsorb = vec3(0.020, 0.0070, 0.0045);
// Light lost per world unit along the line of sight.
const vec3 kFogExtinction = vec3(0.0170, 0.0055, 0.0035);

vec2 opticsHash(vec2 p)
{
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

// Smooth value noise in 0..1.
float opticsNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(opticsHash(i).x, opticsHash(i + vec2(1.0, 0.0)).x, f.x),
               mix(opticsHash(i + vec2(0.0, 1.0)).x, opticsHash(i + vec2(1.0, 1.0)).x, f.x), f.y);
}

// One layer of the caustic web: how far p is from the line where two moving
// cells meet (0 on it).
float causticEdge(vec2 p, float t)
{
    p += vec2(sin(p.y * 0.9 + t * 0.7), sin(p.x * 0.8 - t * 0.6)) * 0.3;
    // A second, finer warp a few times across each cell bends the cells'
    // straight edges into curving, uneven threads, as real caustics are.
    p += vec2(sin(p.y * 2.7 + p.x * 0.6 + t * 0.9), sin(p.x * 2.3 - p.y * 0.7 - t * 0.8)) * 0.2;
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
    return d2 - d1;
}

// The lines of the web at width w, each colour a little wider than the one
// before (the water bends blue more than red, so the lines' edges are
// bluish), divided by their average so they average 1.
vec3 causticLines(float e, float w)
{
    vec3 width = w * vec3(0.8, 1.0, 1.25);
    return exp(-e / width) * (1.0 + 1.6 * width) / (2.45 * width);
}

// The caustic web where a sun ray crossed the surface at s, with line width
// w: two webs of different size whose lines add faintly and multiply
// brightly where they cross, so the light gathers in knots and bright runs
// instead of an even net. About 0.9 on average. The cells move in place, so
// the web shifts and reforms rather than sliding along.
vec3 causticWeb(vec2 s, float w, float t)
{
    vec3 a = causticLines(causticEdge(s * 0.11, t), w);
    vec3 b = causticLines(causticEdge(mat2(0.8, -0.6, 0.6, 0.8) * s * 0.17 + 3.7, t * 1.3), w * 1.6);
    return (a * 0.3 + b * 0.15 + a * b * 0.55) * 0.92;
}

// Groups of bigger waves come and go: patches (0..1) where the waves above
// focus the light strongly and patches where they hardly do.
float causticPatch(vec2 s, float t)
{
    return opticsNoise(s * 0.025 + vec2(t * 0.03, -t * 0.02));
}

// The big swells under the ripples: a coarse web whose crossings are the
// spots where they gather the light most (cells of about 20 units). These
// are what the light shafts follow, and they brighten the caustics on the
// sea bed under each shaft. About 1 on average. Costly: drawn once a frame
// into PostEffects' caustic map and read from there.
float causticSwell(vec2 s, float t)
{
    float a = exp(-causticEdge(s * 0.04, t * 0.6) / 0.11);
    float b = exp(-causticEdge(mat2(0.8, -0.6, 0.6, 0.8) * s * 0.055 + 1.3, t * 0.75) / 0.143);
    return a * b * 13.7;
}

// Caustic light for a point below the surface, as a factor on the sunlight
// there: about 1 on average. The pattern is looked up where the (bent) sun
// ray through the point crossed the surface, so it slides with the sun.
// Just under the waves the lines are sharp and bright; deeper down the
// light that the waves focused spreads out again, so they widen and fade.
// Far from the camera (eyeDist) they widen too, as the water blurs them,
// which also keeps them from flickering. swell is causticSwell() where the
// sun ray crossed the surface (1 where it is not known): the sea bed is
// brighter under each light shaft.
vec3 caustics(vec3 p, vec3 lightDir, float waterLevel, float t, float eyeDist, float swell)
{
    float down = max(waterLevel - p.y, 0.0);
    vec2 s = p.xz + lightDir.xz / max(lightDir.y, 0.2) * down;
    float w = 0.04 + down * 0.0012 + eyeDist * 0.0012;
    // Kept gentle: a web drawn at full strength reads as a net laid on the
    // sand rather than light.
    float contrast = 0.8 * (0.6 + 0.4 * exp(-down / 80.0)) * exp(-eyeDist * 0.004) *
                     (0.35 + 0.9 * causticPatch(s, t));
    vec3 web = mix(vec3(0.92), causticWeb(s, w, t), clamp(contrast, 0.0, 1.0));
    return web * mix(1.0, swell, 0.3 * exp(-eyeDist * 0.004));
}

// Fraction of sunlight that reaches a point at this depth.
vec3 sunThroughWater(float depth, vec3 lightDir)
{
    return exp(-kWaterAbsorb * (max(depth, 0.0) / max(lightDir.y, 0.2)));
}

// Colour of the open water looking along v.
vec3 openWater(vec3 v, vec3 lightDir, vec3 lightColor, vec3 shallow, vec3 deep)
{
    vec3 c = mix(deep, shallow, smoothstep(-0.35, 0.75, v.y));
    float sun = max(dot(v, lightDir), 0.0);
    return c + lightColor * vec3(0.03, 0.10, 0.13) * (pow(sun, 6.0) * 0.5 + pow(sun, 40.0));
}
)GLSL";
