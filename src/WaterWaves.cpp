/************************************************************************
     File:        WaterWaves.cpp

     Comment:     See WaterWaves.h.

*************************************************************************/

#include "WaterWaves.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include <stb_image.h>

#include "Utilities/Utf8File.H"

namespace WaterWaves
{

namespace
{
const float kPi = 3.14159265f;

int wrap(int i, int n)
{
    const int r = i % n;
    return r < 0 ? r + n : r;
}

float fract(float x)
{
    return x - floorf(x);
}

// The whole file, or nothing when it cannot be read.
std::vector<unsigned char> readFile(const std::string &path)
{
    std::vector<unsigned char> bytes;
    FILE *fp = openUtf8(path.c_str(), "rb");
    if (!fp)
        return bytes;
    std::vector<unsigned char> buffer(65536);
    size_t got;
    while ((got = fread(buffer.data(), 1, buffer.size(), fp)) > 0)
        bytes.insert(bytes.end(), buffer.begin(),
                     buffer.begin() + static_cast<std::ptrdiff_t>(got));
    fclose(fp);
    return bytes;
}

// A PNG as grey levels (the red channel of whatever it holds).
bool readGrey(const std::string &path, int &width, int &height, std::vector<unsigned char> &grey)
{
    const std::vector<unsigned char> file = readFile(path);
    if (file.empty() || file.size() > 256u * 1024u * 1024u)
        return false;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
                                                  &width, &height, &channels, 1);
    if (!pixels)
        return false;
    grey.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height));
    stbi_image_free(pixels);
    return width > 0 && height > 0;
}

void findMean(HeightFrames &frames)
{
    double sum = 0.0;
    for (unsigned char t : frames.texels)
        sum += t;
    frames.mean = frames.texels.empty()
                      ? 0.5f
                      : static_cast<float>(sum / static_cast<double>(frames.texels.size()) / 255.0);
}

// The three layers: a broad swell, a cross sea about half its size from
// another way, and a fine chop; their sizes, turns and speeds in no simple
// ratio so the copies of the picture never line up.
const HeightLayer kLayers[kHeightLayers] = {
    {52.0f, 0.35f, 0.0f, 0.0f, 9.0f, 0.0f, 2.40f},
    {23.0f, 1.45f, 0.31f, 0.62f, 6.3f, 0.37f, 0.90f},
    {9.5f, 2.60f, 0.73f, 0.17f, 4.1f, 0.71f, 0.30f},
};
} // namespace

const char *name(WaveModel model)
{
    switch (model)
    {
    case WaveModel::HeightMap:
        return "Height Map";
    case WaveModel::Equation:
        return "Wave Equation";
    case WaveModel::Gerstner:
    default:
        return "Gerstner";
    }
}

WaveModel next(WaveModel model)
{
    return fromIndex((static_cast<int>(model) + 1) % kModels);
}

WaveModel fromIndex(int index)
{
    if (index == 1)
        return WaveModel::HeightMap;
    if (index == 2)
        return WaveModel::Equation;
    return WaveModel::Gerstner;
}

//************************************************************************
// Height maps
//========================================================================
float HeightFrames::at(int frame, int x, int z) const
{
    if (empty())
        return 0.5f;
    const size_t f = static_cast<size_t>(wrap(frame, count));
    const size_t row = static_cast<size_t>(wrap(z, size));
    const size_t column = static_cast<size_t>(wrap(x, size));
    const size_t side = static_cast<size_t>(size);
    return static_cast<float>(texels[(f * side + row) * side + column]) / 255.0f;
}

float HeightFrames::sample(float u, float v, float w) const
{
    if (empty())
        return 0.5f;
    // texel centres at (i + 0.5) / size, as GL samples
    const float x = u * static_cast<float>(size) - 0.5f;
    const float z = v * static_cast<float>(size) - 0.5f;
    const float f = w * static_cast<float>(count) - 0.5f;
    const int x0 = static_cast<int>(floorf(x)), z0 = static_cast<int>(floorf(z)),
              f0 = static_cast<int>(floorf(f));
    const float tx = x - floorf(x), tz = z - floorf(z), tf = f - floorf(f);
    float result = 0.0f;
    for (int c = 0; c < 8; ++c)
    {
        const int dx = c & 1, dz = (c >> 1) & 1, df = (c >> 2) & 1;
        const float weight = ((dx != 0) ? tx : 1.0f - tx) * ((dz != 0) ? tz : 1.0f - tz) *
                             ((df != 0) ? tf : 1.0f - tf);
        result += weight * at(f0 + df, x0 + dx, z0 + dz);
    }
    return result;
}

