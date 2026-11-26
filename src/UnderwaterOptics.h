/************************************************************************
     File:        UnderwaterOptics.h

     Comment:     GLSL shared by the ocean surface, the lighting shader, the
                  light shafts and the underwater fog pass, so the sea
                  surface and everything below it move with the same waves.

                  * kOceanWavesGlsl: the eight Gerstner ocean swells (used
                    for surface displacement/normals, underwater caustic
                    focusing and volumetric light shafts).
                  * refractedLightDir(): Snell's law bend of sunlight
                    entering the water (n = 1.333).
                  * causticsRgb(): analytic C2 optical Jacobian focusing
                    combining the real Gerstner oceanWaves with rotated
                    quintic surface ripples, inverse-ray fold warping,
                    macro swell lensing, depth softening and dispersion.
                  * waterShaftField(): distinct, swaying volumetric beam
                    modulation driven directly by the ocean swells overhead.
                  * sunThroughWater(): depth absorption along the refracted
                    sun ray (red absorbed first).
                  * openWater(): water colour along a line of sight.

                  Pasted into the shaders after "#version 120".

*************************************************************************/
#pragma once

// Shared by the water surface shaders and underwater optics: the large waves.
// Gerstner waves move each point of the surface round a circle instead of
// just up and down, which sharpens the crests and flattens the troughs.
// Eight waves with unrelated lengths and directions spread round the wind's
// avoid any repeating grid of crests.
const char *const kOceanWavesGlsl = R"GLSL(
// Slow spatial curvature so long swells bend and break instead of forming
// straight infinite ridges across the ocean.
vec2 waveWarp(vec2 p, float t)
{
    return vec2(sin(p.y * 0.023 + t * 0.17) * 5.5 + cos((p.x - p.y) * 0.017 - t * 0.13) * 4.0,
                cos(p.x * 0.021 - t * 0.15) * 5.5 + sin((p.x + p.y) * 0.019 + t * 0.11) * 4.0);
}

// Patchy wave-group envelope (sets of larger swells separated by calmer lulls).
float waveGroup(vec2 p, float t, float seed)
{
    float a = sin(p.x * 0.029 - p.y * 0.018 - t * 0.23 + seed);
    float b = cos(p.x * 0.015 + p.y * 0.031 + t * 0.19 - seed * 1.7);
    return 0.62 + 0.38 * (0.5 + 0.5 * a * b) * 1.6;
}

void gerstner(vec2 dir, float len, float amp, float phase, vec2 p, float t, float shortest,
              inout vec3 disp, inout vec3 slope)
{
    if (len < shortest)
        return;
    float k = 6.2831853 / len;
    float w = sqrt(9.8 * k) * 0.85;
    float ph = k * dot(dir, p) - w * t + phase;
    float q = 0.085 / (k * max(amp, 0.001));
    disp.xz += dir * (q * amp * cos(ph));
    disp.y += amp * sin(ph);
    slope.xz += dir * (k * amp * cos(ph));
    slope.y += q * k * amp * sin(ph);
}

// Displacement and normal of the surface at rest position p, from the
// waves at least `shortest` long (the grid's vertices are too far apart to
// follow the shortest ones, which only shade it).
void oceanWaves(vec2 p, float t, float shortest, out vec3 disp, out vec3 normal)
{
    disp = vec3(0.0);
    vec3 s = vec3(0.0);
    vec2 wp = p + waveWarp(p, t);
    float gA = waveGroup(p, t, 0.4);
    float gB = waveGroup(p, t, 2.7);
    gerstner(normalize(vec2(0.92, 0.39)), 43.7, 0.38 * gA, 0.0, wp, t, shortest, disp, s);
    gerstner(normalize(vec2(0.58, -0.81)), 29.3, 0.24 * gB, 2.1, wp, t, shortest, disp, s);
    gerstner(normalize(vec2(-0.31, 0.95)), 21.1, 0.18 * gA, 4.4, wp, t, shortest, disp, s);
    gerstner(normalize(vec2(0.87, -0.49)), 15.7, 0.13 * gB, 1.3, wp, t, shortest, disp, s);
    gerstner(normalize(vec2(-0.64, 0.77)), 11.9, 0.095 * gA, 5.6, wp, t, shortest, disp, s);
    gerstner(normalize(vec2(0.36, 0.93)), 8.9, 0.068 * gB, 3.2, wp, t, shortest, disp, s);
    gerstner(normalize(vec2(-0.82, -0.57)), 6.7, 0.050 * gA, 0.7, wp, t, shortest, disp, s);
    gerstner(normalize(vec2(0.76, 0.65)), 4.9, 0.034 * gB, 4.9, wp, t, shortest, disp, s);
    normal = normalize(vec3(-s.x, 1.0 - s.y, -s.z));
}

