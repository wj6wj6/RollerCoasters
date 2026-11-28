/************************************************************************
     File:        SkyHorizon.h

     Comment:     The sky's colour at the horizon, for the sky shader and
                  for the fog the water and the land fade into. At sunrise
                  and sunset the band of sky along the horizon glows orange
                  on the sun's side and rose to violet on the other, with a
                  wide glow round the sun; the fog takes the same colour
                  looking the same way, so the distant sea and island fade
                  into the very sky behind them instead of into one colour
                  all round, which cut a straight line across the golden
                  sky.

*************************************************************************/
#pragma once

const char *const kSkyHorizonGlsl = R"GLSL(
// The golden-hour colour of the horizon looking along dir (only its x and
// z count): orange towards the sun, rose to violet away from it.
vec3 horizonWarm(vec3 dir, vec3 sunDir)
{
    vec2 d = dir.xz / max(length(dir.xz), 1e-4);
    vec2 s = sunDir.xz / max(length(sunDir.xz), 1e-4);
    float toward = dot(d, s) * 0.5 + 0.5;
    return mix(vec3(0.34, 0.10, 0.28), vec3(1.25, 0.36, 0.06), toward * toward);
}

// The wide glow round the sun, stronger and warmer at dusk (sunColor is
// zero while the sun is down).
vec3 sunGlow(vec3 dir, vec3 sunDir, vec3 sunColor, float dusk)
{
    float cs = max(dot(dir, sunDir), 0.0);
    return sunColor * (pow(cs, 24.0) * (0.25 + 0.5 * dusk) + pow(cs, 4.0) * 0.3 * dusk);
}

// The sky right at the horizon looking along dir: what the fog over the
// water and the land fades into. horizon: the plain horizon colour (grey
// in the rain).
vec3 horizonSky(vec3 dir, vec3 horizon, vec3 sunDir, vec3 sunColor, float dusk)
{
    vec3 level = normalize(vec3(dir.x, 0.0, dir.z) + vec3(1e-4, 0.0, 0.0));
    return mix(horizon, horizonWarm(level, sunDir), dusk * 0.9) +
           sunGlow(level, sunDir, sunColor, dusk);
}
)GLSL";
