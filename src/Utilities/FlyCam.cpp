/************************************************************************
     File:        FlyCam.cpp

     Comment:     Free-fly observer camera, see FlyCam.H.

*************************************************************************/

#include "FlyCam.H"

#include <math.h>
#include <windows.h>

#include <GL/GL.h>
#include <GL/GLU.h>

#include "Platform.H"

namespace
{
const float degreesToRadians = 3.14159265f / 180.0f;
const float pitchLimit = 89.0f;
const double tickSeconds = 1.0 / 60.0;
// A stalled frame (window drag, breakpoint) must not fling the camera away.
const float longestStep = .1f;
} // namespace

FlyCam::FlyCam()
    : timerActive(false), dragging(false), lastX(0), lastY(0), turnActive(false), turnFromYaw(0),
      turnFromPitch(0), turnYaw(0), turnPitch(0), eyeX(0), eyeY(40.0f), eyeZ(150.0f), yawDegrees(0),
      pitchDegrees(-15.0f), fieldOfView(60.0f), moveSpeed(30.0f), groundY(0), wind(nullptr)
{
    for (bool &key : held)
        key = false;
}

FlyCam::~FlyCam()
{
    platform::removeTimeout(tick, this);
    platform::removeTimeout(turnTick, this);
}

void FlyCam::setup(Viewport *window, float fov)
{
    stop();
    wind = window;
    setFieldOfView(fov);
}

void FlyCam::setFieldOfView(float degrees)
{
    if (degrees < minFieldOfView)
        degrees = minFieldOfView;
    if (degrees > maxFieldOfView)
        degrees = maxFieldOfView;
    fieldOfView = degrees;
    redraw();
}

void FlyCam::setPose(float x, float y, float z, float yaw, float pitch)
{
    eyeX = x;
    eyeY = y;
    eyeZ = z;
    yawDegrees = 0;
    pitchDegrees = 0;
    look(yaw, pitch);
    keepInBounds();
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

void FlyCam::keepInBounds()
{
    if (eyeY < groundY + minHeight)
        eyeY = groundY + minHeight;
    if (eyeY > maxHeight)
        eyeY = maxHeight;
    if (eyeX < -maxDistance)
        eyeX = -maxDistance;
    if (eyeX > maxDistance)
        eyeX = maxDistance;
    if (eyeZ < -maxDistance)
        eyeZ = -maxDistance;
    if (eyeZ > maxDistance)
        eyeZ = maxDistance;
}

void FlyCam::look(float yawDelta, float pitchDelta)
{
    // The user is steering again; a lookAt() turn must not fight it.
    if (turnActive && (yawDelta != 0 || pitchDelta != 0))
        endTurn();
    yawDegrees += yawDelta;
    // Keep yaw in [-180, 180) so it never loses precision after long spins.
    yawDegrees -= 360.0f * floorf((yawDegrees + 180.0f) / 360.0f);
    pitchDegrees += pitchDelta;
    if (pitchDegrees > pitchLimit)
        pitchDegrees = pitchLimit;
    if (pitchDegrees < -pitchLimit)
        pitchDegrees = -pitchLimit;
}

void FlyCam::move(float forwardDistance, float rightDistance, float upDistance, bool level)
{
    float fx, fy, fz;
    forward(fx, fy, fz);
    const float yaw = yawDegrees * degreesToRadians;
    if (level)
    {
        fx = sinf(yaw);
        fy = 0;
        fz = -cosf(yaw);
    }
    eyeX += fx * forwardDistance + cosf(yaw) * rightDistance;
    eyeY += fy * forwardDistance + upDistance;
    eyeZ += fz * forwardDistance + sinf(yaw) * rightDistance;
    keepInBounds();
}

void FlyCam::lookAt(float x, float y, float z)
{
    const float dx = x - eyeX;
    const float dy = y - eyeY;
    const float dz = z - eyeZ;
    const float horizontal = sqrtf(dx * dx + dz * dz);
    if (horizontal < 1e-4f && fabsf(dy) < 1e-4f)
        return;
    const float yaw = horizontal > 1e-4f ? atan2f(dx, -dz) / degreesToRadians : yawDegrees;
    float pitch = atan2f(dy, horizontal) / degreesToRadians;
    if (pitch > pitchLimit)
        pitch = pitchLimit;
    if (pitch < -pitchLimit)
        pitch = -pitchLimit;
    turnFromYaw = yawDegrees;
    turnFromPitch = pitchDegrees;
    // Take the short way round.
    turnYaw = yaw - yawDegrees;
    turnYaw -= 360.0f * floorf((turnYaw + 180.0f) / 360.0f);
    turnPitch = pitch - pitchDegrees;
    turnStart = std::chrono::steady_clock::now();
    if (!turnActive)
        platform::addTimeout(tickSeconds, turnTick, this);
    turnActive = true;
}

void FlyCam::endTurn()
{
    platform::removeTimeout(turnTick, this);
    turnActive = false;
}

void FlyCam::turnTick(void *data)
{
    FlyCam *camera = static_cast<FlyCam *>(data);
    const float elapsed =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - camera->turnStart).count();
    float t = elapsed / turnSeconds;
    if (t > 1)
        t = 1;
    // Ease in and out so the turn starts and stops gently.
    const float eased = t * t * (3 - 2 * t);
    float yaw = camera->turnFromYaw + camera->turnYaw * eased;
    yaw -= 360.0f * floorf((yaw + 180.0f) / 360.0f);
    camera->yawDegrees = yaw;
    camera->pitchDegrees = camera->turnFromPitch + camera->turnPitch * eased;
    camera->redraw();
    if (t < 1)
        platform::repeatTimeout(tickSeconds, turnTick, data);
    else
        camera->turnActive = false;
}

