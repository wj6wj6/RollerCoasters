/************************************************************************
     File:        CallBacks.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu
     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     
						Programming Assignment 3 - Trains and Roller Coasters

						The "main file" and entry point for the train project
						this basically opens up the window and starts up the world

	  Note:			this code is meant to serve as both the example solution, 
						as well as the starting point for student projects. 
						Therefore, some (questionable)
						design tradeoffs have been made - please see the 
						ReadMe-Initial.txt file

     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/

#include "stdio.h"
#include <cstring>
#include <cwchar>
#include "TrainWindow.H"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#pragma warning(push)
#pragma warning(disable : 4312)
#pragma warning(disable : 4311)
#include <FL/Fl.H>
#pragma warning(pop)

// Load and Save default to TrackFiles\ beside the exe, however it was started
// (Visual Studio, a double click, a terminal elsewhere). The build creates the
// folder; it is made again here in case it was deleted.
static void useExeFolderForTrackFiles()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash)
        return;
    *slash = L'\0';
    if (SetCurrentDirectoryW(path))
        CreateDirectoryW(L"TrackFiles", nullptr);
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0)
    {
        printf("RollerCoasters %s\n", ROLLERCOASTERS_VERSION);
        return 0;
    }
    if (argc != 1)
    {
        fprintf(stderr, "Usage: RollerCoasters [--version]\n");
        return 2;
    }
    printf("CS559 Train Assignment\n");
    useExeFolderForTrackFiles();

    TrainWindow tw;
    tw.show();

    Fl::run();
}
