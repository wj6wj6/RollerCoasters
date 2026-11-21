/************************************************************************
     File:        CallBacks.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu
     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     The actions of the menu's buttons and the hotbar
                  (GameMenu.cpp): editing the track, loading and saving
                  it, the demo ride and the texture packs. Problems are
                  shown on the menu's notice screen (GameMenu::notify).

*************************************************************************/
#pragma once

#include <cstddef>
#include <string>

class TrainWindow;

// Reset the track to its starting points, for editing one's own
void resetCB(TrainWindow *tw);

// Add Point: the next click on the track puts a new point there
void addPointCB(TrainWindow *tw);
// Delete the selected point (or the last one)
void deletePointCB(TrainWindow *tw);

// Step the train forward or back
void forwCB(TrainWindow *tw);
void backCB(TrainWindow *tw);

// Load and save the control points: TrackFiles\ beside the program, and a
// file there. False, with the reason in `error`, when it cannot be read or
// written; a failed load leaves the track as it was.
std::string trackFolder();
bool loadTrack(TrainWindow *tw, const std::string &path, std::string &error);
bool saveTrack(TrainWindow *tw, const std::string &path, std::string &error);

// Load the demo ride over the island and under the sea (see OceanWorld), ready to ride
void grandTourCB(TrainWindow *tw);

// Texture packs: use the one at this place in TrainWindow::texturePackPaths
// (0 is built in), or show the folder beside the program where downloaded
// packs go, as a block game's "Open Pack Folder" does.
void useTexturePack(TrainWindow *tw, std::size_t index);
void openTexturePackFolder(TrainWindow *tw);

// Roll the selected control point about the x or z axis, one way or the other
void rpxCB(TrainWindow *tw);
void rmxCB(TrainWindow *tw);
void rpzCB(TrainWindow *tw);
void rmzCB(TrainWindow *tw);