bool fromAtlas(const unsigned char *grey, int width, int height, int columns, int count,
               HeightFrames &out)
{
    if (!grey || columns <= 0 || count <= 0 || width % columns != 0)
        return false;
    const int size = width / columns;
    const int rows = (count + columns - 1) / columns;
    if (size <= 0 || height < rows * size)
        return false;
    out.size = size;
    out.count = count;
    const size_t side = static_cast<size_t>(size);
    out.texels.resize(side * side * static_cast<size_t>(count));
    for (int f = 0; f < count; ++f)
    {
        const size_t left = static_cast<size_t>((f % columns) * size);
        const size_t top = static_cast<size_t>((f / columns) * size);
        for (size_t z = 0; z < side; ++z)
        {
            const unsigned char *from = grey + (top + z) * static_cast<size_t>(width) + left;
            std::copy(from, from + side,
                      out.texels.begin() +
                          static_cast<std::ptrdiff_t>((static_cast<size_t>(f) * side + z) * side));
        }
    }
    findMean(out);
    return true;
}

bool loadSequence(const std::string &folder, HeightFrames &out, std::string &error)
{
    HeightFrames frames;
    for (int f = 0; f < 1000; ++f)
    {
        char name[16];
        snprintf(name, sizeof(name), "/%03d.png", f);
        const std::string path = folder + name;
        int width = 0, height = 0;
        std::vector<unsigned char> grey;
        if (!readGrey(path, width, height, grey))
        {
            if (f == 0)
            {
                error = "no " + path;
                return false;
            }
            break; // the end of the sequence
        }
        if (width != height || (f > 0 && width != frames.size))
        {
            error = path + " is not the same square size as 000.png";
            return false;
        }
        frames.size = width;
        frames.texels.insert(frames.texels.end(), grey.begin(), grey.end());
        frames.count = f + 1;
    }
    findMean(frames);
    out = std::move(frames);
    return true;
}

bool loadAtlas(const std::string &path, int columns, int count, HeightFrames &out,
               std::string &error)
{
    int width = 0, height = 0;
    std::vector<unsigned char> grey;
    if (!readGrey(path, width, height, grey))
    {
        error = "cannot read " + path;
        return false;
    }
    if (!fromAtlas(grey.data(), width, height, columns, count, out))
    {
        error = path + " is not an atlas of the expected frames";
        return false;
    }
    return true;
}

HeightFrames fallbackFrames()
{
    // round bumps drifting across and back, so it loops
    HeightFrames frames;
    frames.size = 64;
    frames.count = 32;
    frames.texels.resize(64u * 64u * 32u);
    for (int f = 0; f < frames.count; ++f)
        for (int z = 0; z < 64; ++z)
            for (int x = 0; x < 64; ++x)
            {
                const float t = static_cast<float>(f) / 32.0f * 2.0f * kPi;
                const float u = static_cast<float>(x) / 64.0f * 2.0f * kPi;
                const float v = static_cast<float>(z) / 64.0f * 2.0f * kPi;
                const float h = 0.5f + 0.2f * sinf(u * 2.0f + t) + 0.15f * sinf(v * 3.0f - t) +
                                0.1f * sinf((u + v) * 2.0f + 2.0f * t);
                frames.texels[(static_cast<size_t>(f) * 64u + static_cast<size_t>(z)) * 64u +
                              static_cast<size_t>(x)] =
                    static_cast<unsigned char>(std::min(255.0f, std::max(0.0f, h * 255.0f)));
            }
    findMean(frames);
    return frames;
}

const HeightLayer &heightLayer(int index)
{
    return kLayers[std::min(std::max(index, 0), kHeightLayers - 1)];
}

void layerMatrix(const HeightLayer &layer, float a[4])
{
    const float c = cosf(layer.angle) / layer.tile, s = sinf(layer.angle) / layer.tile;
    a[0] = c;
    a[1] = s;
    a[2] = -s;
    a[3] = c;
}

