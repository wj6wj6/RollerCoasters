/************************************************************************
     File:        PixelArt.cpp

     Comment:     See PixelArt.h.

*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include "PixelArt.h"

namespace PixelArt
{

unsigned int hash(int x, int y, unsigned int seed)
{
    unsigned int h = static_cast<unsigned int>(x) * 374761393u +
                     static_cast<unsigned int>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

Image blank(int size)
{
    return Image(static_cast<size_t>(size * size * 4), 0);
}

int sizeOf(const Image &pixels)
{
    int size = 1;
    while (static_cast<size_t>(size * size * 4) < pixels.size())
        size *= 2;
    return size;
}

void set(Image &pixels, int x, int y, int r, int g, int b, int a)
{
    const int size = sizeOf(pixels);
    if (x < 0 || y < 0 || x >= size || y >= size)
        return;
    const size_t i = static_cast<size_t>((y * size + x) * 4);
    pixels[i + 0] = static_cast<unsigned char>(r < 0 ? 0 : (r > 255 ? 255 : r));
    pixels[i + 1] = static_cast<unsigned char>(g < 0 ? 0 : (g > 255 ? 255 : g));
    pixels[i + 2] = static_cast<unsigned char>(b < 0 ? 0 : (b > 255 ? 255 : b));
    pixels[i + 3] = static_cast<unsigned char>(a < 0 ? 0 : (a > 255 ? 255 : a));
}

unsigned int upload(Image pixels, bool repeatT)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    uploadInto(texture, pixels, repeatT);
    return texture;
}

void uploadInto(unsigned int texture, Image pixels, bool repeatT)
{
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    int size = sizeOf(pixels);
    int level = 0;
    while (true)
    {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels.data());
        if (size == 1)
            break;
        const int half = size / 2;
        Image next(static_cast<size_t>(half * half * 4));
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeatT ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    // the last level is 1 x 1: a smaller picture put in place of a larger
    // one leaves the larger one's extra levels unused
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
}

const char *buildStamp()
{
    return __DATE__ " " __TIME__;
}

} // namespace PixelArt
