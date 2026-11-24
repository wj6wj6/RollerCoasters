/************************************************************************
     File:        TrainWindow.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     The program's window: a GLFW window filled by the 3D view
                  (TrainView), with the interface drawn over it
                  (GameMenu.h: the hotbar, the key hints and the Esc menu).
                  It owns the track, its undo history and what the controls
                  are set to (SceneSettings), runs the event loop and moves
                  the train while Run is on.

*************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "GameMenu.h"
#include "KeyBinds.h"
#include "SceneSettings.h"
#include "Track.H"
#include "TrackHistory.h"
#include "Utilities/GlfwInput.H"

class TrainView;
struct GLFWwindow;
namespace AppControl
{
struct Options;
}

class TrainWindow
{
  public:
    TrainWindow();
    ~TrainWindow();
    TrainWindow(const TrainWindow &) = delete;
    TrainWindow &operator=(const TrainWindow &) = delete;

    // Makes the window and its OpenGL context, not shown yet. False, with a
    // message, when there is no usable OpenGL.
    bool open(std::string &error);
    // Shows the window and runs until it is closed.
    void run();
    // Asks the loop to end (Quit, the automated runs).
    void close();

    // call this method when things change
    void damageMe();

    // this moves the train forward on the track - its up to you to do this
    // correctly. it gets called from the idle callback loop
    // it should handle forward and backwards
    // Moves the train dir steps along the track (fractions too).
    void advanceTrain(float dir = 1);

    // Switches the camera and lets the view take over (Fly starts where
    // the last view was seen from).
    void setCamera(CameraView camera);
    // Moves the selected control point a unit along axis 0, 1 or 2.
    void nudgeSelected(int axis, float units);

    // Undo and redo for the control points (TrackHistory.h). Call
    // recordHistory() just before changing the points, or pushHistory()
    // with a snapshot taken before a change that has happened.
    void recordHistory();
    void pushHistory(const TrackHistory::Snapshot &before);
    void undoEdit();
    void redoEdit();

    // Sets the scene up as the command line asks (AppControl.h): the track,
    // texture pack, camera, switches, hour and window size. False, with a
    // message, if a file cannot be used; nothing is shown in a dialog.
    bool applyOptions(const AppControl::Options &options, std::string &error);

    // The texture packs to choose from: "Built-in" (path ""), the packs
    // found on disk and one opened from elsewhere (`select`, if not found).
    void listTexturePacks(const std::string &select = std::string());

    // After Options > Controls changed a key: the Fly camera takes the new
    // keys and they are kept in data/keybinds.txt for the next run.
    void keysChanged();

    // TrainView::draw() calls it after the scene: the hotbar, hints and menu.
    void drawInterface();

    // Fullscreen, as a block game's F11 and its Fullscreen option: the
    // whole monitor the window is mostly on, at the monitor's own mode, and
    // back to the window where and as big as it was (maximized again if it
    // was). The switch happens in the loop, outside drawing and callbacks;
    // fullscreen() already answers with the switch asked for.
    bool fullscreen() const;
    void setFullscreen(bool on);

  public:
    CTrack m_Track;
    TrackHistory m_history; // undo and redo for the control points
    // What the controls are set to; the view and the menu read and change it.
    SceneSettings settings;
    // Which key does what (Options > Controls); read from data/keybinds.txt.
    KeyBinds keys;

    TrainView *trainView;
    GameMenu menu;

    std::vector<std::string> texturePackNames; // "Built-in" first
    std::vector<std::string> texturePackPaths; // the same order; "" for built in
    std::string texturePackPath;               // the pack in use, empty when built in
    std::string texturePackNote;               // how much of the island the pack covers

    // The train's speed setting is how far it goes in one step of this many
    // a second. It moves every frame, by the time since the last one, so the
    // pace is the same at any frame rate.
    static constexpr double kStepsPerSecond = 30.0;

    GLFWwindow *window = nullptr;
    // --doctor and --screenshot: the frames show the scene without the hotbar and hints
    bool automated = false;

  private:
    static void pumpEvents(double seconds);
    void input(const InputEvent &e);
    void resized(int width, int height);
    void drawNow();
    void switchFullscreen(); // does what setFullscreen() asked for
    // Lock Windows Key is on and the world is played (in front, the scene
    // shown, no menu open): the Windows key does nothing then (WindowsKey.h).
    bool locksWindowsKey() const;

    static void keyCallback(GLFWwindow *w, int key, int scancode, int action, int mods);
    static void buttonCallback(GLFWwindow *w, int button, int action, int mods);
    static void cursorCallback(GLFWwindow *w, double x, double y);
    static void scrollCallback(GLFWwindow *w, double dx, double dy);
    static void focusCallback(GLFWwindow *w, int focused);
    static void enterCallback(GLFWwindow *w, int entered);
    static void charCallback(GLFWwindow *w, unsigned int codepoint);
    static void sizeCallback(GLFWwindow *w, int width, int height);
    static void refreshCallback(GLFWwindow *w);
    static void dropCallback(GLFWwindow *w, int count, const char **paths);

    GlfwInput m_input;
    double m_lastAdvance = 0.0; // when the train last moved
    bool m_running = false;     // the train was running at the last frame
    double m_lastDraw = 0.0;    // when the last frame was drawn (F3 refreshes)
    int m_fullscreenAsked = -1; // 1 or 0 until the loop switches; -1 nothing asked
    bool m_fullscreen = false;  // the borderless window covers the screen
    // the window as it was before fullscreen, to go back to
    int m_windowedX = 0, m_windowedY = 0;
    int m_windowedWidth = 0, m_windowedHeight = 0;
    bool m_windowedMaximized = false;
};