// Analytic slope (grad) and symmetric 2x2 curvature Hessian (Hxx, Hzz, Hxz) of
// one Gerstner wave, for optical caustic focusing and volumetric light shafts.
void gerstnerGradHessian(vec2 dir, float len, float amp, float phase, vec2 p, float t,
                         float shortest, inout vec2 grad, inout vec3 hess)
{
    if (len < shortest)
        return;
    float k = 6.2831853 / len;
    float w = sqrt(9.8 * k) * 0.85;
    float ph = k * dot(dir, p) - w * t + phase;
    grad += dir * (k * amp * cos(ph));
    float h = -k * k * amp * sin(ph);
    hess += vec3(h * dir.x * dir.x, h * dir.y * dir.y, h * dir.x * dir.y);
}

void oceanWavesGradHessian(vec2 p, float t, float shortest, out vec2 grad, out vec3 hess)
{
    grad = vec2(0.0);
    hess = vec3(0.0);
    vec2 wp = p + waveWarp(p, t);
    float gA = waveGroup(p, t, 0.4);
    float gB = waveGroup(p, t, 2.7);
    gerstnerGradHessian(normalize(vec2(0.92, 0.39)), 43.7, 0.38 * gA, 0.0, wp, t, shortest, grad,
                        hess);
    gerstnerGradHessian(normalize(vec2(0.58, -0.81)), 29.3, 0.24 * gB, 2.1, wp, t, shortest, grad,
                        hess);
    gerstnerGradHessian(normalize(vec2(-0.31, 0.95)), 21.1, 0.18 * gA, 4.4, wp, t, shortest, grad,
                        hess);
    gerstnerGradHessian(normalize(vec2(0.87, -0.49)), 15.7, 0.13 * gB, 1.3, wp, t, shortest, grad,
                        hess);
    gerstnerGradHessian(normalize(vec2(-0.64, 0.77)), 11.9, 0.095 * gA, 5.6, wp, t, shortest, grad,
                        hess);
    gerstnerGradHessian(normalize(vec2(0.36, 0.93)), 8.9, 0.068 * gB, 3.2, wp, t, shortest, grad,
                        hess);
    gerstnerGradHessian(normalize(vec2(-0.82, -0.57)), 6.7, 0.050 * gA, 0.7, wp, t, shortest, grad,
                        hess);
    gerstnerGradHessian(normalize(vec2(0.76, 0.65)), 4.9, 0.034 * gB, 4.9, wp, t, shortest, grad,
                        hess);
}
)GLSL";

const char *const kUnderwaterOpticsGlsl = R"GLSL(
// Sunlight lost per world unit of depth (red, green, blue).
const vec3 kWaterAbsorb = vec3(0.020, 0.0070, 0.0045);
// Light lost per world unit along the line of sight.
const vec3 kFogExtinction = vec3(0.0195, 0.0086, 0.0060);

// Direction towards the sun or moon after refracting into water (n_air / n_water = 0.75).
vec3 refractedLightDir(vec3 lightDir)
{
    vec2 hz = lightDir.xz * 0.75;
    float vy = sqrt(max(1.0 - dot(hz, hz), 0.16));
    return vec3(hz.x, vy, hz.y);
}

