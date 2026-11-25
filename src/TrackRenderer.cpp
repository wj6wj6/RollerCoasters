/************************************************************************
     File:        TrackRenderer.cpp

     Comment:     Blocky pixel-art rails and sleepers. See TrackRenderer.H.
*************************************************************************/

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // keep std::min / std::max usable
#endif
#include <windows.h>
#endif
#include <glad/glad.h>

#include <algorithm>
#include <cstddef>
#include <math.h>

#include "TrackRenderer.H"

namespace
{

// Track dimensions in world units (the default track is about 100 units across).
// As a block game's rails sit inside its minecarts' width: the carts
// (TrainModel) are a little wider than the rails' outer edges.
const float kRailOffset = 1.1f;     // distance from the centre line to each rail
const float kRailHalfWidth = 0.12f; // rails are square iron bars
const float kRailBottom = 0.12f;    // rails sit on top of the sleepers
const float kRailTop = 0.46f;       // stays just below the bottom of the train car
const float kSleeperHalfLength = 1.85f;
const float kSleeperHalfDepth = 0.45f;
const float kSleeperBottom = -0.18f;
const float kSleeperTop = kRailBottom;
const float kSleeperSpacing = 1.5f; // target centre-to-centre spacing along the track
const int kArcSamplesPerSegment =
    TrackMesh::kUniformSamplesPerSegment; // density of the arc-length table

// Pixel-art texture setup: 16x16 texels, about 4 texels per world unit.
const int kTextureSize = 16;
const float kTexelsPerUnit = 4.0f;

Pnt3f subtract(const Pnt3f &a, const Pnt3f &b)
{
    return Pnt3f(a.x - b.x, a.y - b.y, a.z - b.z);
}

float length(const Pnt3f &v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Small integer hash so the textures look hand-dithered but stay identical every run.
unsigned int hash2(int x, int y, unsigned int seed)
{
    unsigned int h = seed;
    h ^= static_cast<unsigned int>(x) * 0x27d4eb2du;
    h = (h ^ (h >> 15)) * 0x85ebca6bu;
    h ^= static_cast<unsigned int>(y) * 0x165667b1u;
    h = (h ^ (h >> 13)) * 0xc2b2ae35u;
    return h ^ (h >> 16);
}

unsigned char clampByte(int value)
{
    return static_cast<unsigned char>(std::min(255, std::max(0, value)));
}

void setTexel(std::vector<unsigned char> &pixels, int x, int y, int r, int g, int b, int noise)
{
    const size_t index = (static_cast<size_t>(y) * kTextureSize + static_cast<size_t>(x)) * 4;
    pixels[index + 0] = clampByte(r + noise);
    pixels[index + 1] = clampByte(g + noise);
    pixels[index + 2] = clampByte(b + noise);
    pixels[index + 3] = 255;
}

// Oak-like planks. x runs along the sleeper; every 4 rows is one plank band and
// each sleeper shows a different band, starting with its darker seam row.
std::vector<unsigned char> makeWoodPixels()
{
    std::vector<unsigned char> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * 4);
    for (int y = 0; y < kTextureSize; ++y)
    {
        const int rowInBand = y % 4;
        const int rowShade = static_cast<int>(hash2(0, y, 11u) % 3) * 8 - 8;
        for (int x = 0; x < kTextureSize; ++x)
        {
            const unsigned int h = hash2(x, y, 7u);
            const int noise = static_cast<int>(h % 9) - 4;
            int r = 150, g = 107, b = 62; // light oak
            if (rowInBand == 0 || x == 0 || x == kTextureSize - 1)
            {
                r = 92;
                g = 62;
                b = 34; // dark seam and end grain
            }
            else if (rowInBand == 3)
            {
                r = 124;
                g = 87;
                b = 50; // shaded lower edge of the plank
            }
            else if ((x + y * 5) % 7 == 0 || h % 11 == 0)
            {
                r = 112;
                g = 78;
                b = 44; // grain streaks
            }
            r += rowShade;
            g += rowShade;
            b += rowShade;
            setTexel(pixels, x, y, r, g, b, noise);
        }
    }
    return pixels;
}

// Iron bar. x runs along the rail; each face reads as dark edge, bright centre, dark edge,
// with a darker joint every 16 texels and a few speckles.
std::vector<unsigned char> makeIronPixels()
{
    std::vector<unsigned char> pixels(static_cast<size_t>(kTextureSize) * kTextureSize * 4);
    for (int y = 0; y < kTextureSize; ++y)
    {
        const int band = y < 5 ? 0 : (y < 11 ? 1 : 2);
        for (int x = 0; x < kTextureSize; ++x)
        {
            const unsigned int h = hash2(x, y, 3u);
            const int noise = static_cast<int>(h % 7) - 3;
            int r = 112, g = 115, b = 124; // edge grey
            if (band == 1)
            {
                r = 182;
                g = 185;
                b = 192; // polished centre
                if (h % 13 == 0)
                {
                    r = 206;
                    g = 209;
                    b = 215; // glint
                }
            }
            else if (h % 9 == 0)
            {
                r = 88;
                g = 90;
                b = 98; // speckle
            }
            if (x == 0)
            {
                r = 64;
                g = 66;
                b = 74; // rail joint
            }
            setTexel(pixels, x, y, r, g, b, noise);
        }
    }
    return pixels;
}

// Upload an RGBA texture with box-filtered mip levels. Magnification stays nearest,
// so the texels look like crisp blocks up close and do not shimmer far away.
unsigned int uploadPixelTexture(std::vector<unsigned char> pixels)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // square, a power of two on a side (a texture pack's may be larger)
    int size = 1;
    while (static_cast<size_t>(size) * static_cast<size_t>(size) * 4 < pixels.size())
        size *= 2;
    int level = 0;
    while (true)
    {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels.data());
        if (size == 1)
            break;
        const int half = size / 2;
        const size_t source = static_cast<size_t>(size);
        const size_t target = static_cast<size_t>(half);
        std::vector<unsigned char> smaller(target * target * 4);
        for (size_t y = 0; y < target; ++y)
            for (size_t x = 0; x < target; ++x)
                for (size_t c = 0; c < 4; ++c)
                {
                    const int sum = pixels[((2 * y) * source + 2 * x) * 4 + c] +
                                    pixels[((2 * y) * source + 2 * x + 1) * 4 + c] +
                                    pixels[((2 * y + 1) * source + 2 * x) * 4 + c] +
                                    pixels[((2 * y + 1) * source + 2 * x + 1) * 4 + c];
                    smaller[(y * target + x) * 4 + c] = static_cast<unsigned char>(sum / 4);
                }
        pixels.swap(smaller);
        size = half;
        ++level;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return texture;
}

