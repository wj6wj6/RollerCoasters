/************************************************************************
     File:        CallBacks.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu
     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     The entry point: reads the command line, opens the
                  window (TrainWindow) and runs it.

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
    tw.automated = automated;
    if (!tw.open(error))
    {
        if (automated)
            AppControl::report({{"window", false, error}}, stdout);
        else
            MessageBoxA(nullptr, error.c_str(), "Train and Roller Coaster", MB_ICONERROR);
        return AppControl::kFail;
    }
    if (!tw.applyOptions(options, error))
    {
        AppControl::report({{"options", false, error}}, stdout);
        return AppControl::kFail;
    }
    tw.trainView->startAutomation(automated ? &options : nullptr);
    tw.run();
    if (!automated)
        return AppControl::kPass;
    if (tw.trainView->automationResult < 0)
        return AppControl::kFail; // the window was closed before the check finished
    // a doctor's folder failure still fails the run
    return result != AppControl::kPass ? result : tw.trainView->automationResult;
}
