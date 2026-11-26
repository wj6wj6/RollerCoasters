/************************************************************************
     File:        WaterWaves.h

     Comment:     How the sea's surface moves, apart from drawing it
                  (WaterSurface.* runs the same maths on the graphics
                  card). Three models, picked in World > Waves:

                  * Gerstner: the eight Gerstner (sine) waves PostEffects
                    has always drawn.
                  * Height Map: the course's height map sequence (200
                    pictures of a patch of sea, looping), as a 3D texture
                    b(u, v, w) that repeats in all three directions, laid
                    over the sea in three layers of different size,
                    direction and speed and added up:
                      H(x, z, t) = sum_i a_i (b(A_i (x, z) + B_i, At_i t + Bt_i) - mean).
                  * Wave Equation: d2h/dt2 = c^2 (d2h/dx2 + d2h/dz2),
                    solved on a grid with the five-point Laplacian and
                    Verlet steps, h(t+dt) = h + damping (h - h(t-dt)) +
                    k (sum of the four neighbours - 4 h), k = (c dt / dx)^2.
                    Stones dropped with the mouse or a key, the train
                    going into and out of the sea and the rain disturb it.

                  The grid follows the camera, moved in whole cells so a
                  ripple stays where it is in the world.

*************************************************************************/
#pragma once

#include <string>
#include <vector>

enum class WaveModel
{
    Gerstner = 0,
    HeightMap = 1,
    Equation = 2
};

namespace WaterWaves
{

constexpr int kModels = 3;

// "Gerstner", "Height Map", "Wave Equation"
const char *name(WaveModel model);
// the next model, round to the first after the last
WaveModel next(WaveModel model);
// the model numbered 0, 1 or 2; Gerstner for anything else
WaveModel fromIndex(int index);

//************************************************************************
// Height maps
//========================================================================

// The frames of a height map sequence: `count` square pictures `size` texels
// a side, grey (0 low, 255 high), one after the other.
struct HeightFrames
{
    int size = 0;
    int count = 0;
    std::vector<unsigned char> texels;
    // the mean of every texel, 0 to 1 (the still water's height)
    float mean = 0.5f;

    bool empty() const
    {
        return count == 0;
    }
    // texel (x, z) of a frame, 0 to 1; x, z and the frame wrap round
    float at(int frame, int x, int z) const;
    // trilinear, wrapping in all three directions as the GL_REPEAT 3D
    // texture does: u, v across a frame (1 = once across), w through
    // the sequence (1 = once through)
    float sample(float u, float v, float w) const;
};

// Cuts the frames out of a grey atlas `columns` frames wide, read row by row,
// the first `count` of them. False when the picture is not that shape.
bool fromAtlas(const unsigned char *grey, int width, int height, int columns, int count,
               HeightFrames &out);

// The course's sequence as it comes, 000.png, 001.png, ... in `folder`, all
// the same square size; false (with `error`) when there is no 000.png or a
// frame is unreadable.
bool loadSequence(const std::string &folder, HeightFrames &out, std::string &error);

// The atlas shipped in assets/heightmaps/ (made from the course's sequence).
bool loadAtlas(const std::string &path, int columns, int count, HeightFrames &out,
               std::string &error);

// A small stand-in sequence (moving bumps), so the model still works when
// neither is there.
HeightFrames fallbackFrames();

// One layer: b at A (x, z) + B and At t + Bt, times `amplitude`. A turns
// (x, z) by `angle` (radians) and shrinks it so one copy of the picture
// covers `tile` world units; the whole sequence plays in `loopSeconds`.
struct HeightLayer
{
    float tile;
    float angle;
    float offsetU;
    float offsetV;
    float loopSeconds;
    float phase; // where in the sequence it starts, 0 to 1
    float amplitude;
};
constexpr int kHeightLayers = 3;
const HeightLayer &heightLayer(int index);

// The layer's A as a 2 x 2 matrix (row major: u = a[0] x + a[1] z,
// v = a[2] x + a[3] z).
void layerMatrix(const HeightLayer &layer, float a[4]);

// Height of the surface above the still water at (x, z), t seconds in.
float heightAt(const HeightFrames &frames, float x, float z, float t);

//************************************************************************
// Wave equation
//========================================================================

constexpr int kGridCells = 1024;  // cells along each side
constexpr float kGridCell = 0.5f; // world units a cell
constexpr float kStepSeconds = 1.0f / 60.0f;
constexpr float kWaveSpeed = 7.0f;  // c, world units a second
constexpr float kDamping = 0.996f;  // of the velocity, each step
constexpr int kSpongeCells = 48;    // the edge that soaks waves up
constexpr int kMostStepsAFrame = 4; // a slow frame slows the water down

// k = (c dt / dx)^2; the steps are stable while it is below 0.5.
float courantSquared();

// A disturbance: the surface at (x, z) moved by `strength` world units at
// the middle, falling off as a cosine bump to nothing at `radius`.
struct Drop
{
    float x;
    float z;
    float radius;
    float strength;
};

// The grid: heights now and one step before, kept as the graphics card
// keeps them (row after row, `cells` a side). The checks run it on the
// processor to see the same numbers the shader works with.
class WaveGrid
{
  public:
    explicit WaveGrid(int cells = kGridCells);

    int cells() const
    {
        return m_cells;
    }
    float height(int x, int z) const;
    // world position of the corner of cell (0, 0)
    float originX() const
    {
        return m_originX;
    }
    float originZ() const
    {
        return m_originZ;
    }

    // Moves the grid so (x, z) is in its middle, snapped to whole cells.
    // Gives how many cells everything moved (what was at cell i is now at
    // i - shiftX); a jump further than the grid clears it.
    void follow(float x, float z, int &shiftX, int &shiftZ);
    // as follow(), but only works out where the grid would be
    static void snap(float x, float z, int cells, float &originX, float &originZ);

    void addDrop(const Drop &drop);
    void step(float k = courantSquared(), float damping = kDamping);
    void clear();
    // sum of h^2 over the grid (how much is still moving)
    double energy() const;

  private:
    void shift(int dx, int dz);
    int m_cells;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    bool m_placed = false;
    std::vector<float> m_now;
    std::vector<float> m_before;
};

// Where a drop lands in a grid whose corner is at (originX, originZ):
// centre and radius in cells.
void dropInCells(const Drop &drop, float originX, float originZ, float &cx, float &cz,
                 float &radius);

// The train going through the surface. Each vehicle that is touching the
// water pushes it aside as it goes (a wake); going in or out makes a splash.
class TrainSplash
{
  public:
    // positions: x, y, z of each vehicle (at its wheels); the step's
    // seconds. Appends what it does to `drops`.
    void update(const std::vector<float> &positions, float waterLevel, float seconds,
                std::vector<Drop> &drops);
    void reset()
    {
        m_last.clear();
    }

  private:
    std::vector<float> m_last;
};

// Rain landing on the grid: about `rain` (0 to 1) times 60 drops a second
// within `reach` of (x, z). `seed` is the random state, kept by the caller.
void rainDrops(float rain, float seconds, float x, float z, float reach, unsigned &seed,
               std::vector<Drop> &drops);

// Where a line of sight (origin, direction) meets the still water at
// `level`; false when it never does (level or going away from it).
bool hitWater(const double origin[3], const double direction[3], float level, float &x, float &z);

// A stone dropped at (x, z).
Drop stone(float x, float z);

} // namespace WaterWaves
