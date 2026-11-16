// Checks the control point move handles: that the handle under the mouse is found, and that
// dragging a handle moves the point exactly under the mouse and only along what the handle allows.
#include "TransformGizmo.h"

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

// A camera at `eye` looking at the origin, y up, 60 degree field of view, 800 x 600 pixels.
TransformGizmo::Camera lookAtOrigin(double ex, double ey, double ez)
{
    TransformGizmo::Camera camera = {};
    const double length = std::sqrt(ex * ex + ey * ey + ez * ez);
    const double f[3] = {-ex / length, -ey / length, -ez / length};
    double s[3] = {f[1] * 0.0 - f[2] * 1.0, f[2] * 0.0 - f[0] * 0.0, f[0] * 1.0 - f[1] * 0.0};
    const double sLength = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (double &v : s)
        v /= sLength;
    const double u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2],
                         s[0] * f[1] - s[1] * f[0]};
    double *m = camera.view;
    m[0] = s[0], m[4] = s[1], m[8] = s[2], m[12] = -(s[0] * ex + s[1] * ey + s[2] * ez);
    m[1] = u[0], m[5] = u[1], m[9] = u[2], m[13] = -(u[0] * ex + u[1] * ey + u[2] * ez);
    m[2] = -f[0], m[6] = -f[1], m[10] = -f[2], m[14] = f[0] * ex + f[1] * ey + f[2] * ez;
    m[15] = 1.0;

    const double t = 1.0 / std::tan(30.0 * 3.14159265358979 / 180.0);
    const double zNear = 0.5, zFar = 1000.0;
    double *p = camera.projection;
    p[0] = t / (800.0 / 600.0);
    p[5] = t;
    p[10] = (zFar + zNear) / (zNear - zFar);
    p[11] = -1.0;
    p[14] = 2.0 * zFar * zNear / (zNear - zFar);
    camera.viewport[2] = 800;
    camera.viewport[3] = 600;
    return camera;
}

int pixel(float v)
{
    return static_cast<int>(std::floor(v + 0.5f));
}
} // namespace

