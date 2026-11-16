/************************************************************************
     File:        FlyCam.cpp

     Comment:     Free-fly observer camera, see FlyCam.H.

*************************************************************************/

#include "FlyCam.H"

#include <math.h>
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4311) // convert void* to long
#pragma warning(disable : 4312) // convert long to void*
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <GL/GL.h>
#include <GL/GLU.h>
#pragma warning(pop)

namespace
{
const float degreesToRadians = 3.14159265f / 180.0f;
const float pitchLimit = 89.0f;
const double tickSeconds = 1.0 / 60.0;
// A stalled frame (window drag, breakpoint) must not fling the camera away.
const float longestStep = .1f;
} // namespace

FlyCam::FlyCam()
    : timerActive(false), dragging(false), lastX(0), lastY(0), eyeX(0), eyeY(40.0f), eyeZ(150.0f),
      yawDegrees(0), pitchDegrees(-15.0f), fieldOfView(60.0f), moveSpeed(30.0f), wind(nullptr)
{
    for (bool &key : held)
        key = false;
}

FlyCam::~FlyCam()
{
    Fl::remove_timeout(tick, this);
}

void FlyCam::setup(Fl_Gl_Window *window, float fov)
{
    stop();
    wind = window;
    fieldOfView = fov;
}

void FlyCam::setPose(float x, float y, float z, float yaw, float pitch)
{
    eyeX = x;
    eyeY = y;
    eyeZ = z;
    yawDegrees = 0;
    pitchDegrees = 0;
    look(yaw, pitch);
}

void FlyCam::setFromViewMatrix(const float m[16], float sourceFieldOfView)
{
    // Column-major: element (row, column) is m[column * 4 + row]. The camera sits
    // at -R^T t and looks down the negated third row of the rotation R.
    float position[3];
    for (int column = 0; column < 3; ++column)
        position[column] =
            -(m[column * 4 + 0] * m[12] + m[column * 4 + 1] * m[13] + m[column * 4 + 2] * m[14]);
    const float fx = -m[2];
    const float fy = -m[6];
    const float fz = -m[10];
    const float horizontal = sqrtf(fx * fx + fz * fz);
    const float yaw = horizontal > 1e-6f ? atan2f(fx, -fz) / degreesToRadians : yawDegrees;
    const float pitch = atan2f(fy, horizontal) / degreesToRadians;
    setPose(position[0], position[1], position[2], yaw, pitch);

    // The origin is -m[14] in front of the camera; scale that distance by the
    // ratio of the view widths so the scene does not jump when switching.
    const float originDepth = -m[14];
    if (sourceFieldOfView > 0 && originDepth > 0)
    {
        const float ratio = tanf(sourceFieldOfView * .5f * degreesToRadians) /
                            tanf(fieldOfView * .5f * degreesToRadians);
        move(originDepth * (1.0f - ratio), 0, 0);
    }
}

void FlyCam::position(float &x, float &y, float &z) const
{
    x = eyeX;
    y = eyeY;
    z = eyeZ;
}

void FlyCam::forward(float &x, float &y, float &z) const
{
    const float yaw = yawDegrees * degreesToRadians;
    const float pitch = pitchDegrees * degreesToRadians;
    x = sinf(yaw) * cosf(pitch);
    y = sinf(pitch);
    z = -cosf(yaw) * cosf(pitch);
}

void FlyCam::setSpeed(float unitsPerSecond)
{
    if (unitsPerSecond < minSpeed)
        unitsPerSecond = minSpeed;
    if (unitsPerSecond > maxSpeed)
        unitsPerSecond = maxSpeed;
    moveSpeed = unitsPerSecond;
}

void FlyCam::look(float yawDelta, float pitchDelta)
{
    yawDegrees += yawDelta;
    // Keep yaw in [-180, 180) so it never loses precision after long spins.
    yawDegrees -= 360.0f * floorf((yawDegrees + 180.0f) / 360.0f);
    pitchDegrees += pitchDelta;
    if (pitchDegrees > pitchLimit)
        pitchDegrees = pitchLimit;
    if (pitchDegrees < -pitchLimit)
        pitchDegrees = -pitchLimit;
}

void FlyCam::move(float forwardDistance, float rightDistance, float upDistance)
{
    float fx, fy, fz;
    forward(fx, fy, fz);
    const float yaw = yawDegrees * degreesToRadians;
    eyeX += fx * forwardDistance + cosf(yaw) * rightDistance;
    eyeY += fy * forwardDistance + upDistance;
    eyeZ += fz * forwardDistance + sinf(yaw) * rightDistance;
}

void FlyCam::redraw() const
{
    if (wind)
        wind->damage(1);
}

int FlyCam::keyFor(int key)
{
    switch (key)
    {
    case 'w':
        return Forward;
    case 's':
        return Back;
    case 'a':
        return Left;
    case 'd':
        return Right;
    case 'e':
    case FL_Page_Up:
        return Up;
    case 'q':
    case FL_Page_Down:
        return Down;
    case FL_Left:
        return TurnLeft;
    case FL_Right:
        return TurnRight;
    case FL_Up:
        return LookUp;
    case FL_Down:
        return LookDown;
    case FL_Shift_L:
    case FL_Shift_R:
        return Fast;
    }
    return -1;
}

bool FlyCam::anyMotionHeld() const
{
    for (int key = 0; key < KeyCount; ++key)
        if (key != Fast && held[key])
            return true;
    return false;
}