// Where a sleeper or rail vertex goes: the frame along the track.
struct Frame
{
    Pnt3f position;
    Pnt3f side;
    Pnt3f up;
    Pnt3f forward;
};

TrackVertex makeVertex(const Pnt3f &position, const Pnt3f &normal, float s, float t)
{
    TrackVertex v;
    v.position[0] = position.x;
    v.position[1] = position.y;
    v.position[2] = position.z;
    v.normal[0] = normal.x;
    v.normal[1] = normal.y;
    v.normal[2] = normal.z;
    v.texCoord[0] = s;
    v.texCoord[1] = t;
    return v;
}

// Axis-aligned box in the sleeper's local frame (x = side, y = up, z = forward),
// put into the world with the frame. s0..s1 / t0..t1 pick the part of the wood
// texture shown on the large faces. Six quads, in the order they were once
// drawn one by one.
void appendSleeperBox(std::vector<TrackVertex> &out, const Frame &f, float s0, float s1, float t0,
                      float t1, float tHeight)
{
    const float x0 = -kSleeperHalfLength, x1 = kSleeperHalfLength;
    const float y0 = kSleeperBottom, y1 = kSleeperTop;
    const float z0 = -kSleeperHalfDepth, z1 = kSleeperHalfDepth;
    const float sEnd = (z1 - z0) * kTexelsPerUnit / kTextureSize;

    Pnt3f normal;
    auto setNormal = [&](float x, float y, float z) {
        normal = f.side * x + f.up * y + f.forward * z;
    };
    auto add = [&](float s, float t, float x, float y, float z) {
        out.push_back(makeVertex(f.position + f.side * x + f.up * y + f.forward * z, normal, s, t));
    };
    // top
    setNormal(0, 1, 0);
    add(s0, t0, x0, y1, z0);
    add(s0, t1, x0, y1, z1);
    add(s1, t1, x1, y1, z1);
    add(s1, t0, x1, y1, z0);
    // bottom
    setNormal(0, -1, 0);
    add(s0, t0, x0, y0, z0);
    add(s1, t0, x1, y0, z0);
    add(s1, t1, x1, y0, z1);
    add(s0, t1, x0, y0, z1);
    // front (+z) and back (-z): the side grain
    setNormal(0, 0, 1);
    add(s0, t1, x0, y0, z1);
    add(s1, t1, x1, y0, z1);
    add(s1, t1 - tHeight, x1, y1, z1);
    add(s0, t1 - tHeight, x0, y1, z1);
    setNormal(0, 0, -1);
    add(s1, t0 + tHeight, x1, y0, z0);
    add(s0, t0 + tHeight, x0, y0, z0);
    add(s0, t0, x0, y1, z0);
    add(s1, t0, x1, y1, z0);
    // ends: dark end grain column
    setNormal(1, 0, 0);
    add(0, t0, x1, y0, z1);
    add(sEnd, t0, x1, y0, z0);
    add(sEnd, t0 + tHeight, x1, y1, z0);
    add(0, t0 + tHeight, x1, y1, z1);
    setNormal(-1, 0, 0);
    add(0, t0, x0, y0, z0);
    add(sEnd, t0, x0, y0, z1);
    add(sEnd, t0 + tHeight, x0, y1, z1);
    add(0, t0 + tHeight, x0, y1, z0);
}

