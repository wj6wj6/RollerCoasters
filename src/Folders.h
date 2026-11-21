/************************************************************************
     File:        Folders.h

     Comment:     The folders beside the program that the menu works with:
                  TrackFiles\ for Load and Save (listed in the game's own
                  track screens instead of the system's file dialogs) and
                  the texture packs' folder, which can be opened in the
                  file manager. Paths and names are UTF-8.

*************************************************************************/
#pragma once

#include <string>
#include <vector>

namespace Folders
{

// A folder beside the program's working folder (where main() moved to:
// the exe's folder), as a full path.
std::string besideProgram(const char *name);

// The names (not paths) of the files in `folder` whose extension is
// `extension` (".txt", any case), sorted ignoring case; none when the
// folder is missing.
std::vector<std::string> list(const std::string &folder, const char *extension);

// Can a file be saved under this name on Windows? Not empty, no folder
// separators or : * ? " < > |, no control characters, not ending in a dot or
// a space, and not a device name (CON, NUL, COM1...).
bool validName(const std::string &name);

// The name ending in `extension`, added unless it is already there in any
// case: "track" and "loop v1.2" become "track.txt" and "loop v1.2.txt", while
// "track.TXT" stays as it is (the list shows only files with the extension).
std::string withExtension(const std::string &name, const char *extension);

// Does this file exist?
bool exists(const std::string &path);

// Makes the folder (and the ones above it) if it is missing. False if it
// still is not there.
bool make(const std::string &folder);

// Opens the folder in the file manager, making it first if it is missing.
// False if it could not be shown.
bool show(const std::string &folder);

} // namespace Folders
