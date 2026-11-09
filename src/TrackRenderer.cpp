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
const float kRailOffset = 1.35f;    // distance from the centre line to each rail
const float kRailHalfWidth = 0.17f; // rails are square iron bars
const float kRailBottom = 0.12f;    // rails sit on top of the sleepers
const float kRailTop = 0.46f;       // stays just below the bottom of the train car
const float kSleeperHalfLength = 2.1f;
const float kSleeperHalfDepth = 0.45f;
const float kSleeperBottom = -0.18f;
const float kSleeperTop = kRailBottom;
const float kSleeperSpacing = 1.5f;   // target centre-to-centre spacing along the track
const int kArcSamplesPerSegment = 48; // density of the arc-length table

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

    int size = kTextureSize;
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

void vertex(const Pnt3f &p)
{
    glVertex3f(p.x, p.y, p.z);
}

// Axis-aligned box in the current local frame (x = side, y = up, z = forward).
// s0..s1 / t0..t1 pick the part of the wood texture shown on the large faces.
void drawSleeperBox(float s0, float s1, float t0, float t1, float tHeight)
{
    const float x0 = -kSleeperHalfLength, x1 = kSleeperHalfLength;
    const float y0 = kSleeperBottom, y1 = kSleeperTop;
    const float z0 = -kSleeperHalfDepth, z1 = kSleeperHalfDepth;
    const float sEnd = (z1 - z0) * kTexelsPerUnit / kTextureSize;

    glBegin(GL_QUADS);
    // top
    glNormal3f(0, 1, 0);
    glTexCoord2f(s0, t0);
    glVertex3f(x0, y1, z0);
    glTexCoord2f(s0, t1);
    glVertex3f(x0, y1, z1);
    glTexCoord2f(s1, t1);
    glVertex3f(x1, y1, z1);
    glTexCoord2f(s1, t0);
    glVertex3f(x1, y1, z0);
    // bottom
    glNormal3f(0, -1, 0);
    glTexCoord2f(s0, t0);
    glVertex3f(x0, y0, z0);
    glTexCoord2f(s1, t0);
    glVertex3f(x1, y0, z0);
    glTexCoord2f(s1, t1);
    glVertex3f(x1, y0, z1);
    glTexCoord2f(s0, t1);
    glVertex3f(x0, y0, z1);
    // front (+z) and back (-z): the side grain
    glNormal3f(0, 0, 1);
    glTexCoord2f(s0, t1);
    glVertex3f(x0, y0, z1);
    glTexCoord2f(s1, t1);
    glVertex3f(x1, y0, z1);
    glTexCoord2f(s1, t1 - tHeight);
    glVertex3f(x1, y1, z1);
    glTexCoord2f(s0, t1 - tHeight);
    glVertex3f(x0, y1, z1);
    glNormal3f(0, 0, -1);
    glTexCoord2f(s1, t0 + tHeight);
    glVertex3f(x1, y0, z0);
    glTexCoord2f(s0, t0 + tHeight);
    glVertex3f(x0, y0, z0);
    glTexCoord2f(s0, t0);
    glVertex3f(x0, y1, z0);
    glTexCoord2f(s1, t0);
    glVertex3f(x1, y1, z0);
    // ends: dark end grain column
    glNormal3f(1, 0, 0);
    glTexCoord2f(0, t0);
    glVertex3f(x1, y0, z1);
    glTexCoord2f(sEnd, t0);
    glVertex3f(x1, y0, z0);
    glTexCoord2f(sEnd, t0 + tHeight);
    glVertex3f(x1, y1, z0);
    glTexCoord2f(0, t0 + tHeight);
    glVertex3f(x1, y1, z1);
    glNormal3f(-1, 0, 0);
    glTexCoord2f(0, t0);
    glVertex3f(x0, y0, z0);
    glTexCoord2f(sEnd, t0);
    glVertex3f(x0, y0, z1);
    glTexCoord2f(sEnd, t0 + tHeight);
    glVertex3f(x0, y1, z1);
    glTexCoord2f(0, t0 + tHeight);
    glVertex3f(x0, y1, z0);
    glEnd();
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
    m_samples.clear();
    const int pointCount = static_cast<int>(track.points.size());
    if (pointCount < 4 || samplesPerSegment < 1)
        return;

    const int sampleCount = pointCount * samplesPerSegment;
    m_samples.reserve(sampleCount + 1);
    float distance = 0.0f;
    Pnt3f previous = track.evaluatePosition(0.0f, splineType);
    for (int i = 0; i <= sampleCount; ++i)
    {
        TrackArcSample sample;
        sample.u = static_cast<float>(i) / static_cast<float>(samplesPerSegment);
        sample.position = track.evaluatePosition(sample.u, splineType);
        distance += length(subtract(sample.position, previous));
        sample.distance = distance;
        m_samples.push_back(sample);
        previous = sample.position;
    }
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
// * Renderer
//========================================================================
TrackRenderer::TrackRenderer() : m_woodTexture(0), m_ironTexture(0)
{
}

void TrackRenderer::releaseTextures()
{
    m_woodTexture = 0;
    m_ironTexture = 0;
}

void TrackRenderer::ensureTextures()
{
    if (m_woodTexture == 0)
        m_woodTexture = uploadPixelTexture(makeWoodPixels());
    if (m_ironTexture == 0)
        m_ironTexture = uploadPixelTexture(makeIronPixels());
}

void TrackRenderer::draw(const CTrack &track, TrackSplineType splineType, bool doingShadows)
{
    if (track.points.size() < 4)
        return;

    m_arcTable.build(track, splineType, kArcSamplesPerSegment);
    if (m_arcTable.totalLength() <= 0.0f)
        return;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_LIGHTING_BIT);
    if (!doingShadows)
    {
        ensureTextures();
        glEnable(GL_LIGHTING);
        glEnable(GL_NORMALIZE);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor3f(1.0f, 1.0f, 1.0f); // the texture provides the colour
    }

    drawSleepers(track, splineType, doingShadows);
    drawRails(track, splineType, doingShadows);

    glPopAttrib();
}

