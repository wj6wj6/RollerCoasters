// Checks how BlockMesh::pack() lays out a chunk's batches for its vertex
// buffer: the batches in texture order as the display lists drew them, one
// range per non-empty batch with its texture name, positions, texture
// coordinates and the lighting data unchanged, normals and colours as bytes
// that give the same values back, and the last vertex kept for the current
// values a draw leaves. No OpenGL.
#include "BlockMesh.h"

#include <cmath>
#include <cstdio>

static BlockMesh::Vertex vertex(float x, float nx, float ny, float nz, float shade, float glow)
{
    BlockMesh::Vertex v = {};
    v.pos[0] = x;
    v.pos[1] = -40.0f + x;
    v.pos[2] = 3500.0f - x;
    v.normal[0] = nx;
    v.normal[1] = ny;
    v.normal[2] = nz;
    v.uv[0] = -700.0f + x;
    v.uv[1] = 0.25f * x;
    v.color[0] = shade;
    v.color[1] = shade;
    v.color[2] = 1.0f;
    v.extra[0] = glow;
    v.extra[1] = 1.1f;
    v.extra[2] = 1.6129f;
    v.extra[3] = 0.015833f;
    return v;
}

int main()
{
    bool ok = true;
    auto expect = [&](bool condition, const char *what) {
        if (!condition)
        {
            std::printf("%s FAILED\n", what);
            ok = false;
        }
    };

    expect(sizeof(BlockMesh::PackedVertex) == 44, "44 bytes a vertex");

    BlockMesh::Batches batches(5);
    batches[1] = {vertex(1, 1, 0, 0, 0.85f, -2.5f), vertex(2, -1, 0, 0, 0.9f, 0.0f),
                  vertex(3, 0, 1, 0, 0.95f, 1.3f), vertex(4, 0, -1, 0, 1.0f, -1.0f)};
    batches[3] = {vertex(5, 0, 0, 1, 0.85f, 0.5f), vertex(6, 0, 0, -1, 0.9f, 0.0f),
                  vertex(7, 0.7071f, 0, 0.7071f, 1.0f, 0.0f), vertex(8, 0, 1, 0, 0.9f, 0.2f)};
    const std::vector<unsigned int> textures = {10, 11, 12, 13, 14};

    const BlockMesh::Mesh mesh = BlockMesh::pack(batches, textures);
    expect(mesh.buffer == 0, "pack() makes no buffer");
    expect(mesh.ranges.size() == 2, "one range per non-empty batch");
    expect(mesh.ranges.size() == 2 && mesh.ranges[0].texture == 11 && mesh.ranges[0].first == 0 &&
               mesh.ranges[0].count == 4,
           "first range: texture 1's name, vertices 0..3");
    expect(mesh.ranges.size() == 2 && mesh.ranges[1].texture == 13 && mesh.ranges[1].first == 4 &&
               mesh.ranges[1].count == 4,
           "second range: texture 3's name, vertices 4..7");
    expect(mesh.memory.size() == 8, "all vertices kept");
    expect(BlockMesh::bytes(mesh) == 8 * 44, "bytes()");

    // the vertices in the same order, values as the display list had them
    size_t i = 0;
    for (const std::vector<BlockMesh::Vertex> &batch : batches)
        for (const BlockMesh::Vertex &v : batch)
        {
            const BlockMesh::PackedVertex &p = mesh.memory[i++];
            bool same = true;
            for (int k = 0; k < 3; ++k)
                same = same && p.pos[k] == v.pos[k];
            for (int k = 0; k < 2; ++k)
                same = same && p.uv[k] == v.uv[k];
            for (int k = 0; k < 4; ++k)
                same = same && p.extra[k] == v.extra[k];
            // signed bytes read back as b / 127 (OpenGL 4.2 and later)
            for (int k = 0; k < 3; ++k)
                same = same && std::fabs(static_cast<float>(p.normal[k]) / 127.0f - v.normal[k]) <
                                   0.5f / 127.0f + 1e-6f;
            for (int k = 0; k < 3; ++k)
                same = same && std::fabs(static_cast<float>(p.color[k]) / 255.0f - v.color[k]) <
                                   0.5f / 255.0f + 1e-6f;
            same = same && p.color[3] == 255;
            if (!same)
            {
                std::printf("vertex %d differs\n", static_cast<int>(i - 1));
                ok = false;
            }
        }
    // block faces face along an axis: those normals are exact
    expect(mesh.memory[0].normal[0] == 127 && mesh.memory[1].normal[0] == -127 &&
               mesh.memory[2].normal[1] == 127,
           "axis normals exact");
    expect(mesh.last.pos[0] == 8.0f && mesh.last.extra[0] == 0.2f, "last vertex kept");

    const BlockMesh::Mesh empty = BlockMesh::pack(BlockMesh::Batches(3), textures);
    expect(empty.ranges.empty() && empty.memory.empty(), "empty batches: nothing to draw");

    std::printf(ok ? "BlockMeshCheck passed\n" : "BlockMeshCheck FAILED\n");
    return ok ? 0 : 1;
}