int main()
{
    using namespace TransformGizmo;
    const Camera camera = lookAtOrigin(60.0, 45.0, 80.0);
    const float point[3] = {4.0f, 2.0f, -3.0f};
    const float size = worldSize(camera, point, 90.0f);

    // The arrows are 90 pixels long on screen wherever the point is.
    float cx = 0.0f, cy = 0.0f, tx = 0.0f, ty = 0.0f;
    const float sideways[3] = {point[0] + static_cast<float>(camera.view[0]) * size,
                               point[1] + static_cast<float>(camera.view[4]) * size,
                               point[2] + static_cast<float>(camera.view[8]) * size};
    project(camera, point, cx, cy);
    project(camera, sideways, tx, ty);
    expect(std::fabs(std::hypot(tx - cx, ty - cy) - 90.0f) < 1.0f, "gizmo covers 90 pixels");

    // Each handle is found under the mouse at its own place, and nothing far away.
    expect(pick(camera, point, size, pixel(cx), pixel(cy)) == VIEW, "centre picks the view handle");
    const Handle axes[3] = {AXIS_X, AXIS_Y, AXIS_Z};
    const Handle planes[3] = {PLANE_YZ, PLANE_XZ, PLANE_XY};
    for (int axis = 0; axis < 3; ++axis)
    {
        float tip[3] = {point[0], point[1], point[2]};
        tip[axis] += size * 0.7f;
        float sx = 0.0f, sy = 0.0f;
        project(camera, tip, sx, sy);
        expect(pick(camera, point, size, pixel(sx), pixel(sy)) == axes[axis], "arrow is picked");

        float middle[3] = {point[0], point[1], point[2]};
        middle[(axis + 1) % 3] += size * 0.4f;
        middle[(axis + 2) % 3] += size * 0.4f;
        project(camera, middle, sx, sy);
        expect(pick(camera, point, size, pixel(sx), pixel(sy)) == planes[axis],
               "plane square is picked");
    }
    expect(pick(camera, point, size, pixel(cx) + 300, pixel(cy) + 200) == NONE,
           "nothing is picked far from the gizmo");

    // Dragging an arrow: the point ends up under the mouse and only that coordinate changes.
    for (int axis = 0; axis < 3; ++axis)
    {
        float from[3] = {point[0], point[1], point[2]};
        float to[3] = {point[0], point[1], point[2]};
        from[axis] += size * 0.5f;
        to[axis] += size * 0.5f + 7.0f;
        float fx = 0.0f, fy = 0.0f, ux = 0.0f, uy = 0.0f;
        project(camera, from, fx, fy);
        project(camera, to, ux, uy);
        float moved[3] = {0.0f, 0.0f, 0.0f};
        const bool ok =
            drag(axes[axis], camera, point, pixel(fx), pixel(fy), pixel(ux), pixel(uy), moved);
        bool exact = ok && std::fabs(moved[axis] - (point[axis] + 7.0f)) < 0.35f;
        for (int k = 0; k < 3; ++k)
            if (k != axis)
                exact = exact && std::fabs(moved[k] - point[k]) < 1.0e-4f;
        expect(exact, "axis drag moves 7 units along its axis only");
    }

    // Dragging a plane square keeps the third coordinate.
    for (int normal = 0; normal < 3; ++normal)
    {
        float to[3] = {point[0], point[1], point[2]};
        to[(normal + 1) % 3] += 5.0f;
        to[(normal + 2) % 3] -= 3.0f;
        float ux = 0.0f, uy = 0.0f;
        project(camera, to, ux, uy);
        float moved[3] = {0.0f, 0.0f, 0.0f};
        const bool ok =
            drag(planes[normal], camera, point, pixel(cx), pixel(cy), pixel(ux), pixel(uy), moved);
        bool exact = ok && std::fabs(moved[normal] - point[normal]) < 1.0e-4f;
        for (int k = 0; k < 3; ++k)
            exact = exact && std::fabs(moved[k] - to[k]) < 0.35f;
        expect(exact, "plane drag follows the mouse within its plane");
    }

    // The view handle moves parallel to the screen: the distance to the camera plane is kept.
    {
        float moved[3] = {0.0f, 0.0f, 0.0f};
        const bool ok =
            drag(VIEW, camera, point, pixel(cx), pixel(cy), pixel(cx) + 120, pixel(cy) - 60, moved);
        const double *m = camera.view;
        const double before = m[2] * point[0] + m[6] * point[1] + m[10] * point[2];
        const double after = m[2] * moved[0] + m[6] * moved[1] + m[10] * moved[2];
        float sx = 0.0f, sy = 0.0f;
        project(camera, moved, sx, sy);
        expect(ok && std::fabs(before - after) < 1.0e-3 && std::fabs(sx - (cx + 120.0f)) < 1.0f &&
                   std::fabs(sy - (cy - 60.0f)) < 1.0f,
               "view drag stays parallel to the screen and under the mouse");
    }

    // An axis seen end-on cannot be dragged, and the point is left alone.
    {
        const Camera along = lookAtOrigin(0.0001, 0.0, 80.0);
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        float moved[3] = {9.0f, 9.0f, 9.0f};
        const bool ok = drag(AXIS_Z, along, origin, 400, 300, 420, 310, moved);
        expect(!ok && moved[0] == 9.0f, "an end-on axis refuses the drag");
    }

    // A quarter turn about Y takes +X to -Z.
    {
        const float x[3] = {1.0f, 0.0f, 0.0f};
        const float y[3] = {0.0f, 1.0f, 0.0f};
        float turned[3] = {0.0f, 0.0f, 0.0f};
        rotate(x, y, 1.5707963f, turned);
        expect(std::fabs(turned[0]) < 1.0e-5f && std::fabs(turned[2] + 1.0f) < 1.0e-5f,
               "rotate turns right-handed");
    }

    return failures == 0 ? 0 : 1;
}