void FlyCam::redraw() const
{
    if (wind)
        wind->requestRedraw();
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
    case ' ':
    case 'e':
    case Key::PageUp:
        return Up;
    case Key::ShiftL:
    case Key::ShiftR:
    case 'q':
    case Key::PageDown:
        return Down;
    case Key::Left:
        return TurnLeft;
    case Key::Right:
        return TurnRight;
    case Key::Up:
        return LookUp;
    case Key::Down:
        return LookDown;
    case Key::ControlL:
    case Key::ControlR:
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
    move(forwardAmount * distance, rightAmount * distance, upAmount * distance, true);
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
        platform::repeatTimeout(tickSeconds, tick, data);
    else
        camera->timerActive = false;
}

void FlyCam::stop()
{
    endTurn();
    platform::removeTimeout(tick, this);
    timerActive = false;
    dragging = false;
    for (bool &key : held)
        key = false;
}

void FlyCam::beginDrag(int x, int y)
{
    dragging = true;
    lastX = x;
    lastY = y;
}

void FlyCam::scroll(float dx, float dy, int state, bool precise)
{
    const float wheelDistance = moveSpeed * scrollSeconds;
    if (state & Mod::Ctrl)
        // Spreading two fingers (or Ctrl + wheel up) flies forward.
        move(-dy * wheelDistance, 0, 0);
    else if (state & Mod::Alt)
        move(0, dx * wheelDistance, -dy * wheelDistance);
    else if (precise)
        look(dx * scrollDegrees, -dy * scrollDegrees);
    else
        // Like a spectator: wheel up flies faster, wheel down slower.
        setSpeed(moveSpeed * powf(wheelSpeedStep, -dy));
    redraw();
}

void FlyCam::mouseLook(float dx, float dy)
{
    look(dx * mouseDegreesPerPixel, -dy * mouseDegreesPerPixel);
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

int FlyCam::handle(const InputEvent &event)
{
    switch (event.type)
    {
    case InputType::Push:
        if (event.button == Mouse::Right)
        {
            beginDrag(event.x, event.y);
            return 1;
        }
        break;

    case InputType::Drag:
        if (dragging)
        {
            const int x = event.x;
            const int y = event.y;
            // Drag right to turn right, drag up to look up.
            look((float)(x - lastX) * dragDegreesPerPixel,
                 (float)(lastY - y) * dragDegreesPerPixel);
            lastX = x;
            lastY = y;
            redraw();
            return 1;
        }
        break;

    case InputType::Release:
        if (dragging)
        {
            dragging = false;
            return 1;
        }
        break;

    case InputType::KeyDown: {
        const int key = event.key;
        if (key == '=' || key == '+' || key == Key::Keypad + '+')
        {
            setSpeed(moveSpeed * 1.25f);
            return 1;
        }
        if (key == '-' || key == Key::Keypad + '-')
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
            platform::addTimeout(tickSeconds, tick, this);
        }
        return 1;
    }

    case InputType::KeyUp: {
        const int index = keyFor(event.key);
        if (index < 0)
            return 0;
        held[index] = false;
        // The timer notices on its next tick and stops itself.
        return 1;
    }

    // A key released while another window has focus never sends KeyUp.
    case InputType::Unfocus:
    case InputType::Hide:
        stop();
        break;

    case InputType::Wheel:
        scroll(event.dx, event.dy, event.state, false);
        return 1;
    default:
        break;
    }
    return 0;
}

void FlyCam::setProjection(bool doClear) const
{
    glMatrixMode(GL_PROJECTION);
    if (doClear)
        glLoadIdentity();
    const double aspect = (double)wind->viewWidth() / (double)wind->viewHeight();
    gluPerspective(fieldOfView, aspect, .1, 3000);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float fx, fy, fz;
    forward(fx, fy, fz);
    gluLookAt(eyeX, eyeY, eyeZ, eyeX + fx, eyeY + fy, eyeZ + fz, 0, 1, 0);
}
