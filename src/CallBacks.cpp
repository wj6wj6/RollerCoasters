/************************************************************************
     File:        CallBacks.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu
     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     Header file to define callback functions.
						define the callbacks for the TrainWindow

						these are little functions that get called when the 
						various widgets
						get accessed (or the fltk timer ticks). these 
						functions are used 
						when TrainWindow sets itself up.

     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/

#include <time.h>
#include <math.h>

#include "TrainWindow.H"
#include "TrainView.H"
#include "CallBacks.H"
#include "TrackInsert.h"
#include "OceanWorld.h"

#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/Fl_File_Chooser.H>
#include <FL/math.h>
#pragma warning(pop)

//***************************************************************************
//
// * Reset the control points back to their base setup
//===========================================================================
void resetCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    tw->recordHistory();
    tw->m_Track.resetPoints();
    tw->trainView->selectedCube = -1;
    tw->m_Track.trainU = 0;
    // a track of one's own is edited on the level island, seen from outside
    tw->sceneryButton->value(0);
    tw->editButton->value(1);
    tw->runButton->value(0);
    tw->worldCam->setonly();
    tw->trainView->cameraChanged();
    tw->damageMe();
}

//***************************************************************************
//
// * any time something changes, you need to force a redraw
//===========================================================================
void damageCB(Fl_Widget *, TrainWindow *tw)
{
    tw->damageMe();
}

//***************************************************************************
//
// * The camera radio buttons changed
//===========================================================================
void cameraCB(Fl_Widget *, TrainWindow *tw)
{
    tw->trainView->cameraChanged();
    tw->damageMe();
}

//***************************************************************************
//
// * The observer camera speed slider moved
//===========================================================================
void flySpeedCB(Fl_Widget *, TrainWindow *tw)
{
    tw->trainView->flyCam.setSpeed(static_cast<float>(tw->flySpeed->value()));
}

//***************************************************************************
//
// * The observer camera field of view slider moved
//===========================================================================
void fovCB(Fl_Widget *, TrainWindow *tw)
{
    tw->trainView->setFieldOfView(static_cast<float>(tw->fovSlider->value()));
}

//***************************************************************************
//
// * Add Pt: the next click on the track puts a new point there, between
//   the two points around the spot, and picks it up to drag (TrainView::
//   beginPlacePoint, TrackInsert.h). Esc or a right click gives up.
//===========================================================================
void addPointCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    tw->trainView->beginPlacePoint();
}

//***************************************************************************
//
// * Undo and Redo: step back and forward through the changes to the points
//===========================================================================
void undoCB(Fl_Widget *, TrainWindow *tw)
{
    tw->undoEdit();
}

void redoCB(Fl_Widget *, TrainWindow *tw)
{
    tw->redoEdit();
}

//***************************************************************************
//
// * Callback that deletes a point from the spline
//===========================================================================
void deletePointCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    // the selected point, or the last one; the train stays about where it was
    const int count = static_cast<int>(tw->m_Track.points.size());
    const int selected = tw->trainView->selectedCube;
    const int index = (selected >= 0 && selected < count) ? selected : count - 1;
    const TrackHistory::Snapshot before = TrackHistory::capture(tw->m_Track, selected);
    if (TrackInsert::removeAt(tw->m_Track, index))
    {
        tw->pushHistory(before);
        if (selected == index)
            tw->trainView->selectedCube = -1;
    }
    tw->damageMe();
}
//***************************************************************************
//
// * Advancing the train
//===========================================================================
void forwCB(Fl_Widget *, TrainWindow *tw)
{
    tw->advanceTrain(2);
    tw->damageMe();
}
//***************************************************************************
//
// * Reverse the movement of the train
//===========================================================================
void backCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    tw->advanceTrain(-2);
    tw->damageMe();
}

static unsigned long lastRedraw = 0;
//***************************************************************************
//
// * Callback for idling - if things are sitting, this gets called
// if the run button is pushed, then we need to make the train go.
// This is taken from the old "RunButton" demo.
// another nice problem to have - most likely, we'll be too fast
// don't draw more than 30 times per second
//===========================================================================
void runButtonCB(TrainWindow *tw)
//===========================================================================
{
    if (tw->runButton->value())
    { // only advance time if appropriate
        if (clock() - lastRedraw > CLOCKS_PER_SEC / 30)
        {
            lastRedraw = clock();
            tw->advanceTrain();
            tw->damageMe();
        }
    }
}

//***************************************************************************
//
// * Load the control points from the files
//===========================================================================
void loadCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    const char *fname = fl_file_chooser("Pick a Track File", "*.txt", "TrackFiles/track.txt");
    if (fname)
    {
        tw->recordHistory();
        tw->m_Track.readPoints(fname);
        tw->damageMe();
    }
}
//***************************************************************************
//
// * Replace the track with the demo ride, ready to go (Run starts it): the
//   scenery and the shader effects on, seen from the train, starting where
//   the ride comes ashore so the island is the first thing in view
//===========================================================================
void grandTourCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    const std::vector<float> xyz = OceanWorld::demoTrack();
    tw->recordHistory();
    tw->m_Track.points.clear();
    for (size_t i = 0; i + 2 < xyz.size(); i += 3)
        tw->m_Track.points.push_back(ControlPoint(Pnt3f(xyz[i], xyz[i + 1], xyz[i + 2])));
    tw->m_Track.trainU = static_cast<float>(OceanWorld::landStart()) - 1.3f;
    tw->trainView->selectedCube = -1;
    tw->lightingButton->value(1);
    tw->waterButton->value(1);
    tw->sceneryButton->value(1);
    tw->editButton->value(0);
    tw->splineBrowser->select(3);
    tw->arcLength->value(1);
    tw->speed->value(1.8); // the world is larger: a little faster round it
    tw->trainCam->setonly();
    // the World camera frames the whole scenery island when switched to
    tw->trainView->resetArcball();
    tw->trainView->cameraChanged();
    tw->damageMe();
}

//***************************************************************************
//
// * Save the control points
//===========================================================================
void saveCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    const char *fname = fl_input("File name for save (should be *.txt)", "TrackFiles/");
    if (fname)
        tw->m_Track.writePoints(fname);
}

//***************************************************************************
//
// * Rotate the selected control point about x axis
//===========================================================================
void rollx(TrainWindow *tw, float dir)
{
    int s = tw->trainView->selectedCube;
    if (s >= 0)
    {
        tw->recordHistory();
        Pnt3f old = tw->m_Track.points[s].orient;
        float si = sinf(((float)M_PI_4) * dir);
        float co = cosf(((float)M_PI_4) * dir);
        tw->m_Track.points[s].orient.y = co * old.y - si * old.z;
        tw->m_Track.points[s].orient.z = si * old.y + co * old.z;
    }
    tw->damageMe();
}

//***************************************************************************
//
// * Rotate the selected control point about x axis by one more degree
//===========================================================================
void rpxCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    rollx(tw, 1);
}
//***************************************************************************
//
// * Rotate the selected control point  about x axis by less one degree
//===========================================================================
void rmxCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    rollx(tw, -1);
}

//***************************************************************************
//
// * Rotate the selected control point  about z axis
//===========================================================================
void rollz(TrainWindow *tw, float dir)
//===========================================================================
{
    int s = tw->trainView->selectedCube;
    if (s >= 0)
    {
        tw->recordHistory();

        Pnt3f old = tw->m_Track.points[s].orient;

        float si = sinf(((float)M_PI_4) * dir);
        float co = cosf(((float)M_PI_4) * dir);

        tw->m_Track.points[s].orient.y = co * old.y - si * old.x;
        tw->m_Track.points[s].orient.x = si * old.y + co * old.x;
    }

    tw->damageMe();
}

//***************************************************************************
//
// * Rotate the selected control point  about the z axis one more degree
//===========================================================================
void rpzCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    rollz(tw, 1);
}

//***************************************************************************
//
// *  Rotate the selected control point  about the z axis one less degree
//===========================================================================
void rmzCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    rollz(tw, -1);
}