struct RailFrame
{
    Pnt3f position;
    Pnt3f side;
    Pnt3f up;
    float s; // texture coordinate along the rail
};

} // namespace

//************************************************************************
//
// * Frame helpers
//========================================================================
void makeTrackFrame(const Pnt3f &forwardInput, Pnt3f &side, Pnt3f &up)
{
    Pnt3f forward = forwardInput;
    forward.normalize();

    side.x = forward.z;
    side.y = 0.0f;
    side.z = -forward.x;
    float sideLength = length(side);
    if (sideLength < 0.0001f)
    {
        side.x = -forward.y;
        side.y = forward.x;
        side.z = 0.0f;
        sideLength = length(side);
    }
    if (sideLength > 0.0001f)
    {
        side.x /= sideLength;
        side.y /= sideLength;
        side.z /= sideLength;
    }

    up.x = forward.y * side.z - forward.z * side.y;
    up.y = forward.z * side.x - forward.x * side.z;
    up.z = forward.x * side.y - forward.y * side.x;
    up.normalize();
}

//************************************************************************
//
// * Arc-length table
//========================================================================
void TrackArcTable::build(const CTrack &track, TrackSplineType splineType, int samplesPerSegment)
{
    // Kept dense and even in u whatever the rails use: looking up a distance
    // interpolates u linearly between two entries, which needs short ones.
    m_samples = sampleTrackUniform(track, splineType, samplesPerSegment);
}

float TrackArcTable::totalLength() const
{
    return m_samples.empty() ? 0.0f : m_samples.back().distance;
}

