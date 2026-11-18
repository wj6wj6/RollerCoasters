/************************************************************************
     File:        WaterFlow.cpp

     Comment:     See WaterFlow.h.

*************************************************************************/

#include "WaterFlow.h"

#include <cstddef>

namespace
{
// The four sideways directions; direction d ^ 1 is its opposite.
const int kDx[4] = {1, -1, 0, 0};
const int kDz[4] = {0, 0, 1, -1};
} // namespace

namespace WaterFlow
{

Field::Field(int sizeX, int sizeY, int sizeZ)
    : m_x(sizeX), m_y(sizeY), m_z(sizeZ),
      m_cell(static_cast<size_t>(sizeX) * static_cast<size_t>(sizeY) * static_cast<size_t>(sizeZ),
             OPEN),
      m_level(m_cell.size(), 0)
{
}

bool Field::inside(int x, int y, int z) const
{
    return x >= 0 && y >= 0 && z >= 0 && x < m_x && y < m_y && z < m_z;
}

int Field::index(int x, int y, int z) const
{
    return (x * m_y + y) * m_z + z;
}

int Field::cell(int x, int y, int z) const
{
    return inside(x, y, z) ? m_cell[static_cast<size_t>(index(x, y, z))] : SOLID;
}

int Field::level(int x, int y, int z) const
{
    return inside(x, y, z) ? m_level[static_cast<size_t>(index(x, y, z))] : 0;
}

void Field::setCell(int x, int y, int z, int cell)
{
    if (inside(x, y, z))
        m_cell[static_cast<size_t>(index(x, y, z))] = static_cast<unsigned char>(cell);
}

void Field::addSource(int x, int y, int z)
{
    if (!inside(x, y, z))
        return;
    const size_t i = static_cast<size_t>(index(x, y, z));
    m_cell[i] = WATER;
    m_level[i] = 0;
}

bool Field::canEnter(int x, int y, int z) const
{
    const int c = cell(x, y, z);
    return c == OPEN || c == SINK || (c == WATER && level(x, y, z) != 0);
}

bool Field::hole(int x, int y, int z) const
{
    const int below = cell(x, y - 1, z);
    return below == OPEN || below == SINK || below == WATER;
}

// The shortest way from (x, y, z), reached at `depth`, to a cell with a hole
// under it, not going back the way it came (`from`).
int Field::slopeDistance(int x, int y, int z, int depth, int from) const
{
    int best = NO_WAY;
    for (int d = 0; d < 4; ++d)
    {
        if (d == from)
            continue;
        const int nx = x + kDx[d], nz = z + kDz[d];
        if (!canEnter(nx, y, nz))
            continue;
        if (hole(nx, y, nz))
            return depth;
        if (depth < SLOPE_SEARCH)
        {
            const int further = slopeDistance(nx, y, nz, depth + 1, d ^ 1);
            if (further < best)
                best = further;
        }
    }
    return best;
}

void Field::weights(int x, int y, int z, int out[4]) const
{
    for (int d = 0; d < 4; ++d)
    {
        out[d] = NO_WAY;
        const int nx = x + kDx[d], nz = z + kDz[d];
        if (!canEnter(nx, y, nz))
            continue;
        out[d] = hole(nx, y, nz) ? 0 : slopeDistance(nx, y, nz, 1, d ^ 1);
    }
}

bool Field::fill(int x, int y, int z, int level)
{
    if (!inside(x, y, z))
        return false;
    const size_t i = static_cast<size_t>(index(x, y, z));
    const int c = m_cell[i];
    if (c == SINK || c == SOLID)
        return false; // a sink takes the water and keeps nothing
    if (c == WATER)
    {
        const int now = m_level[i];
        if (now == 0)
            return false; // already a source
        // falling water fills a cell more than any sideways flow; between
        // sideways flows the lower level (closer to its source) wins
        if (now == FALLING || (level != FALLING && level >= now))
            return false;
    }
    m_cell[i] = WATER;
    m_level[i] = static_cast<unsigned char>(level);
    m_queue.push_back(index(x, y, z));
    return true;
}

void Field::spread()
{
    m_queue.clear();
    for (int x = 0; x < m_x; ++x)
        for (int y = 0; y < m_y; ++y)
            for (int z = 0; z < m_z; ++z)
                if (cell(x, y, z) == WATER)
                    m_queue.push_back(index(x, y, z));

    for (size_t head = 0; head < m_queue.size(); ++head)
    {
        const int i = m_queue[head];
        const int z = i % m_z;
        const int y = (i / m_z) % m_y;
        const int x = i / (m_z * m_y);
        if (cell(x, y, z) != WATER)
            continue;
        const int lvl = level(x, y, z);

        // Down first.
        const int below = cell(x, y - 1, z);
        if (below == OPEN || below == SINK ||
            (below == WATER && level(x, y - 1, z) != 0 && level(x, y - 1, z) != FALLING))
            fill(x, y - 1, z, FALLING);
        // Flowing water over a hole only goes down; a source also spreads.
        if (lvl != 0 && hole(x, y, z))
            continue;

        const int next = lvl == FALLING ? 1 : lvl + 1;
        if (next > MAX_LEVEL)
            continue;
        int w[4];
        weights(x, y, z, w);
        int lowest = NO_WAY + 1;
        for (int d = 0; d < 4; ++d)
            if (canEnter(x + kDx[d], y, z + kDz[d]) && w[d] < lowest)
                lowest = w[d];
        for (int d = 0; d < 4; ++d)
            if (canEnter(x + kDx[d], y, z + kDz[d]) && w[d] == lowest)
                fill(x + kDx[d], y, z + kDz[d], next);
    }
    m_queue.clear();
}

} // namespace WaterFlow
