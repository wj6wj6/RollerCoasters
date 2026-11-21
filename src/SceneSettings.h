/************************************************************************
     File:        SceneSettings.h

     Comment:     What the controls are set to: the ride, the camera, the
                  track's spline, editing, lighting and the world. The
                  view, the callbacks and the menu (GameMenu) read and
                  change these.

*************************************************************************/
#pragma once

enum class CameraView
{
    World, // orbit around the scene (ArcBallCam)
    Train, // ride the train and turn your head
    Chase, // behind the train, looking over it
    Top,   // straight down, orthographic
    Fly    // free-moving observer (FlyCam)
};

struct SceneSettings
{
    // Ride
    bool run = false;      // is the train moving?
    float speed = 2.0f;    // how fast, 0 to 10
    bool arcLength = true; // move by distance along the track, not by parameter

    CameraView camera = CameraView::World;

    // Track
    int spline = 3;    // TrackSplineType: 1 linear, 2 cardinal, 3 B-spline
    bool edit = false; // show the control points and their handles

    // Lighting
    bool lighting = true; // shader lighting, shadow maps, sky and fog
    bool shadows = true;
    bool headlight = true;   // spotlight on the train's nose
    bool bloom = true;       // bloom and god rays
    bool ssao = true;        // screen space ambient occlusion
    float timeOfDay = 15.0f; // hours, 0 to 24

    // World
    bool water = true;   // reflective ocean
    bool rain = false;   // wet ground, streaks and ripples
    bool scenery = true; // the island's scenery, or level grass
};
