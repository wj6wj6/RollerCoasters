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
#include "OceanWorld.h"
#include "FileDialogs.h"
#include "LandTextures.h"
#include "TexturePack.h"

#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/fl_ask.H>
#include <FL/math.h>
#pragma warning(pop)

#include <memory>
#include <string>

//***************************************************************************
//
// * Reset the control points back to their base setup
//===========================================================================
void resetCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
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
// * Callback that adds a new point to the spline
// idea: add the point AFTER the selected point
//===========================================================================
void addPointCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    // get the number of points
    size_t npts = tw->m_Track.points.size();
    if (npts < 4)
        return;

    // Insert after the selected point. Without a selection, insert at the end
    // of the loop between the last and first control points.
    int selected = tw->trainView->selectedCube;
    size_t selectedIndex = (selected >= 0 && static_cast<size_t>(selected) < npts)
                               ? static_cast<size_t>(selected)
                               : npts - 1;
    size_t nextIndex = (selectedIndex + 1) % npts;
    size_t insertIndex = selectedIndex + 1;

    // pick a reasonable location
    Pnt3f npos = (tw->m_Track.points[selectedIndex].pos + tw->m_Track.points[nextIndex].pos) * .5f;

    tw->m_Track.points.insert(tw->m_Track.points.begin() + insertIndex, npos);

    // make it so that the train doesn't move - unless its affected by this control point
    // it should stay between the same points
    if (tw->m_Track.trainU >= static_cast<float>(insertIndex))
    {
        tw->m_Track.trainU += 1;
        if (tw->m_Track.trainU >= static_cast<float>(npts))
            tw->m_Track.trainU -= static_cast<float>(npts);
    }

    tw->damageMe();
}

//***************************************************************************
//
// * Callback that deletes a point from the spline
//===========================================================================
void deletePointCB(Fl_Widget *, TrainWindow *tw)
//===========================================================================
{
    if (tw->m_Track.points.size() > 4)
    {
        if (tw->trainView->selectedCube >= 0)
        {
            tw->m_Track.points.erase(tw->m_Track.points.begin() + tw->trainView->selectedCube);
        }
        else
            tw->m_Track.points.pop_back();
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
    std::string path;
    if (FileDialogs::open("Load a track", "Track files (*.txt)\t*.txt\nAll files\t*",
                          FileDialogs::besideProgram("TrackFiles"), path))
    {
        tw->m_Track.readPoints(path.c_str());
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
    std::string path;
    if (FileDialogs::save("Save the track", "Track files (*.txt)\t*.txt",
                          FileDialogs::besideProgram("TrackFiles"), "track.txt", ".txt", path))
        tw->m_Track.writePoints(path.c_str());
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

//************************************************************************
//
// * The texture pack list: "Built-in" or a pack, read and put in place;
//   "Open pack..." chooses a .zip (or a folder's pack.mcmeta) anywhere in
//   the system's file dialog; "Open texturepacks folder" shows the folder
//   beside the program where downloaded packs go. A pack that cannot be
//   read is reported and the list stays on what was in use.
//========================================================================
void texturePackCB(Fl_Widget *, TrainWindow *tw)
//========================================================================
{
    std::string &current = tw->texturePackPath; // the path in use, empty when built in
    const int item = tw->texturePackMenu->value();
    const int packs = 1 + static_cast<int>(tw->texturePackPaths.size()); // with "Built-in"
    const std::string folder = TexturePack::searchFolders().front();
    std::string path;
    if (item == packs + 1)
    {
        if (!FileDialogs::showFolder(folder))
            fl_alert("Put texture packs (.zip, or folders) in\n%s", folder.c_str());
        tw->listTexturePacks(current);
        return;
    }
    if (item == packs)
    {
        if (!FileDialogs::open("Open a texture pack",
                               "Texture packs (*.zip)\t*.zip\n"
                               "A pack in a folder (pack.mcmeta)\tpack.mcmeta",
                               folder, path))
        {
            tw->listTexturePacks(current);
            return;
        }
    }
    else if (item > 0 && item < packs)
        path = tw->texturePackPaths[static_cast<size_t>(item - 1)];

    std::shared_ptr<TexturePack::Pack> pack;
    if (!path.empty())
    {
        pack = std::make_shared<TexturePack::Pack>();
        if (!pack->open(path))
        {
            fl_alert("No textures this program can use were found in\n%s", path.c_str());
            tw->listTexturePacks(current);
            return;
        }
    }
    current = path;
    tw->trainView->setTexturePack(pack);
    tw->listTexturePacks(current);
    // the tooltip says how much of the island the pack covers
    static std::string tip;
    if (pack)
        tip = pack->name() + ": " + std::to_string(pack->coverage()) + " of " +
              std::to_string(LandTextures::TEXTURE_COUNT) +
              " textures from the pack, the rest built in.";
    else
        tip = "The island's block textures, drawn in the program.\n"
              "Pick a texture pack in this list to use its textures.";
    tw->texturePackMenu->tooltip(tip.c_str());
    tw->damageMe();
}
