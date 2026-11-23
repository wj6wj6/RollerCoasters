/************************************************************************
     File:        BlockMesh.cpp

     Comment:     See BlockMesh.h.
*************************************************************************/

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

#include <math.h>
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

Mesh compile(const Batches &batches, const std::vector<unsigned int> &textures)
{
    Mesh mesh = pack(batches, textures);
    if (mesh.memory.empty() || !GLAD_GL_VERSION_1_5)
        return mesh;
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.memory.size() * sizeof(PackedVertex)),
                 mesh.memory.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    mesh.buffer = buffer;
    std::vector<PackedVertex>().swap(mesh.memory); // on the card now
    return mesh;
}

void draw(const Mesh &mesh)
{
    if (mesh.ranges.empty())
        return;
    const char *base =
        mesh.buffer != 0 ? nullptr : reinterpret_cast<const char *>(mesh.memory.data());
    const GLsizei stride = static_cast<GLsizei>(sizeof(PackedVertex));

    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    if (mesh.buffer != 0)
        glBindBuffer(GL_ARRAY_BUFFER, mesh.buffer);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, stride, base + offsetof(PackedVertex, pos));
    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_BYTE, stride, base + offsetof(PackedVertex, normal));
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, base + offsetof(PackedVertex, color));
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(4, GL_FLOAT, stride, base + offsetof(PackedVertex, extra));
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, stride, base + offsetof(PackedVertex, uv));

    for (const Range &range : mesh.ranges)
    {
        glBindTexture(GL_TEXTURE_2D, range.texture);
        glDrawArrays(GL_QUADS, range.first, range.count);
    }

    if (mesh.buffer != 0)
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    glPopClientAttrib();

    // Drawing from arrays leaves these undefined; a display list left the
    // last vertex's, and what is drawn next may rely on that.
    const Vertex &v = mesh.last;
    glColor3fv(v.color);
    glMultiTexCoord4fv(GL_TEXTURE1, v.extra);
    glTexCoord2fv(v.uv);
    glNormal3fv(v.normal);
}

void release(Mesh &mesh)
{
    if (mesh.buffer != 0)
    {
        const GLuint buffer = mesh.buffer;
        glDeleteBuffers(1, &buffer);
    }
    mesh = Mesh();
}

size_t bytes(const Mesh &mesh)
{
    size_t count = 0;
    for (const Range &range : mesh.ranges)
        count += static_cast<size_t>(range.count);
    return count * sizeof(PackedVertex);
}

} // namespace BlockMesh
