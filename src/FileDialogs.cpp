/************************************************************************
     File:        FileDialogs.cpp

     Comment:     See FileDialogs.h.

*************************************************************************/

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // keep std::min / std::max usable
#endif
#include <windows.h>
#include <shellapi.h> // ShellExecuteW
#endif

// The same guard as the other FLTK includes: FLTK's headers convert between
// pointers and long (C4311, C4312); it covers these includes only.
#pragma warning(push)
#pragma warning(disable : 4311)
#pragma warning(disable : 4312)
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/filename.H>
#pragma warning(pop)

#include <filesystem>

#include "FileDialogs.h"

namespace fs = std::filesystem;

namespace FileDialogs
{

namespace
{

bool run(Fl_Native_File_Chooser &chooser, std::string &path)
{
    // 0: picked; 1: cancelled; -1: failed
    if (chooser.show() != 0 || chooser.filename() == nullptr || *chooser.filename() == '\0')
        return false;
    path = chooser.filename();
    return true;
}

} // namespace

bool open(const char *title, const char *filter, const std::string &folder, std::string &path)
{
    Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_FILE);
    chooser.title(title);
    chooser.filter(filter);
    if (!folder.empty())
        chooser.directory(folder.c_str());
    return run(chooser, path);
}

bool save(const char *title, const char *filter, const std::string &folder, const char *name,
          const char *extension, std::string &path)
{
    Fl_Native_File_Chooser chooser(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.title(title);
    chooser.filter(filter);
    chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM | Fl_Native_File_Chooser::NEW_FOLDER);
    if (!folder.empty())
        chooser.directory(folder.c_str());
    chooser.preset_file(name);
    if (!run(chooser, path))
        return false;
    if (extension && fs::u8path(path).extension().empty())
        path += extension;
    return true;
}

bool showFolder(const std::string &folder)
{
    std::error_code error;
    const fs::path where = fs::u8path(folder);
    fs::create_directories(where, error);
#ifdef _WIN32
    // Explorer, given the path as wide characters so any name works
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", where.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    const std::string uri = "file://" + where.u8string();
    return fl_open_uri(uri.c_str()) != 0;
#endif
}

std::string besideProgram(const char *name)
{
    std::error_code error;
    const fs::path here = fs::current_path(error);
    return (error ? fs::u8path(name) : here / name).u8string();
}

} // namespace FileDialogs
