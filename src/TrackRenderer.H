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

class TrackRenderer
{
  public:
    TrackRenderer();

    // Forget the texture names. Call this when the GL context was recreated,
    // because the old textures no longer exist in the new context.
    void releaseTextures();

    // The pictures for the sleepers and the rails: RGBA, square, a power of
    // two on a side (a texture pack's planks and iron), or empty for the
    // built-in ones. Takes effect at the next draw().
    void setPictures(std::vector<unsigned char> wood, std::vector<unsigned char> iron);

    // Draw sleepers and rails. With doingShadows the caller has already set
    // the shadow colour, so nothing is coloured or textured here.
    void draw(const CTrack &track, TrackSplineType splineType, bool doingShadows);

  private:
    void ensureTextures();
    void drawSleepers(const CTrack &track, TrackSplineType splineType, bool doingShadows);
    void drawRails(const CTrack &track, TrackSplineType splineType, bool doingShadows);

    TrackArcTable m_arcTable;
    unsigned int m_woodTexture;
    unsigned int m_ironTexture;
    std::vector<unsigned char> m_woodPicture, m_ironPicture; // empty: built in
    bool m_picturesChanged = false;
};
