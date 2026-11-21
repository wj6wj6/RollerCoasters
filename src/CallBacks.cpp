/************************************************************************
     File:        CallBacks.cpp

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu
     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     See CallBacks.H.

*************************************************************************/

#include <cmath>

#include "TrainWindow.H"
#include "TrainView.H"
#include "CallBacks.H"
#include "TrackInsert.h"
#include "OceanWorld.h"
#include "Folders.h"
#include "LandTextures.h"
#include "TexturePack.h"

#include <memory>
#include <string>

namespace
{
const float kEighthTurn = 0.785398163f; // R+X and the others tilt by 45 degrees
}

//***************************************************************************
//
// * Reset the control points back to their base setup
//===========================================================================
void resetCB(TrainWindow *tw)
//===========================================================================
{
    tw->recordHistory();
    tw->m_Track.resetPoints();
    tw->trainView->selectedCube = -1;
    tw->m_Track.trainU = 0;
    // a track of one's own is edited on the level island, seen from outside
    tw->settings.scenery = false;
    tw->settings.edit = true;
    tw->settings.run = false;
    tw->setCamera(CameraView::World);
}

//***************************************************************************
//
// * Add point (hotbar slot 7): the next click on the track puts a new point there, between
//   the two points around the spot, and picks it up to drag (TrainView::
//   beginPlacePoint, TrackInsert.h). Esc or a right click gives up.
//===========================================================================
void addPointCB(TrainWindow *tw)
//===========================================================================
{
    tw->trainView->beginPlacePoint();
}

//***************************************************************************
//
// * Callback that deletes a point from the spline
//===========================================================================
void deletePointCB(TrainWindow *tw)
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
void forwCB(TrainWindow *tw)
{
    tw->advanceTrain(2);
    tw->damageMe();
}
//***************************************************************************
//
// * Reverse the movement of the train
//===========================================================================
void backCB(TrainWindow *tw)
//===========================================================================
{
    tw->advanceTrain(-2);
    tw->damageMe();
}

//***************************************************************************
//
// * Load and save the control points (the menu's Load Track and Save Track
//   screens). A file that cannot be read leaves the track as it was.
//===========================================================================
std::string trackFolder()
{
    return Folders::besideProgram("TrackFiles");
}

bool loadTrack(TrainWindow *tw, const std::string &path, std::string &error)
{
    CTrack loaded = tw->m_Track;
    if (!loaded.readPoints(path.c_str(), error))
        return false;
    tw->recordHistory();
    tw->m_Track = loaded;
    tw->trainView->selectedCube = -1;
    tw->damageMe();
    return true;
}

bool saveTrack(TrainWindow *tw, const std::string &path, std::string &error)
{
    Folders::make(trackFolder());
    return tw->m_Track.writePoints(path.c_str(), error);
}
//***************************************************************************
//
// * Replace the track with the demo ride, ready to go (Run starts it): the
//   scenery and the shader effects on, seen from the train, starting where
//   the ride comes ashore so the island is the first thing in view
//===========================================================================
void grandTourCB(TrainWindow *tw)
//===========================================================================
{
    const std::vector<float> xyz = OceanWorld::demoTrack();
    tw->recordHistory();
    tw->m_Track.points.clear();
    for (size_t i = 0; i + 2 < xyz.size(); i += 3)
        tw->m_Track.points.push_back(ControlPoint(Pnt3f(xyz[i], xyz[i + 1], xyz[i + 2])));
    tw->m_Track.trainU = static_cast<float>(OceanWorld::landStart()) - 1.3f;
    tw->trainView->selectedCube = -1;
    tw->settings.lighting = true;
    tw->settings.water = true;
    tw->settings.scenery = true;
    tw->settings.edit = false;
    tw->settings.spline = TRACK_SPLINE_BSPLINE;
    tw->settings.arcLength = true;
    tw->settings.speed = 1.8f; // the world is larger: a little faster round it
    // the World camera frames the whole scenery island when switched to
    tw->trainView->resetArcball();
    tw->setCamera(CameraView::Train);
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
        float si = std::sin(kEighthTurn * dir);
        float co = std::cos(kEighthTurn * dir);
        tw->m_Track.points[s].orient.y = co * old.y - si * old.z;
        tw->m_Track.points[s].orient.z = si * old.y + co * old.z;
    }
    tw->damageMe();
}

//***************************************************************************
//
// * Rotate the selected control point about x axis by one more degree
//===========================================================================
void rpxCB(TrainWindow *tw)
//===========================================================================
{
    rollx(tw, 1);
}
//***************************************************************************
//
// * Rotate the selected control point  about x axis by less one degree
//===========================================================================
void rmxCB(TrainWindow *tw)
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

        float si = std::sin(kEighthTurn * dir);
        float co = std::cos(kEighthTurn * dir);

        tw->m_Track.points[s].orient.y = co * old.y - si * old.x;
        tw->m_Track.points[s].orient.x = si * old.y + co * old.x;
    }

    tw->damageMe();
}

//***************************************************************************
//
// * Rotate the selected control point  about the z axis one more degree
//===========================================================================
void rpzCB(TrainWindow *tw)
//===========================================================================
{
    rollz(tw, 1);
}

//***************************************************************************
//
// *  Rotate the selected control point  about the z axis one less degree
//===========================================================================
void rmzCB(TrainWindow *tw)
//===========================================================================
{
    rollz(tw, -1);
}

//************************************************************************
//
// * Texture packs: put one in place for the island and the sea floor, or
//   the built-in textures for an empty path. A pack that cannot be read is
//   reported and the one in use stays.
//========================================================================
static void usePackAt(TrainWindow *tw, const std::string &path)
{
    std::shared_ptr<TexturePack::Pack> pack;
    if (!path.empty())
    {
        pack = std::make_shared<TexturePack::Pack>();
        if (!pack->open(path))
        {
            tw->menu.notify(*tw, "Not a texture pack",
                            "No textures this program can use were found in\n" + path);
            return;
        }
    }
    tw->texturePackPath = path;
    tw->trainView->setTexturePack(pack);
    tw->listTexturePacks(path);
    // how much of the island the pack covers
    if (pack)
        tw->texturePackNote = std::to_string(pack->coverage()) + " of " +
                              std::to_string(LandTextures::TEXTURE_COUNT) +
                              " textures from the pack";
    else
        tw->texturePackNote.clear();
    tw->damageMe();
}

void useTexturePack(TrainWindow *tw, std::size_t index)
{
    if (index < tw->texturePackPaths.size())
        usePackAt(tw, tw->texturePackPaths[index]);
}

bool addTexturePack(TrainWindow *tw, const std::string &path, std::string &name, std::string &error)
{
    TexturePack::Pack pack;
    if (!pack.open(path))
    {
        error = "not a track file (.txt) or a texture pack";
        return false;
    }
    const std::string copied = Folders::copyInto(path, TexturePack::searchFolders().front(), error);
    if (copied.empty())
        return false;
    tw->listTexturePacks(tw->texturePackPath);
    // listed as findIn() names it: a .zip without its extension
    const bool zip = copied.size() > 4 && Folders::withExtension(copied, ".zip") == copied;
    name = zip ? copied.substr(0, copied.size() - 4) : copied;
    return true;
}

void openTexturePackFolder(TrainWindow *tw)
{
    const std::string folder = TexturePack::searchFolders().front();
    if (!Folders::show(folder))
        tw->menu.notify(*tw, "Texture packs", "Put texture packs (.zip, or folders) in\n" + folder);
    tw->listTexturePacks(tw->texturePackPath);
}