float TrackArcTable::parameterAtDistance(float distance) const
{
    const float total = totalLength();
    if (m_samples.size() < 2 || total <= 0.0f)
        return 0.0f;

    distance -= floorf(distance / total) * total;

    // First sample whose distance is past the target; interpolate from the one before it.
    auto next =
        std::upper_bound(m_samples.begin(), m_samples.end(), distance,
                         [](float d, const TrackArcSample &sample) { return d < sample.distance; });
    if (next == m_samples.begin())
        return m_samples.front().u;
    if (next == m_samples.end())
        return m_samples.back().u;
    const TrackArcSample &a = *(next - 1);
    const TrackArcSample &b = *next;
    const float span = b.distance - a.distance;
    const float t = span > 0.0f ? (distance - a.distance) / span : 0.0f;
    return a.u + (b.u - a.u) * t;
}

//************************************************************************
//
// * Mesh
//========================================================================
bool TrackMesh::update(const CTrack &track, TrackSplineType splineType, bool adaptive)
{
    bool same = m_built && splineType == m_splineType && adaptive == m_adaptive &&
                track.points.size() == m_points.size();
    for (size_t i = 0; same && i < m_points.size(); ++i)
    {
        const Pnt3f &a = track.points[i].pos, &b = m_points[i];
        same = a.x == b.x && a.y == b.y && a.z == b.z;
    }
    if (same)
        return false;

    m_built = true;
    m_splineType = splineType;
    m_adaptive = adaptive;
    m_points.clear();
    for (const ControlPoint &p : track.points)
        m_points.push_back(p.pos);
    build(track, splineType);
    return true;
}

void TrackMesh::build(const CTrack &track, TrackSplineType splineType)
{
    m_vertices.clear();
    m_railSamples.clear();
    m_sleeperVertices = 0;
    m_railStripVertices = 0;
    if (track.points.size() < 4)
        return;
    m_arcTable.build(track, splineType, kArcSamplesPerSegment);
    const float total = m_arcTable.totalLength();
    if (total <= 0.0f)
        return;

    // Sleepers. Round to a whole number of them so the spacing is identical
    // all the way round, including across the seam where the loop closes.
    const int count = std::max(1, static_cast<int>(floorf(total / kSleeperSpacing + 0.5f)));
    const float spacing = total / static_cast<float>(count);
    // The rails: adaptive subdivision, or the arc-length table's even steps.
    m_railSamples = m_adaptive ? sampleTrackAdaptive(track, splineType) : m_arcTable.samples();
    const std::vector<TrackArcSample> &samples = m_railSamples;
    m_vertices.reserve(static_cast<size_t>(count) * 24 + samples.size() * 2 * kRailStrips);

    const float depthTexels = 2.0f * kSleeperHalfDepth * kTexelsPerUnit;
    const float heightTexels = (kSleeperTop - kSleeperBottom) * kTexelsPerUnit;
    for (int i = 0; i < count; ++i)
    {
        const float u = m_arcTable.parameterAtDistance(spacing * static_cast<float>(i));
        Frame frame;
        frame.position = track.evaluatePosition(u, splineType);
        frame.forward = track.evaluateTangent(u, splineType);
        makeTrackFrame(frame.forward, frame.side, frame.up);

        // Each sleeper shows a different plank band and may be flipped end to end,
        // so neighbouring sleepers do not look copy-pasted.
        const int band = static_cast<int>(hash2(i, 0, 5u) % 4);
        const bool flip = (hash2(i, 1, 5u) & 1u) != 0;
        const float t0 = static_cast<float>(band * 4) / kTextureSize;
        const float t1 = t0 + depthTexels / kTextureSize;
        appendSleeperBox(m_vertices, frame, flip ? 1.0f : 0.0f, flip ? 0.0f : 1.0f, t0, t1,
                         heightTexels / kTextureSize);
    }
    m_sleeperVertices = static_cast<int>(m_vertices.size());

    // Rails: a frame at every rail sample.
    std::vector<RailFrame> frames;
    frames.reserve(samples.size());
    for (const TrackArcSample &sample : samples)
    {
        RailFrame frame;
        frame.position = sample.position;
        makeTrackFrame(track.evaluateTangent(sample.u, splineType), frame.side, frame.up);
        frame.s = sample.distance * kTexelsPerUnit / kTextureSize;
        frames.push_back(frame);
    }

    // Each rail is a square bar swept along the curve: four quad strips, one per face.
    // corner(a, b): a picks the left (-1) or right (+1) edge, b the bottom (0) or top (1).
    for (int rail = -1; rail <= 1; rail += 2)
    {
        auto corner = [&](const RailFrame &f, float a, int b) {
            const float x = kRailOffset * static_cast<float>(rail) + kRailHalfWidth * a;
            const float y = b ? kRailTop : kRailBottom;
            return f.position + f.side * x + f.up * y;
        };
        struct Face
        {
            float a0;
            int b0;
            float a1;
            int b1;
            float normalSide;
            float normalUp;
        };
        const Face faces[4] = {
            {-1.0f, 1, 1.0f, 1, 0.0f, 1.0f},   // top
            {1.0f, 1, 1.0f, 0, 1.0f, 0.0f},    // right
            {-1.0f, 0, -1.0f, 1, -1.0f, 0.0f}, // left
            {1.0f, 0, -1.0f, 0, 0.0f, -1.0f},  // bottom
        };
        for (const Face &face : faces)
            for (const RailFrame &f : frames)
            {
                const Pnt3f normal = f.side * face.normalSide + f.up * face.normalUp;
                m_vertices.push_back(makeVertex(corner(f, face.a0, face.b0), normal, f.s, 0.0f));
                m_vertices.push_back(makeVertex(corner(f, face.a1, face.b1), normal, f.s, 1.0f));
            }
    }
    m_railStripVertices = static_cast<int>(frames.size()) * 2;
}

