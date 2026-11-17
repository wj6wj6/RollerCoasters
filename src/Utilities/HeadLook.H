/************************************************************************
     File:        HeadLook.H

     Comment:     Turning the rider's head in the Train camera.

                  The head keeps a yaw and pitch relative to the train, so
                  the view still follows the track while the rider looks to
                  the side, up or behind. Yaw can go all the way round; the
                  pitch stops short of straight up and down, so the view
                  never flips.

                  Input follows the other cameras:
                    drag        drag right to look right, up to look up
                    touchpad    two fingers: the scene follows the fingers
                    scroll      precise touchpad scrolling looks around
                    reset()     look straight ahead again

*************************************************************************/
#pragma once

#include "Pnt3f.H"

class HeadLook
{
  public:
    HeadLook();

    // Turn by a change in degrees: positive yaw turns right, positive pitch
    // looks up. Yaw wraps to (-180, 180]; pitch stops at +/- maxPitch.
    void turn(float yawDelta, float pitchDelta);

    // Mouse drag in pixels since the last event (+x right, +y down).
    void drag(float dx, float dy);

    // Two-finger touchpad movement in window pixels (+x right, +y down). Like
    // the World and Fly cameras, the scene follows the fingers.
    void touchpadGesture(float dx, float dy);

    // Precise touchpad scrolling in wheel notches with FLTK signs (+dx right,
    // +dy down), as the Fly camera reads it.
    void scroll(float dx, float dy);

    // Look straight ahead along the track again.
    void reset();
    bool centered() const;

    float yaw() const
    {
        return yawDegrees;
    }
    float pitch() const
    {
        return pitchDegrees;
    }

    // Turn the train's frame into the rider's view. forward, side and up are
    // the train's unit axes as makeTrackFrame() gives them; side points to the
    // rider's left. Returns the unit view direction and view up.
    void apply(const Pnt3f &forward, const Pnt3f &side, const Pnt3f &up, Pnt3f &direction,
               Pnt3f &viewUp) const;

    static constexpr float maxPitch = 85.0f;
    static constexpr float dragDegreesPerPixel = .25f; // same as FlyCam
    static constexpr float scrollDegrees = 3.0f;       // touchpad look per notch

  private:
    float yawDegrees;
    float pitchDegrees;
};
