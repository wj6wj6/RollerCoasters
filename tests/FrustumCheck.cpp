// Checks the view volume test the worlds use to skip chunks outside the view (Frustum.h): a
// box in front of the camera is seen, boxes behind it, beyond the far plane or off to a side
// are not, and a box the frustum's edge passes through still counts as seen.
#include "Frustum.h"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

// gluPerspective's matrix, column major.
void perspective(float fovY, float aspect, float zNear, float zFar, float m[16])
{
    const float f = 1.0f / std::tan(fovY * 3.14159265f / 360.0f);
    for (int i = 0; i < 16; ++i)
        m[i] = 0.0f;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.0f;
    m[14] = 2.0f * zFar * zNear / (zNear - zFar);
}

// glOrtho's matrix, column major.
void ortho(float l, float r, float b, float t, float n, float f, float m[16])
{
    for (int i = 0; i < 16; ++i)
        m[i] = 0.0f;
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

bool sees(const Frustum &f, float x, float y, float z, float half)
{
    const float lo[3] = {x - half, y - half, z - half};
    const float hi[3] = {x + half, y + half, z + half};
    return f.sees(lo, hi);
}
} // namespace

int main()
{
    {
        // The camera at the origin looking down -z, 60 degrees, square, 1 to 100.
        float m[16];
        perspective(60.0f, 1.0f, 1.0f, 100.0f, m);
        const Frustum f(m);
        expect(sees(f, 0, 0, -20, 2), "a box ahead is seen");
        expect(!sees(f, 0, 0, 20, 2), "a box behind is not");
        expect(!sees(f, 0, 0, -130, 2), "a box beyond the far plane is not");
        expect(!sees(f, 40, 0, -20, 2), "a box far off to the right is not");
        expect(!sees(f, 0, -40, -20, 2), "a box far below is not");
        // at z = -20 the right edge is at x = 20 tan(30) = 11.5
        expect(sees(f, 12.5f, 0, -20, 2), "a box the right edge passes through is seen");
        expect(!sees(f, 16.0f, 0, -20, 2), "a box just outside the right edge is not");
        expect(sees(f, 0, 0, 0, 200), "a box round the camera is seen");
    }
    {
        // The shadow pass: an orthographic box 50 wide, its view moved 100 along +x.
        float p[16], view[16], m[16];
        ortho(-25, 25, -25, 25, 1, 200, p);
        for (int i = 0; i < 16; ++i)
            view[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        view[12] = -100.0f; // the world moved left: the view looks at x = 100
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += p[k * 4 + r] * view[c * 4 + k];
                m[c * 4 + r] = sum;
            }
        const Frustum f(m);
        expect(sees(f, 100, 0, -50, 5), "orthographic: a box in the moved view is seen");
        expect(!sees(f, 0, 0, -50, 5), "orthographic: a box where the view was is not");
        expect(sees(f, 128, 0, -50, 5), "orthographic: a box across its edge is seen");
    }
    std::printf("%s\n", failures == 0 ? "FrustumCheck passed" : "FrustumCheck FAILED");
    return failures == 0 ? 0 : 1;
}
