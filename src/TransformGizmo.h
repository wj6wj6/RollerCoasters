/************************************************************************
     File:        TransformGizmo.h

     Comment:     The move handles drawn on the selected control point,
                  in the manner of a 3D modelling program: an arrow for
                  each axis, a small square for each plane and a dot in
                  the middle that moves the point parallel to the screen.

                  pick() tells which handle is under the mouse, drag()
                  turns a mouse movement into a new position and draw()
                  draws the handles. pick() and drag() are plain maths on
                  the camera matrices and need no OpenGL context, so
                  they can be tested (tests/GizmoDragCheck.cpp).

                  Mouse coordinates are in pixels from the top left of
                  the view, as FLTK reports them.
*************************************************************************/
#pragma once

namespace TransformGizmo
{
enum Handle
{
    NONE = 0,
    AXIS_X,
    AXIS_Y,
    AXIS_Z,
    PLANE_YZ, // slides along Y and Z, X stays
    PLANE_XZ,
    PLANE_XY,
    VIEW // parallel to the screen
};

// The camera the view was last drawn with: OpenGL's column-major
// modelview and projection matrices and the viewport.
struct Camera
{
    double view[16];
    double projection[16];
    int viewport[4];
};

// How long the arrows must be in world units to cover `pixels` on screen.
float worldSize(const Camera &camera, const float position[3], float pixels);

// Where a world position lands on screen. False if it is behind the camera.
bool project(const Camera &camera, const float position[3], float &screenX, float &screenY);

// The handle under the mouse, or NONE.
Handle pick(const Camera &camera, const float position[3], float size, int mouseX, int mouseY);

// Where the point that was at `start` when the mouse was at (startX, startY)
// should be now that the mouse is at (mouseX, mouseY). False (and `result`
// left alone) when the handle is seen too nearly edge-on to tell.
bool drag(Handle handle, const Camera &camera, const float start[3], int startX, int startY,
          int mouseX, int mouseY, float result[3]);

// Unit vector from the eye towards `position` (the view direction for an
// orthographic camera).
void viewDirection(const Camera &camera, const float position[3], float direction[3]);

// Turns `vector` by `angle` radians about the unit `axis` (right-handed).
void rotate(const float vector[3], const float axis[3], float angle, float result[3]);

// Draws the handles on top of everything; `active` is drawn highlighted.
// With `constraintOnly` only the long line or plane outline of `active`
// is drawn, as while a point is being moved from the keyboard.
void draw(const float position[3], float size, Handle active, bool constraintOnly);
} // namespace TransformGizmo
