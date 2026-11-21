/************************************************************************
     File:        GuiGl.cpp

     Comment:     See GuiGl.h.

*************************************************************************/

#include "GuiGl.h"
#include "Gui.h"
#include "UiAtlas.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

void ui::GuiRenderer::draw(const Gui &gui)
{
    if (m_texture == 0)
    {
        const std::vector<unsigned char> atlas = UiAtlas::build();
        GLuint name = 0;
        glGenTextures(1, &name);
        glBindTexture(GL_TEXTURE_2D, name);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, UiAtlas::kSize, UiAtlas::kSize, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, atlas.data());
        m_texture = name;
    }

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT |
                 GL_VIEWPORT_BIT | GL_TRANSFORM_BIT);
    glUseProgram(0);
    glViewport(0, 0, gui.pixelWidth(), gui.pixelHeight());
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, gui.pixelWidth(), gui.pixelHeight(), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBegin(GL_QUADS);
    for (const Quad &q : gui.quads())
    {
        glColor4ub(q.color.r, q.color.g, q.color.b, q.color.a);
        glTexCoord2f(q.u0, q.v0);
        glVertex2f(q.x0, q.y0);
        glTexCoord2f(q.u1, q.v0);
        glVertex2f(q.x1, q.y0);
        glTexCoord2f(q.u1, q.v1);
        glVertex2f(q.x1, q.y1);
        glTexCoord2f(q.u0, q.v1);
        glVertex2f(q.x0, q.y1);
    }
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}
