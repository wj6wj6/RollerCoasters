/************************************************************************
     File:        GameMenu.h

     Comment:     The program's interface, drawn over the scene in the
                  style of a block game (Ui/Gui.h):

                  - the title screen the program starts on, as a block
                    game's: the name in big blocks over the world turning
                    slowly behind (or the loading dirt while it is built),
                    Play, Load Track..., Options... and Quit; Play
                    lists the worlds to pick from, as Singleplayer does;
                  - the hotbar at the bottom: the five cameras, Edit, Add
                    and Delete point, Run / Stop; keys 1 to 9 or a click;
                  - key hints in the top left corner, and the view's
                    status line above the hotbar; F1 hides all of it;
                  - F3 switches the performance screen on and off
                    (PerfOverlay.h), in place of the key hints, as a block
                    game's debug screen;
                  - Esc opens the game menu, whose screens (Ride, Camera,
                    Track, Lighting, World) hold every control the old side
                    panel had. Esc goes back a screen;
                  - Controls lists every keyboard command with its key and
                    a second key, as a block game's Key Binds: click a key,
                    press the new one (Esc leaves none); clashing keys are
                    red; Reset and Reset All bring the defaults back
                    (KeyBinds.h). The riding keys here (hotbar, P, [ ], F1)
                    and the view's keys go by those binds;
                  - Load Track and Save Track list the tracks in
                    TrackFiles\ (a block game's world list, not the
                    system's file dialogs), and a notice or a yes / no
                    question takes the place of a message box;
                  - files dropped on the window are added as a block game
                    adds dropped packs: track files (.txt) are copied into
                    TrackFiles\ and picked in Load Track, texture packs
                    into the packs' folder and listed in World. The lists
                    are read again when the window comes back to the
                    front, so files put in the folders show up.

                  It changes TrainWindow::settings and calls the same
                  actions (CallBacks.H) the panel did. Every frame draw()
                  lays the interface out again, so it always shows the
                  settings as they are, whoever changed them.

*************************************************************************/
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Ui/Gui.h"
#include "Utilities/Input.H"

class TrainWindow;

class GameMenu
{
  public:
    enum class Screen
    {
        None,        // riding: only the hotbar and the hints
        Title,       // before the ride: the program starts here
        Options,     // from the title: the settings screens
        SelectWorld, // from the title's Play: the island or the flat grass
        Pause,
        Ride,
        Camera,
        Track,
        Lighting,
        World,
        Controls, // the key binds, from Options and the game menu
        LoadTrack,
        SaveTrack,
        Notice, // notify(): a message and OK
        Confirm // confirm(): a question, Yes and No
    };
    static constexpr int kSlots = 9;

    Screen screen() const
    {
        return m_screen;
    }
    bool open() const
    {
        return m_screen != Screen::None;
    }
    void show(TrainWindow &tw, Screen screen);
    // Esc: a settings screen goes back to the game menu (or to Options before
    // the ride), the game menu to the ride. The title stays.
    void back(TrainWindow &tw);

    // An event before the view sees it: true when the interface took it.
    bool input(TrainWindow &tw, const InputEvent &e);
    // Controls is waiting for the new key of a command: every key goes there,
    // Fullscreen's too.
    bool pickingKey() const
    {
        return m_pickAction >= 0;
    }
    // Lay out and draw the interface for this frame, acting on the clicks
    // fed since the last one.
    void draw(TrainWindow &tw, ui::Gui &gui, double seconds);

    // What a hotbar slot (0 to 8) does, by key or click.
    void useSlot(TrainWindow &tw, int slot, double seconds);

    // A message over everything, with OK to go back to where the menu was
    // (or to the ride). Lines are split at '\n'.
    void notify(TrainWindow &tw, const std::string &title, const std::string &text);
    // A question: Yes runs `yes`, No (or Esc) goes back to where the menu was.
    void confirm(TrainWindow &tw, const std::string &question,
                 std::function<void(TrainWindow &)> yes);