void TrackRenderer::drawSleepers(const CTrack &track, TrackSplineType splineType, bool doingShadows)
{
    // Round to a whole number of sleepers so the spacing is identical all the way round,
    // including across the seam where the loop closes.
    const float total = m_arcTable.totalLength();
    const int count = std::max(1, static_cast<int>(floorf(total / kSleeperSpacing + 0.5f)));
    const float spacing = total / static_cast<float>(count);

    if (!doingShadows)
        glBindTexture(GL_TEXTURE_2D, m_woodTexture);

    const float depthTexels = 2.0f * kSleeperHalfDepth * kTexelsPerUnit;
    const float heightTexels = (kSleeperTop - kSleeperBottom) * kTexelsPerUnit;
    for (int i = 0; i < count; ++i)
    {
        const float u = m_arcTable.parameterAtDistance(spacing * static_cast<float>(i));
        const Pnt3f position = track.evaluatePosition(u, splineType);
        const Pnt3f forward = track.evaluateTangent(u, splineType);
        Pnt3f side, up;
        makeTrackFrame(forward, side, up);

        const GLfloat frame[16] = {side.x,     side.y,     side.z,     0.0f,      up.x,      up.y,
                                   up.z,       0.0f,       forward.x,  forward.y, forward.z, 0.0f,
                                   position.x, position.y, position.z, 1.0f};
        glPushMatrix();
        glMultMatrixf(frame);

        // Each sleeper shows a different plank band and may be flipped end to end,
        // so neighbouring sleepers do not look copy-pasted.
        const int band = static_cast<int>(hash2(i, 0, 5u) % 4);
        const bool flip = (hash2(i, 1, 5u) & 1u) != 0;
        const float t0 = static_cast<float>(band * 4) / kTextureSize;
        const float t1 = t0 + depthTexels / kTextureSize;
        drawSleeperBox(flip ? 1.0f : 0.0f, flip ? 0.0f : 1.0f, t0, t1, heightTexels / kTextureSize);
        glPopMatrix();
    }
}

void TrackRenderer::drawRails(const CTrack &track, TrackSplineType splineType, bool doingShadows)
{
    const std::vector<TrackArcSample> &samples = m_arcTable.samples();
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

    if (!doingShadows)
        glBindTexture(GL_TEXTURE_2D, m_ironTexture);

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
        {
            glBegin(GL_QUAD_STRIP);
            for (const RailFrame &f : frames)
            {
                const Pnt3f normal = f.side * face.normalSide + f.up * face.normalUp;
                glNormal3f(normal.x, normal.y, normal.z);
                glTexCoord2f(f.s, 0.0f);
                vertex(corner(f, face.a0, face.b0));
                glTexCoord2f(f.s, 1.0f);
                vertex(corner(f, face.a1, face.b1));
            }
            glEnd();
        }
    }
}
