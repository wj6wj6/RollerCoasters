// Checks that water spreads the way it does in a block game (WaterFlow.h): down first, seven
// cells sideways at most, and sideways only towards the nearest way down within four cells.
#include "WaterFlow.h"

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

// A 31 x 6 x 31 field with a solid floor at y = 0 and y = 1 the air above it.
WaterFlow::Field floorField()
{
    WaterFlow::Field f(31, 6, 31);
    for (int x = 0; x < 31; ++x)
        for (int z = 0; z < 31; ++z)
            f.setCell(x, 0, z, WaterFlow::SOLID);
    return f;
}

bool water(const WaterFlow::Field &f, int x, int y, int z)
{
    return f.cell(x, y, z) == WaterFlow::WATER;
}
} // namespace

int main()
{
    using namespace WaterFlow;

    {
        // On flat ground a source spreads seven cells each way, one level per cell.
        Field f = floorField();
        f.addSource(15, 1, 15);
        f.spread();
        expect(water(f, 22, 1, 15) && f.level(22, 1, 15) == 7 && !water(f, 23, 1, 15),
               "flat ground: seven cells east, level 7 at the end, then no further");
        expect(water(f, 8, 1, 15) && !water(f, 7, 1, 15) && water(f, 15, 1, 8) &&
                   !water(f, 15, 1, 7),
               "flat ground: the same west, north and south");
        expect(f.level(16, 1, 15) == 1 && f.level(18, 1, 15) == 3, "levels count the cells");
        expect(water(f, 18, 1, 19) && f.level(18, 1, 19) == 7 && !water(f, 19, 1, 19),
               "flat ground: a diamond, seven steps round corners too");
    }

    {
        // A hole three cells east: every direction weighs 999 but east, which weighs its
        // distance, so the water goes east only.
        Field f = floorField();
        f.setCell(19, 0, 15, OPEN); // the hole, below (19, 1, 15)
        f.addSource(15, 1, 15);
        int w[4];
        f.weights(15, 1, 15, w);
        expect(w[0] == 3 && w[1] == NO_WAY && w[2] == NO_WAY && w[3] == NO_WAY,
               "hole 4 east: weights east 3 (the neighbour is 3 from it), the others 999");
        f.spread();
        expect(water(f, 16, 1, 15) && !water(f, 14, 1, 15) && !water(f, 15, 1, 16) &&
                   !water(f, 15, 1, 14),
               "hole 4 east: the source flows east only");
        expect(water(f, 19, 1, 15) && water(f, 19, 0, 15) && f.level(19, 0, 15) == FALLING,
               "the water reaches the hole and falls into it");
        expect(!water(f, 20, 1, 15), "flowing water over a hole goes down, not on");
    }

    {
        // Weight 0: a hole right under the neighbour.
        Field f = floorField();
        f.setCell(16, 0, 15, OPEN);
        int w[4];
        f.weights(15, 1, 15, w);
        expect(w[0] == 0 && w[1] == 4 && w[2] == 2 && w[3] == 2,
               "hole under the east neighbour: east 0; round by the sides 2, the long way round 4");
    }

    {
        // Two ways down, two and three cells away: only the nearer one is followed.
        Field f = floorField();
        f.setCell(18, 0, 15, OPEN); // 3 east of the source
        f.setCell(12, 0, 15, OPEN); // 3 west
        f.setCell(15, 0, 18, OPEN); // 3 south
        f.setCell(15, 0, 11, OPEN); // 4 north
        int w[4];
        f.weights(15, 1, 15, w);
        expect(w[0] == 2 && w[1] == 2 && w[2] == 2 && w[3] == 3,
               "weights: 2, 2, 2 for the holes 3 away, 3 for the one 4 away");
        f.addSource(15, 1, 15);
        f.spread();
        expect(water(f, 16, 1, 15) && water(f, 14, 1, 15) && water(f, 15, 1, 16) &&
                   !water(f, 15, 1, 14),
               "the water takes the three nearest ways and not the farther one");
    }

    {
        // A way down six cells away is out of reach (the neighbour is five from it, the
        // search goes four): the water spreads evenly.
        Field f = floorField();
        f.setCell(21, 0, 15, OPEN);
        int w[4];
        f.weights(15, 1, 15, w);
        expect(w[0] == NO_WAY && w[1] == NO_WAY && w[2] == NO_WAY && w[3] == NO_WAY,
               "a hole 6 away is beyond the 4 cell search");
        f.addSource(15, 1, 15);
        f.spread();
        expect(water(f, 14, 1, 15) && water(f, 15, 1, 16) && water(f, 15, 1, 14),
               "with no way down in reach the water spreads every way");
    }

    {
        // The search goes round corners but not through walls.
        Field f = floorField();
        for (int z = 13; z <= 17; ++z)
            if (z != 15)
                f.setCell(16, 1, z, SOLID);
        f.setCell(17, 1, 15, SOLID); // a pocket east, closed at its end
        f.setCell(15, 0, 18, OPEN);  // 3 south: the only way down in reach
        int w[4];
        f.weights(15, 1, 15, w);
        expect(w[2] == 2 && w[0] == NO_WAY, "walls: the pocket leads nowhere, south weighs 2");
    }

    {
        // Falling water lands and spreads again from level 1.
        WaterFlow::Field f(31, 12, 31);
        for (int x = 0; x < 31; ++x)
            for (int z = 0; z < 31; ++z)
                f.setCell(x, 0, z, SOLID);
        for (int x = 0; x < 31; ++x)
            for (int z = 0; z < 31; ++z)
                for (int y = 1; y <= 5; ++y)
                    if (x <= 15)
                        f.setCell(x, y, z, SOLID); // a cliff, its top at y = 5, edge at x = 15
        f.addSource(13, 6, 15);
        f.spread();
        expect(water(f, 14, 6, 15) && water(f, 15, 6, 15) && water(f, 16, 6, 15),
               "from the cliff top the water runs over the edge");
        expect(water(f, 16, 3, 15) && f.level(16, 3, 15) == FALLING,
               "and falls down the column below");
        expect(water(f, 16, 1, 15) && f.level(17, 1, 15) == 1 && f.level(23, 1, 15) == 7 &&
                   !water(f, 24, 1, 15),
               "landing, it spreads seven cells again from level 1");
        expect(!water(f, 12, 6, 15) && !water(f, 13, 6, 14),
               "on top it only heads for the edge, the nearest way down");
    }

    {
        // Water falling into a sink is gone; a source keeps spreading beside a hole.
        Field f = floorField();
        f.setCell(16, 0, 15, SINK);
        f.addSource(15, 1, 15);
        f.spread();
        expect(water(f, 16, 1, 15) && f.cell(16, 0, 15) == SINK,
               "a sink takes the water and stays a sink");
        expect(!water(f, 14, 1, 15), "a source with a drop beside it heads only for the drop");
    }

    std::printf("%s\n", failures == 0 ? "WaterFlowCheck passed" : "WaterFlowCheck FAILED");
    return failures == 0 ? 0 : 1;
}
