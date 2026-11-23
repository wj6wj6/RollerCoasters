/************************************************************************
     File:        BlockMesh.h

     Comment:     The block worlds' chunk meshes (LandWorld, OceanWorld)
                  kept in vertex buffers on the graphics card.

                  They used to be display lists, which kept every vertex
                  as five float attributes (60 bytes) in the program's
                  own memory. A Mesh packs each vertex into 44 bytes
                  (normal and colour as bytes) in one buffer per mesh,
                  with one range per texture, and draws it as the list
                  did: GL_QUADS, the textures bound by name (a texture
                  pack's pictures go into the same names), and afterwards
                  the current colour, normal and texture coordinates are
                  the last vertex's, as a display list leaves them.

                  Texture unit 1's coordinate carries the extra lighting
                  data the shader reads (see AdvancedLighting).
*************************************************************************/
#pragma once

#include <vector>

namespace BlockMesh
{

struct Vertex
{
    float pos[3];
    float normal[3];
    float uv[2];
    float color[3];
    float extra[4]; // glow, sway, crystal light, sky light
};

typedef std::vector<std::vector<Vertex>> Batches; // one batch per texture

// As stored: 44 bytes.
struct PackedVertex
{
    float pos[3];
    float uv[2];
    float extra[4];
    signed char normal[4]; // -127..127, the fourth unused
    unsigned char color[4];
};

struct Range
{
    unsigned int texture;
    int first;
    int count;
};

struct Mesh
{
    unsigned int buffer = 0;          // vertex buffer, 0 when there is none
    std::vector<PackedVertex> memory; // the vertices, only without vertex buffers
    std::vector<Range> ranges;        // one per texture used
    Vertex last = {};                 // the current values a draw leaves
};

// Pack the batches into the order the display list drew them; textures[t]
// is the name batch t is drawn with. No OpenGL.
Mesh pack(const Batches &batches, const std::vector<unsigned int> &textures);

// Pack and upload into a vertex buffer (needs the OpenGL context).
Mesh compile(const Batches &batches, const std::vector<unsigned int> &textures);

// Draw it. Leaves the texture of the last range bound.
void draw(const Mesh &mesh);

// Bytes the vertices take.
size_t bytes(const Mesh &mesh);

} // namespace BlockMesh
