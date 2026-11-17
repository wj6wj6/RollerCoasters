#include "HeadLook.H"

#include <cmath>

static const float degreesToRadians = 3.14159265f / 180.0f;

HeadLook::HeadLook() : yawDegrees(0), pitchDegrees(0)
{
}

void HeadLook::turn(float yawDelta, float pitchDelta)
{
    yawDegrees = std::fmod(yawDegrees + yawDelta, 360.0f);
    if (yawDegrees > 180.0f)
        yawDegrees -= 360.0f;
    else if (yawDegrees <= -180.0f)
        yawDegrees += 360.0f;

    pitchDegrees += pitchDelta;
    if (pitchDegrees > maxPitch)
        pitchDegrees = maxPitch;
    else if (pitchDegrees < -maxPitch)
        pitchDegrees = -maxPitch;
}

void HeadLook::drag(float dx, float dy)
{
    turn(dx * dragDegreesPerPixel, -dy * dragDegreesPerPixel);
}

void HeadLook::touchpadGesture(float dx, float dy)
{
    turn(-dx * dragDegreesPerPixel, dy * dragDegreesPerPixel);
}

void HeadLook::scroll(float dx, float dy)
{
    turn(dx * scrollDegrees, -dy * scrollDegrees);
}

void HeadLook::reset()
{
    yawDegrees = 0;
    pitchDegrees = 0;
}

bool HeadLook::centered() const
{
    return yawDegrees == 0 && pitchDegrees == 0;
}

void HeadLook::apply(const Pnt3f &forward, const Pnt3f &side, const Pnt3f &up, Pnt3f &direction,
                     Pnt3f &viewUp) const
{
    const float yaw = yawDegrees * degreesToRadians;
    const float pitch = pitchDegrees * degreesToRadians;
    // side points left, so turning right moves toward -side.
    const Pnt3f level = forward * std::cos(yaw) + side * -std::sin(yaw);
    direction = level * std::cos(pitch) + up * std::sin(pitch);
    viewUp = level * -std::sin(pitch) + up * std::cos(pitch);
}
