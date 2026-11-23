/************************************************************************
     File:        KeyBinds.h

     Comment:     Which key does what, as a block game's Controls > Key
                  Binds: every keyboard command has a name, a group and up
                  to two keys (a key and an alternative), each a key with
                  the Ctrl / Shift / Alt it needs. Options > Controls
                  (GameMenu) changes them, and they are kept in
                  data/keybinds.txt between runs, one line per command:

                    fly.forward=W
                    fly.forward.alt=None
                    edit.redo=Ctrl+Y
                    edit.redo.alt=Ctrl+Shift+Z

                  A command matches a key press when the key is the same
                  and exactly the bound modifiers are held, so Ctrl + Z
                  (Undo) is not Ctrl + Shift + Z (Redo). The Fly camera's
                  own keys (its held motions and its speed) ignore the
                  modifiers, so Ctrl (faster) with W still flies forward.
                  Left and right Shift, Ctrl and Alt are the same key.

                  Two commands clash when they share a key and can be
                  used at the same time: the Train camera's "Look Ahead"
                  (R) never clashes with "Tilt" (R), which only works
                  where the points can be edited. The menu shows clashing
                  keys in red, as the game does.

                  Esc is not a command: it opens and closes the menu, and
                  while a key is being picked it leaves the command with
                  no key.

                  Nothing here needs a window; tests/KeyBindsCheck.cpp
                  checks it.

*************************************************************************/
#pragma once

#include <array>
#include <string>

#include "Utilities/Input.H"

class FlyCam;

class KeyBinds
{
  public:
    enum class Action
    {
        // Interface
        Fullscreen,
        HideHud,
        PerfScreen,
        // Hotbar
        Slot1,
        Slot2,
        Slot3,
        Slot4,
        Slot5,
        Slot6,
        Slot7,
        Slot8,
        Slot9,
        // Ride
        Run,
        Slower,
        Faster,
        LookAhead,
        CameraWorld,
        CameraTop,
        CameraTrain,
        // Editing (World, Top and Fly)
        ToggleEdit,
        Undo,
        Redo,
        Grab,
        Tilt,
        Pick,
        AddPoint,
        RemovePoint,
        // Fly
        FlyForward,
        FlyBack,
        FlyLeft,
        FlyRight,
        FlyUp,
        FlyDown,
        FlyFast,
        FlyTurnLeft,
        FlyTurnRight,
        FlyLookUp,
        FlyLookDown,
        FlySpeedUp,
        FlySpeedDown,
        FlyFaceTrain,
        FlyToWorld,
        FlyAddPoint,
        FlyRemovePoint,
        Count
    };
    static constexpr int kActions = static_cast<int>(Action::Count);
    static constexpr int kSlots = 2; // a key and an alternative

    enum class Group
    {
        Interface,
        Hotbar,
        Ride,
        Editing,
        Fly
    };

    struct Bind
    {
        int key = 0;  // Input.H key code, 0 for none
        int mods = 0; // Mod::Ctrl | Mod::Shift | Mod::Alt that must be held
        bool operator==(const Bind &other) const
        {
            return key == other.key && mods == other.mods;
        }
        bool operator!=(const Bind &other) const
        {
            return !(*this == other);
        }
        bool none() const
        {
            return key == 0;
        }
    };

    // About each command, in the order of Action.
    static const char *id(Action action);   // "fly.forward", the name in the file
    static const char *name(Action action); // "Fly Forward", shown in the menu
    static Group group(Action action);
    static const char *groupName(Group group);
    // A key the Fly camera reads itself (a motion held down, or its speed):
    // the modifiers are ignored, and a bind never has any.
    static bool held(Action action);
    static Bind defaultBind(Action action, int slot);

    KeyBinds(); // the defaults

    Bind get(Action action, int slot) const;
    void set(Action action, int slot, Bind bind);
    void reset(Action action);
    void resetAll();
    bool isDefault(Action action) const;
    bool allDefault() const;

    // Does this key press (or release) belong to the command? key and state
    // as in InputEvent.
    bool matches(Action action, int key, int state) const;
    bool matches(Action action, const InputEvent &e) const
    {
        return matches(action, e.key, e.state);
    }
    // Is this slot's key also another command's, where both can be used?
    bool clashes(Action action, int slot) const;

    // Gives the Fly camera its keys (it reads held keys itself).
    void applyTo(FlyCam &camera) const;

    // data/keybinds.txt: every command on its own line. load() keeps the
    // defaults for commands the text leaves out or that it cannot read.
    std::string save() const;
    void load(const std::string &text);
    bool loadFile(const std::string &path); // false when there is no file
    bool saveFile(const std::string &path) const;

    // Names, as shown and saved: "W", "Space", "Left", "Numpad 1", "F4",
    // "Shift"; a bind adds its modifiers: "Ctrl+Shift+Z"; no key is "None".
    static std::string keyName(int key);
    static int keyFromName(const std::string &name); // 0 if unknown
    static std::string bindName(Bind bind);
    // The command's first key for hints ("G"), or "None".
    std::string keyText(Action action) const;
    static bool parseBind(const std::string &text, Bind &bind);

    // The right Shift, Ctrl and Alt as the left ones.
    static int normalize(int key);
    // The Mod:: bit a modifier key sets (Shift, Ctrl or Alt), else 0.
    static int modifierBit(int key);

  private:
    std::array<std::array<Bind, kSlots>, kActions> m_binds;
};
