/************************************************************************
     File:        WaterFlow.h

     Comment:     How water spreads from its sources over block terrain,
                  following the rules of a block game, worked out once
                  when the scenery is built (LandWorld) and testable on
                  its own (tests/WaterFlowCheck.cpp).

                  Every water cell has a level: 0 is a source, 1 to 7 is
                  water that has flowed that many cells sideways, FALLING
                  is water falling down a column.

                  * Down first: water flows into the cell below when it
                    can. Flowing water that can go down does not spread
                    sideways; a source does both.
                  * Sideways it goes one level higher per cell (falling
                    water that lands starts again at 1) and stops after
                    level 7.
                  * Which way: every direction it could flow into gets a
                    weight, 999 to begin with. If the cell below the
                    neighbour is open, the weight is 0; otherwise the
                    shortest way, at most 4 cells, from the neighbour to
                    a cell with an open cell below it. Water flows only in
                    the directions with the lowest weight, so it heads for
                    the nearest drop, and spreads evenly where there is
                    none in reach.

                  Cells outside the field are solid. A SINK cell takes
                  water and keeps nothing (the sea).

*************************************************************************/
#pragma once

#include <vector>

namespace WaterFlow
{

enum Cell
{
    OPEN = 0,  // air: water can flow in
    SOLID = 1, // ground, or anything else water cannot enter
    WATER = 2,
    SINK = 3 // water flows in and is gone (the sea)
};

const int FALLING = 8;      // the level of falling water
const int MAX_LEVEL = 7;    // the furthest sideways water goes
const int SLOPE_SEARCH = 4; // how far it looks for a way down
const int NO_WAY = 999;     // the weight of a direction with no drop in reach

class Field
{
  public:
    Field(int sizeX, int sizeY, int sizeZ);

    int sizeX() const
    {
        return m_x;
    }
    int sizeY() const
    {
        return m_y;
    }
    int sizeZ() const
    {
        return m_z;
    }

    // SOLID outside the field.
    int cell(int x, int y, int z) const;
    // The level of a WATER cell (0 source .. 7, or FALLING).
    int level(int x, int y, int z) const;

    void setCell(int x, int y, int z, int cell);
    // Puts a source of water in the cell.
    void addSource(int x, int y, int z);

    // Spreads the water from every WATER cell until nothing changes.
    void spread();

    // The weight of each of the four directions (+x, -x, +z, -z) for water
    // flowing sideways out of the cell; for tests.
    void weights(int x, int y, int z, int out[4]) const;

  private:
    bool inside(int x, int y, int z) const;
    int index(int x, int y, int z) const;
    // Water can flow into it: open, sink, or water that is not a source.
    bool canEnter(int x, int y, int z) const;
    // The cell below is somewhere water can go down to.
    bool hole(int x, int y, int z) const;
    int slopeDistance(int x, int y, int z, int depth, int from) const;
    // Puts water of this level in the cell if that is more water than it has.
    bool fill(int x, int y, int z, int level);

    int m_x, m_y, m_z;
    std::vector<unsigned char> m_cell;
    std::vector<unsigned char> m_level;
    std::vector<int> m_queue; // cells whose water has to flow on
};

} // namespace WaterFlow
