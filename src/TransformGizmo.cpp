/************************************************************************
     File:        TransformGizmo.cpp

     Comment:     See TransformGizmo.h.
*************************************************************************/
#include "TransformGizmo.h"

#include <glad/glad.h>
#include <math.h>

namespace
{
// Where on an arrow the grabbable part starts and ends, and where the plane
// squares sit, as fractions of the gizmo size.
const double kAxisStart = 0.16;
const double kAxisEnd = 1.0;
const double kPlaneNear = 0.28;
const double kPlaneFar = 0.52;
const double kAxisPickPixels = 7.0;
const double kCentrePickPixels = 9.0;

const double kAxes[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
const float kAxisColours[3][3] = {{1.0f, 0.2f, 0.32f}, {0.55f, 0.86f, 0.0f}, {0.16f, 0.56f, 1.0f}};

struct Vec4
{
    double x, y, z, w;
};

Vec4 transform(const double m[16], const Vec4 &v)
{
    return Vec4{m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
}

void multiply(const double a[16], const double b[16], double out[16])
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
        {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k)
                sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
}

// General 4x4 inverse by Gauss-Jordan elimination. False if singular.
bool invert(const double m[16], double out[16])
{
    double a[4][8] = {};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
        {
            a[row][column] = m[column * 4 + row];
            a[row][column + 4] = row == column ? 1.0 : 0.0;
        }
    for (int column = 0; column < 4; ++column)
    {
        int pivot = column;
        for (int row = column + 1; row < 4; ++row)
            if (fabs(a[row][column]) > fabs(a[pivot][column]))
                pivot = row;
        if (fabs(a[pivot][column]) < 1.0e-12)
            return false;
        for (int k = 0; k < 8; ++k)
        {
            const double keep = a[column][k];
            a[column][k] = a[pivot][k];
            a[pivot][k] = keep;
        }
        const double scale = 1.0 / a[column][column];
        for (int k = 0; k < 8; ++k)
            a[column][k] *= scale;
        for (int row = 0; row < 4; ++row)
        {
            if (row == column)
                continue;
            const double factor = a[row][column];
            for (int k = 0; k < 8; ++k)
                a[row][k] -= factor * a[column][k];
        }
    }
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            out[column * 4 + row] = a[row][column + 4];
    return true;
}

double dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool projectPoint(const TransformGizmo::Camera &camera, const double p[3], double &sx, double &sy)
{
    const Vec4 eye = transform(camera.view, Vec4{p[0], p[1], p[2], 1.0});
    const Vec4 clip = transform(camera.projection, eye);
    if (clip.w <= 1.0e-9)
        return false;
    sx = camera.viewport[0] + (clip.x / clip.w * 0.5 + 0.5) * camera.viewport[2];
    sy = camera.viewport[3] - (clip.y / clip.w * 0.5 + 0.5) * camera.viewport[3];
    return true;
}

// The line of sight through a pixel: a point on it and its unit direction.
bool mouseRay(const TransformGizmo::Camera &camera, double mx, double my, double origin[3],
              double direction[3])
{
    double both[16] = {};
    double inverse[16] = {};
    multiply(camera.projection, camera.view, both);
    if (!invert(both, inverse))
        return false;
    const double nx = (mx - camera.viewport[0]) / camera.viewport[2] * 2.0 - 1.0;
    const double ny = (camera.viewport[3] - my) / camera.viewport[3] * 2.0 - 1.0;
    const Vec4 a = transform(inverse, Vec4{nx, ny, -1.0, 1.0});
    const Vec4 b = transform(inverse, Vec4{nx, ny, 1.0, 1.0});
    if (fabs(a.w) < 1.0e-12 || fabs(b.w) < 1.0e-12)
        return false;
    origin[0] = a.x / a.w;
    origin[1] = a.y / a.w;
    origin[2] = a.z / a.w;
    direction[0] = b.x / b.w - origin[0];
    direction[1] = b.y / b.w - origin[1];
    direction[2] = b.z / b.w - origin[2];
    const double length = sqrt(dot(direction, direction));
    if (length < 1.0e-12)
        return false;
    for (int k = 0; k < 3; ++k)
        direction[k] /= length;
    return true;
}

double distanceToSegment(double px, double py, double ax, double ay, double bx, double by)
{
    const double vx = bx - ax, vy = by - ay;
    const double lengthSquared = vx * vx + vy * vy;
    double t = lengthSquared > 0.0 ? ((px - ax) * vx + (py - ay) * vy) / lengthSquared : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return sqrt(dx * dx + dy * dy);
}

// Is (px, py) inside the convex quad whose corners are given in order?
bool insideQuad(double px, double py, const double x[4], const double y[4])
{
    int positive = 0, negative = 0;
    for (int i = 0; i < 4; ++i)
    {
        const int j = (i + 1) % 4;
        const double cross = (x[j] - x[i]) * (py - y[i]) - (y[j] - y[i]) * (px - x[i]);
        if (cross > 0.0)
            ++positive;
        else if (cross < 0.0)
            ++negative;
    }
    return positive == 0 || negative == 0;
}

// The two axes a plane handle slides along; `normal` is the third.
void planeAxes(TransformGizmo::Handle handle, int &first, int &second, int &normal)
{
    normal = handle == TransformGizmo::PLANE_YZ ? 0 : (handle == TransformGizmo::PLANE_XZ ? 1 : 2);
    first = (normal + 1) % 3;
    second = (normal + 2) % 3;
}

// The point the mouse ray picks out on the handle's line or plane.
bool solve(TransformGizmo::Handle handle, const TransformGizmo::Camera &camera,
           const double start[3], double mx, double my, double hit[3])
{
    double origin[3] = {0.0, 0.0, 0.0};
    double direction[3] = {0.0, 0.0, 0.0};
    if (!mouseRay(camera, mx, my, origin, direction))
        return false;
    const double toStart[3] = {start[0] - origin[0], start[1] - origin[1], start[2] - origin[2]};

    if (handle >= TransformGizmo::AXIS_X && handle <= TransformGizmo::AXIS_Z)
    {
        // Closest point of the axis to the ray.
        const double *axis = kAxes[handle - TransformGizmo::AXIS_X];
        const double along = dot(axis, direction);
        const double denominator = 1.0 - along * along;
        if (denominator < 1.0e-3)
            return false;
        const double t = (dot(toStart, direction) * along - dot(toStart, axis)) / denominator;
        for (int k = 0; k < 3; ++k)
            hit[k] = start[k] + axis[k] * t;
        return true;
    }

    double normal[3] = {0.0, 0.0, 0.0};
    if (handle == TransformGizmo::VIEW)
    {
        // The plane through the point that faces the camera.
        const double viewRow[3] = {camera.view[2], camera.view[6], camera.view[10]};
        for (int k = 0; k < 3; ++k)
            normal[k] = viewRow[k];
    }
    else
    {
        int first = 0, second = 0, n = 0;
        planeAxes(handle, first, second, n);
        for (int k = 0; k < 3; ++k)
            normal[k] = kAxes[n][k];
    }
    const double facing = dot(direction, normal);
    if (fabs(facing) < 0.03)
        return false;
    const double s = dot(toStart, normal) / facing;
    for (int k = 0; k < 3; ++k)
        hit[k] = origin[k] + direction[k] * s;
    return true;
}

void setColour(int axis, bool highlight, float alpha)
{
    if (highlight)
        glColor4f(1.0f, 0.95f, 0.45f, alpha);
    else
        glColor4f(kAxisColours[axis][0], kAxisColours[axis][1], kAxisColours[axis][2], alpha);
}

void drawArrow(const float p[3], int axis, float size, bool highlight)
{
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;
    const float shaftEnd = size * 0.8f;
    const float radius = size * 0.065f;
    float a[3] = {p[0], p[1], p[2]};
    float b[3] = {p[0], p[1], p[2]};
    a[axis] += size * static_cast<float>(kAxisStart);
    b[axis] += shaftEnd;

    setColour(axis, highlight, 1.0f);
    glLineWidth(highlight ? 4.0f : 3.0f);
    glBegin(GL_LINES);
    glVertex3fv(a);
    glVertex3fv(b);
    glEnd();

    // Cone tip.
    float tip[3] = {p[0], p[1], p[2]};
    tip[axis] += size;
    glBegin(GL_TRIANGLE_FAN);
    glVertex3fv(tip);
    for (int i = 0; i <= 12; ++i)
    {
        const float angle = static_cast<float>(i) * 6.2831853f / 12.0f;
        float rim[3] = {b[0], b[1], b[2]};
        rim[u] += cosf(angle) * radius;
        rim[v] += sinf(angle) * radius;
        glVertex3fv(rim);
    }
    glEnd();
}

void drawPlane(const float p[3], int normal, float size, bool highlight)
{
    const int u = (normal + 1) % 3;
    const int v = (normal + 2) % 3;
    const float inner = size * static_cast<float>(kPlaneNear);
    const float outer = size * static_cast<float>(kPlaneFar);
    const float corners[4][2] = {{inner, inner}, {outer, inner}, {outer, outer}, {inner, outer}};
    for (int pass = 0; pass < 2; ++pass)
    {
        setColour(normal, highlight, pass == 0 ? 0.35f : 1.0f);
        glLineWidth(1.5f);
        glBegin(pass == 0 ? GL_QUADS : GL_LINE_LOOP);
        for (const float *corner : corners)
        {
            float q[3] = {p[0], p[1], p[2]};
            q[u] += corner[0];
            q[v] += corner[1];
            glVertex3fv(q);
        }
        glEnd();
    }
}
} // namespace

float TransformGizmo::worldSize(const Camera &camera, const float position[3], float pixels)
{
    // One world unit sideways at the point: how many pixels is that?
    const double p[3] = {position[0], position[1], position[2]};
    const double q[3] = {p[0] + camera.view[0], p[1] + camera.view[4], p[2] + camera.view[8]};
    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    if (!projectPoint(camera, p, ax, ay) || !projectPoint(camera, q, bx, by))
        return 1.0f;
    const double perUnit = sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    if (perUnit < 1.0e-6)
        return 1.0f;
    return static_cast<float>(static_cast<double>(pixels) / perUnit);
}

bool TransformGizmo::project(const Camera &camera, const float position[3], float &screenX,
                             float &screenY)
{
    const double p[3] = {position[0], position[1], position[2]};
    double x = 0.0, y = 0.0;
    if (!projectPoint(camera, p, x, y))
        return false;
    screenX = static_cast<float>(x);
    screenY = static_cast<float>(y);
    return true;
}

TransformGizmo::Handle TransformGizmo::pick(const Camera &camera, const float position[3],
                                            float size, int mouseX, int mouseY)
{
    const double p[3] = {position[0], position[1], position[2]};
    const double s = size;
    const double mx = mouseX, my = mouseY;
    double cx = 0.0, cy = 0.0;
    if (!projectPoint(camera, p, cx, cy))
        return NONE;
    if (sqrt((mx - cx) * (mx - cx) + (my - cy) * (my - cy)) <= kCentrePickPixels)
        return VIEW;

    // The arrows win over the squares: they are the thinner target.
    Handle best = NONE;
    double bestDistance = kAxisPickPixels;
    for (int axis = 0; axis < 3; ++axis)
    {
        double a[3] = {0.0, 0.0, 0.0};
        double b[3] = {0.0, 0.0, 0.0};
        double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            a[k] = p[k] + kAxes[axis][k] * s * kAxisStart;
            b[k] = p[k] + kAxes[axis][k] * s * kAxisEnd;
        }
        if (!projectPoint(camera, a, ax, ay) || !projectPoint(camera, b, bx, by))
            continue;
        const double distance = distanceToSegment(mx, my, ax, ay, bx, by);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = static_cast<Handle>(AXIS_X + axis);
        }
    }
    if (best != NONE)
        return best;

    for (int normal = 0; normal < 3; ++normal)
    {
        const int u = (normal + 1) % 3;
        const int v = (normal + 2) % 3;
        const double offsets[4][2] = {{kPlaneNear, kPlaneNear},
                                      {kPlaneFar, kPlaneNear},
                                      {kPlaneFar, kPlaneFar},
                                      {kPlaneNear, kPlaneFar}};
        double x[4] = {0.0, 0.0, 0.0, 0.0};
        double y[4] = {0.0, 0.0, 0.0, 0.0};
        bool visible = true;
        for (int i = 0; i < 4 && visible; ++i)
        {
            double q[3] = {p[0], p[1], p[2]};
            q[u] += offsets[i][0] * s;
            q[v] += offsets[i][1] * s;
            visible = projectPoint(camera, q, x[i], y[i]);
        }
        if (visible && insideQuad(mx, my, x, y))
            return static_cast<Handle>(PLANE_YZ + normal);
    }
    return NONE;
}

