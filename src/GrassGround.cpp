/************************************************************************
     File:        GrassGround.cpp

     Comment:     Grass blocks with procedural pixel textures.
                  See GrassGround.h.

*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <vector>

#include "GrassGround.h"

namespace
{

const int kTextureSize = 16;

// Small integer hash so every texel gets a repeatable random value.
unsigned int hashTexel(int x, int y, unsigned int seed)
{
    unsigned int h = static_cast<unsigned int>(x) * 374761393u +
                     static_cast<unsigned int>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

void setTexel(std::vector<unsigned char> &pixels, int x, int y, int r, int g, int b)
{
    const size_t i = static_cast<size_t>((y * kTextureSize + x) * 4);
    pixels[i + 0] = static_cast<unsigned char>(r < 0 ? 0 : (r > 255 ? 255 : r));
    pixels[i + 1] = static_cast<unsigned char>(g < 0 ? 0 : (g > 255 ? 255 : g));
    pixels[i + 2] = static_cast<unsigned char>(b < 0 ? 0 : (b > 255 ? 255 : b));
    pixels[i + 3] = 255;
}

// Top of a grass block: mottled greens with a few darker and lighter blades.
std::vector<unsigned char> makeGrassTop()
{
    std::vector<unsigned char> pixels(kTextureSize * kTextureSize * 4);
    for (int y = 0; y < kTextureSize; ++y)
        for (int x = 0; x < kTextureSize; ++x)
        {
            const unsigned int h = hashTexel(x, y, 1u);
            const int shade = static_cast<int>(h % 23u) - 11;
            int r = 92 + shade, g = 158 + shade * 2, b = 56 + shade;
            if (h % 11u == 0)
            {
                r -= 18;
                g -= 26;
                b -= 12; // dark blade
            }
            else if (h % 13u == 0)
            {
                r += 16;
                g += 20;
                b += 8; // sunlit blade
            }
            setTexel(pixels, x, y, r, g, b);
        }
    return pixels;
}

// Side of a grass block: dirt with a ragged fringe of grass along the top
// row (row 0 is the top edge).
std::vector<unsigned char> makeGrassSide()
{
    std::vector<unsigned char> pixels(kTextureSize * kTextureSize * 4);
    for (int x = 0; x < kTextureSize; ++x)
    {
        const int fringe = 2 + static_cast<int>(hashTexel(x, 0, 7u) % 3u);
        for (int y = 0; y < kTextureSize; ++y)
        {
            const unsigned int h = hashTexel(x, y, 3u);
            const int shade = static_cast<int>(h % 19u) - 9;
            if (y < fringe)
                setTexel(pixels, x, y, 92 + shade, 158 + shade * 2, 56 + shade);
            else if (h % 9u == 0)
                setTexel(pixels, x, y, 104 + shade, 72 + shade, 48 + shade); // dark clod
            else if (h % 17u == 0)
                setTexel(pixels, x, y, 150 + shade, 140 + shade, 130 + shade); // pebble
            else
                setTexel(pixels, x, y, 134 + shade, 96 + shade, 66 + shade);
        }
    }
    return pixels;
}

// Uploads with box-filtered mip levels: nearest when close so the pixels stay
// crisp, mip-mapped far away so the field does not shimmer.
unsigned int uploadTexture(std::vector<unsigned char> pixels)
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
        std::vector<unsigned char> next(static_cast<size_t>(half * half * 4));
        for (int y = 0; y < half; ++y)
            for (int x = 0; x < half; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    const int sum =
                        pixels[static_cast<size_t>(((2 * y) * size + 2 * x) * 4 + c)] +
                        pixels[static_cast<size_t>(((2 * y) * size + 2 * x + 1) * 4 + c)] +
                        pixels[static_cast<size_t>(((2 * y + 1) * size + 2 * x) * 4 + c)] +
                        pixels[static_cast<size_t>(((2 * y + 1) * size + 2 * x + 1) * 4 + c)];
                    next[static_cast<size_t>((y * half + x) * 4 + c)] =
                        static_cast<unsigned char>(sum / 4);
                }
        pixels.swap(next);
        size = half;
        ++level;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

} // namespace

GrassGround::GrassGround() : m_grassTop(0), m_grassSide(0)
{
}

void GrassGround::releaseTextures()
{
    m_grassTop = 0;
    m_grassSide = 0;
}

void GrassGround::ensureTextures()
{
    if (m_grassTop == 0)
        m_grassTop = uploadTexture(makeGrassTop());
    if (m_grassSide == 0)
        m_grassSide = uploadTexture(makeGrassSide());
}

void GrassGround::draw(float size, int cells, float bottom)
{
    if (cells <= 0)
        return;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    ensureTextures();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    const float half = size * 0.5f;
    const float cell = size / static_cast<float>(cells);

    // Grass tops. A slight tint per block breaks up the repetition.
    glBindTexture(GL_TEXTURE_2D, m_grassTop);
    glBegin(GL_QUADS);
    glNormal3f(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < cells; ++i)
        for (int j = 0; j < cells; ++j)
        {
            const float x0 = -half + static_cast<float>(i) * cell;
            const float z0 = -half + static_cast<float>(j) * cell;
            const float tint =
                0.9f + 0.1f * static_cast<float>(hashTexel(i, j, 11u) % 100u) / 100.0f;
            glColor3f(tint, tint, tint);
            // Rotate the texture per block like Minecraft does, so the
            // pattern does not line up.
            const int turn = static_cast<int>(hashTexel(i, j, 5u) % 4u);
            const float s[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            const float t[4] = {0.0f, 1.0f, 1.0f, 0.0f};
            const float xs[4] = {x0, x0, x0 + cell, x0 + cell};
            const float zs[4] = {z0, z0 + cell, z0 + cell, z0};
            for (int k = 0; k < 4; ++k)
            {
                const int r = (k + turn) % 4;
                glTexCoord2f(s[r], t[r]);
                glVertex3f(xs[k], 0.0f, zs[k]);
            }
        }
    glEnd();

    // Dirt sides around the edge; row 0 of the texture (the grass fringe)
    // sits at the top.
    const float depthT = (0.0f - bottom) / cell;
    glBindTexture(GL_TEXTURE_2D, m_grassSide);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    for (int i = 0; i < cells; ++i)
    {
        const float a = -half + static_cast<float>(i) * cell;
        const float b = a + cell;
        glNormal3f(0.0f, 0.0f, -1.0f);
        glTexCoord2f(0.0f, depthT);
        glVertex3f(a, bottom, -half);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(a, 0.0f, -half);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(b, 0.0f, -half);
        glTexCoord2f(1.0f, depthT);
        glVertex3f(b, bottom, -half);

        glNormal3f(0.0f, 0.0f, 1.0f);
        glTexCoord2f(0.0f, depthT);
        glVertex3f(b, bottom, half);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(b, 0.0f, half);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(a, 0.0f, half);
        glTexCoord2f(1.0f, depthT);
        glVertex3f(a, bottom, half);

        glNormal3f(-1.0f, 0.0f, 0.0f);
        glTexCoord2f(0.0f, depthT);
        glVertex3f(-half, bottom, b);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(-half, 0.0f, b);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(-half, 0.0f, a);
        glTexCoord2f(1.0f, depthT);
        glVertex3f(-half, bottom, a);

        glNormal3f(1.0f, 0.0f, 0.0f);
        glTexCoord2f(0.0f, depthT);
        glVertex3f(half, bottom, a);
        glTexCoord2f(0.0f, 0.0f);
        glVertex3f(half, 0.0f, a);
        glTexCoord2f(1.0f, 0.0f);
        glVertex3f(half, 0.0f, b);
        glTexCoord2f(1.0f, depthT);
        glVertex3f(half, bottom, b);
    }
    glEnd();

    glPopAttrib();
}
