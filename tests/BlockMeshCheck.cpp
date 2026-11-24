// Checks how BlockMesh::pack() lays out a chunk's batches for its vertex
// buffer: the batches in texture order as the display lists drew them, one
// range per non-empty batch with its texture name, positions, texture
// coordinates and the lighting data unchanged, normals and colours as bytes
// that give the same values back, and the last vertex kept for the current
// values a draw leaves. Then how an Arena (in memory, as without OpenGL)
// places meshes in pages and which calls a pass makes: one per page and
// texture, the textures in order of their names, the ranges of one texture
// in the order of the meshes. No OpenGL.
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
    expect(mesh.page == -1, "pack() puts it in no arena");
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

    // An arena of 10-vertex pages, in memory (no OpenGL here).
    BlockMesh::Arena arena(10);
    const BlockMesh::Mesh a = arena.add(BlockMesh::pack(batches, textures)); // 8 vertices
    const BlockMesh::Mesh none = arena.add(BlockMesh::pack(BlockMesh::Batches(3), textures));
    BlockMesh::Batches two(5);
    two[3] = {vertex(9, 0, 1, 0, 1.0f, 0.0f), vertex(10, 0, 1, 0, 1.0f, 0.0f)};
    const BlockMesh::Mesh b = arena.add(BlockMesh::pack(two, textures)); // fits after a
    BlockMesh::Batches big(5);
    for (int k = 0; k < 12; ++k)
        big[k % 2 == 0 ? 0 : 3].push_back(
            vertex(20.0f + static_cast<float>(k), 0, 1, 0, 1.0f, 0.0f));
    const BlockMesh::Mesh c = arena.add(BlockMesh::pack(big, textures)); // a page of its own
    expect(a.page == 0 && b.page == 0 && c.page == 1,
           "pages filled in turn, a large mesh gets its own");
    expect(none.page == -1 && none.ranges.empty(), "an empty mesh takes no room");
    expect(a.memory.empty() && b.memory.empty(), "the arena took the vertices");
    expect(b.ranges.size() == 1 && b.ranges[0].first == 8 && b.ranges[0].count == 2,
           "ranges count from the page's start");
    expect(c.ranges.size() == 2 && c.ranges[0].first == 0 && c.ranges[1].first == 6,
           "a new page starts at 0");
    expect(arena.bytes() == 22 * 44 && arena.capacity() == 22 * 44 && arena.buffers() == 2,
           "bytes(), capacity(), buffers()");
    arena.compact(); // needs OpenGL 3.1: leaves it as it is here
    expect(arena.buffers() == 2, "compact() without OpenGL changes nothing");

    const std::vector<BlockMesh::Arena::Call> calls = arena.plan({&c, &none, &a, &b});
    // page 0: texture 11 (a), texture 13 (a then b); page 1: texture 10, 13 (c)
    expect(calls.size() == 4, "one call per page and texture");
    if (calls.size() == 4)
    {
        expect(calls[0].buffer == 0 && calls[0].texture == 11 &&
                   calls[0].first == std::vector<int>{0} && calls[0].count == std::vector<int>{4},
               "page 0, texture 11");
        expect(calls[1].buffer == 0 && calls[1].texture == 13 &&
                   calls[1].first == std::vector<int>({4, 8}) &&
                   calls[1].count == std::vector<int>({4, 2}),
               "page 0, texture 13: a's range, then b's");
        expect(calls[2].buffer == 1 && calls[2].texture == 10 &&
                   calls[2].first == std::vector<int>{0} && calls[2].count == std::vector<int>{6},
               "page 1, texture 10");
        expect(calls[3].buffer == 1 && calls[3].texture == 13 &&
                   calls[3].first == std::vector<int>{6} && calls[3].count == std::vector<int>{6},
               "page 1, texture 13");
    }
    expect(arena.plan({&none}).empty(), "nothing to draw: no calls");

    // windows of one mesh: each mesh's textures on their own, in mesh order
    const std::vector<BlockMesh::Arena::Call> one = arena.plan({&b, &a}, 1);
    expect(one.size() == 3 && one[0].texture == 13 && one[0].first == std::vector<int>{8} &&
               one[1].texture == 11 && one[2].texture == 13 && one[2].first == std::vector<int>{4},
           "window 1: b, then a's two textures");
    // a window of two takes both together again
    expect(arena.plan({&b, &a}, 2).size() == 2, "window 2: one call per texture");

    // which window for which card (ADR 0018)
    expect(BlockMesh::windowFor("NVIDIA Corporation") == 0 &&
               BlockMesh::windowFor("ATI Technologies Inc.") == 1 &&
               BlockMesh::windowFor("Intel") == 1 && BlockMesh::windowFor(nullptr) == 1,
           "windowFor(): NVIDIA all at once, others one chunk at a time");
    arena.forget();
    expect(arena.bytes() == 0 && arena.buffers() == 0, "forget() empties it");

    std::printf(ok ? "BlockMeshCheck passed\n" : "BlockMeshCheck FAILED\n");
    return ok ? 0 : 1;
}
