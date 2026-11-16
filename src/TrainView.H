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
#include "Utilities/ArcBallCam.H"
#include "AdvancedLighting.h"
#include "PostEffects.h"
#include "GrassGround.h"
#include "OceanWorld.h"
#include "TransformGizmo.h"
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

    // all of the actual drawing happens in this routine
    // it has to be encapsulated, since we draw differently if
    // we're drawing shadows (no colors, for example)
    void drawStuff(bool doingShadows = false);

    // setup the projection - assuming that the projection stack has been
    // cleared for you
    void setProjection();

    // Reset the Arc ball control
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

    // the grass island everything stands on, and the sea floor around it
    // (TrainViewEffects.cpp)
    void drawGround();
    void drawUnderwater(bool shadowPass);

    // seconds since the program started: drives everything that moves by
    // itself (TrainViewEffects.cpp)
    float animationTime() const;

  public:
    ArcBallCam arcball; // keep an ArcBall for the UI
    int selectedCube;   // simple - just remember which cube is selected

    TrainWindow *tw;               // The parent of this display window
    CTrack *m_pTrack;              // The track of the entire scene
    TrackRenderer m_trackRenderer; // Draws the rails and sleepers
    TouchpadGestures touchpad;     // Orbit and pinch zoom in one gesture

    AdvancedLighting lighting; // shadow maps, per-pixel light, headlight
    PostEffects effects;       // sky, water, SSAO, bloom, god rays
    GrassGround m_ground;      // grass blocks with dirt sides
    OceanWorld m_ocean;        // blocky sea floor, reef, ruins, wreck and tunnel

    // Control point editing state.
    TransformGizmo::Camera m_editCamera = {}; // the camera of the last frame drawn
    bool m_editCameraValid = false;
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
