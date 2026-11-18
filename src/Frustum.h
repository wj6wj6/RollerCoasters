/************************************************************************
     File:        Frustum.h

     Comment:     The six planes of a view volume, taken from a
                  projection times modelview matrix, and a test whether a
                  box can be seen. The worlds (LandWorld, OceanWorld) split
                  their meshes into chunks and draw only the chunks inside
                  the current view: the camera's, the light's in the shadow
                  pass, or the mirrored one of the water's reflection.
                  No OpenGL here, so it can be tested on its own
                  (tests/FrustumCheck.cpp).

*************************************************************************/
#pragma once

class Frustum
{
  public:
    // m: projection * modelview, column major as OpenGL stores it.
    explicit Frustum(const float m[16]);

    // The view of the current OpenGL matrices.
    static Frustum current();

    // Could any part of the box lo..hi be inside the view?
    bool sees(const float lo[3], const float hi[3]) const;

  private:
    float m_plane[6][4]; // a x + b y + c z + d >= 0 inside
};