bool TransformGizmo::drag(Handle handle, const Camera &camera, const float start[3], int startX,
                          int startY, int mouseX, int mouseY, float result[3])
{
    if (handle == NONE)
        return false;
    const double origin[3] = {start[0], start[1], start[2]};
    double from[3] = {0.0, 0.0, 0.0};
    double to[3] = {0.0, 0.0, 0.0};
    if (!solve(handle, camera, origin, startX, startY, from) ||
        !solve(handle, camera, origin, mouseX, mouseY, to))
        return false;
    // A handle seen almost edge-on can send the point to the horizon.
    const double limit = 5000.0;
    for (int k = 0; k < 3; ++k)
        if (fabs(to[k] - from[k]) > limit)
            return false;
    for (int k = 0; k < 3; ++k)
        result[k] = static_cast<float>(origin[k] + to[k] - from[k]);
    return true;
}

void TransformGizmo::viewDirection(const Camera &camera, const float position[3],
                                   float direction[3])
{
    double origin[3] = {0.0, 0.0, 0.0};
    double towards[3] = {0.0, 0.0, 0.0};
    float sx = 0.0f, sy = 0.0f;
    if (project(camera, position, sx, sy) &&
        mouseRay(camera, static_cast<double>(sx), static_cast<double>(sy), origin, towards))
    {
        for (int k = 0; k < 3; ++k)
            direction[k] = static_cast<float>(towards[k]);
        return;
    }
    direction[0] = static_cast<float>(-camera.view[2]);
    direction[1] = static_cast<float>(-camera.view[6]);
    direction[2] = static_cast<float>(-camera.view[10]);
}

