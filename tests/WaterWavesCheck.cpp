// Checks the sea's wave models (WaterWaves.h): the height map sequence is cut out of its atlas and
// sampled as the 3D texture does, the layers add up to waves of the right size, and the wave
// equation's grid spreads a drop at the wave speed, stays stable, dies away, keeps ripples in
// place when it moves, and is stirred by the train, the rain and a stone where they should.
#include "WaterWaves.h"

#include <math.h>
#include <stdio.h>

#include <vector>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

// The distance from (cx, cz) to the cell holding the highest |h| along the row through it.
float frontRadius(const WaterWaves::WaveGrid &grid, int cx, int cz)
{
    float best = 0.0f;
    int at = cx;
    for (int x = cx; x < grid.cells(); ++x)
        if (fabsf(grid.height(x, cz)) > best)
        {
            best = fabsf(grid.height(x, cz));
            at = x;
        }
    return static_cast<float>(at - cx) * WaterWaves::kGridCell;
}
} // namespace

int main()
{
    using namespace WaterWaves;

    // ---- the models ----
    expect(next(WaveModel::Gerstner) == WaveModel::HeightMap &&
               next(WaveModel::HeightMap) == WaveModel::Equation &&
               next(WaveModel::Equation) == WaveModel::Gerstner,
           "V goes Gerstner, Height Map, Wave Equation and round again");
    expect(fromIndex(7) == WaveModel::Gerstner && fromIndex(2) == WaveModel::Equation,
           "a setting out of range is Gerstner");

    // ---- height maps ----
    {
        // a 3 x 2 atlas of 4 x 4 frames, 5 used: each texel holds its frame number * 10 + x
        const int size = 4, columns = 3, width = size * columns, height = size * 2;
        std::vector<unsigned char> atlas(static_cast<size_t>(width * height), 255);
        for (int f = 0; f < 5; ++f)
            for (int z = 0; z < size; ++z)
                for (int x = 0; x < size; ++x)
                    atlas[static_cast<size_t>(((f / columns) * size + z) * width +
                                              (f % columns) * size + x)] =
                        static_cast<unsigned char>(f * 10 + x);
        HeightFrames frames;
        expect(fromAtlas(atlas.data(), width, height, columns, 5, frames) && frames.size == 4 &&
                   frames.count == 5,
               "an atlas is cut into its frames");
        expect(fabsf(frames.at(3, 2, 1) * 255.0f - 32.0f) < 0.01f &&
                   fabsf(frames.at(4, 1, 3) * 255.0f - 41.0f) < 0.01f,
               "frames are read row by row, left to right");
        expect(fabsf(frames.at(5, 0, 0) - frames.at(0, 0, 0)) < 1e-6f &&
                   fabsf(frames.at(0, -1, 0) - frames.at(0, 3, 0)) < 1e-6f,
               "frames and texels wrap round");
        expect(!fromAtlas(atlas.data(), width, height, columns, 7, frames),
               "an atlas too small for the frames asked for is refused");
        // at a texel's centre the sample is that texel, as GL samples it
        HeightFrames one;
        fromAtlas(atlas.data(), width, height, columns, 5, one);
        const float u = 2.5f / 4.0f, v = 1.5f / 4.0f, w = 3.5f / 5.0f;
        expect(fabsf(one.sample(u, v, w) - one.at(3, 2, 1)) < 1e-5f,
               "sampling at a texel's centre gives that texel");
        expect(fabsf(one.sample(u + 1.0f, v - 2.0f, w + 1.0f) - one.sample(u, v, w)) < 1e-5f,
               "sampling repeats in all three directions");
    }

    HeightFrames course;
    std::string error;
    const bool loaded = loadAtlas("assets/heightmaps/waves.png", 10, 100, course, error);
    expect(loaded && course.size == 256 && course.count == 100,
           "the shipped atlas holds 100 frames of 256 x 256");
    if (loaded)
    {
        expect(course.mean > 0.3f && course.mean < 0.7f, "the pictures' still level is mid grey");
        // the course's pictures tile: across an edge they change no more than between
        // neighbouring texels inside
        double across = 0.0, inside = 0.0;
        for (int z = 0; z < course.size; ++z)
        {
            across += fabsf(course.at(0, 0, z) - course.at(0, course.size - 1, z));
            inside += fabsf(course.at(0, 1, z) - course.at(0, 2, z));
        }
        expect(across < inside * 2.0, "a frame tiles without a seam");
        double loop = 0.0, step = 0.0;
        for (int z = 0; z < course.size; z += 3)
            for (int x = 0; x < course.size; x += 3)
            {
                loop += fabsf(course.at(99, x, z) - course.at(0, x, z));
                step += fabsf(course.at(40, x, z) - course.at(41, x, z));
            }
        expect(loop < step * 2.0, "the sequence loops: its last frame leads into its first");

        float lo = 1e9f, hi = -1e9f;
        double sum = 0.0;
        int n = 0;
        for (float x = -60.0f; x < 60.0f; x += 1.7f)
            for (float z = -60.0f; z < 60.0f; z += 1.9f)
            {
                const float h = heightAt(course, x, z, 3.0f);
                lo = fminf(lo, h);
                hi = fmaxf(hi, h);
                sum += h;
                ++n;
            }
        printf("  height maps: %.2f to %.2f, mean %.3f\n", static_cast<double>(lo),
               static_cast<double>(hi), sum / n);
        expect(hi - lo > 0.4f && hi - lo < 2.0f,
               "the layers make waves a block or so from trough to crest");
        expect(fabs(sum / n) < 0.08, "on average the surface stays at the still water's level");
        expect(fabsf(heightAt(course, 10.0f, 4.0f, 0.0f) - heightAt(course, 10.0f, 4.0f, 1.5f)) >
                   1e-3f,
               "the waves move with time");
    }
    HeightFrames fallback = fallbackFrames();
    expect(!fallback.empty() && fallback.mean > 0.4f && fallback.mean < 0.6f,
           "made-up frames stand in when no pictures are found");

    // ---- the wave equation ----
    expect(courantSquared() > 0.0f && courantSquared() < 0.5f,
           "the steps are stable (k below 1/2)");
    {
        const int n = 160;
        WaveGrid grid(n);
        int sx, sz;
        grid.follow(0.0f, 0.0f, sx, sz);
        const int c = n / 2;
        const float cx = grid.originX() + (static_cast<float>(c) + 0.5f) * kGridCell;
        const float cz = grid.originZ() + (static_cast<float>(c) + 0.5f) * kGridCell;
        grid.addDrop(stone(cx, cz));
        expect(grid.height(c, c) < -0.5f && grid.height(c + 6, c) == 0.0f,
               "a stone pushes the water down where it lands, and only there");
        const double start = grid.energy();
        const int steps = 90; // 1.5 s
        for (int i = 0; i < steps; ++i)
            grid.step();
        const float travelled = frontRadius(grid, c, c);
        const float expected = kWaveSpeed * static_cast<float>(steps) * kStepSeconds;
        printf("  front at %.1f units after %.1f s (c t = %.1f)\n", static_cast<double>(travelled),
               static_cast<double>(steps * kStepSeconds), static_cast<double>(expected));
        expect(travelled > expected * 0.6f && travelled < expected * 1.15f,
               "the ring spreads at about the wave speed");
        expect(fabsf(grid.height(c + 10, c) - grid.height(c - 10, c)) < 1e-4f &&
                   fabsf(grid.height(c + 10, c) - grid.height(c, c + 10)) < 1e-4f,
               "the ring is round");
        for (int i = 0; i < 1200; ++i)
            grid.step();
        expect(grid.energy() < start * 0.05, "the ripples die away");
        float biggest = 0.0f;
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x)
                biggest = fmaxf(biggest, fabsf(grid.height(x, z)));
        expect(biggest < 0.2f, "and never grow (no blow-up after 21 s)");
    }
    {
        // moving the grid keeps a ripple where it is in the world
        WaveGrid grid(96);
        int sx, sz;
        grid.follow(0.0f, 0.0f, sx, sz);
        grid.addDrop({3.0f, -2.0f, 2.0f, 1.0f});
        const int x0 = static_cast<int>((3.0f - grid.originX()) / kGridCell);
        const int z0 = static_cast<int>((-2.0f - grid.originZ()) / kGridCell);
        const float before = grid.height(x0, z0);
        grid.follow(4.2f, -3.1f, sx, sz);
        expect(sx == 8 && sz == -7, "following the camera moves the grid in whole cells");
        const int x1 = static_cast<int>((3.0f - grid.originX()) / kGridCell);
        const int z1 = static_cast<int>((-2.0f - grid.originZ()) / kGridCell);
        expect(x1 == x0 - sx && z1 == z0 - sz && fabsf(grid.height(x1, z1) - before) < 1e-6f,
               "a ripple stays at the same place in the world");
        grid.follow(1000.0f, 0.0f, sx, sz);
        expect(grid.energy() == 0.0, "a jump further than the grid starts it still");
    }

    // ---- what stirs it ----
    {
        TrainSplash splash;
        std::vector<Drop> drops;
        const float level = -1.0f;
        // one cart diving through the surface: above it, then below it
        splash.update({0.0f, 1.0f, 0.0f}, level, 0.1f, drops);
        expect(drops.empty(), "the first look only remembers where the train is");
        splash.update({1.0f, -2.5f, 0.0f}, level, 0.1f, drops);
        bool splashIn = false;
        for (const Drop &d : drops)
            splashIn = splashIn || (d.radius > 2.0f && d.strength < -0.3f);
        expect(splashIn, "going into the sea makes a big splash pushing the water down");
        drops.clear();
        splash.update({1.0f, 0.5f, 0.0f}, level, 0.1f, drops);
        bool splashOut = false;
        for (const Drop &d : drops)
            splashOut = splashOut || d.strength > 0.0f;
        expect(splashOut, "coming out lifts the water");
        // skimming along the surface leaves a wake; standing still or deep under it does not
        drops.clear();
        splash.update({1.0f, -1.3f, 0.0f}, level, 0.1f, drops);
        splash.update({2.0f, -1.3f, 0.0f}, level, 0.1f, drops);
        bool wake = false;
        for (const Drop &d : drops)
            wake = wake || (d.strength < 0.0f && d.radius < 2.0f);
        expect(wake, "a cart skimming the surface leaves a wake");
        drops.clear();
        splash.update({2.0f, -1.3f, 0.0f}, level, 0.1f, drops);
        expect(drops.empty(), "a train standing in the water makes no waves");
        splash.update({2.0f, -20.0f, 0.0f}, level, 0.1f, drops);
        drops.clear();
        splash.update({4.0f, -20.0f, 0.0f}, level, 0.1f, drops);
        expect(drops.empty(), "deep under the surface it does not stir it");
        splash.update({300.0f, 3.0f, 0.0f}, level, 0.1f, drops);
        expect(drops.empty(), "the train put somewhere else is not a ride through the water");
    }
    {
        std::vector<Drop> drops;
        unsigned seed = 7u;
        for (int i = 0; i < 100; ++i)
            rainDrops(1.0f, 0.05f, 10.0f, 20.0f, 30.0f, seed, drops);
        bool inside = true;
        for (const Drop &d : drops)
            inside = inside && hypotf(d.x - 10.0f, d.z - 20.0f) <= 30.0f && d.strength < 0.0f;
        expect(drops.size() > 250 && drops.size() < 350 && inside,
               "heavy rain lands about 60 drops a second round the camera");
        drops.clear();
        rainDrops(0.0f, 1.0f, 0.0f, 0.0f, 30.0f, seed, drops);
        expect(drops.empty(), "no rain, no drops");
    }
    {
        const double eye[3] = {0.0, 10.0, 0.0}, down[3] = {0.6, -0.8, 0.0}, up[3] = {0.0, 1.0, 0.0};
        float x = 0.0f, z = 0.0f;
        expect(hitWater(eye, down, -1.0f, x, z) && fabsf(x - 8.25f) < 1e-4f && fabsf(z) < 1e-6f,
               "a line of sight down meets the sea where it should");
        expect(!hitWater(eye, up, -1.0f, x, z), "looking up from above never meets it");
    }

    if (failures)
        printf("%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
