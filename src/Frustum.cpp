/************************************************************************
     File:        Frustum.cpp

     Comment:     See Frustum.h. The planes are the sums and differences
                  of the matrix rows (Gribb and Hartmann's method).

*************************************************************************/

#include "Frustum.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

Frustum::Frustum(const float m[16])
{
    // row i of the matrix
    auto row = [&](int i, int k) { return m[k * 4 + i]; };
    for (int p = 0; p < 6; ++p)
    {
        const int axis = p / 2;                  // x, y, z
        const float sign = p % 2 ? -1.0f : 1.0f; // left / right, ...
        for (int k = 0; k < 4; ++k)
            m_plane[p][k] = row(3, k) + sign * row(axis, k);
    }
}

Frustum Frustum::current()
{
    float proj[16], view[16], m[16];
    glGetFloatv(GL_PROJECTION_MATRIX, proj);
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += proj[k * 4 + r] * view[c * 4 + k];
            m[c * 4 + r] = sum;
        }
    return Frustum(m);
}

bool Frustum::sees(const float lo[3], const float hi[3]) const
{
    for (const float *p : m_plane)
    {
        // the corner of the box furthest along the plane's normal
        const float x = p[0] >= 0.0f ? hi[0] : lo[0];
        const float y = p[1] >= 0.0f ? hi[1] : lo[1];
        const float z = p[2] >= 0.0f ? hi[2] : lo[2];
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f)
            return false;
    }
    return true;
}
