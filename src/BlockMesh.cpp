/************************************************************************
     File:        BlockMesh.cpp

     Comment:     See BlockMesh.h.
*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include <cstddef>

#include "BlockMesh.h"

namespace BlockMesh
{

namespace
{

signed char normalByte(float v)
{
    const float scaled = roundf(v * 127.0f);
    return static_cast<signed char>(scaled > 127.0f ? 127.0f
                                                    : (scaled < -127.0f ? -127.0f : scaled));
}

unsigned char colorByte(float v)
{
    const float scaled = roundf(v * 255.0f);
    return static_cast<unsigned char>(scaled > 255.0f ? 255.0f : (scaled < 0.0f ? 0.0f : scaled));
}

} // namespace

Mesh pack(const Batches &batches, const std::vector<unsigned int> &textures)
{
    Mesh mesh;
    size_t total = 0;
    for (const std::vector<Vertex> &batch : batches)
        total += batch.size();
    mesh.memory.reserve(total);
    for (size_t t = 0; t < batches.size(); ++t)
    {
        const std::vector<Vertex> &batch = batches[t];
        if (batch.empty())
            continue;
        Range range;
        range.texture = t < textures.size() ? textures[t] : 0u;
        range.first = static_cast<int>(mesh.memory.size());
        range.count = static_cast<int>(batch.size());
        mesh.ranges.push_back(range);
        for (const Vertex &v : batch)
        {
            PackedVertex p;
            for (int k = 0; k < 3; ++k)
            {
                p.pos[k] = v.pos[k];
                p.normal[k] = normalByte(v.normal[k]);
                p.color[k] = colorByte(v.color[k]);
            }
            p.normal[3] = 0;
            p.color[3] = 255;
            p.uv[0] = v.uv[0];
            p.uv[1] = v.uv[1];
            for (int k = 0; k < 4; ++k)
                p.extra[k] = v.extra[k];
            mesh.memory.push_back(p);
        }
        mesh.last = batch.back();
    }
    return mesh;
}

size_t bytes(const Mesh &mesh)
{
    size_t count = 0;
    for (const Range &range : mesh.ranges)
        count += static_cast<size_t>(range.count);
    return count * sizeof(PackedVertex);
}

float viewDepth(const float modelview[16], const float lo[3], const float hi[3])
{
    const float x = 0.5f * (lo[0] + hi[0]);
    const float y = 0.5f * (lo[1] + hi[1]);
    const float z = 0.5f * (lo[2] + hi[2]);
    // the eye looks down -z
    return -(modelview[2] * x + modelview[6] * y + modelview[10] * z + modelview[14]);
}

int knob(const char *name, int fallback)
{
#ifdef _MSC_VER
    char *value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value)
        return fallback;
    const int result = atoi(value);
    free(value);
    return result;
#else
    const char *value = getenv(name);
    return value ? atoi(value) : fallback;
#endif
}

int drawWindow()
{
    static const int window = knob("RC_ARENA_WINDOW", 8);
    return window;
}

bool nearestFirst()
{
    static const bool sort = knob("RC_ARENA_NEAR", 1) != 0;
    return sort;
}

Arena::Arena(int pageVertices) : m_pageVertices(pageVertices > 0 ? pageVertices : 1)
{
}

Mesh Arena::add(Mesh packed)
{
    Mesh mesh;
    mesh.last = packed.last;
    const int count = static_cast<int>(packed.memory.size());
    if (count == 0)
        return mesh;
    if (m_pages.empty() || m_pages.back().capacity - m_pages.back().used < count)
    {
        // a new page (also after compact(), which leaves no room)
        Page page;
        page.capacity = count > m_pageVertices ? count : m_pageVertices;
        page.group = static_cast<int>(m_pages.size());
        if (GLAD_GL_VERSION_1_5)
        {
            GLuint buffer = 0;
            glGenBuffers(1, &buffer);
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(page.capacity) *
                             static_cast<GLsizeiptr>(sizeof(PackedVertex)),
                         nullptr, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            page.buffer = buffer;
        }
        else
        {
            page.memory.reserve(static_cast<size_t>(page.capacity));
        }
        m_pages.push_back(std::move(page));
    }
    Page &page = m_pages.back();
    if (page.buffer != 0)
    {
        glBindBuffer(GL_ARRAY_BUFFER, page.buffer);
        glBufferSubData(
            GL_ARRAY_BUFFER,
            static_cast<GLintptr>(page.used) * static_cast<GLintptr>(sizeof(PackedVertex)),
            static_cast<GLsizeiptr>(count) * static_cast<GLsizeiptr>(sizeof(PackedVertex)),
            packed.memory.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    else
    {
        page.memory.insert(page.memory.end(), packed.memory.begin(), packed.memory.end());
    }
    mesh.page = static_cast<int>(m_pages.size()) - 1;
    mesh.ranges = std::move(packed.ranges);
    for (Range &range : mesh.ranges)
        range.first += page.used;
    page.used += count;
    return mesh;
}

void Arena::compact()
{
    if (m_pages.empty() || !GLAD_GL_VERSION_3_1)
        return;
    int total = 0;
    bool single = true;
    for (const Page &page : m_pages)
    {
        if (page.buffer == 0)
            return; // in memory: nothing to give back
        total += page.used;
        single = single && page.buffer == m_pages[0].buffer;
    }
    if (single && capacity() == bytes())
        return; // already one buffer with no spare room
    const GLsizeiptr stride = static_cast<GLsizeiptr>(sizeof(PackedVertex));
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(total) * stride, nullptr,
                 GL_STATIC_DRAW);
    std::vector<GLuint> old;
    int base = 0;
    for (Page &page : m_pages)
    {
        glBindBuffer(GL_COPY_READ_BUFFER, page.buffer);
        glCopyBufferSubData(
            GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(page.base) * stride,
            static_cast<GLintptr>(base) * stride, static_cast<GLsizeiptr>(page.used) * stride);
        if (std::find(old.begin(), old.end(), page.buffer) == old.end())
            old.push_back(page.buffer);
        page.buffer = buffer;
        page.base = base;
        page.capacity = page.used;
        page.group = 0;
        base += page.used;
    }
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    glDeleteBuffers(static_cast<GLsizei>(old.size()), old.data());
}

const Mesh *Arena::gather(const std::vector<const Mesh *> &meshes, int window) const
{
    m_items.clear();
    const Mesh *last = nullptr;
    for (size_t m = 0; m < meshes.size(); ++m)
    {
        const Mesh *mesh = meshes[m];
        if (!mesh || mesh->page < 0 || mesh->page >= static_cast<int>(m_pages.size()) ||
            mesh->ranges.empty())
            continue;
        const Page &page = m_pages[static_cast<size_t>(mesh->page)];
        for (const Range &range : mesh->ranges)
        {
            Item item;
            item.window = window > 0 ? static_cast<int>(m / static_cast<size_t>(window)) : 0;
            item.group = page.group;
            item.page = mesh->page;
            item.texture = range.texture;
            item.first = page.base + range.first;
            item.count = range.count;
            m_items.push_back(item);
        }
        last = mesh;
    }
    std::stable_sort(m_items.begin(), m_items.end(), [](const Item &a, const Item &b) {
        if (a.window != b.window)
            return a.window < b.window;
        return a.group != b.group ? a.group < b.group : a.texture < b.texture;
    });
    return last;
}

std::vector<Arena::Call> Arena::plan(const std::vector<const Mesh *> &meshes, int window) const
{
    gather(meshes, window);
    std::vector<Call> calls;
    int lastWindow = -1;
    for (const Item &item : m_items)
    {
        if (calls.empty() || calls.back().buffer != item.group ||
            calls.back().texture != item.texture || item.window != lastWindow)
        {
            Call call;
            call.buffer = item.group;
            call.texture = item.texture;
            calls.push_back(call);
        }
        lastWindow = item.window;
        calls.back().first.push_back(item.first);
        calls.back().count.push_back(item.count);
    }
    return calls;
}

void Arena::draw(const std::vector<const Mesh *> &meshes, int window) const
{
    const Mesh *last = gather(meshes, window);
    if (!last)
        return;
    const GLsizei stride = static_cast<GLsizei>(sizeof(PackedVertex));

    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    bool bound = false;
    size_t i = 0;
    while (i < m_items.size())
    {
        // one buffer (or memory page): point the arrays at it once
        const int group = m_items[i].group;
        const int part = m_items[i].window;
        const Page &page = m_pages[static_cast<size_t>(m_items[i].page)];
        const char *base =
            page.buffer != 0 ? nullptr : reinterpret_cast<const char *>(page.memory.data());
        glBindBuffer(GL_ARRAY_BUFFER, page.buffer);
        bound = bound || page.buffer != 0;
        glVertexPointer(3, GL_FLOAT, stride, base + offsetof(PackedVertex, pos));
        glNormalPointer(GL_BYTE, stride, base + offsetof(PackedVertex, normal));
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, base + offsetof(PackedVertex, color));
        glClientActiveTexture(GL_TEXTURE1);
        glTexCoordPointer(4, GL_FLOAT, stride, base + offsetof(PackedVertex, extra));
        glClientActiveTexture(GL_TEXTURE0);
        glTexCoordPointer(2, GL_FLOAT, stride, base + offsetof(PackedVertex, uv));

        while (i < m_items.size() && m_items[i].group == group && m_items[i].window == part)
        {
            // every range with this texture, in one call
            const unsigned int texture = m_items[i].texture;
            m_first.clear();
            m_count.clear();
            for (; i < m_items.size() && m_items[i].group == group && m_items[i].texture == texture;
                 ++i)
            {
                m_first.push_back(m_items[i].first);
                m_count.push_back(m_items[i].count);
            }
            glBindTexture(GL_TEXTURE_2D, texture);
            if (m_first.size() == 1 || !GLAD_GL_VERSION_1_4)
                for (size_t k = 0; k < m_first.size(); ++k)
                    glDrawArrays(GL_QUADS, m_first[k], m_count[k]);
            else
                glMultiDrawArrays(GL_QUADS, m_first.data(), m_count.data(),
                                  static_cast<GLsizei>(m_first.size()));
        }
    }

    if (bound)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    glPopClientAttrib();

    // Drawing from arrays leaves these undefined; a display list left the
    // last vertex's, and what is drawn next may rely on that.
    const Vertex &v = last->last;
    glColor3fv(v.color);
    glMultiTexCoord4fv(GL_TEXTURE1, v.extra);
    glTexCoord2fv(v.uv);
    glNormal3fv(v.normal);
}

void Arena::release()
{
    std::vector<GLuint> names;
    for (const Page &page : m_pages)
        if (page.buffer != 0 && std::find(names.begin(), names.end(), page.buffer) == names.end())
            names.push_back(page.buffer); // merged pages share one
    if (!names.empty())
        glDeleteBuffers(static_cast<GLsizei>(names.size()), names.data());
    forget();
}

void Arena::forget()
{
    m_pages.clear();
    m_items.clear();
}

size_t Arena::bytes() const
{
    size_t count = 0;
    for (const Page &page : m_pages)
        count += static_cast<size_t>(page.used);
    return count * sizeof(PackedVertex);
}

size_t Arena::capacity() const
{
    size_t count = 0;
    for (const Page &page : m_pages)
        count += static_cast<size_t>(page.capacity);
    return count * sizeof(PackedVertex);
}

int Arena::buffers() const
{
    int count = 0;
    for (size_t i = 0; i < m_pages.size(); ++i)
        if (i == 0 || m_pages[i].group != m_pages[i - 1].group)
            ++count;
    return count;
}

} // namespace BlockMesh
