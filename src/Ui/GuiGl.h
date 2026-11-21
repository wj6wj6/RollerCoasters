/************************************************************************
     File:        GuiGl.h

     Comment:     Draws a Gui frame's quads with OpenGL: uploads the
                  UiAtlas once (nearest filtering, so the pixels stay
                  sharp at any GUI scale) and draws over whatever is on
                  screen in a pixel projection with y going down.

                  Call with the GL context current. The texture belongs to
                  that context: after the context is made again, call
                  forget() so the next draw uploads a new one.

*************************************************************************/
#pragma once

namespace ui
{
class Gui;

class GuiRenderer
{
  public:
    void draw(const Gui &gui);
    // The context that held the texture is gone (or about to go).
    void forget()
    {
        m_texture = 0;
    }

  private:
    unsigned int m_texture = 0;
};
} // namespace ui