    // Files dropped on the window (UTF-8 paths): tracks to Load Track,
    // texture packs to World, a notice for anything else.
    void dropped(TrainWindow &tw, const std::vector<std::string> &paths);

    bool hudHidden = false; // F1
    bool perfShown = false; // F3: the performance screen (PerfOverlay.h)

  private:
    void drawHud(TrainWindow &tw, ui::Gui &gui, double seconds);
    void drawPerf(TrainWindow &tw, ui::Gui &gui);
    void drawTitle(TrainWindow &tw, ui::Gui &gui, double seconds);
    void drawOptions(TrainWindow &tw, ui::Gui &gui);
    void drawSelectWorld(TrainWindow &tw, ui::Gui &gui);
    void playWorld(TrainWindow &tw, int world);
    void drawPause(TrainWindow &tw, ui::Gui &gui);
    void drawRide(TrainWindow &tw, ui::Gui &gui);
    void drawCamera(TrainWindow &tw, ui::Gui &gui);
    void drawTrack(TrainWindow &tw, ui::Gui &gui);
    void drawLighting(TrainWindow &tw, ui::Gui &gui);
    void drawWorld(TrainWindow &tw, ui::Gui &gui);
    void drawControls(TrainWindow &tw, ui::Gui &gui);
    bool pickKey(TrainWindow &tw, const InputEvent &e); // a key for the command being set
    void setPickedKey(TrainWindow &tw, int key, int mods);
    void drawLoadTrack(TrainWindow &tw, ui::Gui &gui);
    void drawSaveTrack(TrainWindow &tw, ui::Gui &gui);
    void drawNotice(TrainWindow &tw, ui::Gui &gui);
    void drawConfirm(TrainWindow &tw, ui::Gui &gui);
    void markSaveName(); // select the listed track that m_saveName would save over
    // read TrackFiles\ again, keeping the track picked (or picking `pick`)
    void readTracks(const std::string &pick = std::string());
    void readFolders(TrainWindow &tw); // the list on screen, from its folder again
    void loadSelected(TrainWindow &tw);
    void save(TrainWindow &tw);
    void say(const std::string &text); // a line above the hotbar for a moment
    void title(ui::Gui &gui, const std::string &text);
    bool done(TrainWindow &tw, ui::Gui &gui, int row);

    Screen m_screen = Screen::None;
    bool m_riding = false;      // left the title: Esc and Back lead to the game menu
    double m_titleSeconds = -1; // the time the title last turned the view
    int m_worldSelected = 0;    // Select World: 0 the island, 1 the flat grass
    bool m_guiHasMouse = false; // a press on the hotbar: its drag and release go there too
    double m_now = 0.0;         // the time input() was last given, for useSlot()
    int m_packScroll = 0;
    // Controls: the first row in view, and the command (and its first or
    // second key) waiting for a new key, -1 when none; a modifier held
    // meanwhile, to be the key itself if it is let go alone.
    int m_keyScroll = 0;
    int m_pickAction = -1;
    int m_pickSlot = 0;
    int m_pickModifier = 0;
    bool m_enter = false; // Enter was pressed: Load or Save, as their button

    // Load Track and Save Track: the files in TrackFiles\, the one picked,
    // and the name being typed.
    std::vector<std::string> m_tracks;
    int m_trackSelected = -1;
    int m_trackScroll = 0;
    std::string m_saveName = "track";
    std::string m_dropNote; // "Added loop.txt", on the screen the files were dropped on

    // Notice and Confirm, and the screen to go back to.
    std::string m_title;
    std::vector<std::string> m_lines;
    std::function<void(TrainWindow &)> m_yes;
    Screen m_return = Screen::None;
    // the slot's name shows above the hotbar for a moment, as an item's does
    std::string m_slotName;
    double m_slotNameUntil = 0.0;
};
