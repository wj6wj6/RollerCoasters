/************************************************************************
     File:        TrainModel.cpp

     Comment:     See TrainModel.h. Every car is modelled facing +z with
                  the top of the rails at y = 0, in a block game's pixels
                  (kPx world units, the island's blocks being 16 of them),
                  so the carts and the riders are the size they would be
                  in such a world.
*************************************************************************/
#include "TrainModel.h"

#include "PixelArt.h"
#include "TexturePack.h"
#include "TrackRenderer.H"

#include <glad/glad.h>
#include <math.h>
#include <string>
#include <vector>

namespace
{
// A furnace minecart pushing five minecarts, a block game's player sitting
// in each as they sit in a minecart.
const int kCarts = 5;
const int kVehicles = 1 + kCarts;
// One pixel of the train: a 2.5 unit block's, a quarter bigger, so the carts
// (16 pixels wide, 4 units) stand a little wider than the rails' outer
// edges (TrackRenderer) as a block game's minecarts do on its rails.
const float kPx = 1.25f * 2.5f / 16.0f;
// Along the track, centre to centre: a cart is 20 pixels (3.9 units) long.
const float kEngineToCart = 4.6f;
const float kCartToCart = 4.6f;
// How far the carts' floors ride above the track's line, in pixels: just
// on top of the rails.
const float kRide = 2.7f;
// How high the riders' hips sit above the carts' bottoms, in pixels.
const float kHips = 4.2f;

bool gShadowPass = false;

void colour(int r, int g, int b)
{
    if (!gShadowPass)
        glColor3ub(static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b));
}

