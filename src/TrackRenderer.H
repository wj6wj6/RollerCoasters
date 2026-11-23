/************************************************************************
     File:        TrackRenderer.H

     Comment:     Blocky pixel-art rendering of the rails and sleepers.

                  The look is inspired by voxel games: chunky wooden
                  sleepers and square iron rails, textured with small
                  16x16 textures that are generated procedurally at
                  start-up and sampled with nearest filtering. No
                  external image files are used.

                  Sleepers are placed at equal arc-length intervals using
                  a cumulative arc-length table, so their spacing stays
                  even no matter how the control points are spread.

                  The sleepers and rails are worked out once into a mesh
                  (TrackMesh) kept in a vertex buffer, and worked out again
                  only when a control point moves or the spline type
                  changes. Every pass of a frame (shadow maps, reflection,
                  the scene) draws the same buffer.
*************************************************************************/
#pragma once

#include <vector>

#include "Track.H"

// Build an orthonormal frame (side, up) for a direction of travel.
// The frame keeps "up" close to world +Y, so it is shared by the train,
// the train camera and the track.
void makeTrackFrame(const Pnt3f &forward, Pnt3f &side, Pnt3f &up);

// One entry of the cumulative arc-length table.
struct TrackArcSample
{
    float u;        // spline parameter
    float distance; // arc length from u = 0 to here
    Pnt3f position; // curve position at u
};

// Dense, closed-loop table that maps arc length back to the spline parameter.
class TrackArcTable
{
  public:
    void build(const CTrack &track, TrackSplineType splineType, int samplesPerSegment);

    float totalLength() const;
    // Spline parameter at the given arc length; wraps around the loop.
    float parameterAtDistance(float distance) const;
    // samples().front() is u = 0 and samples().back() is the end of the loop.
    const std::vector<TrackArcSample> &samples() const
    {
        return m_samples;
    }

  private:
    std::vector<TrackArcSample> m_samples;
};

// One corner of the track mesh, in world coordinates.
struct TrackVertex
{
    float position[3];
    float normal[3];
    float texCoord[2];
};

// The sleepers and rails of a track as ready-to-draw vertices: first the
// sleepers (GL_QUADS, wood), then eight quad strips of equal length for the
// four faces of the two rails (iron).
class TrackMesh
{
  public:
    // Work the mesh out again if the control points or the spline type have
    // changed since the last call; true when it did.
    bool update(const CTrack &track, TrackSplineType splineType);

    const std::vector<TrackVertex> &vertices() const
    {
        return m_vertices;
    }
    int sleeperVertexCount() const
    {
        return m_sleeperVertices;
    }
    // Vertices in each of the eight rail strips.
    int railStripVertexCount() const
    {
        return m_railStripVertices;
    }
    static const int kRailStrips = 8;

  private:
    void build(const CTrack &track, TrackSplineType splineType);

    TrackArcTable m_arcTable;
    std::vector<TrackVertex> m_vertices;
    int m_sleeperVertices = 0;
    int m_railStripVertices = 0;
    // what the mesh was worked out from
    bool m_built = false;
    std::vector<Pnt3f> m_points;
    TrackSplineType m_splineType = TRACK_SPLINE_LINEAR;
};

class TrackRenderer
{
  public:
    TrackRenderer();

    // Forget the texture and buffer names. Call this when the GL context was
    // recreated, because the old ones no longer exist in the new context.
    void releaseResources();

    // The pictures for the sleepers and the rails: RGBA, square, a power of
    // two on a side (a texture pack's planks and iron), or empty for the
    // built-in ones. Takes effect at the next draw().
    void setPictures(std::vector<unsigned char> wood, std::vector<unsigned char> iron);

    // Draw sleepers and rails. With doingShadows the caller has already set
    // the shadow colour, so nothing is coloured or textured here.
    void draw(const CTrack &track, TrackSplineType splineType, bool doingShadows);

  private:
    void ensureTextures();
    // Bind the vertex arrays to the mesh (in its buffer when the card has
    // buffers); false when there is nothing to draw.
    bool bindMesh();

    TrackMesh m_mesh;
    unsigned int m_buffer = 0; // vertex buffer holding m_mesh
    bool m_bufferStale = true; // m_mesh changed since it was uploaded
    unsigned int m_woodTexture;
    unsigned int m_ironTexture;
    std::vector<unsigned char> m_woodPicture, m_ironPicture; // empty: built in
    bool m_picturesChanged = false;
};