float opticsHash12(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float opticsValueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(opticsHash12(i), opticsHash12(i + vec2(1.0, 0.0)), u.x),
               mix(opticsHash12(i + vec2(0.0, 1.0)), opticsHash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Analytic gradient and C2-continuous 2x2 Hessian (Hxx, Hyy, Hxy) of quintic
// value noise: second derivative vanishes at cell edges so Jacobian caustics
// show zero lattice seams.
void quinticNoiseGH(vec2 p, out vec2 grad, out vec3 hess)
{
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f - 1.0) * (f - 1.0);
    vec2 d2u = 60.0 * f * (f - 1.0) * (2.0 * f - 1.0);
    float a = opticsHash12(i);
    float b = opticsHash12(i + vec2(1.0, 0.0));
    float c = opticsHash12(i + vec2(0.0, 1.0));
    float d = opticsHash12(i + vec2(1.0, 1.0));
    float k1 = b - a;
    float k2 = c - a;
    float k3 = a - b - c + d;
    grad = vec2((k1 + k3 * u.y) * du.x, (k2 + k3 * u.x) * du.y);
    hess = vec3((k1 + k3 * u.y) * d2u.x, (k2 + k3 * u.x) * d2u.y, k3 * du.x * du.y);
}

void accumRotatedRippleGH(vec2 p, float cRot, float sRot, float scale, vec2 offset, float amp,
                          inout vec2 grad, inout vec3 hess)
{
    vec2 q = scale * vec2(cRot * p.x - sRot * p.y, sRot * p.x + cRot * p.y) + offset;
    vec2 gq;
    vec3 hq;
    quinticNoiseGH(q, gq, hq);
    float gs = amp * scale;
    grad += gs * vec2(cRot * gq.x + sRot * gq.y, -sRot * gq.x + cRot * gq.y);
    float hs = amp * scale * scale;
    float m00 = hq.x * cRot + hq.z * sRot;
    float m01 = -hq.x * sRot + hq.z * cRot;
    float m10 = hq.z * cRot + hq.y * sRot;
    float m11 = -hq.z * sRot + hq.y * cRot;
    hess += hs * vec3(cRot * m00 + sRot * m10, -sRot * m01 + cRot * m11, cRot * m01 + sRot * m11);
}

// Combined water surface slope and curvature from the real 8 oceanWaves plus
// three rotated C2 quintic capillary ripple octaves at ocean-block tempo.
void oceanSurfaceGH(vec2 p, float t, float detail, out vec2 grad, out vec3 hess)
{
    oceanWavesGradHessian(p, t, 0.0, grad, hess);
    grad *= 2.8;
    hess *= 5.4;
    accumRotatedRippleGH(p, 0.94, 0.34, 0.19, vec2(-t * 0.84, -t * 0.32), 2.8, grad, hess);
    accumRotatedRippleGH(p, 0.65, -0.76, 0.32, vec2(t * 0.64 + 3.1, -t * 0.72 - 1.7), 1.55, grad,
                         hess);
    if (detail > 0.02)
        accumRotatedRippleGH(p, 0.31, 0.95, 0.54, vec2(-t * 1.05 - 4.3, t * 0.58 + 2.2),
                             0.62 * detail, grad, hess);
}

float jacobianIntensity(vec3 hess, float lever, float soft)
{
    float jxx = 1.0 + lever * hess.x;
    float jzz = 1.0 + lever * hess.y;
    float jxz = lever * hess.z;
    float det = jxx * jzz - jxz * jxz;
    return clamp((1.0 + soft) / (abs(det) + soft), 0.50, 2.75);
}

// Coloured caustic multiplier for a point below the surface: uses first-order
// inverse-ray fold warping, analytic Jacobian of oceanWaves + ripples, and
// macro swell lensing from the large overhead waves so light sweeps across
// the sea floor even at deep ride depths (25-40 units).
vec3 causticsRgb(vec3 p, vec3 lightDir, float waterLevel, float t)
{
    float down = max(waterLevel - p.y, 0.0);
    vec3 lr = refractedLightDir(lightDir);
    vec2 s = p.xz + lr.xz / lr.y * down;

    float nearSurface = smoothstep(0.0, 2.0, down);
    float deepSoft = clamp((down - 10.0) / 55.0, 0.0, 1.0);
    float detail = 1.0 - 0.55 * deepSoft;
    float strength = nearSurface * mix(1.0, 0.72, deepSoft);
    float lever = mix(0.88, 1.42, smoothstep(0.0, 10.0, down)) * mix(1.0, 0.85, deepSoft);

    vec2 grad0;
    vec3 hess0;
    oceanSurfaceGH(s, t, detail, grad0, hess0);
    vec2 warped = s - grad0 * (lever * 0.36);
    vec2 gradW;
    vec3 hessW;
    oceanSurfaceGH(warped, t, detail, gradW, hessW);

    // Broad light-and-shadow bands cast by the 5 large ocean swells overhead.
    vec2 swellGrad;
    vec3 swellHess;
    oceanWavesGradHessian(s, t, 12.0, swellGrad, swellHess);
    float swell = mix(1.0, mix(0.68, 1.40, smoothstep(-0.042, 0.046, -(swellHess.x + swellHess.y))),
                      nearSurface);

    float soft = mix(0.70, 0.36, detail);
    float dispScale = 0.08 * (1.0 - 0.35 * deepSoft);
    vec3 focused = vec3(jacobianIntensity(hessW, lever * (1.0 - dispScale), soft),
                        jacobianIntensity(hessW, lever, soft),
                        jacobianIntensity(hessW, lever * (1.0 + dispScale), soft));
    return mix(vec3(1.0), focused, strength) * swell;
}

// Irregular, domain-warped multi-scale volumetric sunbeams: sparse macro
// bundles leave wide calm gaps, heavily warped medium shafts vary in width
// and spacing, and transient narrow needles shimmer unpredictably inside,
// subtly steered by the curved overhead oceanWaves slopes.
float waterShaftField(vec2 s, float depth, float t)
{
    vec2 waveGrad;
    vec3 waveHess;
    oceanWavesGradHessian(s, t, 12.0, waveGrad, waveHess);
    vec2 warp =
        vec2(opticsValueNoise(s * 0.032 + vec2(t * 0.14, -t * 0.11)),
             opticsValueNoise(s * 0.032 + vec2(-t * 0.12, t * 0.15) + vec2(17.3, 9.1))) -
        0.5;
    vec2 r = s + warp * (16.0 + min(depth, 30.0) * 0.30) - waveGrad * (min(depth, 28.0) * 0.75);

    // Sparse asymmetric bundles (~50-70 unit calm stretches vs sunlit clusters).
    float m1 = opticsValueNoise(r * 0.024 + vec2(t * 0.07, -t * 0.05) + vec2(0.0, 3.1));
    float m2 = opticsValueNoise(mat2(0.68, 0.73, -0.73, 0.68) * (r * 0.031) +
                                vec2(-t * 0.06, t * 0.08) + vec2(8.4, -5.2));
    float bundle = smoothstep(0.14, 0.56, m1 * m2);

    // Uneven medium shafts with non-linear time wobble to break steady rhythm.
    float t1 = t * 0.24 + sin(t * 0.37) * 0.35;
    float t2 = t * 0.21 + cos(t * 0.43) * 0.35;
    vec2 a1 = mat2(0.91, 0.41, -0.41, 0.91) * (r * 0.058) + vec2(t1, -t2 * 0.8);
    vec2 a2 =
        mat2(0.53, -0.85, 0.85, 0.53) * (r * 0.095) + vec2(-t2, t1 * 0.7) + vec2(7.4, -3.9);
    float med = smoothstep(0.16, 0.54, opticsValueNoise(a1) * opticsValueNoise(a2));

    // Narrow, transient flickering slivers inside the clusters.
    vec2 b1 =
        mat2(0.37, 0.93, -0.93, 0.37) * (r * 0.17) + vec2(t * 0.44, t * 0.33) + vec2(2.1, -6.3);
    vec2 b2 = mat2(-0.84, 0.54, -0.54, -0.84) * (r * 0.22) + vec2(-t * 0.40, -t * 0.35) +
              vec2(-4.8, 9.2);
    float needle = smoothstep(0.22, 0.60, opticsValueNoise(b1) * opticsValueNoise(b2));

    float nearDetail = 1.0 - smoothstep(4.0, 30.0, depth);
    float swellPulse = mix(0.84, 1.22, smoothstep(-0.042, 0.046, -(waveHess.x + waveHess.y)));
    float shaft = bundle * (0.28 + 1.15 * med + 1.65 * needle * (0.35 + 0.65 * nearDetail)) *
                  swellPulse;
    shaft += 0.38 * med * needle * nearDetail;
    float depthFade = exp(-depth * 0.017);
    return (0.02 + 1.85 * shaft) * depthFade;
}

// Fraction of sunlight that reaches a point at this depth along the refracted ray.
vec3 sunThroughWater(float depth, vec3 lightDir)
{
    vec3 lr = refractedLightDir(lightDir);
    return exp(-kWaterAbsorb * (max(depth, 0.0) / lr.y));
}

// Colour of the open water looking along v.
vec3 openWater(vec3 v, vec3 lightDir, vec3 lightColor, vec3 shallow, vec3 deep)
{
    vec3 c = mix(deep, shallow, smoothstep(-0.35, 0.75, v.y));
    float sun = max(dot(v, lightDir), 0.0);
    return c + lightColor * vec3(0.03, 0.10, 0.13) * (pow(sun, 6.0) * 0.5 + pow(sun, 40.0));
}
)GLSL";
