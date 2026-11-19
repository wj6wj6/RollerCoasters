/************************************************************************
     File:        Mobs.cpp

     Comment:     See Mobs.h.

*************************************************************************/

#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include "Mobs.h"
#include "PixelArt.h"

namespace Mobs
{
namespace
{

const int kWidth = 64, kRows = 32; // the built-in pictures

void put(Picture &p, int x, int y, int r, int g, int b, int a = 255)
{
    if (x < 0 || y < 0 || x >= p.width || y >= p.height)
        return;
    unsigned char *t = &p.rgba[static_cast<size_t>((y * p.width + x) * 4)];
    const int c[4] = {r, g, b, a};
    for (int k = 0; k < 4; ++k)
        t[k] = static_cast<unsigned char>(c[k] < 0 ? 0 : (c[k] > 255 ? 255 : c[k]));
}

struct Colour
{
    int r, g, b;
};

// A colour with a little noise in it, the same for the same texel.
void speckle(Picture &p, int x, int y, Colour c, int amount, unsigned int seed)
{
    const unsigned int span = static_cast<unsigned int>(2 * amount + 1);
    const int n = static_cast<int>(PixelArt::hash(x, y, seed) % span) - amount;
    put(p, x, y, c.r + n, c.g + n, c.b + n);
}

// Fills a rectangle of the picture.
void fill(Picture &p, int x0, int y0, int w, int h, Colour rgb, int amount, unsigned int seed)
{
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            speckle(p, x, y, rgb, amount, seed);
}

// Fills the whole net of a box: its top and bottom, then its sides.
void fillNet(Picture &p, int u, int v, int w, int h, int d, Colour rgb, int amount,
             unsigned int seed)
{
    fill(p, u + d, v, 2 * w, d, rgb, amount, seed);
    fill(p, u, v + d, 2 * (d + w), h, rgb, amount, seed);
}

Picture blankPicture()
{
    Picture p;
    p.width = kWidth;
    p.height = kRows;
    p.rgba.assign(static_cast<size_t>(kWidth * kRows * 4), 0);
    return p;
}

// Eyes on a face: a dark pupil with the white beside it, towards the
// middle of the face, at x and its mirror (face from x0, w wide).
void eyes(Picture &p, int x0, int w, int y, int inset)
{
    const int left = x0 + inset, right = x0 + w - 1 - inset;
    put(p, left, y, 30, 26, 24);
    put(p, left + 1, y, 236, 236, 236);
    put(p, right, y, 30, 26, 24);
    put(p, right - 1, y, 236, 236, 236);
}

Picture sheepSkin()
{
    Picture p = blankPicture();
    const Colour wool{222, 222, 216}, face{204, 168, 144}, skin{200, 164, 140};
    fillNet(p, 0, 0, 6, 6, 8, wool, 8, 11u); // the head, woolly round the face
    fill(p, 8, 8, 6, 6, face, 6, 12u);       // the face
    eyes(p, 8, 6, 10, 0);
    fill(p, 10, 12, 2, 1, Colour{232, 150, 150}, 4, 13u); // the nose
    fillNet(p, 28, 8, 8, 16, 6, skin, 8, 14u);            // under the wool
    fillNet(p, 0, 16, 4, 12, 4, face, 6, 15u);            // legs
    fill(p, 0, 30, 16, 2, Colour{96, 78, 66}, 4, 16u);    // hooves
    return p;
}

Picture sheepWool()
{
    Picture p = blankPicture();
    const Colour wool{236, 236, 232};
    fillNet(p, 0, 0, 6, 6, 6, wool, 10, 21u);
    fillNet(p, 28, 8, 8, 16, 6, wool, 10, 22u);
    fillNet(p, 0, 16, 4, 6, 4, wool, 10, 23u);
    return p;
}

Picture pigSkin()
{
    Picture p = blankPicture();
    const Colour pink{238, 164, 168};
    fillNet(p, 0, 0, 8, 8, 8, pink, 8, 31u);
    eyes(p, 8, 8, 12, 0);
    fillNet(p, 16, 16, 4, 3, 1, Colour{250, 190, 196}, 4, 32u); // the snout
    put(p, 17, 18, 160, 90, 100);                               // nostrils
    put(p, 20, 18, 160, 90, 100);
    fillNet(p, 28, 8, 10, 16, 8, pink, 8, 33u);
    fillNet(p, 0, 16, 4, 6, 4, pink, 8, 34u);
    fill(p, 0, 25, 16, 1, Colour{150, 96, 100}, 4, 35u); // trotters
    return p;
}

Picture cowSkin()
{
    Picture p = blankPicture();
    const Colour brown{84, 58, 42}, white{232, 230, 224};
    fillNet(p, 0, 0, 8, 8, 6, brown, 8, 41u);
    eyes(p, 6, 8, 9, 1);
    fill(p, 6, 11, 8, 3, Colour{204, 194, 176}, 5, 42u); // the muzzle
    put(p, 8, 12, 60, 44, 40);
    put(p, 11, 12, 60, 44, 40);
    fillNet(p, 22, 0, 1, 3, 1, Colour{222, 216, 198}, 4, 43u); // horns
    fillNet(p, 18, 4, 12, 18, 10, brown, 8, 44u);
    // white patches, in blobs three texels across
    for (int y = 4; y < 32; ++y)
        for (int x = 18; x < 62; ++x)
            if (p.rgba[static_cast<size_t>((y * kWidth + x) * 4 + 3)] != 0 &&
                PixelArt::hash(x / 3, y / 3, 45u) % 3u == 0u)
                speckle(p, x, y, white, 6, 46u);
    fillNet(p, 52, 0, 4, 6, 1, Colour{232, 164, 164}, 4, 47u); // the udder
    fillNet(p, 0, 16, 4, 12, 4, brown, 8, 48u);
    fill(p, 0, 30, 16, 2, Colour{52, 48, 46}, 3, 49u); // hooves
    return p;
}

// One face of a box: its corners, and the texture coordinate at each, in
// the game's pixels.
struct Corner
{
    float x, y, z, u, v;
};

void quad(float nx, float ny, float nz, const Corner (&c)[4], float rows)
{
    glNormal3f(nx, ny, nz);
    for (const Corner &k : c)
    {
        glTexCoord2f(k.u / 64.0f, k.v / rows);
        glVertex3f(k.x, k.y, k.z);
    }
}

// A box's six faces, as the game cuts them from the picture (see Mobs.h).
void drawBox(const Box &b, float rows)
{
    const float x0 = b.x0, y0 = b.y0, z0 = b.z0, x1 = b.x1, y1 = b.y1, z1 = b.z1;
    const float u = static_cast<float>(b.u), v = static_cast<float>(b.v);
    const float w = static_cast<float>(b.w), h = static_cast<float>(b.h),
                d = static_cast<float>(b.d);
    // the four columns of the side row, and the two rows
    const float a0 = u, a1 = u + d, a2 = u + d + w, a3 = u + 2 * d + w, a4 = u + 2 * d + 2 * w;
    const float r0 = v, r1 = v + d, r2 = v + d + h;
    if (!b.lying)
    {
        quad(0, 0, 1,
             {{x0, y0, z1, a1, r2},
              {x1, y0, z1, a2, r2},
              {x1, y1, z1, a2, r1},
              {x0, y1, z1, a1, r1}},
             rows); // front (the face)
        quad(0, 0, -1,
             {{x1, y0, z0, a3, r2},
              {x0, y0, z0, a4, r2},
              {x0, y1, z0, a4, r1},
              {x1, y1, z0, a3, r1}},
             rows);
        quad(-1, 0, 0,
             {{x0, y0, z0, a0, r2},
              {x0, y0, z1, a1, r2},
              {x0, y1, z1, a1, r1},
              {x0, y1, z0, a0, r1}},
             rows); // its right side
        quad(1, 0, 0,
             {{x1, y0, z1, a2, r2},
              {x1, y0, z0, a3, r2},
              {x1, y1, z0, a3, r1},
              {x1, y1, z1, a2, r1}},
             rows);
        quad(0, 1, 0,
             {{x0, y1, z1, a1, r1},
              {x1, y1, z1, a2, r1},
              {x1, y1, z0, a2, r0},
              {x0, y1, z0, a1, r0}},
             rows);
        quad(0, -1, 0,
             {{x0, y0, z0, a2, r1},
              {x1, y0, z0, a2 + w, r1},
              {x1, y0, z1, a2 + w, r0},
              {x0, y0, z1, a2, r0}},
             rows);
        return;
    }
    // Tipped forward: the picture's front face is the belly and its back
    // face the back, both running from the head (r1) to the tail (r2);
    // its top is the chest and its bottom the rump.
    quad(0, -1, 0,
         {{x0, y0, z0, a1, r2}, {x1, y0, z0, a2, r2}, {x1, y0, z1, a2, r1}, {x0, y0, z1, a1, r1}},
         rows);
    quad(0, 1, 0,
         {{x0, y1, z1, a4, r1}, {x1, y1, z1, a3, r1}, {x1, y1, z0, a3, r2}, {x0, y1, z0, a4, r2}},
         rows);
    quad(-1, 0, 0,
         {{x0, y0, z0, a1, r2}, {x0, y0, z1, a1, r1}, {x0, y1, z1, a0, r1}, {x0, y1, z0, a0, r2}},
         rows);
    quad(1, 0, 0,
         {{x1, y0, z1, a2, r1}, {x1, y0, z0, a2, r2}, {x1, y1, z0, a3, r2}, {x1, y1, z1, a3, r1}},
         rows);
    quad(0, 0, 1,
         {{x0, y0, z1, a1, r1}, {x1, y0, z1, a2, r1}, {x1, y1, z1, a2, r0}, {x0, y1, z1, a1, r0}},
         rows);
    quad(0, 0, -1,
         {{x1, y0, z0, a2, r1},
          {x0, y0, z0, a2 + w, r1},
          {x0, y1, z0, a2 + w, r0},
          {x1, y1, z0, a2, r0}},
         rows);
}

// Four legs, legW square and `tall` high, centred at x = +-spread and at
// z = front and z = back.
void addLegs(std::vector<Box> &out, int skin, float spread, float front, float back, float tall,
             int u, int v, int legW, int legH)
{
    const float half = static_cast<float>(legW) / 2.0f;
    const float xs[4] = {-spread, spread, -spread, spread};
    const float zs[4] = {front, front, back, back};
    for (int k = 0; k < 4; ++k)
        out.push_back(Box{xs[k] - half, 0.0f, zs[k] - half, xs[k] + half, tall, zs[k] + half, u, v,
                          legW, legH, legW, false, skin, k + 1});
}

} // namespace

Picture builtin(int skin)
{
    switch (skin)
    {
    case SHEEP_SKIN:
        return sheepSkin();
    case SHEEP_WOOL:
        return sheepWool();
    case PIG_SKIN:
        return pigSkin();
    default:
        return cowSkin();
    }
}

const char *packNames(int skin)
{
    switch (skin)
    {
    case SHEEP_SKIN:
        return "sheep/sheep";
    case SHEEP_WOOL:
        return "sheep/sheep_wool|sheep/sheep_fur";
    case PIG_SKIN:
        return "pig/pig_temperate|pig/pig|pig";
    default:
        return "cow/cow_temperate|cow/cow|cow";
    }
}

bool fits(int width, int height)
{
    return width >= 64 && width % 64 == 0 && (height * 2 == width || height == width);
}

std::vector<Box> model(int kind)
{
    // In the game's pixels, from its models: the heads stick out in front
    // of the bodies, which lie on the legs.
    std::vector<Box> out;
    if (kind == SHEEP)
    {
        out.push_back(Box{-3, 16, 6, 3, 22, 14, 0, 0, 6, 6, 8, false, SHEEP_SKIN, 0});
        out.push_back(Box{-4, 12, -8, 4, 18, 8, 28, 8, 8, 16, 6, true, SHEEP_SKIN, 0});
        addLegs(out, SHEEP_SKIN, 3, 5, -7, 12, 0, 16, 4, 12);
        // the wool, a little bigger than what it covers
        out.push_back(
            Box{-3.6f, 15.4f, 5.4f, 3.6f, 22.6f, 12.6f, 0, 0, 6, 6, 6, false, SHEEP_WOOL, 0});
        out.push_back(Box{-5.75f, 10.25f, -9.75f, 5.75f, 19.75f, 9.75f, 28, 8, 8, 16, 6, true,
                          SHEEP_WOOL, 0});
        const float xs[4] = {-3, 3, -3, 3}, zs[4] = {5, 5, -7, -7};
        for (int k = 0; k < 4; ++k)
            out.push_back(Box{xs[k] - 2.5f, 5.5f, zs[k] - 2.5f, xs[k] + 2.5f, 12.5f, zs[k] + 2.5f,
                              0, 16, 4, 6, 4, false, SHEEP_WOOL, k + 1});
    }
    else if (kind == PIG)
    {
        out.push_back(Box{-4, 8, 6, 4, 16, 14, 0, 0, 8, 8, 8, false, PIG_SKIN, 0});
        out.push_back(Box{-2, 9, 14, 2, 12, 15, 16, 16, 4, 3, 1, false, PIG_SKIN, 0}); // snout
        out.push_back(Box{-5, 6, -8, 5, 14, 8, 28, 8, 10, 16, 8, true, PIG_SKIN, 0});
        addLegs(out, PIG_SKIN, 3, 5, -7, 6, 0, 16, 4, 6);
    }
    else
    {
        out.push_back(Box{-4, 16, 8, 4, 24, 14, 0, 0, 8, 8, 6, false, COW_SKIN, 0});
        out.push_back(Box{-5, 22, 11, -4, 25, 12, 22, 0, 1, 3, 1, false, COW_SKIN, 0}); // horns
        out.push_back(Box{4, 22, 11, 5, 25, 12, 22, 0, 1, 3, 1, false, COW_SKIN, 0});
        out.push_back(Box{-6, 12, -10, 6, 22, 8, 18, 4, 12, 18, 10, true, COW_SKIN, 0});
        out.push_back(Box{-2, 11, -10, 2, 12, -4, 52, 0, 4, 6, 1, true, COW_SKIN, 0}); // udder
        addLegs(out, COW_SKIN, 4, 6, -7, 12, 0, 16, 4, 12);
    }
    return out;
}

void upload(Skins &skins, int skin, const Picture &picture)
{
    unsigned int &texture = skins.texture[skin];
    if (texture == 0)
        glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, picture.width, picture.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, picture.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    // a picture 64 x 64 in the game's pixels has the layout in its upper half
    skins.rows[skin] = picture.height * 64 / picture.width;
}

void draw(int kind, float stride, const Skins &skins, const float wool[3])
{
    // The legs swing about their tops in pairs, front left with back right.
    const float hip = kind == PIG ? 6.0f : 12.0f;
    const float swing = stride * 30.0f;
    for (const Box &b : model(kind))
    {
        glBindTexture(GL_TEXTURE_2D, skins.texture[b.skin]);
        if (b.skin == SHEEP_WOOL)
            glColor3f(wool[0], wool[1], wool[2]);
        else
            glColor3f(1.0f, 1.0f, 1.0f);
        const float rows = static_cast<float>(skins.rows[b.skin] > 0 ? skins.rows[b.skin] : kRows);
        if (b.leg == 0)
        {
            glBegin(GL_QUADS);
            drawBox(b, rows);
            glEnd();
            continue;
        }
        const float cx = (b.x0 + b.x1) * 0.5f, cz = (b.z0 + b.z1) * 0.5f;
        glPushMatrix();
        glTranslatef(cx, hip, cz);
        glRotatef(b.leg == 1 || b.leg == 4 ? swing : -swing, 1.0f, 0.0f, 0.0f);
        glTranslatef(-cx, -hip, -cz);
        glBegin(GL_QUADS);
        drawBox(b, rows);
        glEnd();
        glPopMatrix();
    }
}

} // namespace Mobs