void TransformGizmo::rotate(const float vector[3], const float axis[3], float angle,
                            float result[3])
{
    // Rodrigues' rotation formula.
    const float c = cosf(angle);
    const float s = sinf(angle);
    const float along = vector[0] * axis[0] + vector[1] * axis[1] + vector[2] * axis[2];
    const float cross[3] = {axis[1] * vector[2] - axis[2] * vector[1],
                            axis[2] * vector[0] - axis[0] * vector[2],
                            axis[0] * vector[1] - axis[1] * vector[0]};
    for (int k = 0; k < 3; ++k)
        result[k] = vector[k] * c + cross[k] * s + axis[k] * along * (1.0f - c);
}

void TransformGizmo::draw(const float position[3], float size, Handle active, bool constraintOnly)
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT | GL_DEPTH_BUFFER_BIT |
                 GL_COLOR_BUFFER_BIT);
    glUseProgram(0);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);

    if (constraintOnly)
    {
        // The line or lines the point is held to, running off both ways.
        const float reach = size * 40.0f;
        glLineWidth(1.5f);
        glBegin(GL_LINES);
        for (int axis = 0; axis < 3; ++axis)
        {
            const bool onAxis = active == AXIS_X + axis;
            const bool inPlane =
                active >= PLANE_YZ && active <= PLANE_XY && active != PLANE_YZ + axis;
            if (!onAxis && !inPlane)
                continue;
            setColour(axis, false, 0.9f);
            float a[3] = {position[0], position[1], position[2]};
            float b[3] = {position[0], position[1], position[2]};
            a[axis] -= reach;
            b[axis] += reach;
            glVertex3fv(a);
            glVertex3fv(b);
        }
        glEnd();
    }
    else
    {
        for (int normal = 0; normal < 3; ++normal)
            drawPlane(position, normal, size, active == PLANE_YZ + normal);
        for (int axis = 0; axis < 3; ++axis)
            drawArrow(position, axis, size, active == AXIS_X + axis);
    }

    // The dot in the middle: white with an orange rim.
    glEnable(GL_POINT_SMOOTH);
    glPointSize(active == VIEW ? 15.0f : 12.0f);
    glColor4f(1.0f, 0.62f, 0.16f, 1.0f);
    glBegin(GL_POINTS);
    glVertex3fv(position);
    glEnd();
    glPointSize(active == VIEW ? 10.0f : 7.0f);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_POINTS);
    glVertex3fv(position);
    glEnd();

    glPopAttrib();
}
