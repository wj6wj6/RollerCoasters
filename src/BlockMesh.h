/************************************************************************
     File:        BlockMesh.h

     Comment:     The block worlds' chunk meshes (LandWorld, OceanWorld)
                  kept in vertex buffers on the graphics card.

                  They used to be display lists, which kept every vertex
                  as five float attributes (60 bytes) in the program's
                  own memory. A Mesh packs each vertex into 44 bytes
                  (normal and colour as bytes), with one range per
                  texture, and is drawn as the list was: GL_QUADS, the
                  textures bound by name (a texture pack's pictures go
                  into the same names), and afterwards the current
                  colour, normal and texture coordinates are the last
                  vertex's, as a display list leaves them.

                  All the meshes of one world live in one Arena: a few
                  large vertex buffers (pages) that are merged into one
                  once the world is built. A pass hands the Arena every
                  mesh it wants drawn, and the Arena draws them all with
                  one glMultiDrawArrays per texture, instead of binding a
                  buffer, setting the vertex arrays and drawing each
                  texture for every chunk on its own.

                  Texture unit 1's coordinate carries the extra lighting
                  data the shader reads (see AdvancedLighting).
*************************************************************************/
#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
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
    int page = -1;                    // the Arena page that holds it; -1: none
    std::vector<PackedVertex> memory; // pack()'s vertices, until an Arena takes them
    std::vector<Range> ranges;        // one per texture used; first counts from the
                                      // page's start (pack(): from memory's)
    Vertex last = {};                 // the current values a draw leaves
};

// Pack the batches into the order the display list drew them; textures[t]
// is the name batch t is drawn with. No OpenGL.
Mesh pack(const Batches &batches, const std::vector<unsigned int> &textures);

// Bytes the vertices take.
size_t bytes(const Mesh &mesh);

// How far in front of the eye the middle of a box is, along the view
// direction of the given modelview matrix (column-major, as OpenGL gives
// it): the camera's, or the light's in the shadow pass. For drawing the
// nearest chunks first, so the depth test throws away more of what is
// behind them before it is shaded.
float viewDepth(const float modelview[16], const float lo[3], const float hi[3]);

// EXPERIMENT, to be removed: a whole number from the environment, for
// trying draw orders on the laptop without rebuilding.
int knob(const char *name, int fallback);
int drawWindow();    // RC_ARENA_WINDOW, meshes per window of Arena::draw()
bool nearestFirst(); // RC_ARENA_NEAR, sort the chunks nearest first

// The chunks (anything with lo[3] and hi[3]) that sees(lo, hi) keeps, as
// indices into chunks, nearest first for the given modelview matrix.
template <class Chunk, class Sees>
void visibleNearestFirst(const std::vector<Chunk> &chunks, Sees sees, const float modelview[16],
                         std::vector<std::pair<float, int>> &order)
{
    order.clear();
    for (size_t i = 0; i < chunks.size(); ++i)
        if (sees(chunks[i].lo, chunks[i].hi))
            order.emplace_back(viewDepth(modelview, chunks[i].lo, chunks[i].hi),
                               static_cast<int>(i));
    if (nearestFirst())
        std::sort(order.begin(), order.end());
}

class Arena
{
  public:
    // Vertices in one page (32 MiB) unless a mesh needs more.
    static const int kPageVertices = (32 << 20) / static_cast<int>(sizeof(PackedVertex));

    explicit Arena(int pageVertices = kPageVertices);
    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;
    // Deletes no OpenGL names (there may be no context any more): call
    // release() first where there is one.
    ~Arena() = default;

    // Moves a packed mesh's vertices into the arena: into the last page if
    // they fit, else into a new one. With OpenGL 1.5 a page is a vertex
    // buffer (needs the context); without, it is kept in memory.
    Mesh add(Mesh packed);
    Mesh compile(const Batches &batches, const std::vector<unsigned int> &textures)
    {
        return add(pack(batches, textures));
    }

    // Once nothing more is coming: copies the pages into one vertex buffer
    // just large enough (OpenGL 3.1), so a pass binds one buffer and the
    // last page's spare room is given back. The meshes stay valid.
    void compact();

    // Draws the meshes (of this arena; empty ones are skipped) in one go.
    // They are taken window meshes at a time (0: all at once), in the order
    // given; within a window, for each buffer the vertex arrays are set once
    // and each texture is bound once and drawn with one glMultiDrawArrays,
    // the textures in the order of their names and each texture's ranges in
    // the order of the meshes. Callers hand the meshes nearest first and a
    // small window: sorting the whole world by texture scatters the draws
    // across the screen, which costs a GPU with little bandwidth (Radeon
    // 860M) more than the calls it saves. Leaves the last texture bound and
    // the current values as the last non-empty mesh's last vertex.
    void draw(const std::vector<const Mesh *> &meshes, int window = 0) const;

    // Deletes the vertex buffers (in the current context) and empties it.
    // Meshes added before are no longer valid.
    void release();
    // The same when the context is already gone: the names are dropped
    // without deleting them, since in a new context they may be something
    // else's.
    void forget();

    size_t bytes() const;    // vertices held, in bytes
    size_t capacity() const; // bytes the pages take
    int buffers() const;     // vertex buffers (or memory pages) behind it

    // What draw() does, without OpenGL, for tests: one call per buffer and
    // texture.
    struct Call
    {
        int buffer; // which of buffers()
        unsigned int texture;
        std::vector<int> first, count;
    };
    std::vector<Call> plan(const std::vector<const Mesh *> &meshes, int window = 0) const;

  private:
    struct Page
    {
        unsigned int buffer = 0;          // vertex buffer, 0 when in memory
        std::vector<PackedVertex> memory; // the vertices, only without buffers
        int base = 0;                     // where the page starts in its buffer
        int used = 0;
        int capacity = 0;
        int group = 0; // pages with the same group share a buffer
    };
    std::vector<Page> m_pages;
    int m_pageVertices;

    struct Item
    {
        int window; // which window of meshes
        int group;
        int page; // one of the group's pages (they share the buffer)
        unsigned int texture;
        int first, count;
    };
    // filled by gather(): every range to draw, by group, then texture
    mutable std::vector<Item> m_items;
    mutable std::vector<int> m_first;
    mutable std::vector<int> m_count;
    const Mesh *gather(const std::vector<const Mesh *> &meshes, int window) const;
};

} // namespace BlockMesh
