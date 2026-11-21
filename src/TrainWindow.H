/************************************************************************
     File:        TrainWindow.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     
						this class defines the window in which the project 
						runs - its the outer windows that contain all of 
						the widgets, including the "TrainView" which has the 
						actual OpenGL window in which the train is drawn

						You might want to modify this class to add new widgets
						for controlling	your train

						This takes care of lots of things - including installing 
						itself into the FlTk "idle" loop so that we get periodic 
						updates (if we're running the train).


     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/
#pragma once

#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Menu_Button.H>
#pragma warning(pop)

#include <string>
#include <vector>

// we need to know what is in the world to show
#include "SceneSettings.h"
#include "Track.H"
#include "TrackHistory.h"

// other things we just deal with as pointers, to avoid circular references
class TrainView;
namespace AppControl
{
struct Options;
}

// if we're also making the sample solution, then we need to know
// about the stuff we don't tell students
#ifdef EXAMPLE_SOLUTION
#include "TrainExample/TrainExample.H"
#endif

class TrainWindow : public Fl_Double_Window
{
  public:
    TrainWindow(const int x = 50, const int y = 50);

  public:
    // call this method when things change
    void damageMe();

    // this moves the train forward on the track - its up to you to do this
    // correctly. it gets called from the idle callback loop
    // it should handle forward and backwards
    void advanceTrain(float dir = 1);

    // simple helper function to set up a button: makes it a toggle and gives
    // it the panel's on/off colours (see UiTheme.h)
    void togglify(Fl_Button *, int state = 0);

    // What the controls are set to. Everything outside the panel reads and
    // changes these; the panel's widgets follow them.
    SceneSettings settings;
    // the widgets show settings (and the Fly speed, field of view and
    // selected point); TrainView calls it before every frame
    void syncPanel();
    // a widget was used: settings take what the widgets show
    void readPanel();

    // shows the selected control point in the Transform panel's fields
    void syncTransform();

    // timer that redraws the view so the water and leaves keep moving
    static void animateEffectsCB(void *data);

    // a Transform field was edited
    static void transformCB(Fl_Widget *widget, void *data);

    // Undo and redo for the control points (TrackHistory.h). Call
    // recordHistory() just before changing the points, or pushHistory()
    // with a snapshot taken before a change that has happened.
    void recordHistory();
    void pushHistory(const TrackHistory::Snapshot &before);
    void undoEdit();
    void redoEdit();
    void updateHistoryButtons(); // greys out Undo / Redo with nothing to do

  public:
    // keep track of the stuff in the world
    CTrack m_Track;
    TrackHistory m_history; // undo and redo for the control points

    // the widgets that make up the Window
    TrainView *trainView;

    Fl_Group *widgets; // all widgets, grouped for resizing ease

    // utility buttons
    Fl_Button *resetButton;
    Fl_Button *undoButton = nullptr;
    Fl_Button *redoButton = nullptr;

    // which viewpoint are we drawing from
    Fl_Button *worldCam;
    Fl_Button *trainCam;
    Fl_Button *chaseCam; // behind the train, looking over it
    Fl_Button *topCam;
    Fl_Button *flyCam; // free-moving observer camera

    // the type of the spline (use its value to determine)
    Fl_Browser *splineBrowser;

    // position of the selected control point (Transform panel)
    Fl_Value_Input *pointX;
    Fl_Value_Input *pointY;
    Fl_Value_Input *pointZ;

    // are we animating the train?
    Fl_Button *runButton;
    // if we're animating it, how fast should it go?
    Fl_Value_Slider *speed;
    Fl_Button *arcLength; // do we use arc length for speed?

    // shader lighting and effects (see AdvancedLighting.h and PostEffects.h)
    Fl_Button *lightingButton;  // per-pixel lighting + shadow maps
    Fl_Button *shadowButton;    // draw shadows at all
    Fl_Button *headlightButton; // spotlight on the train's nose
    Fl_Value_Slider *timeOfDay; // moves the sun, 0 to 24 hours
    Fl_Button *waterButton;     // reflective ocean around the island
    Fl_Button *rainButton;      // rain: wet ground, streaks and ripples
    Fl_Button *sceneryButton;   // the island's scenery, or level grass
    // the island's textures: a button showing the pack in use whose list
    // holds "Built-in", the packs found on disk (and one opened from
    // elsewhere), "Open pack..." and "Open texturepacks folder"; see
    // TexturePack.h
    Fl_Menu_Button *texturePackMenu;
    std::vector<std::string> texturePackPaths; // for the items after "Built-in"
    std::string texturePackPath;               // the pack in use, empty when built in
    std::string texturePackLabel;              // "Textures: <name>"
    void listTexturePacks(const std::string &select = std::string());
    // Sets the scene up as the command line asks (AppControl.h): the track,
    // texture pack, camera, switches and hour. False, with a message, if a
    // file cannot be used; nothing is shown in a dialog.
    bool applyOptions(const AppControl::Options &options, std::string &error);
    Fl_Button *editButton;  // show the control points for editing
    Fl_Button *bloomButton; // bloom and god rays
    Fl_Button *ssaoButton;  // screen space ambient occlusion
    // how fast the observer camera flies, in world units per second
    Fl_Value_Slider *flySpeed;
    // the field of view of every perspective camera, in degrees
    Fl_Value_Slider *fovSlider;

    // we have other widgets as part of the sample solution
    // this is not for 559 students to know about
#ifdef EXAMPLE_SOLUTION
    ExampleWidgets ew;
#endif
};