void FlyCam::step(float seconds)
{
    const float distance = moveSpeed * (held[Fast] ? boost : 1.0f) * seconds;
    const float forwardAmount = (held[Forward] ? 1.0f : 0.0f) - (held[Back] ? 1.0f : 0.0f);
    const float rightAmount = (held[Right] ? 1.0f : 0.0f) - (held[Left] ? 1.0f : 0.0f);
    const float upAmount = (held[Up] ? 1.0f : 0.0f) - (held[Down] ? 1.0f : 0.0f);
    const float turn = (held[TurnRight] ? 1.0f : 0.0f) - (held[TurnLeft] ? 1.0f : 0.0f);
    const float tilt = (held[LookUp] ? 1.0f : 0.0f) - (held[LookDown] ? 1.0f : 0.0f);
    move(forwardAmount * distance, rightAmount * distance, upAmount * distance);
    look(turn * turnDegreesPerSecond * seconds, tilt * turnDegreesPerSecond * seconds);
    redraw();
}

void FlyCam::tick(void *data)
{
    FlyCam *camera = static_cast<FlyCam *>(data);
    const auto now = std::chrono::steady_clock::now();
    float seconds = std::chrono::duration<float>(now - camera->lastTick).count();
    camera->lastTick = now;
    if (seconds > longestStep)
        seconds = longestStep;
    camera->step(seconds);
    if (camera->anyMotionHeld())
        Fl::repeat_timeout(tickSeconds, tick, data);
    else
        camera->timerActive = false;
}

void FlyCam::stop()
{
    Fl::remove_timeout(tick, this);
    timerActive = false;
    dragging = false;
    for (bool &key : held)
        key = false;
}

void FlyCam::beginDrag()
{
    dragging = true;
    lastX = Fl::event_x();
    lastY = Fl::event_y();
}

void FlyCam::scroll(float dx, float dy, int state, bool precise)
{
    const float wheelDistance = moveSpeed * scrollSeconds;
    if (state & FL_CTRL)
        // Spreading two fingers (or Ctrl + wheel up) flies forward.
        move(-dy * wheelDistance, 0, 0);
    else if (state & (FL_ALT | FL_SHIFT))
        move(0, dx * wheelDistance, -dy * wheelDistance);
    else if (precise)
        look(dx * scrollDegrees, -dy * scrollDegrees);
    else
        move(-dy * wheelDistance, dx * wheelDistance, 0);
    redraw();
}

void FlyCam::touchpadGesture(float dx, float dy, float scale, bool slide)
{
    // Same step as the wheel: one notch per 10% of spread.
    if (scale > 0 && scale != 1)
        move(logf(scale) / logf(1.1f) * moveSpeed * scrollSeconds, 0, 0);
    // A mouse drag owns the view direction until it ends.
    if (!dragging && (dx != 0 || dy != 0))
    {
        if (slide)
            move(0, -dx * moveSpeed * slideSecondsPerPixel, dy * moveSpeed * slideSecondsPerPixel);
        else
            look(-dx * dragDegreesPerPixel, dy * dragDegreesPerPixel);
    }
    redraw();
}

int FlyCam::handle(int event)
{
    switch (event)
    {
    case FL_PUSH:
        if (Fl::event_button() == FL_RIGHT_MOUSE)
        {
            beginDrag();
            return 1;
        }
        break;

    case FL_DRAG:
        if (dragging)
        {
            const int x = Fl::event_x();
            const int y = Fl::event_y();
            // Drag right to turn right, drag up to look up.
            look((float)(x - lastX) * dragDegreesPerPixel,
                 (float)(lastY - y) * dragDegreesPerPixel);
            lastX = x;
            lastY = y;
            redraw();
            return 1;
        }
        break;

    case FL_RELEASE:
        if (dragging)
        {
            dragging = false;
            return 1;
        }
        break;

    case FL_KEYDOWN: {
        const int key = Fl::event_key();
        if (key == '=' || key == '+' || key == FL_KP + '+')
        {
            setSpeed(moveSpeed * 1.25f);
            return 1;
        }
        if (key == '-' || key == FL_KP + '-')
        {
            setSpeed(moveSpeed / 1.25f);
            return 1;
        }
        const int index = keyFor(key);
        if (index < 0)
            return 0;
        held[index] = true;
        if (!timerActive && anyMotionHeld())
        {
            timerActive = true;
            lastTick = std::chrono::steady_clock::now();
            Fl::add_timeout(tickSeconds, tick, this);
        }
        return 1;
    }

    case FL_KEYUP: {
        const int index = keyFor(Fl::event_key());
        if (index < 0)
            return 0;
        held[index] = false;
        // The timer notices on its next tick and stops itself.
        return 1;
    }

    // A key released while another window has focus never sends FL_KEYUP.
    case FL_UNFOCUS:
    case FL_HIDE:
        stop();
        break;

    case FL_MOUSEWHEEL:
        scroll((float)Fl::event_dx(), (float)Fl::event_dy(), Fl::event_state(), false);
        return 1;
    }
    return 0;
}

void FlyCam::setProjection(bool doClear) const
{
    glMatrixMode(GL_PROJECTION);
    if (doClear)
        glLoadIdentity();
    const double aspect = (double)wind->w() / (double)wind->h();
    gluPerspective(fieldOfView, aspect, .1, 2000);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float fx, fy, fz;
    forward(fx, fy, fz);
    gluLookAt(eyeX, eyeY, eyeZ, eyeX + fx, eyeY + fy, eyeZ + fz, 0, 1, 0);
}
