/************************************************************************
     File:        TrainView.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     
						The TrainView is the window that actually shows the 
						train. Its a
						GL display canvas (Fl_Gl_Window).  It is held within 
						a TrainWindow
						that is the outer window with all the widgets. 
						The TrainView needs 
						to be aware of the window - since it might need to 
						check the widgets to see how to draw

	  Note:        we need to have pointers to this, but maybe not know 
						about it (beware circular references)

     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/

#pragma once

// Preclarify for preventing the compiler error
class TrainWindow;
class CTrack;

//#######################################################################
// TODO
// You might change the TrainView in order to add different objects to
// be drawn, or adjust the lighting, or ...
//#######################################################################

// The TrainView also needs to handle its user interface events.
// since this is an FLTk Window, we need to include FlTk Headers
// the pragmas turn off the warnings from FlTk - our code should have no warnings,
// but their code doesn't meet that standard
#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/Fl_Gl_Window.H>
#pragma warning(pop)

// this uses the old ArcBall Code
#include <vector>

#include "Utilities/ArcBallCam.H"
#include "AdvancedLighting.h"
#include "AppControl.h"
#include "PostEffects.h"
#include <memory>
#include <utility>

#include "LandWorld.h"
#include "OceanWorld.h"
#include "TransformGizmo.h"
#include "Utilities/FlyCam.H"
#include "Utilities/HeadLook.H"
#include "TrackRenderer.H"
#include "TouchpadGestures.H"

class TrainView : public Fl_Gl_Window
{
  public:
    // note that we keep the "standard widget" constructor arguments
    TrainView(int x, int y, int w, int h, const char *l = 0);
    ~TrainView();

    // overrides of important window things
    virtual int handle(int);
    virtual void draw();
    void drawFrame(); // the scene; draw() adds the command line checks

    // --doctor and --screenshot (AppControl.h, TrainViewControl.cpp): main()
    // starts them; automationResult is the exit code once they finish.
    void startAutomation(const AppControl::Options *options);
    void finishAutomatedFrame();
    void finishAutomation(int result);
    const AppControl::Options *automation = nullptr;
    int automationFrames = 0;
    int automationResult = -1;

    // all of the actual drawing happens in this routine
    // it has to be encapsulated, since we draw differently if
    // we're drawing shadows (no colors, for example)
    void drawStuff(bool doingShadows = false);

    // setup the projection - assuming that the projection stack has been
    // cleared for you
    void setProjection();

    // Reset the Arc ball control
    // Swaps the island's and the sea floor's textures for a texture pack's
    // (nullptr: built in).
    void setTexturePack(std::shared_ptr<const TexturePack::Pack> pack);
    void resetArcball();

    // pick a point (for when the mouse goes down)
    void doPick();

    // Control point editing: the move handles on the selected point, moving
    // (G) and tilting (R) from the keyboard, undo. See handle().
    enum EditMode
    {
        EDIT_IDLE = 0,
        EDIT_DRAG,  // a handle, or the point itself, held with the mouse
        EDIT_GRAB,  // moving after G: ends with a click, Enter or Esc
        EDIT_ROTATE // tilting after R
    };
    bool canEdit() const;
    bool hasSelection() const;
    void beginEdit(EditMode mode, TransformGizmo::Handle handle, int mouseX, int mouseY);
    void updateEdit(int mouseX, int mouseY);
    void endEdit(bool keep);
    bool editEvent(int event);
    bool editKey(int key);
    void undoEdit();
    void drawEditOverlay();

    // shader lighting, shadow maps and post effects (TrainViewEffects.cpp)
    bool advancedLightingOn();
    void drawAdvanced();
    void fillLightingFrame(LightingFrame &frame);

    // the island everything stands on (the scenery, or the level editor
    // island), the trestles under the track and the sea floor around it
    // (TrainViewEffects.cpp)
    bool sceneryOn() const;
    float topViewHalf() const;                    // half the shorter side of the Top view
    void topViewCentre(float &x, float &z) const; // the ground it centres on
    void drawGround(bool shadowPass = false);
    void drawSupports(bool shadowPass);
    void drawUnderwater(bool shadowPass);

    // seconds since the program started: drives everything that moves by
    // itself (TrainViewEffects.cpp)
    float animationTime() const;

    // Call when the camera buttons change: entering the observer (fly) camera
    // places it where the last frame was seen from; leaving it stops any motion.
    void cameraChanged();

    // Move the observer camera to where the world camera is looking from (Home).
    void placeFlyCamAtWorldView();
    // Move the observer camera to where the last frame not drawn by Fly was
    // seen from (World, Train or Chase).
    void placeFlyCamAtCurrentView();

    // The FOV slider: the field of view of every perspective camera.
    void setFieldOfView(float degrees);

    // Spectator-style mouse look in the observer camera: the cursor is hidden
    // and held at the view centre while captured. Esc or losing focus frees it.
    void setMouseCaptured(bool captured);
    // Turns the keyboard input method (IME) for this window on or off.
    void setInputMethodEnabled(bool enabled);
    void centerCursor();

    // Train camera: look straight ahead again, and mouse drags that turn the
    // rider's head (returns 1 when the event was used).
    void recenterHead();
    int handleTrainLook(int event);

    // Where the train is now (the track centre without a train).
    Pnt3f trainPosition() const;

    // Orientation aids for the observer camera: ground out to the horizon, and
    // an overlay with a centre crosshair.
    void drawFlyHorizon();
    void drawFlyOverlay();

  public:
    ArcBallCam arcball; // keep an ArcBall for the UI
    FlyCam flyCam;      // free-moving observer camera
    HeadLook headLook;  // where the rider looks in the Train camera
    bool headDragging;  // is a mouse drag turning the rider's head?
    int headDragX;      // last mouse position of that drag
    int headDragY;
    float m_fieldOfView; // degrees, the World, Train, Chase and Fly cameras
    bool mouseCaptured;  // does the mouse steer the observer camera?
    int selectedCube;    // simple - just remember which cube is selected

    TrainWindow *tw;               // The parent of this display window
    CTrack *m_pTrack;              // The track of the entire scene
    TrackRenderer m_trackRenderer; // Draws the rails and sleepers
    TouchpadGestures touchpad;     // Orbit and pinch zoom in one gesture

    AdvancedLighting lighting; // shadow maps, per-pixel light, headlight
    PostEffects effects;       // sky, water, SSAO, bloom, god rays
    LandWorld m_land;          // the island: mountains, canyon, volcano, village
    OceanWorld m_ocean;        // blocky sea floor, reef, ruins, wreck and tunnel

    // Control point editing state.
    TransformGizmo::Camera m_editCamera = {}; // the camera of the last frame drawn
    bool m_editCameraValid = false;
    bool m_editCameraOrthographic = false; // the last frame was the Top view
    // The track the trestles were last worked out for (drawSupports).
    std::vector<Pnt3f> m_supportPoints;
    int m_supportSpline = 0;
    std::vector<float> m_supportPath;

    // The last frame drawn by a camera other than Fly, where Fly takes over.
    double m_flyHandover[16] = {};
    bool m_flyHandoverTop = false; // ... and it was the Top view
    bool m_flyHandoverValid = false;
    float m_gizmoSize = 1.0f; // length of the handles in world units
    EditMode m_editMode = EDIT_IDLE;
    TransformGizmo::Handle m_editHandle = TransformGizmo::NONE;
    TransformGizmo::Handle m_hoverHandle = TransformGizmo::NONE;
    int m_editStartX = 0, m_editStartY = 0; // mouse when the edit began
    ControlPoint m_editStart;               // the point when the edit began
    bool m_undoValid = false;
    int m_undoIndex = -1;
    ControlPoint m_undoPoint;

    // Weather, eased towards the Rain button (TrainViewEffects.cpp).
    float m_rain = 0.0f;         // 0 dry sky, 1 pouring
    float m_wetness = 0.0f;      // 0 dry ground, 1 soaked: lags behind the rain
    float m_weatherTime = -1.0f; // when the weather was last advanced
};