// What the lighting shader reads from texture unit 1: how much the surface
// glows by itself (the rest: no sway, no crystal light, full sky light).
void glow(float amount)
{
    glMultiTexCoord4f(GL_TEXTURE1, amount, 0.0f, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------
// A picture as drawn or read, RGBA, row 0 at the top.
struct Picture
{
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
};

Picture blankPicture(int width, int height)
{
    Picture p;
    p.width = width;
    p.height = height;
    p.rgba.assign(static_cast<size_t>(width * height * 4), 0);
    return p;
}

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

// A rectangle of a colour with a little noise in it, the same every time.
void fill(Picture &p, int x0, int y0, int w, int h, Colour c, int noise, unsigned int seed)
{
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
        {
            const int n = noise > 0 ? static_cast<int>(PixelArt::hash(x, y, seed) %
                                                       static_cast<unsigned int>(2 * noise + 1)) -
                                          noise
                                    : 0;
            put(p, x, y, c.r + n, c.g + n, c.b + n);
        }
}

// The whole net of a box laid out as a block game lays out its models'
// pictures (see skinBox()): the top and bottom, then the four sides.
void fillNet(Picture &p, int u, int v, int w, int h, int d, Colour c, int noise, unsigned int seed)
{
    fill(p, u + d, v, 2 * w, d, c, noise, seed);
    fill(p, u, v + d, 2 * (d + w), h, c, noise, seed);
}

// A built-in player skin, 64 x 64 in the layout block games use, so a
// texture pack's player pictures fit the same model: hair, a face with
// eyes and a mouth, a shirt with short sleeves, trousers and shoes.
Picture builtinSkin(int who)
{
    const Colour skins[4] = {{214, 160, 122}, {240, 196, 160}, {150, 98, 66}, {196, 140, 100}};
    const Colour hairs[5] = {
        {60, 40, 26}, {28, 26, 30}, {204, 160, 76}, {140, 64, 36}, {96, 60, 40}};
    const Colour shirts[6] = {{40, 160, 170}, {196, 54, 54},  {236, 186, 52},
                              {84, 156, 76},  {136, 84, 186}, {226, 226, 226}};
    const Colour trousers[4] = {{52, 56, 140}, {64, 64, 72}, {110, 78, 48}, {38, 92, 112}};
    const Colour skin = skins[who % 4], hair = hairs[(who * 3 + 1) % 5];
    const Colour shirt = shirts[who % 6], legs = trousers[(who + who / 3) % 4];
    const unsigned int seed = 500u + static_cast<unsigned int>(who) * 17u;
    Picture p = blankPicture(64, 64);
    // head: skin, hair on the top, the back and the upper sides and brow
    fillNet(p, 0, 0, 8, 8, 8, skin, 4, seed);
    fill(p, 8, 0, 8, 8, hair, 6, seed + 1);  // top
    fill(p, 0, 8, 32, 2, hair, 6, seed + 2); // the hairline all round
    fill(p, 24, 8, 8, 8, hair, 6, seed + 3); // back
    fill(p, 0, 10, 2, 3, hair, 6, seed + 4); // sideburns
    fill(p, 22, 10, 2, 3, hair, 6, seed + 5);
    put(p, 9, 12, 250, 250, 250); // eyes: white, then the pupil
    put(p, 10, 12, 60, 60, 120);
    put(p, 14, 12, 250, 250, 250);
    put(p, 13, 12, 60, 60, 120);
    fill(p, 11, 14, 2, 1, Colour{skin.r - 50, skin.g - 50, skin.b - 40}, 0, 0); // nose
    fill(p, 10, 15, 4, 1, Colour{150, 70, 60}, 0, 0);                           // mouth
    // body, arms (sleeves then hands) and legs (trousers then shoes), the
    // left limbs in their own places as in the newer layout
    fillNet(p, 16, 16, 8, 12, 4, shirt, 8, seed + 6);
    const int arms[2][2] = {{40, 16}, {32, 48}};
    for (const int *limb : arms)
    {
        fillNet(p, limb[0], limb[1], 4, 12, 4, skin, 4, seed + 7);
        fill(p, limb[0] + 4, limb[1], 4, 4, shirt, 8, seed + 8);
        fill(p, limb[0], limb[1] + 4, 16, 4, shirt, 8, seed + 9);
    }
    const int legs2[2][2] = {{0, 16}, {16, 48}};
    for (const int *limb : legs2)
    {
        fillNet(p, limb[0], limb[1], 4, 12, 4, legs, 6, seed + 10);
        fill(p, limb[0], limb[1] + 13, 16, 3, Colour{60, 56, 54}, 4, seed + 11);
        fill(p, limb[0] + 8, limb[1], 4, 4, Colour{60, 56, 54}, 4, seed + 12);
    }
    return p;
}

// A minecart's picture, 64 x 32 in the layout block games use: the side
// panels (a 16 x 8 face with its 2 pixel edges) at the top left, the floor
// and its underside below them. Iron, darker at the rims, riveted.
Picture builtinMinecart()
{
    Picture p = blankPicture(64, 32);
    const Colour iron{150, 154, 160}, rim{96, 100, 108}, floor{126, 128, 132}, under{44, 44, 48};
    fill(p, 0, 0, 36, 10, rim, 6, 601u);
    fill(p, 3, 3, 14, 6, iron, 6, 602u); // the panel inside its rim
    fill(p, 21, 3, 14, 6, iron, 6, 603u);
    for (int x : {4, 9, 14})
    {
        put(p, x, 4, 210, 214, 220);
        put(p, x + 18, 4, 210, 214, 220);
    }
    fill(p, 0, 10, 44, 18, under, 4, 604u);
    fill(p, 2, 12, 20, 16, floor, 6, 605u);
    for (int y = 12; y < 28; y += 4)
        fill(p, 2, y, 20, 1, rim, 4, 606u + static_cast<unsigned int>(y)); // floor plates
    return p;
}

// A furnace's faces, 16 x 16: stone, the front with its fire lit.
Picture builtinFurnace(int face)
{
    Picture p = blankPicture(16, 16);
    fill(p, 0, 0, 16, 16, Colour{116, 116, 116}, 14, 611u + static_cast<unsigned int>(face));
    for (int k = 0; k < 16; ++k)
    {
        put(p, k, 0, 80, 80, 80);
        put(p, k, 15, 80, 80, 80);
        put(p, 0, k, 80, 80, 80);
        put(p, 15, k, 80, 80, 80);
    }
    if (face == 0)
    {
        fill(p, 4, 7, 8, 6, Colour{30, 26, 24}, 4, 621u);
        fill(p, 5, 9, 6, 4, Colour{250, 150, 40}, 20, 622u);
        fill(p, 6, 11, 4, 2, Colour{255, 226, 120}, 10, 623u);
        fill(p, 3, 3, 10, 2, Colour{70, 70, 70}, 6, 624u);
    }
    return p;
}

unsigned int uploadPicture(unsigned int texture, const Picture &p)
{
    if (texture == 0)
        glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p.width, p.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 p.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    return texture;
}

// The pack's pictures this module uses, by what they are of: a player's
// skin (wide arms; 64 x 64, or 64 x 32 from older packs), a minecart's.
bool skinFits(int w, int h)
{
    return w >= 64 && w % 64 == 0 && (h == w || h * 2 == w);
}
bool cartFits(int w, int h)
{
    return w >= 64 && w % 64 == 0 && h * 2 == w;
}
const char *const kSkinNames[] = {
    "player/wide/steve|steve", "player/wide/alex",  "player/wide/ari",
    "player/wide/efe",         "player/wide/kai",   "player/wide/makena",
    "player/wide/noor",        "player/wide/sunny", "player/wide/zuri"};
const int kSkinNameCount = 9;

struct Textures
{
    std::shared_ptr<const TexturePack::Pack> pack;
    bool stale = true;
    unsigned int skins[kCarts] = {};
    int skinRows[kCarts] = {}; // 64, or 32 for an old skin
    unsigned int cart = 0;
    unsigned int furnace[3] = {}; // front, side, top
} gTextures;

void uploadTextures()
{
    Textures &t = gTextures;
    // the pack's player skins, in turn, each rider a different one;
    // without them the built-in ones
    std::vector<Picture> packSkins;
    if (t.pack)
        for (int k = 0; k < kSkinNameCount; ++k)
        {
            Picture p;
            if (t.pack->entityPicture(kSkinNames[k], skinFits, p.width, p.height, p.rgba))
                packSkins.push_back(std::move(p));
        }
    for (int k = 0; k < kCarts; ++k)
    {
        const Picture p = packSkins.empty() ? builtinSkin(k)
                                            : packSkins[static_cast<size_t>(k) % packSkins.size()];
        t.skins[k] = uploadPicture(t.skins[k], p);
        t.skinRows[k] = p.height * 64 / p.width;
    }
    Picture cart;
    if (!t.pack || !t.pack->entityPicture("minecart/minecart|minecart", cartFits, cart.width,
                                          cart.height, cart.rgba))
        cart = builtinMinecart();
    t.cart = uploadPicture(t.cart, cart);
    const char *const faces[3] = {"furnace_front_on|furnace_front_lit|furnace_front",
                                  "furnace_side", "furnace_top"};
    for (int f = 0; f < 3; ++f)
    {
        PixelArt::Image image;
        Picture p;
        if (t.pack && t.pack->standIn(faces[f], TexturePack::Pack::Use::SOLID, image))
        {
            p.width = p.height = PixelArt::sizeOf(image);
            p.rgba = image;
        }
        else
            p = builtinFurnace(f);
        t.furnace[f] = uploadPicture(t.furnace[f], p);
    }
    t.stale = false;
}

// ---------------------------------------------------------------------
// Boxes
// ---------------------------------------------------------------------
void quad(float nx, float ny, float nz, const float (&c)[4][5], float texW, float texH)
{
    glNormal3f(nx, ny, nz);
    for (const float *k : c)
    {
        glTexCoord2f(k[3] / texW, k[4] / texH);
        glVertex3f(k[0] * kPx, k[1] * kPx, k[2] * kPx);
    }
}

// A box in pixels, its faces cut from a picture laid out the block games'
// way: a box w wide, h tall and d deep at (u, v) has its top and bottom in
// the row from v, d tall, and its sides in the row under it, h tall
// (right, front, left, back). texW, texH: the picture's size in pixels of
// that layout (64 x 64 for a skin).
void skinBox(float x0, float y0, float z0, float x1, float y1, float z1, float u, float v, float w,
             float h, float d, float texW, float texH)
{
    const float a0 = u, a1 = u + d, a2 = u + d + w, a3 = u + 2 * d + w, a4 = u + 2 * d + 2 * w;
    const float r0 = v, r1 = v + d, r2 = v + d + h;
    glBegin(GL_QUADS);
    quad(0, 0, 1,
         {{x0, y0, z1, a1, r2}, {x1, y0, z1, a2, r2}, {x1, y1, z1, a2, r1}, {x0, y1, z1, a1, r1}},
         texW, texH);
    quad(0, 0, -1,
         {{x1, y0, z0, a3, r2}, {x0, y0, z0, a4, r2}, {x0, y1, z0, a4, r1}, {x1, y1, z0, a3, r1}},
         texW, texH);
    quad(-1, 0, 0,
         {{x0, y0, z0, a0, r2}, {x0, y0, z1, a1, r2}, {x0, y1, z1, a1, r1}, {x0, y1, z0, a0, r1}},
         texW, texH);
    quad(1, 0, 0,
         {{x1, y0, z1, a2, r2}, {x1, y0, z0, a3, r2}, {x1, y1, z0, a3, r1}, {x1, y1, z1, a2, r1}},
         texW, texH);
    quad(0, 1, 0,
         {{x0, y1, z1, a1, r1}, {x1, y1, z1, a2, r1}, {x1, y1, z0, a2, r0}, {x0, y1, z0, a1, r0}},
         texW, texH);
    quad(0, -1, 0,
         {{x0, y0, z0, a2, r1},
          {x1, y0, z0, a2 + w, r1},
          {x1, y0, z1, a2 + w, r0},
          {x0, y0, z1, a2, r0}},
         texW, texH);
    glEnd();
}

// A box whose six faces all show one rectangle (u0, v0)-(u1, v1) of the
// picture, for the minecart's panels and the furnace's faces.
void panelBox(float x0, float y0, float z0, float x1, float y1, float z1, const float (&r)[4],
              float texW, float texH)
{
    const float u0 = r[0], v0 = r[1], u1 = r[2], v1 = r[3];
    glBegin(GL_QUADS);
    quad(0, 0, 1,
         {{x0, y0, z1, u0, v1}, {x1, y0, z1, u1, v1}, {x1, y1, z1, u1, v0}, {x0, y1, z1, u0, v0}},
         texW, texH);
    quad(0, 0, -1,
         {{x1, y0, z0, u0, v1}, {x0, y0, z0, u1, v1}, {x0, y1, z0, u1, v0}, {x1, y1, z0, u0, v0}},
         texW, texH);
    quad(-1, 0, 0,
         {{x0, y0, z0, u0, v1}, {x0, y0, z1, u1, v1}, {x0, y1, z1, u1, v0}, {x0, y1, z0, u0, v0}},
         texW, texH);
    quad(1, 0, 0,
         {{x1, y0, z1, u0, v1}, {x1, y0, z0, u1, v1}, {x1, y1, z0, u1, v0}, {x1, y1, z1, u0, v0}},
         texW, texH);
    quad(0, 1, 0,
         {{x0, y1, z1, u0, v1}, {x1, y1, z1, u1, v1}, {x1, y1, z0, u1, v0}, {x0, y1, z0, u0, v0}},
         texW, texH);
    quad(0, -1, 0,
         {{x0, y0, z0, u0, v1}, {x1, y0, z0, u1, v1}, {x1, y0, z1, u1, v0}, {x0, y0, z1, u0, v0}},
         texW, texH);
    glEnd();
}

void bind(unsigned int texture)
{
    if (!gShadowPass)
        glBindTexture(GL_TEXTURE_2D, texture);
}

// The see-through layer of a skin (a hat, a jacket's collar): cut out where
// it is see-through, which the lighting shader does by itself; the fixed
// pipeline needs the alpha test for it.
struct CutOut
{
    bool test;
    CutOut()
    {
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        test = program == 0;
        if (test)
        {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5f);
        }
    }
    ~CutOut()
    {
        if (test)
            glDisable(GL_ALPHA_TEST);
    }
};

// ---------------------------------------------------------------------
// The riders
// ---------------------------------------------------------------------
// How a rider sits at this moment, worked out from the track under their
// cart (TrainModel::draw()), so they react to the ride: hands forward on
// the climbs, arms up on the drops, leaning out on the curves, looking
// round on the level and now and then waving. Degrees.
struct Pose
{
    float arm[2];    // each arm (right, left) raised forward from hanging down: 90 forward, 180 up
    float spread[2]; // and out to the side
    float lean;      // the upper body to the side (positive: to +x)
    float pitch;     // and back (negative) or forward
    float headYaw;   // the head turned
    float headPitch; // and tilted up (negative) or down
};

// A player as a block game draws one, sitting in a minecart: the head 8
// pixels square, the body 8 x 12 x 4, each arm and leg 4 x 12 x 4, with the
// skin's outer layer (hair, hats, sleeves) a little bigger round them; the
// legs forward and a little apart as a rider's are. The hips are at the
// origin. The upper body turns about the hips, the head about the neck,
// the arms about the shoulders.
void drawRider(int who, const Pose &pose)
{
    const float th = static_cast<float>(gTextures.skinRows[who]);
    const bool layers = th > 32.5f; // only the newer skins have the outer layer and left limbs
    bind(gTextures.skins[who]);
    colour(255, 255, 255);
    // legs: forward, splayed a little
    for (int k = 0; k < 2; ++k)
    {
        const float s = k == 0 ? -1.0f : 1.0f; // right leg at -x
        glPushMatrix();
        glTranslatef(1.9f * s * kPx, 0.0f, 0.0f);
        glRotatef(-18.0f * s, 0.0f, 1.0f, 0.0f);
        glRotatef(-81.0f, 1.0f, 0.0f, 0.0f);
        const float u = k == 0 || !layers ? 0.0f : 16.0f, v = k == 0 || !layers ? 16.0f : 48.0f;
        skinBox(-2, -12, -2, 2, 0, 2, u, v, 4, 12, 4, 64.0f, th);
        if (layers && !gShadowPass)
        {
            CutOut cut;
            skinBox(-2.25f, -12.25f, -2.25f, 2.25f, 0.25f, 2.25f, 0, k == 0 ? 32.0f : 48.0f, 4, 12,
                    4, 64.0f, th);
        }
        glPopMatrix();
    }
    glPushMatrix();
    glRotatef(pose.lean, 0.0f, 0.0f, -1.0f);
    glRotatef(pose.pitch, 1.0f, 0.0f, 0.0f);
    // body
    skinBox(-4, 0, -2, 4, 12, 2, 16, 16, 8, 12, 4, 64.0f, th);
    if (layers && !gShadowPass)
    {
        CutOut cut;
        skinBox(-4.25f, -0.25f, -2.25f, 4.25f, 12.25f, 2.25f, 16, 32, 8, 12, 4, 64.0f, th);
    }
    // head
    glPushMatrix();
    glTranslatef(0.0f, 12.0f * kPx, 0.0f);
    glRotatef(pose.headYaw, 0.0f, 1.0f, 0.0f);
    glRotatef(pose.headPitch, 1.0f, 0.0f, 0.0f);
    skinBox(-4, 0, -4, 4, 8, 4, 0, 0, 8, 8, 8, 64.0f, th);
    if (!gShadowPass)
    {
        CutOut cut;
        skinBox(-4.5f, -0.5f, -4.5f, 4.5f, 8.5f, 4.5f, 32, 0, 8, 8, 8, 64.0f, th);
    }
    glPopMatrix();
    // arms, about the shoulders
    for (int k = 0; k < 2; ++k)
    {
        const float s = k == 0 ? -1.0f : 1.0f;
        glPushMatrix();
        glTranslatef(5.0f * s * kPx, 10.0f * kPx, 0.0f);
        glRotatef(pose.spread[k] * s, 0.0f, 0.0f, 1.0f);
        glRotatef(-pose.arm[k], 1.0f, 0.0f, 0.0f);
        const float u = k == 0 || !layers ? 40.0f : 32.0f, v = k == 0 || !layers ? 16.0f : 48.0f;
        const float x0 = k == 0 ? -3.0f : -1.0f;
        skinBox(x0, -10, -2, x0 + 4, 2, 2, u, v, 4, 12, 4, 64.0f, th);
        if (layers && !gShadowPass)
        {
            CutOut cut;
            skinBox(x0 - 0.25f, -10.25f, -2.25f, x0 + 4.25f, 2.25f, 2.25f, k == 0 ? 40.0f : 48.0f,
                    k == 0 ? 32.0f : 48.0f, 4, 12, 4, 64.0f, th);
        }
        glPopMatrix();
    }
    glPopMatrix();
}

// How a cart's rider sits, from the track under it: climb is the slope it
// points along (the y of its unit direction), turn how fast that direction
// swings to the side (positive: to +x) per unit along the track.
Pose poseFor(int who, float climb, float turn, float time)
{
    // how steeply the cart climbs or drops, 0..1
    const float up = fminf(fmaxf((climb - 0.08f) / 0.25f, 0.0f), 1.0f);
    const float down = fminf(fmaxf((-climb - 0.08f) / 0.27f, 0.0f), 1.0f);
    const float level = 1.0f - fmaxf(up, down);
    const float phase = static_cast<float>((who * 37) % 17);
    // some keep their hands forward on the rim even on the drops
    const bool brave = who % 3 != 1;
    const float thrill = down * (brave ? 1.0f : 0.2f);
    Pose p;
    for (int arm = 0; arm < 2; ++arm)
    {
        // at rest the arms reach forward over the rim; on the drops up
        const float sway = sinf(time * 5.0f + phase + static_cast<float>(arm) * 1.9f) * 10.0f;
        p.arm[arm] = 68.0f - 8.0f * up + (168.0f - 68.0f) * thrill + sway * thrill;
        p.spread[arm] = 4.0f + 16.0f * thrill;
    }
    // on the level now and then one waves
    const float wave = level * fmaxf(sinf(time * 0.6f + phase) - 0.7f, 0.0f) / 0.3f;
    const int hand = who % 2;
    p.arm[hand] += (150.0f - p.arm[hand]) * wave;
    p.spread[hand] += (18.0f + sinf(time * 7.0f + phase) * 12.0f - p.spread[hand]) * wave;
    // leaning out on a curve, as it throws them, but not over the side
    p.lean = fminf(fmaxf(-turn * 200.0f, -12.0f), 12.0f);
    p.pitch = 6.0f * up - 10.0f * down;
    // they look up the climbs, round them on the level
    p.headYaw = level * sinf(time * 0.45f + phase) * 32.0f;
    p.headPitch = -16.0f * up + 6.0f * down;
    return p;
}

// ---------------------------------------------------------------------
// The carts
// ---------------------------------------------------------------------
// A minecart as a block game models one, 16 pixels wide and 20 long: a
// floor 2 pixels thick and four iron sides 8 high and 2 thick, the ends
// across the full width; its picture's side panel on every side, its
// floor's picture inside. kRide pixels above the rails.
void drawMinecart()
{
    const float tw = 64.0f, th = 32.0f;
    bind(gTextures.cart);
    colour(255, 255, 255);
    glPushMatrix();
    glTranslatef(0.0f, kRide * kPx, 0.0f);
    const float edge[4] = {2, 0, 18, 2};
    // the floor: its inside up, its underside down
    glBegin(GL_QUADS);
    quad(0, 1, 0,
         {{-8, 2, 10, 2, 28}, {8, 2, 10, 18, 28}, {8, 2, -10, 18, 12}, {-8, 2, -10, 2, 12}}, tw,
         th);
    quad(0, -1, 0,
         {{-8, 0, -10, 24, 12}, {8, 0, -10, 40, 12}, {8, 0, 10, 40, 28}, {-8, 0, 10, 24, 28}}, tw,
         th);
    glEnd();
    // the floor's edges
    panelBox(-8, 0, -10, 8, 2, -9.99f, edge, tw, th);
    panelBox(-8, 0, 9.99f, 8, 2, 10, edge, tw, th);
    panelBox(-8, 0, -10, -7.99f, 2, 10, edge, tw, th);
    panelBox(7.99f, 0, -10, 8, 2, 10, edge, tw, th);
    // the sides, as the game turns one panel round the cart: its outer
    // face out, its inner face in, the ends across the full width
    for (int k = 0; k < 4; ++k)
    {
        glPushMatrix();
        glRotatef(90.0f * static_cast<float>(k), 0.0f, 1.0f, 0.0f);
        if (k % 2 == 0)
            skinBox(-8, 2, 8, 8, 10, 10, 0, 0, 16, 8, 2, tw, th); // an end
        else
            skinBox(-8, 2, 6, 8, 10, 8, 0, 0, 16, 8, 2, tw, th); // a side
        glPopMatrix();
    }
    glPopMatrix();
}

// The furnace inside the leading cart, as in a furnace minecart: a block
// three quarters the size, its lit front looking ahead and glowing.
void drawFurnace()
{
    glPushMatrix();
    glTranslatef(0.0f, (kRide + 2.0f) * kPx, 0.0f);
    colour(255, 255, 255);
    const float whole[4] = {0, 0, 16, 16};
    const float s = 6.0f; // half its 12 pixels
    bind(gTextures.furnace[1]);
    panelBox(-s, 0, -s, s, 2 * s, s, whole, 16.0f, 16.0f);
    bind(gTextures.furnace[2]);
    glBegin(GL_QUADS);
    quad(0, 1, 0,
         {{-s, 2 * s + 0.02f, s, 0, 16},
          {s, 2 * s + 0.02f, s, 16, 16},
          {s, 2 * s + 0.02f, -s, 16, 0},
          {-s, 2 * s + 0.02f, -s, 0, 0}},
         16.0f, 16.0f);
    glEnd();
    bind(gTextures.furnace[0]);
    glow(0.9f);
    glBegin(GL_QUADS);
    quad(0, 0, 1,
         {{-s, 0, s + 0.02f, 0, 16},
          {s, 0, s + 0.02f, 16, 16},
          {s, 2 * s, s + 0.02f, 16, 0},
          {-s, 2 * s, s + 0.02f, 0, 0}},
         16.0f, 16.0f);
    glEnd();
    glow(0.0f);
    glPopMatrix();
}

// The coupling to the cart behind: a short dark bar between them.
void drawCoupling()
{
    bind(0);
    colour(50, 50, 56);
    glPushMatrix();
    glTranslatef(0.0f, (kRide + 1.0f) * kPx, 0.0f);
    const float none[4] = {0, 0, 1, 1};
    panelBox(-0.6f, -0.6f, -16.0f, 0.6f, 0.6f, -10.0f, none, 1.0f, 1.0f);
    glPopMatrix();
}

// The track parameter `distance` further back along the track from u.
float stepBack(const CTrack &track, float u, float distance, TrackSplineType splineType)
{
    const float period = static_cast<float>(track.points.size());
    const float step = 0.004f;
    float remaining = distance;
    Pnt3f here = track.evaluatePosition(u, splineType);
    for (int i = 0; i < 20000 && remaining > 0.0f; ++i)
    {
        float next = u - step;
        if (next < 0.0f)
            next += period;
        const Pnt3f there = track.evaluatePosition(next, splineType);
        const float dx = there.x - here.x, dy = there.y - here.y, dz = there.z - here.z;
        const float length = sqrtf(dx * dx + dy * dy + dz * dz);
        if (length >= remaining && length > 1.0e-6f)
        {
            u -= step * (remaining / length);
            if (u < 0.0f)
                u += period;
            return u;
        }
        remaining -= length;
        u = next;
        here = there;
    }
    return u;
}
} // namespace

