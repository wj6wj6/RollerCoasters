/************************************************************************
     File:        CellQueue.h

     Comment:     A first-in, first-out queue kept in one vector, for the
                  light floods of LandWorld and OceanWorld. std::deque in
                  the MSVC library puts every element larger than 8 bytes
                  in a heap block of its own, so flooding millions of cells
                  spent most of the world's build time in the allocator,
                  worst of all in a Debug build with the debugger's heap.

*************************************************************************/
#pragma once

#include <cstddef>
#include <vector>

template <typename T> class CellQueue
{
  public:
    bool empty() const
    {
        return m_head == m_items.size();
    }

    void push_back(const T &item)
    {
        m_items.push_back(item);
    }

    // Takes the oldest item out. Call only when not empty().
    T pop_front()
    {
        const T item = m_items[m_head++];
        if (m_head == m_items.size())
        {
            m_items.clear();
            m_head = 0;
        }
        else if (m_head >= kCompactAt && m_head * 2 >= m_items.size())
        {
            // Drop the items already taken, so the vector does not keep
            // growing while the flood goes on.
            m_items.erase(m_items.begin(), m_items.begin() + static_cast<std::ptrdiff_t>(m_head));
            m_head = 0;
        }
        return item;
    }

  private:
    static const std::size_t kCompactAt = 1 << 16;
    std::vector<T> m_items;
    std::size_t m_head = 0;
};