//************************************************************************
//
// * Renderer
//========================================================================
TrackRenderer::TrackRenderer() : m_woodTexture(0), m_ironTexture(0)
{
}

void TrackRenderer::releaseResources()
{
    m_woodTexture = 0;
    m_ironTexture = 0;
    m_buffer = 0;
    m_bufferStale = true;
}

void TrackRenderer::setPictures(std::vector<unsigned char> wood, std::vector<unsigned char> iron)
{
    m_woodPicture.swap(wood);
    m_ironPicture.swap(iron);
    m_picturesChanged = true;
}

void TrackRenderer::ensureTextures()
{
    if (m_picturesChanged)
    {
        // new pictures (a texture pack's, or back to the built-in ones):
        // the old textures go, the new ones are made below
        const GLuint old[2] = {m_woodTexture, m_ironTexture};
        if (old[0] != 0 || old[1] != 0)
            glDeleteTextures(2, old);
        m_woodTexture = 0;
        m_ironTexture = 0;
        m_picturesChanged = false;
    }
    if (m_woodTexture == 0)
        m_woodTexture =
            uploadPixelTexture(m_woodPicture.empty() ? makeWoodPixels() : m_woodPicture);
    if (m_ironTexture == 0)
        m_ironTexture =
            uploadPixelTexture(m_ironPicture.empty() ? makeIronPixels() : m_ironPicture);
}

bool TrackRenderer::bindMesh()
{
    const std::vector<TrackVertex> &vertices = m_mesh.vertices();
    if (vertices.empty())
        return false;

    // Without vertex buffers (OpenGL before 1.5) the arrays are read from memory.
    const char *base = reinterpret_cast<const char *>(vertices.data());
    if (GLAD_GL_VERSION_1_5)
    {
        if (m_buffer == 0)
        {
            GLuint buffer = 0;
            glGenBuffers(1, &buffer);
            m_buffer = buffer;
            m_bufferStale = true;
        }
        glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
        if (m_bufferStale)
        {
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(TrackVertex)),
                         vertices.data(), GL_STATIC_DRAW);
            m_bufferStale = false;
        }
        base = nullptr; // offsets into the buffer
    }

    const GLsizei stride = static_cast<GLsizei>(sizeof(TrackVertex));
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, base + offsetof(TrackVertex, position));
    glNormalPointer(GL_FLOAT, stride, base + offsetof(TrackVertex, normal));
    glTexCoordPointer(2, GL_FLOAT, stride, base + offsetof(TrackVertex, texCoord));
    return true;
}

