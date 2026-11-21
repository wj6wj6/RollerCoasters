/************************************************************************
     File:        TrainView.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     The TrainView draws the scene into the OpenGL context of
                  the program's window (TrainWindow) and handles the mouse
                  and keyboard input the interface does not take. It reads
                  what the controls are set to from TrainWindow::settings.

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

// this uses the old ArcBall Code
#include <string>
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
#include "TrackHistory.h"
#include "Utilities/FlyCam.H"
#include "Utilities/HeadLook.H"
#include "Utilities/Input.H"
#include "Ui/Gui.h"
#include "Ui/GuiGl.h"
#include "TrackRenderer.H"
#include "TouchpadGestures.H"

struct GLFWwindow;

class TrainView : public Viewport
{
  public:
    TrainView();
    ~TrainView();

    // The window whose OpenGL context this draws into, once it is open.
    void attach(GLFWwindow *window);
    // The size of what is drawn, in pixels (TrainWindow passes it on).
    void resize(int width, int height);
    int w() const
    {
        return m_width;
    }
    int h() const
    {
        return m_height;
    }
    bool shown() const
    {
        return m_window != nullptr;
    }
    void make_current();
    // Ask for a new frame; TrainWindow's loop draws it.
    void damage(int)
    {
        m_needsDraw = true;
    }
    void redraw()
    {
        m_needsDraw = true;
    }
    bool needsDraw() const
    {
        return m_needsDraw;
    }

    void draw();      // a frame: the scene, the interface, the command line checks
    void drawFrame(); // the scene

    // Mouse and keyboard input the interface did not take, as InputEvent
    // (GlfwInput.H). Returns 1 when the view used the event.
    int handleInput(const InputEvent &e);
    // A WM_MOUSEWHEEL or WM_MOUSEHWHEEL, read before GLFW rounds it, so
    // touchpad scrolling and pinching are smooth (TrainView.cpp). True when used.
    bool systemWheel(unsigned message, unsigned long long wParam, long long lParam);

    // Viewport, for the cameras
    int viewWidth() const override
    {
        return w();
    }
    int viewHeight() const override
    {
        return h();
    }
    void requestRedraw() override
    {
        damage(1);
    }

    // --doctor and --screenshot (AppControl.h, TrainViewControl.cpp): main()
    // starts them; automationResult is the exit code once they finish.
    void startAutomation(const AppControl::Options *options);
    void finishAutomatedFrame();
    void finishAutomation(int result);
    const AppControl::Options *automation = nullptr;
    int automationFrames = 0;
    int automationResult = -1;
    // Did the last drawFrame() show the scene, not the loading screen? Only
    // those frames count, so a screenshot is never of the progress bar.
    bool frameShowsScene = false;
    double automationStart = 0; // seconds, when the run started
    double worldSeconds = 0;    // until the first frame of the scene

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

    // pick a point (for when the mouse goes down at window pixel mx, my)
    void doPick(int mx, int my);

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
    // Is a move, tilt, point placing or carrying in progress (Esc cancels it)?
    bool editing() const
    {
        return m_editMode != EDIT_IDLE || m_placing || m_carrying;
    }
    void beginEdit(EditMode mode, TransformGizmo::Handle handle, int mouseX, int mouseY);
    void updateEdit(int mouseX, int mouseY, bool snap); // snap: Ctrl, whole units or 5 degrees
    void endEdit(bool keep);
    bool editEvent(const InputEvent &e);
    bool editKey(const InputEvent &e); // a KeyDown with no edit in progress
    // ends a move, tilt or point placing in progress, keeping what was done
    void finishEdits();
    void drawEditOverlay();

    // Adding a point (hotbar slot 7, Shift + D, Alt + D in Fly): the mouse shows
    // where on the track the new point would go, a left click puts it there
    // and picks it up to drag, Esc or a right click gives up. See TrackInsert.h.
    void beginPlacePoint();
    void endPlacePoint();
    void updatePlaceHover(int mouseX, int mouseY);
    bool placeEvent(const InputEvent &e);
    void flyBuildClick(int button); // Mouse::Left, Middle or Right
    void updateFlyPlace();

  public:
    // Building in Fly (mouse captured, Edit on), as in Minecraft: left click
    // removes the point at the crosshair, right places one at the crosshair
    // the wheel's distance ahead, middle selects (the system wheel handler in
    // TrainView.cpp asks these).
    bool flyBuilding() const;
    bool wheelSetsDepth() const; // building, or carrying a point (G)

    // G in Fly picks up the point at the crosshair (or the selected one) to
    // carry along with the view until G again or a left click; the wheel sets
    // its distance, Esc puts it back.
    void beginCarry();
    void updateCarry();
    void endCarry(bool keep);
    bool carryEvent(const InputEvent &e);
    void flyAxes(float right[3], float up[3], float forward[3]) const;
    // the control point at the Fly crosshair, or -1
    int crosshairPoint() const;
    // the point the middle button or C selects (crosshair in Fly, else the
    // mouse at window pixel mouseX, mouseY)
    int pointUnderMouse(int mouseX, int mouseY) const;
    static constexpr float kCrosshairReach = 18.0f; // pixels
    void adjustPlaceDepth(float wheelDy);

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
    // the island's see-through water, positions only, for the sea's shader;
    // while landWaterLater is set drawStuff() leaves it out
    void drawLandWater();
    bool landWaterLater = false;

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
    void centerCursor();

    // Train camera: look straight ahead again, and mouse drags that turn the
    // rider's head (returns 1 when the event was used).
    void recenterHead();
    int handleTrainLook(const InputEvent &e);

    // Where the train is now (the track centre without a train).
    Pnt3f trainPosition() const;

    // Orientation aids for the observer camera: ground out to the horizon, and
    // an overlay with a centre crosshair.
    void drawFlyHorizon();
    void drawFlyOverlay();

    // While the island and the sea floor are built (at start, and when
    // Scenery is switched): false, after drawing a loading screen with how
    // far they have got instead of the scene (TrainViewEffects.cpp).
    bool worldReady();
    void drawLoadingScreen(float done, const char *stage);
    static void redrawSoon(void *view);
    unsigned int m_loadingTexture = 0; // the dirt behind the loading screen
    ui::Gui m_gui;                     // the block-game style interface (Ui/Gui.h)
    ui::GuiRenderer m_guiRenderer;     // ... and its atlas in this GL context
    // What the view is doing, in a line the interface shows above the hotbar
    // (the camera, the selected point, the edit in progress).
    std::string m_statusText;

    GLFWwindow *m_window = nullptr;
    void *m_hwnd = nullptr; // the window's HWND (touchpad, cursor)
    int m_width = 1, m_height = 1;
    bool m_needsDraw = true;
    bool m_contextValid = false; // the textures and shaders exist in this context

  public:
    ArcBallCam arcball; // keep an ArcBall for the UI
    FlyCam flyCam;      // free-moving observer camera
    HeadLook headLook;  // where the rider looks in the Train camera
    bool headDragging;  // is a mouse drag turning the rider's head?
    int headDragX;      // last mouse position of that drag
    int headDragY;
    float m_fieldOfView; // degrees, the World, Train, Chase and Fly cameras
    // the Chase camera, followed smoothly (setProjection()): how far behind
    // the last cart along the track and how high above it, and when
    bool m_chaseValid = false;
    float m_chaseBehind = 0.0f;
    float m_chaseHeight = 0.0f;
    float m_chaseTime = 0.0f;
    bool chaseBlocked(const Pnt3f &p) const; // a block of the island or the sea floor there?
    float chaseRoom(const Pnt3f &base, const Pnt3f &up,
                    float upTo) const; // clear height above base
    bool mouseCaptured;                // does the mouse steer the observer camera?
    int selectedCube;                  // simple - just remember which cube is selected

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
    int m_editStartX = 0, m_editStartY = 0;      // mouse when the edit began
    ControlPoint m_editStart;                    // the point when the edit began
    TrackHistory::Snapshot m_editBefore;         // the track when the edit began (undo)
    bool m_editJoinsInsert = false;              // dragging a point just added: one undo step
    bool m_placing = false;                      // adding a point: waiting for a click on the track
    bool m_placeHit = false;                     // ... and the mouse is over the track
    float m_placeU = 0.0f;                       // where on the track (track parameter)
    Pnt3f m_placeSpot;                           // ... and where that is in the world
    float m_placeDepth = 30.0f;                  // Fly: a new point goes this far ahead of the eye
    bool m_carrying = false;                     // a point picked up with G in Fly
    float m_carryOffset[3] = {0.0f, 0.0f, 0.0f}; // ... where in the view (right, up, ahead)
    TrackHistory::Snapshot m_carryBefore;        // ... and the track before (undo)
    static constexpr float kPlaceDepthMin = 2.0f;
    static constexpr float kPlaceDepthMax = 1500.0f;

    // Weather, eased towards the Rain button (TrainViewEffects.cpp).
    float m_rain = 0.0f;         // 0 dry sky, 1 pouring
    float m_wetness = 0.0f;      // 0 dry ground, 1 soaked: lags behind the rain
    float m_weatherTime = -1.0f; // when the weather was last advanced
};