float TrainModel::length()
{
    return kEngineToCart + static_cast<float>(kCarts - 1) * kCartToCart;
}

float TrainModel::riderTop()
{
    // the hips where draw() seats them, the body 12 pixels and the head 8
    // with its outer layer half a pixel over it
    return (kRide + kHips + 12.0f + 8.5f) * kPx;
}

float TrainModel::parameterBehind(const CTrack &track, float u, float distance,
                                  TrackSplineType splineType)
{
    return stepBack(track, u, distance, splineType);
}

void TrainModel::setTexturePack(std::shared_ptr<const TexturePack::Pack> pack)
{
    gTextures.pack = std::move(pack);
    gTextures.stale = true;
}

void TrainModel::releaseTextures()
{
    // the names went with the old context; made again at the next draw
    const std::shared_ptr<const TexturePack::Pack> pack = gTextures.pack;
    gTextures = Textures();
    gTextures.pack = pack;
}

void TrainModel::draw(const CTrack &track, TrackSplineType splineType, bool shadowPass, float time)
{
    if (track.points.size() < 4)
        return;
    gShadowPass = shadowPass;
    if (gTextures.stale)
        uploadTextures();
    glPushAttrib(GL_CURRENT_BIT | GL_ENABLE_BIT | GL_TEXTURE_BIT);
    if (!shadowPass)
    {
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
    glow(0.0f);
    float u = track.trainU;
    for (int car = 0; car < kVehicles; ++car)
    {
        if (car > 0)
            u = stepBack(track, u, car == 1 ? kEngineToCart : kCartToCart, splineType);
        const Pnt3f position = track.evaluatePosition(u, splineType);
        Pnt3f forward = track.evaluateTangent(u, splineType);
        Pnt3f side;
        Pnt3f up;
        makeTrackFrame(forward, side, up);
        const GLfloat frame[16] = {side.x,     side.y,     side.z,     0.0f,      up.x,      up.y,
                                   up.z,       0.0f,       forward.x,  forward.y, forward.z, 0.0f,
                                   position.x, position.y, position.z, 1.0f};
        glPushMatrix();
        glMultMatrixf(frame);
        drawMinecart();
        if (car < kVehicles - 1)
            drawCoupling();
        if (car == 0)
            drawFurnace();
        else
        {
            // how the track bends under the cart, for its rider: its
            // slope, and how fast it turns to the side over the next stretch
            const float period = static_cast<float>(track.points.size());
            float ahead = u + 0.05f;
            if (ahead >= period)
                ahead -= period;
            Pnt3f next = track.evaluateTangent(ahead, splineType);
            next.normalize();
            Pnt3f dir = forward;
            dir.normalize();
            const Pnt3f there = track.evaluatePosition(ahead, splineType);
            const float dx = there.x - position.x, dy = there.y - position.y,
                        dz = there.z - position.z;
            const float stretch = fmaxf(sqrtf(dx * dx + dy * dy + dz * dz), 0.01f);
            const float turn = ((next.x - dir.x) * side.x + (next.y - dir.y) * side.y +
                                (next.z - dir.z) * side.z) /
                               stretch;
            glPushMatrix();
            // sitting on the floor, a little back so the legs fit
            glTranslatef(0.0f, (kRide + kHips) * kPx, -4.5f * kPx);
            drawRider(car - 1, poseFor(car - 1, dir.y, turn, time));
            glPopMatrix();
        }
        glPopMatrix();
    }
    glow(0.0f);
    glBindTexture(GL_TEXTURE_2D, 0);
    glPopAttrib();
}
