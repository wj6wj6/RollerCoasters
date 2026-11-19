/************************************************************************
     File:        FileDialogs.h

     Comment:     The system's own file dialogs (on Windows the Explorer
                  style Open and Save As windows), for loading and saving
                  track files and opening texture packs, and opening a
                  folder in the file manager. Paths are UTF-8.

*************************************************************************/
#pragma once

#include <string>

namespace FileDialogs
{

// Asks for a file to open. filter: "Name\tPattern" lines, for example
// "Track files\t*.txt\nAll files\t*". folder: where the dialog starts.
// False when cancelled.
bool open(const char *title, const char *filter, const std::string &folder, std::string &path);

// Asks for a file to save, starting in `folder` with `name` filled in; asks
// before replacing a file and adds `extension` (".txt") when the name has
// none. False when cancelled.
bool save(const char *title, const char *filter, const std::string &folder, const char *name,
          const char *extension, std::string &path);

// Opens the folder in the file manager, making it first if it is missing.
// False if it could not be shown.
bool showFolder(const std::string &folder);

// A folder beside the program's working folder (where main() moved to:
// the exe's folder), as a full path.
std::string besideProgram(const char *name);

} // namespace FileDialogs
