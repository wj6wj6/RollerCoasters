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
#include <filesystem>
#include <string>
#include <vector>
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
#include <shellapi.h> // CommandLineToArgvW

// On a laptop with two graphics cards, ask for the faster one, as games do:
// the NVIDIA and AMD drivers look for these exported names in the exe.
// Measured on an AMD 860M + RTX 5060 laptop: about twice the frame rate.
extern "C"
{
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

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
    std::error_code error;
    const std::filesystem::path full =
        std::filesystem::absolute(std::filesystem::u8path(path), error);
    if (!error)
        path = full.u8string();
}

// The command line in UTF-8, as every path in the program is (Utf8File.H):
// main()'s argv is in the ANSI code page, which cannot hold every name.
static std::vector<std::string> utf8Arguments(int argc, char **argv)
{
    std::vector<std::string> args;
    int count = 0;
    LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide && count == argc)
        for (int i = 0; i < count; ++i)
            args.push_back(std::filesystem::path(wide[i]).u8string());
    else
        args.assign(argv, argv + argc); // not expected; the ANSI words as they are
    if (wide)
        LocalFree(wide);
    return args;
}

int main(int argc, char **argv)
{
    AppControl::Options options;
    std::string error;
    std::vector<std::string> args = utf8Arguments(argc, argv);
    std::vector<char *> argPointers;
    for (std::string &arg : args)
        argPointers.push_back(arg.data());
    if (!AppControl::parse(static_cast<int>(argPointers.size()), argPointers.data(), options,
                           error))
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
    // A person starts on the title screen; a check goes straight to the ride,
    // with a game menu screen open over it when --menu asks for one.
    if (!automated)
        tw.menu.show(tw, GameMenu::Screen::Title);
    else if (options.menu != AppControl::Menu::None)
    {
        static const GameMenu::Screen kScreens[] = {
            GameMenu::Screen::None,   GameMenu::Screen::Pause,   GameMenu::Screen::Ride,
            GameMenu::Screen::Camera, GameMenu::Screen::Track,   GameMenu::Screen::Lighting,
            GameMenu::Screen::World,  GameMenu::Screen::Controls};
        tw.menu.show(tw, GameMenu::Screen::None); // riding: Back leads to the game menu
        tw.menu.show(tw, kScreens[static_cast<int>(options.menu)]);
    }
    tw.run();
    if (!automated)
        return AppControl::kPass;
    if (tw.trainView->automationResult < 0)
        return AppControl::kFail; // the window was closed before the check finished
    // a doctor's folder failure still fails the run
    return result != AppControl::kPass ? result : tw.trainView->automationResult;
}