float heightAt(const HeightFrames &frames, float x, float z, float t)
{
    float h = 0.0f;
    for (int i = 0; i < kHeightLayers; ++i)
    {
        const HeightLayer &layer = kLayers[i];
        float a[4];
        layerMatrix(layer, a);
        const float u = a[0] * x + a[1] * z + layer.offsetU;
        const float v = a[2] * x + a[3] * z + layer.offsetV;
        const float w = fract(t / layer.loopSeconds + layer.phase);
        h += layer.amplitude * (frames.sample(u, v, w) - frames.mean);
    }
    return h;
}

//************************************************************************
// Wave equation
//========================================================================
float courantSquared()
{
    const float r = kWaveSpeed * kStepSeconds / kGridCell;
    return r * r;
}

WaveGrid::WaveGrid(int cells)
    : m_cells(cells), m_now(static_cast<size_t>(cells) * static_cast<size_t>(cells), 0.0f),
      m_before(m_now)
{
}

float WaveGrid::height(int x, int z) const
{
    if (x < 0 || z < 0 || x >= m_cells || z >= m_cells)
        return 0.0f;
    return m_now[static_cast<size_t>(z) * static_cast<size_t>(m_cells) + static_cast<size_t>(x)];
}

void WaveGrid::snap(float x, float z, int cells, float &originX, float &originZ)
{
    const float half = static_cast<float>(cells / 2);
    originX = (floorf(x / kGridCell) - half) * kGridCell;
    originZ = (floorf(z / kGridCell) - half) * kGridCell;
}

void WaveGrid::follow(float x, float z, int &shiftX, int &shiftZ)
{
    float ox, oz;
    snap(x, z, m_cells, ox, oz);
    shiftX = shiftZ = 0;
    if (m_placed)
    {
        shiftX = static_cast<int>(lroundf((ox - m_originX) / kGridCell));
        shiftZ = static_cast<int>(lroundf((oz - m_originZ) / kGridCell));
    }
    m_placed = true;
    m_originX = ox;
    m_originZ = oz;
    if (shiftX != 0 || shiftZ != 0)
        shift(shiftX, shiftZ);
}

void WaveGrid::shift(int dx, int dz)
{
    if (std::abs(dx) >= m_cells || std::abs(dz) >= m_cells)
    {
        clear();
        return;
    }
    std::vector<float> now(m_now.size(), 0.0f), before(m_before.size(), 0.0f);
    const size_t n = static_cast<size_t>(m_cells);
    for (int z = 0; z < m_cells; ++z)
        for (int x = 0; x < m_cells; ++x)
        {
            const int fx = x + dx, fz = z + dz;
            if (fx < 0 || fz < 0 || fx >= m_cells || fz >= m_cells)
                continue;
            const size_t to = static_cast<size_t>(z) * n + static_cast<size_t>(x);
            const size_t from = static_cast<size_t>(fz) * n + static_cast<size_t>(fx);
            now[to] = m_now[from];
            before[to] = m_before[from];
        }
    m_now.swap(now);
    m_before.swap(before);
}

void dropInCells(const Drop &drop, float originX, float originZ, float &cx, float &cz,
                 float &radius)
{
    cx = (drop.x - originX) / kGridCell;
    cz = (drop.z - originZ) / kGridCell;
    radius = drop.radius / kGridCell;
}

void WaveGrid::addDrop(const Drop &drop)
{
    float cx, cz, r;
    dropInCells(drop, m_originX, m_originZ, cx, cz, r);
    const int x0 = std::max(0, static_cast<int>(floorf(cx - r))),
              x1 = std::min(m_cells - 1, static_cast<int>(ceilf(cx + r)));
    const int z0 = std::max(0, static_cast<int>(floorf(cz - r))),
              z1 = std::min(m_cells - 1, static_cast<int>(ceilf(cz + r)));
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
        {
            // the cell's centre, as the shader sees it
            const float dx = static_cast<float>(x) + 0.5f - cx,
                        dz = static_cast<float>(z) + 0.5f - cz;
            const float d = sqrtf(dx * dx + dz * dz) / r;
            if (d < 1.0f)
                m_now[static_cast<size_t>(z) * static_cast<size_t>(m_cells) +
                      static_cast<size_t>(x)] += drop.strength * (0.5f + 0.5f * cosf(kPi * d));
        }
}