void TrackRenderer::draw(const CTrack &track, TrackSplineType splineType, bool doingShadows,
                         bool adaptive)
{
    if (track.points.size() < 4)
        return;
    if (m_mesh.update(track, splineType, adaptive))
        m_bufferStale = true;
    if (m_mesh.vertices().empty())
        return;

    // GL_CURRENT_BIT: the current normal and texture coordinate are left
    // undefined by drawing from arrays, so they are put back afterwards.
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_LIGHTING_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    if (!doingShadows)
    {
        ensureTextures();
        glEnable(GL_LIGHTING);
        glEnable(GL_NORMALIZE);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor3f(1.0f, 1.0f, 1.0f); // the texture provides the colour
    }

    if (bindMesh())
    {
        if (!doingShadows)
            glBindTexture(GL_TEXTURE_2D, m_woodTexture);
        glDrawArrays(GL_QUADS, 0, m_mesh.sleeperVertexCount());

        if (!doingShadows)
            glBindTexture(GL_TEXTURE_2D, m_ironTexture);
        const int strip = m_mesh.railStripVertexCount();
        for (int i = 0; i < TrackMesh::kRailStrips; ++i)
            glDrawArrays(GL_QUAD_STRIP, m_mesh.sleeperVertexCount() + i * strip, strip);
        if (m_buffer != 0)
            glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    glPopClientAttrib();
    glPopAttrib();
}

void TrackRenderer::drawSamples() const
{
    const std::vector<TrackArcSample> &samples = m_mesh.railSamples();
    if (samples.size() < 2)
        return;

    // The frame at each sample, as the rails are built from it. The track's
    // direction there is the average of the pieces on either side, which is
    // what the rails' own frames come close to.
    const size_t n = samples.size();
    std::vector<float> line, ribs;
    line.reserve(n * 3);
    ribs.reserve(n * 6);
    for (size_t i = 0; i < n; ++i)
    {
        const Pnt3f &here = samples[i].position;
        // the last sample is the first one again: its neighbours wrap round
        const Pnt3f &before = samples[i > 0 ? i - 1 : n - 2].position;
        const Pnt3f &after = samples[i + 1 < n ? i + 1 : 1].position;
        Pnt3f side, up;
        makeTrackFrame(subtract(after, before), side, up);
        const Pnt3f centre = here + up * (kRailTop + 0.02f);
        line.insert(line.end(), {centre.x, centre.y, centre.z});
        const float reach = kRailOffset + kRailHalfWidth + 0.25f;
        const Pnt3f a = centre + side * -reach, b = centre + side * reach;
        ribs.insert(ribs.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
    }

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    if (GLAD_GL_VERSION_1_5)
        glBindBuffer(GL_ARRAY_BUFFER, 0); // the arrays below are in memory
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST); // seen through the ground and tunnels too
    glDisable(GL_CULL_FACE);
    glEnableClientState(GL_VERTEX_ARRAY);

    // as a block game's debug lines: a dark outline under bright ones, so
    // they read on snow and on grass alike
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool outline = pass == 0;
        glLineWidth(outline ? 4.0f : 2.0f);
        glVertexPointer(3, GL_FLOAT, 0, ribs.data());
        if (outline)
            glColor3f(0.0f, 0.0f, 0.0f);
        else
            glColor3f(1.0f, 0.85f, 0.1f); // ribs: yellow
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(n * 2));
        glVertexPointer(3, GL_FLOAT, 0, line.data());
        if (!outline)
            glColor3f(0.25f, 0.9f, 1.0f); // centre line: cyan
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(n));
        glPointSize(outline ? 8.0f : 5.0f);
        if (!outline)
            glColor3f(1.0f, 0.3f, 0.2f); // samples: red
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(n));
    }

    glPopClientAttrib();
    glPopAttrib();
}
