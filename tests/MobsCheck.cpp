// Checks the animals' models against their pictures (Mobs.h): every face of every box of the
// sheep, the pig and the cow falls on painted texels of its built-in picture, within the
// picture, so a texture pack's pictures, laid out the same way, fit them too; and only the
// picture sizes the layout allows are taken from a pack.
#include "Mobs.h"

#include <cstdio>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

bool opaque(const Mobs::Picture &p, int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
        {
            if (x < 0 || y < 0 || x >= p.width || y >= p.height)
                return false;
            if (p.rgba[static_cast<size_t>((y * p.width + x) * 4 + 3)] == 0)
                return false;
        }
    return true;
}
} // namespace

int main()
{
    const char *names[Mobs::KIND_COUNT] = {"sheep", "pig", "cow"};
    for (int kind = 0; kind < Mobs::KIND_COUNT; ++kind)
    {
        bool painted = true, legs = true;
        int legCount[5] = {};
        for (const Mobs::Box &b : Mobs::model(kind))
        {
            const Mobs::Picture p = Mobs::builtin(b.skin);
            // the row of tops and bottoms, then the row of sides
            painted = painted && opaque(p, b.u + b.d, b.v, 2 * b.w, b.d) &&
                      opaque(p, b.u, b.v + b.d, 2 * (b.d + b.w), b.h);
            legs = legs && b.x0 < b.x1 && b.y0 < b.y1 && b.z0 < b.z1 && b.leg >= 0 && b.leg <= 4;
            ++legCount[b.leg];
            // a box standing up is as big as its picture says (the wool is a little bigger)
            if (!b.lying && b.skin != Mobs::SHEEP_WOOL)
                legs = legs && b.x1 - b.x0 == static_cast<float>(b.w) &&
                       b.y1 - b.y0 == static_cast<float>(b.h) &&
                       b.z1 - b.z0 == static_cast<float>(b.d);
        }
        char what[96];
        std::snprintf(what, sizeof what, "every face of the %s is on painted texels", names[kind]);
        expect(painted, what);
        std::snprintf(what, sizeof what, "the %s's boxes are the size of their pictures",
                      names[kind]);
        expect(legs, what);
        std::snprintf(what, sizeof what, "the %s has four legs", names[kind]);
        expect(legCount[1] >= 1 && legCount[2] >= 1 && legCount[3] >= 1 && legCount[4] >= 1, what);
    }
    for (int skin = 0; skin < Mobs::SKIN_COUNT; ++skin)
    {
        const Mobs::Picture p = Mobs::builtin(skin);
        expect(p.width == 64 && p.height == 32 &&
                   p.rgba.size() == static_cast<size_t>(p.width * p.height * 4),
               "a built-in picture is 64 x 32");
    }
    expect(Mobs::fits(64, 32) && Mobs::fits(128, 64) && Mobs::fits(128, 128),
           "pictures of the game's sizes fit");
    expect(!Mobs::fits(16, 16) && !Mobs::fits(64, 48) && !Mobs::fits(96, 48), "other sizes do not");
    std::printf("%s\n", failures == 0 ? "MobsCheck passed" : "MobsCheck FAILED");
    return failures == 0 ? 0 : 1;
}
