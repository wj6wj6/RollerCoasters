/************************************************************************
     File:        GameMenu.h

     Comment:     The program's interface, drawn over the scene in the
                  style of a block game (Ui/Gui.h):

                  - the hotbar at the bottom: the five cameras, Edit, Add
                    and Delete point, Run / Stop; keys 1 to 9 or a click;
                  - key hints in the top left corner, and the view's
                    status line above the hotbar; F1 hides all of it;
                  - Esc opens the game menu, whose screens (Ride, Camera,
                    Track, Lighting, World) hold every control the old side
                    panel had. Esc goes back a screen;
                  - Load Track and Save Track list the tracks in
                    TrackFiles\ (a block game's world list, not the
                    system's file dialogs), and a notice or a yes / no
                    question takes the place of a message box.

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
        None, // riding: only the hotbar and the hints
        Pause,
        Ride,
        Camera,
        Track,
        Lighting,
        World,
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
    // Esc: a settings screen goes back to the game menu, the game menu to the ride.
    void back(TrainWindow &tw);

    // An event before the view sees it: true when the interface took it.
    bool input(TrainWindow &tw, const InputEvent &e);
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

    bool hudHidden = false; // F1

  private:
    void drawHud(TrainWindow &tw, ui::Gui &gui, double seconds);
    void drawPause(TrainWindow &tw, ui::Gui &gui);
    void drawRide(TrainWindow &tw, ui::Gui &gui);
    void drawCamera(TrainWindow &tw, ui::Gui &gui);
    void drawTrack(TrainWindow &tw, ui::Gui &gui);
    void drawLighting(TrainWindow &tw, ui::Gui &gui);
    void drawWorld(TrainWindow &tw, ui::Gui &gui);
    void drawLoadTrack(TrainWindow &tw, ui::Gui &gui);
    void drawSaveTrack(TrainWindow &tw, ui::Gui &gui);
    void drawNotice(TrainWindow &tw, ui::Gui &gui);
    void drawConfirm(TrainWindow &tw, ui::Gui &gui);
    void markSaveName(); // select the listed track that m_saveName would save over
    void loadSelected(TrainWindow &tw);
    void save(TrainWindow &tw);
    void say(const std::string &text); // a line above the hotbar for a moment
    void title(ui::Gui &gui, const std::string &text);
    bool done(TrainWindow &tw, ui::Gui &gui, int row);

    Screen m_screen = Screen::None;
    bool m_guiHasMouse = false; // a press on the hotbar: its drag and release go there too
    double m_now = 0.0;         // the time input() was last given, for useSlot()
    int m_packScroll = 0;
    bool m_enter = false; // Enter was pressed: Load or Save, as their button

    // Load Track and Save Track: the files in TrackFiles\, the one picked,
    // and the name being typed.
    std::vector<std::string> m_tracks;
    int m_trackSelected = -1;
    int m_trackScroll = 0;
    std::string m_saveName = "track";

    // Notice and Confirm, and the screen to go back to.
    std::string m_title;
    std::vector<std::string> m_lines;
    std::function<void(TrainWindow &)> m_yes;
    Screen m_return = Screen::None;
    // the slot's name shows above the hotbar for a moment, as an item's does
    std::string m_slotName;
    double m_slotNameUntil = 0.0;
};
