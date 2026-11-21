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
                    panel had. Esc goes back a screen.

                  It changes TrainWindow::settings and calls the same
                  actions (CallBacks.H) the panel did. Every frame draw()
                  lays the interface out again, so it always shows the
                  settings as they are, whoever changed them.

*************************************************************************/
#pragma once

#include <string>

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
        World
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

    bool hudHidden = false; // F1

  private:
    void drawHud(TrainWindow &tw, ui::Gui &gui, double seconds);
    void drawPause(TrainWindow &tw, ui::Gui &gui);
    void drawRide(TrainWindow &tw, ui::Gui &gui);
    void drawCamera(TrainWindow &tw, ui::Gui &gui);
    void drawTrack(TrainWindow &tw, ui::Gui &gui);
    void drawLighting(TrainWindow &tw, ui::Gui &gui);
    void drawWorld(TrainWindow &tw, ui::Gui &gui);
    void title(ui::Gui &gui, const std::string &text);
    bool done(TrainWindow &tw, ui::Gui &gui, int row);

    Screen m_screen = Screen::None;
    bool m_guiHasMouse = false; // a press on the hotbar: its drag and release go there too
    double m_now = 0.0;         // the time input() was last given, for useSlot()
    int m_packScroll = 0;
    // the slot's name shows above the hotbar for a moment, as an item's does
    std::string m_slotName;
    double m_slotNameUntil = 0.0;
};