void WaveGrid::step(float k, float damping)
{
    const size_t n = static_cast<size_t>(m_cells);
    std::vector<float> next(m_now.size());
    for (int z = 0; z < m_cells; ++z)
        for (int x = 0; x < m_cells; ++x)
        {
            const size_t i = static_cast<size_t>(z) * n + static_cast<size_t>(x);
            const float h = m_now[i];
            const float sum =
                height(x - 1, z) + height(x + 1, z) + height(x, z - 1) + height(x, z + 1);
            // the sponge round the edge: the waves die away there instead
            // of bouncing back off it
            const int edge = std::min(std::min(x, m_cells - 1 - x), std::min(z, m_cells - 1 - z));
            const float soak =
                std::min(1.0f, static_cast<float>(edge) / static_cast<float>(kSpongeCells));
            const float d = damping * (0.92f + 0.08f * soak);
            next[i] = h + d * (h - m_before[i]) + k * (sum - 4.0f * h);
        }
    m_before.swap(m_now);
    m_now.swap(next);
}

void WaveGrid::clear()
{
    std::fill(m_now.begin(), m_now.end(), 0.0f);
    std::fill(m_before.begin(), m_before.end(), 0.0f);
}

double WaveGrid::energy() const
{
    double sum = 0.0;
    for (float h : m_now)
        sum += static_cast<double>(h) * static_cast<double>(h);
    return sum;
}

void TrainSplash::update(const std::vector<float> &positions, float waterLevel, float seconds,
                         std::vector<Drop> &drops)
{
    // the middle of a cart is about this far over the rails; it is in the
    // water from its wheels to its rim
    const float kMiddle = 0.4f;
    const float kReach = 0.7f;
    if (m_last.size() != positions.size() || seconds <= 0.0f)
    {
        m_last = positions;
        return;
    }
    for (size_t i = 0; i + 2 < positions.size(); i += 3)
    {
        const float x = positions[i], y = positions[i + 1] + kMiddle, z = positions[i + 2];
        const float lx = m_last[i], ly = m_last[i + 1] + kMiddle, lz = m_last[i + 2];
        const float dx = x - lx, dy = y - ly, dz = z - lz;
        const float moved = sqrtf(dx * dx + dy * dy + dz * dz);
        // a jump (the train put somewhere else) is not a ride through the water
        if (moved > 12.0f)
            continue;
        const float speed = moved / seconds;
        const float below = waterLevel - y, wasBelow = waterLevel - ly;
        if ((below > 0.0f) != (wasBelow > 0.0f) && speed > 0.5f)
        {
            // going in pushes the water down and out, coming out lifts it
            const float hard = std::min(speed / 8.0f, 1.6f);
            if (below > 0.0f)
                drops.push_back({x, z, 2.4f, -0.3f * hard});
            else
                drops.push_back({x, z, 2.0f, 0.2f * hard});
        }
        if (fabsf(below) < kReach && speed > 0.5f)
        {
            // the cart's hull shoves the water aside as it goes: a hollow
            // dragged along makes a bow wave and a wake behind it
            const float touch = 1.0f - fabsf(below) / kReach;
            drops.push_back({x, z, 1.3f, -std::min(speed, 25.0f) * seconds * 0.15f * touch});
        }
    }
    m_last = positions;
}

void rainDrops(float rain, float seconds, float x, float z, float reach, unsigned &seed,
               std::vector<Drop> &drops)
{
    if (rain <= 0.0f || seconds <= 0.0f)
        return;
    auto random = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    float expected = rain * 60.0f * seconds;
    while (expected > 0.0f)
    {
        // a whole drop, then the last part of one by chance
        if (expected < 1.0f && random() > expected)
            break;
        expected -= 1.0f;
        const float angle = random() * 2.0f * kPi;
        const float r = reach * sqrtf(random());
        drops.push_back(
            {x + r * cosf(angle), z + r * sinf(angle), 0.9f, -0.05f - 0.05f * random()});
    }
}

bool hitWater(const double origin[3], const double direction[3], float level, float &x, float &z)
{
    if (fabs(direction[1]) < 1e-6)
        return false;
    const double t = (static_cast<double>(level) - origin[1]) / direction[1];
    if (t <= 0.0)
        return false;
    x = static_cast<float>(origin[0] + direction[0] * t);
    z = static_cast<float>(origin[2] + direction[2] * t);
    return true;
}

Drop stone(float x, float z)
{
    return {x, z, 1.6f, -0.7f};
}

} // namespace WaterWaves
