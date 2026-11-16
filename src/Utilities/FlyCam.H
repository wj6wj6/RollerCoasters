/************************************************************************
     File:        FlyCam.H

     Comment:     A free-fly observer camera for FlTk OpenGL windows.

                  Keyboard (hold to move, the view keeps a level horizon):
                    W / S          fly forward / back, level with the ground
                    A / D          strafe left / right
                    E / Q          rise / sink (also Page Up / Page Down)
                    arrow keys     turn and look up / down
                    Shift          move three times faster
                    + / -          change the flying speed

                  Mouse and touchpad:
                    drag           look around (the caller starts left drags
                                   with beginDrag; right drags are taken here)
                    two-finger     precise touchpad scroll looks around
                    pinch / Ctrl   fly forward / back
                    Alt or Shift   scroll strafes and rises
                    mouse wheel    fly forward / back

                  The camera stays above the ground and within a box around
                  the scene, so it cannot get lost under the floor or in the
                  empty sky. lookAt() turns smoothly toward a point.

                  Held keys move the camera on an FLTK timeout using the
                  real elapsed time, so the speed is in world units per
                  second and does not depend on the frame rate.

*************************************************************************/
#pragma once

#include <chrono>

class Fl_Gl_Window;

class FlyCam
{
  public:
    FlyCam();
    ~FlyCam();
    FlyCam(const FlyCam &) = delete;
    FlyCam &operator=(const FlyCam &) = delete;

    // Attach the camera to a window before using it.
    void setup(Fl_Gl_Window *window, float fieldOfView = 60.0f);

    // Place the camera; yaw 0 looks down -Z, positive yaw turns right, positive
    // pitch looks up. Angles are in degrees.
    void setPose(float x, float y, float z, float yawDegrees, float pitchDegrees);

    // Take the pose of an OpenGL column-major modelview matrix, such as the
    // world camera's current view. Roll is dropped so the horizon stays level.
    // If that view used another field of view (degrees), the camera slides along
    // its view direction so the world origin keeps its size on screen.
    void setFromViewMatrix(const float matrix[16], float sourceFieldOfView = 0);

    // Mimics an FlTk handler: returns 1 when the camera used the event.
    int handle(int event);

    // Start a look drag for a button the caller already took (a left drag on
    // empty space). Later FL_DRAG/FL_RELEASE events go through handle().
    void beginDrag();

    // Wheel input in notches with FLTK signs (+dx right, +dy down). Precise
    // touchpad scrolling looks around; mouse wheel notches fly forward.
    void scroll(float dx, float dy, int state, bool precise);

    // One Direct Manipulation update: finger movement in window pixels and the
    // pinch ratio since the last update. The scene follows the fingers (looking
    // around, or sliding when slide is true) while spreading flies forward.
    void touchpadGesture(float dx, float dy, float scale, bool slide);

    // Set up the projection and modelview matrices. Pass false to keep a pick
    // matrix already on the projection stack.
    void setProjection(bool doClear = true) const;

    // Release held keys, end any drag, stop the movement timer and any turn.
    void stop();

    // Turn smoothly (over turnSeconds) to face a point. Any look input, such as
    // a drag or an arrow key, takes over and ends the turn.
    void lookAt(float x, float y, float z);
    bool turning() const
    {
        return turnActive;
    }

    void setSpeed(float unitsPerSecond);
    float speed() const
    {
        return moveSpeed;
    }
    float yaw() const
    {
        return yawDegrees;
    }
    float pitch() const
    {
        return pitchDegrees;
    }
    void position(float &x, float &y, float &z) const;
    void forward(float &x, float &y, float &z) const;
    bool moving() const
    {
        return timerActive;
    }

    static constexpr float minSpeed = 1.0f;
    static constexpr float maxSpeed = 200.0f;
    static constexpr float boost = 3.0f;                // Shift multiplier
    static constexpr float dragDegreesPerPixel = .25f;  // mouse look
    static constexpr float scrollDegrees = 3.0f;        // touchpad look per notch
    static constexpr float scrollSeconds = .15f;        // wheel travel per notch, in seconds
    static constexpr float slideSecondsPerPixel = .01f; // touchpad slide per pixel
    static constexpr float turnDegreesPerSecond = 90.0f;
    static constexpr float turnSeconds = .35f;   // lookAt() duration
    static constexpr float minHeight = 1.0f;     // eye height above the ground
    static constexpr float maxHeight = 400.0f;   // highest the camera can go
    static constexpr float maxDistance = 400.0f; // |x| and |z| limit

  private:
    enum Key
    {
        Forward,
        Back,
        Left,
        Right,
        Up,
        Down,
        TurnLeft,
        TurnRight,
        LookUp,
        LookDown,
        Fast,
        KeyCount
    };

    static void tick(void *data);
    static void turnTick(void *data);
    void endTurn();
    void keepInBounds();
    static int keyFor(int key);
    bool anyMotionHeld() const;
    void step(float seconds);
    void look(float yawDelta, float pitchDelta);
    // level: forward stays parallel to the ground instead of following the pitch.
    void move(float forwardDistance, float rightDistance, float upDistance, bool level = false);
    void redraw() const;

    bool held[KeyCount];
    bool timerActive;
    bool dragging;
    int lastX;
    int lastY;
    std::chrono::steady_clock::time_point lastTick;

    bool turnActive;
    float turnFromYaw;
    float turnFromPitch;
    float turnYaw;   // yaw change over the whole turn
    float turnPitch; // pitch change over the whole turn
    std::chrono::steady_clock::time_point turnStart;

    float eyeX;
    float eyeY;
    float eyeZ;
    float yawDegrees;
    float pitchDegrees;
    float fieldOfView;
    float moveSpeed;

    Fl_Gl_Window *wind;
};
