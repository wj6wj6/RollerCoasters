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
#include <string>
#include "AppControl.h"
#include "TrainView.H"
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

// Paths on the command line are relative to where the program was started,
// not to the exe's folder it moves to.
static void makeAbsolute(std::string &path)
{
    if (path.empty())
        return;
    char full[MAX_PATH] = {};
    const DWORD length = GetFullPathNameA(path.c_str(), MAX_PATH, full, nullptr);
    if (length > 0 && length < MAX_PATH)
        path = full;
}

int main(int argc, char **argv)
{
    AppControl::Options options;
    std::string error;
    if (!AppControl::parse(argc, argv, options, error))
    {
        fprintf(stderr, "%s\n%s", error.c_str(), AppControl::usage());
        return AppControl::kUsage;
    }
    if (options.mode == AppControl::Mode::Version)
    {
        printf("RollerCoasters %s\n", ROLLERCOASTERS_VERSION);
        return AppControl::kPass;
    }
    if (options.mode == AppControl::Mode::Help)
    {
        printf("%s", AppControl::usage());
        return AppControl::kPass;
    }
    const bool automated = options.mode != AppControl::Mode::Gui;
    makeAbsolute(options.output);
    makeAbsolute(options.track);
    makeAbsolute(options.texturePack);
    if (!automated)
        printf("CS559 Train Assignment\n");
    useExeFolderForTrackFiles();

    int result = AppControl::kPass;
    if (options.mode == AppControl::Mode::Doctor)
        result = AppControl::report(AppControl::environmentChecks(), stdout);

    TrainWindow tw;
    if (!tw.applyOptions(options, error))
    {
        AppControl::report({{"options", false, error}}, stdout);
        return AppControl::kFail;
    }
    tw.trainView->startAutomation(automated ? &options : nullptr);
    tw.show();

    Fl::run();
    if (!automated)
        return AppControl::kPass;
    if (tw.trainView->automationResult < 0)
        return AppControl::kFail; // the window was closed before the check finished
    // a doctor's folder failure still fails the run
    return result != AppControl::kPass ? result : tw.trainView->automationResult;
}
