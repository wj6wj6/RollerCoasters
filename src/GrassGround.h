/************************************************************************
     File:        GrassGround.h

     Comment:     Blocky grass ground: a square island of grass blocks with
                  dirt sides. The 16x16 pixel textures are generated in
                  code, so there are no image files to load.

*************************************************************************/
#pragma once

class GrassGround
{
  public:
    GrassGround();

    // Call when the OpenGL context was recreated: the texture names are gone.
    void releaseTextures();

    // Draws a size x size island centred on the origin, with its top at y = 0,
    // cells x cells grass blocks and dirt sides down to y = bottom.
    void draw(float size, int cells, float bottom);

  private:
    void ensureTextures();

    unsigned int m_grassTop;
    unsigned int m_grassSide;
};
