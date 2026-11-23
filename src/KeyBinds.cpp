/************************************************************************
     File:        KeyBinds.cpp

     Comment:     See KeyBinds.h.

*************************************************************************/

#include "KeyBinds.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "Utilities/FlyCam.H"
#include "Utilities/Utf8File.H"

namespace
{
using Action = KeyBinds::Action;
using Bind = KeyBinds::Bind;
using Group = KeyBinds::Group;

// Where a command can be used, for clashes: two commands on the same key
// clash only if they can be used in the same camera.
constexpr int kWorldTop = 1; // World and Top cameras
constexpr int kRiding = 2;   // Train and Chase cameras
constexpr int kFly = 4;
constexpr int kAnywhere = kWorldTop | kRiding | kFly;
constexpr int kEditable = kWorldTop | kFly; // TrainView::canEdit()

struct Command
{
    Action action;
    const char *id;
    const char *name;
    Group group;
    int where;
    bool held;
    Bind keys[KeyBinds::kSlots];
};

constexpr Bind key(int k, int mods = 0)
{
    return Bind{k, mods};
}
constexpr Bind none()
{
    return Bind{};
}

// The defaults are the keys the program had before they could be changed
// (EditKeys.h, FlyCam.H, GameMenu.h), less Page Up / Page Down for rising and
// sinking: two keys each is enough.
const Command kCommands[KeyBinds::kActions] = {
    {Action::Fullscreen,
     "ui.fullscreen",
     "Fullscreen",
     Group::Interface,
     kAnywhere,
     false,
     {key(Key::F + 11), none()}},
    {Action::HideHud,
     "ui.hideHud",
     "Hide Hotbar and Hints",
     Group::Interface,
     kAnywhere,
     false,
     {key(Key::F + 1), none()}},

    {Action::Slot1,
     "hotbar.1",
     "Slot 1: World Camera",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('1'), none()}},
    {Action::Slot2,
     "hotbar.2",
     "Slot 2: Train Camera",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('2'), none()}},
    {Action::Slot3,
     "hotbar.3",
     "Slot 3: Chase Camera",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('3'), none()}},
    {Action::Slot4,
     "hotbar.4",
     "Slot 4: Top View",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('4'), none()}},
    {Action::Slot5,
     "hotbar.5",
     "Slot 5: Fly Camera",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('5'), none()}},
    {Action::Slot6,
     "hotbar.6",
     "Slot 6: Edit Points",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('6'), none()}},
    {Action::Slot7,
     "hotbar.7",
     "Slot 7: Add Point",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('7'), none()}},
    {Action::Slot8,
     "hotbar.8",
     "Slot 8: Delete Point",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('8'), none()}},
    {Action::Slot9,
     "hotbar.9",
     "Slot 9: Run / Stop",
     Group::Hotbar,
     kAnywhere,
     false,
     {key('9'), none()}},

    {Action::Run, "ride.run", "Run / Stop", Group::Ride, kAnywhere, false, {key('p'), none()}},
    {Action::Slower,
     "ride.slower",
     "Train Slower",
     Group::Ride,
     kAnywhere,
     false,
     {key('['), none()}},
    {Action::Faster,
     "ride.faster",
     "Train Faster",
     Group::Ride,
     kAnywhere,
     false,
     {key(']'), none()}},
    {Action::LookAhead,
     "ride.lookAhead",
     "Look Ahead Again",
     Group::Ride,
     kRiding,
     false,
     {key('r'), key(Key::Home)}},
    {Action::CameraWorld,
     "camera.world",
     "World Camera",
     Group::Ride,
     kAnywhere,
     false,
     {key(Key::Keypad + '1'), none()}},
    {Action::CameraTop,
     "camera.top",
     "Top View",
     Group::Ride,
     kAnywhere,
     false,
     {key(Key::Keypad + '7'), none()}},
    {Action::CameraTrain,
     "camera.train",
     "Train Camera",
     Group::Ride,
     kAnywhere,
     false,
     {key(Key::Keypad + '0'), none()}},

    {Action::ToggleEdit,
     "edit.toggle",
     "Edit On / Off",
     Group::Editing,
     kAnywhere,
     false,
     {key(Key::F + 4), none()}},
    {Action::Undo,
     "edit.undo",
     "Undo",
     Group::Editing,
     kEditable,
     false,
     {key('z', Mod::Ctrl), none()}},
    {Action::Redo,
     "edit.redo",
     "Redo",
     Group::Editing,
     kEditable,
     false,
     {key('y', Mod::Ctrl), key('z', Mod::Ctrl | Mod::Shift)}},
    {Action::Grab, "edit.grab", "Move Point", Group::Editing, kEditable, false, {key('g'), none()}},
    {Action::Tilt, "edit.tilt", "Tilt Point", Group::Editing, kEditable, false, {key('r'), none()}},
    {Action::Pick,
     "edit.pick",
     "Select Point",
     Group::Editing,
     kEditable,
     false,
     {key('c'), none()}},
    {Action::AddPoint,
     "edit.add",
     "Add Point",
     Group::Editing,
     kWorldTop,
     false,
     {key('d', Mod::Shift), none()}},
    {Action::RemovePoint,
     "edit.remove",
     "Delete Point",
     Group::Editing,
     kWorldTop,
     false,
     {key(Key::Delete), key('x')}},

    {Action::FlyForward, "fly.forward", "Forward", Group::Fly, kFly, true, {key('w'), none()}},
    {Action::FlyBack, "fly.back", "Back", Group::Fly, kFly, true, {key('s'), none()}},
    {Action::FlyLeft, "fly.left", "Left", Group::Fly, kFly, true, {key('a'), none()}},
    {Action::FlyRight, "fly.right", "Right", Group::Fly, kFly, true, {key('d'), none()}},
    {Action::FlyUp, "fly.up", "Up", Group::Fly, kFly, true, {key(' '), key('e')}},
    {Action::FlyDown, "fly.down", "Down", Group::Fly, kFly, true, {key(Key::ShiftL), key('q')}},
    {Action::FlyFast,
     "fly.fast",
     "Faster (hold)",
     Group::Fly,
     kFly,
     true,
     {key(Key::ControlL), none()}},
    {Action::FlyTurnLeft,
     "fly.turnLeft",
     "Turn Left",
     Group::Fly,
     kFly,
     true,
     {key(Key::Left), none()}},
    {Action::FlyTurnRight,
     "fly.turnRight",
     "Turn Right",
     Group::Fly,
     kFly,
     true,
     {key(Key::Right), none()}},
    {Action::FlyLookUp, "fly.lookUp", "Look Up", Group::Fly, kFly, true, {key(Key::Up), none()}},
    {Action::FlyLookDown,
     "fly.lookDown",
     "Look Down",
     Group::Fly,
     kFly,
     true,
     {key(Key::Down), none()}},
    {Action::FlySpeedUp,
     "fly.speedUp",
     "Flying Speed +",
     Group::Fly,
     kFly,
     true,
     {key('='), key(Key::Keypad + '+')}},
    {Action::FlySpeedDown,
     "fly.speedDown",
     "Flying Speed -",
     Group::Fly,
     kFly,
     true,
     {key('-'), key(Key::Keypad + '-')}},
    {Action::FlyFaceTrain,
     "fly.faceTrain",
     "Face the Train",
     Group::Fly,
     kFly,
     false,
     {key('f'), none()}},
    {Action::FlyToWorld,
     "fly.toWorld",
     "Go to World View",
     Group::Fly,
     kFly,
     false,
     {key(Key::Home), none()}},
    // Shift is Down and Ctrl is Faster while flying, so adding takes Alt, and
    // X is left out so a stray press cannot delete a point (EditKeys.h).
    {Action::FlyAddPoint,
     "fly.add",
     "Add Point in Fly",
     Group::Fly,
     kFly,
     false,
     {key('d', Mod::Alt), none()}},
    {Action::FlyRemovePoint,
     "fly.remove",
     "Delete Point in Fly",
     Group::Fly,
     kFly,
     false,
     {key(Key::Delete), none()}},
};

const Command &command(Action action)
{
    return kCommands[static_cast<int>(action)];
}

constexpr int kModifiers = Mod::Ctrl | Mod::Shift | Mod::Alt;

std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trim(const std::string &text)
{
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;
    return text.substr(begin, end - begin);
}

// Takes `prefix` (any case) off the front of text.
bool takePrefix(std::string &text, const char *prefix)
{
    const std::string want = lower(prefix);
    if (lower(text.substr(0, want.size())) != want || text.size() == want.size())
        return false;
    text.erase(0, want.size());
    return true;
}

// Every key code a name is given for, for keyFromName().
template <typename Visit> void eachKnownKey(Visit visit)
{
    for (int c = 'a'; c <= 'z'; ++c)
        visit(c);
    for (int c = '0'; c <= '9'; ++c)
        visit(c);
    for (const char c : std::string(" ',-./;=[\\]`"))
        visit(static_cast<int>(c));
    const int special[] = {Key::BackSpace, Key::Tab,        Key::Enter,  Key::Home,   Key::Left,
                           Key::Up,        Key::Right,      Key::Down,   Key::PageUp, Key::PageDown,
                           Key::End,       Key::Insert,     Key::Delete, Key::ShiftL, Key::ControlL,
                           Key::AltL,      Key::KeypadEnter};
    for (int k : special)
        visit(k);
    for (int c = '0'; c <= '9'; ++c)
        visit(Key::Keypad + c);
    for (const char c : std::string("./*-+="))
        visit(Key::Keypad + c);
    for (int n = 1; n <= 25; ++n)
        visit(Key::F + n);
}
} // namespace

//************************************************************************
//
// * About the commands
//========================================================================
const char *KeyBinds::id(Action action)
{
    return command(action).id;
}

const char *KeyBinds::name(Action action)
{
    return command(action).name;
}

KeyBinds::Group KeyBinds::group(Action action)
{
    return command(action).group;
}

const char *KeyBinds::groupName(Group group)
{
    switch (group)
    {
    case Group::Interface:
        return "Interface";
    case Group::Hotbar:
        return "Hotbar";
    case Group::Ride:
        return "Ride and Cameras";
    case Group::Editing:
        return "Editing Points";
    case Group::Fly:
        return "Fly Camera";
    }
    return "";
}

bool KeyBinds::held(Action action)
{
    return command(action).held;
}

KeyBinds::Bind KeyBinds::defaultBind(Action action, int slot)
{
    if (slot < 0 || slot >= kSlots)
        return Bind{};
    return command(action).keys[slot];
}

//************************************************************************
//
// * The keys in use
//========================================================================
KeyBinds::KeyBinds()
{
    resetAll();
}

KeyBinds::Bind KeyBinds::get(Action action, int slot) const
{
    if (slot < 0 || slot >= kSlots || action == Action::Count)
        return Bind{};
    return m_binds[static_cast<size_t>(action)][static_cast<size_t>(slot)];
}

void KeyBinds::set(Action action, int slot, Bind bind)
{
    if (slot < 0 || slot >= kSlots || action == Action::Count)
        return;
    bind.key = normalize(bind.key);
    // a held motion ignores the modifiers; a modifier key does not need itself
    bind.mods = (bind.key == 0 || held(action)) ? 0 : (bind.mods & kModifiers);
    bind.mods &= ~modifierBit(bind.key);
    m_binds[static_cast<size_t>(action)][static_cast<size_t>(slot)] = bind;
}

void KeyBinds::reset(Action action)
{
    for (int slot = 0; slot < kSlots; ++slot)
        set(action, slot, defaultBind(action, slot));
}

void KeyBinds::resetAll()
{
    for (int i = 0; i < kActions; ++i)
        reset(static_cast<Action>(i));
}

bool KeyBinds::isDefault(Action action) const
{
    for (int slot = 0; slot < kSlots; ++slot)
        if (get(action, slot) != defaultBind(action, slot))
            return false;
    return true;
}

bool KeyBinds::allDefault() const
{
    for (int i = 0; i < kActions; ++i)
        if (!isDefault(static_cast<Action>(i)))
            return false;
    return true;
}

bool KeyBinds::matches(Action action, int key, int state) const
{
    const int k = normalize(key);
    if (k == 0)
        return false;
    const int mods = state & kModifiers & ~modifierBit(k);
    for (int slot = 0; slot < kSlots; ++slot)
    {
        const Bind b = get(action, slot);
        if (b.key == k && (held(action) || b.mods == mods))
            return true;
    }
    return false;
}

bool KeyBinds::clashes(Action action, int slot) const
{
    const Bind b = get(action, slot);
    if (b.none())
        return false;
    for (int i = 0; i < kActions; ++i)
    {
        const Action other = static_cast<Action>(i);
        if (other == action || (command(other).where & command(action).where) == 0)
            continue;
        for (int s = 0; s < kSlots; ++s)
            if (get(other, s) == b)
                return true;
    }
    return false;
}

void KeyBinds::applyTo(FlyCam &camera) const
{
    const struct
    {
        Action action;
        int command;
    } map[] = {{Action::FlyForward, FlyCam::Forward},
               {Action::FlyBack, FlyCam::Back},
               {Action::FlyLeft, FlyCam::Left},
               {Action::FlyRight, FlyCam::Right},
               {Action::FlyUp, FlyCam::Up},
               {Action::FlyDown, FlyCam::Down},
               {Action::FlyTurnLeft, FlyCam::TurnLeft},
               {Action::FlyTurnRight, FlyCam::TurnRight},
               {Action::FlyLookUp, FlyCam::LookUp},
               {Action::FlyLookDown, FlyCam::LookDown},
               {Action::FlyFast, FlyCam::Fast},
               {Action::FlySpeedUp, FlyCam::SpeedUp},
               {Action::FlySpeedDown, FlyCam::SpeedDown}};
    for (const auto &entry : map)
        camera.setKeys(entry.command, get(entry.action, 0).key, get(entry.action, 1).key);
}

//************************************************************************
//
// * data/keybinds.txt
//========================================================================
std::string KeyBinds::save() const
{
    std::string text = "# RollerCoasters key binds, set in Options > Controls.\n"
                       "# command=key, command.alt=second key; None for no key.\n";
    for (int i = 0; i < kActions; ++i)
    {
        const Action action = static_cast<Action>(i);
        text += std::string(id(action)) + "=" + bindName(get(action, 0)) + "\n";
        text += std::string(id(action)) + ".alt=" + bindName(get(action, 1)) + "\n";
    }
    return text;
}

void KeyBinds::load(const std::string &text)
{
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == '#' || equals == std::string::npos)
            continue;
        std::string name = trim(line.substr(0, equals));
        int slot = 0;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".alt") == 0)
        {
            name.erase(name.size() - 4);
            slot = 1;
        }
        Bind bind;
        if (!parseBind(line.substr(equals + 1), bind))
            continue; // a key this version does not know keeps its default
        for (int i = 0; i < kActions; ++i)
            if (name == id(static_cast<Action>(i)))
                set(static_cast<Action>(i), slot, bind);
    }
}

bool KeyBinds::loadFile(const std::string &path)
{
    FILE *file = openUtf8(path.c_str(), "rb");
    if (!file)
        return false;
    std::string text;
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        text.append(buffer, got);
    std::fclose(file);
    load(text);
    return true;
}

bool KeyBinds::saveFile(const std::string &path) const
{
    std::error_code ignored;
    const std::filesystem::path folder = std::filesystem::u8path(path).parent_path();
    if (!folder.empty())
        std::filesystem::create_directories(folder, ignored);
    FILE *file = openUtf8(path.c_str(), "wb");
    if (!file)
        return false;
    const std::string text = save();
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    return std::fclose(file) == 0 && written;
}

//************************************************************************
//
// * Key names
//========================================================================
int KeyBinds::normalize(int key)
{
    switch (key)
    {
    case Key::ShiftR:
        return Key::ShiftL;
    case Key::ControlR:
        return Key::ControlL;
    case Key::AltR:
        return Key::AltL;
    default:
        return key;
    }
}

int KeyBinds::modifierBit(int key)
{
    switch (normalize(key))
    {
    case Key::ShiftL:
        return Mod::Shift;
    case Key::ControlL:
        return Mod::Ctrl;
    case Key::AltL:
        return Mod::Alt;
    default:
        return 0;
    }
}

std::string KeyBinds::keyName(int key)
{
    key = normalize(key);
    if (key >= 'a' && key <= 'z')
        return std::string(1, static_cast<char>(key - 'a' + 'A'));
    if (key == ' ')
        return "Space";
    if (key > ' ' && key < 127)
        return std::string(1, static_cast<char>(key));
    if (key == Key::KeypadEnter)
        return "Numpad Enter";
    // F1-F25 first: they come after the numpad's codes (Input.H)
    if (key > Key::F && key <= Key::F + 25)
        return "F" + std::to_string(key - Key::F);
    if ((key >= Key::Keypad + '*' && key <= Key::Keypad + '9') || key == Key::Keypad + '=')
        return "Numpad " + std::string(1, static_cast<char>(key - Key::Keypad));
    switch (key)
    {
    case Key::BackSpace:
        return "Backspace";
    case Key::Tab:
        return "Tab";
    case Key::Enter:
        return "Enter";
    case Key::Escape:
        return "Esc";
    case Key::Home:
        return "Home";
    case Key::Left:
        return "Left";
    case Key::Up:
        return "Up";
    case Key::Right:
        return "Right";
    case Key::Down:
        return "Down";
    case Key::PageUp:
        return "Page Up";
    case Key::PageDown:
        return "Page Down";
    case Key::End:
        return "End";
    case Key::Insert:
        return "Insert";
    case Key::Delete:
        return "Delete";
    case Key::ShiftL:
        return "Shift";
    case Key::ControlL:
        return "Ctrl";
    case Key::AltL:
        return "Alt";
    default:
        return key == 0 ? "None" : "Key " + std::to_string(key);
    }
}

int KeyBinds::keyFromName(const std::string &name)
{
    const std::string wanted = lower(trim(name));
    if (wanted.empty())
        return 0;
    int found = 0;
    eachKnownKey([&](int k) {
        if (found == 0 && lower(keyName(k)) == wanted)
            found = k;
    });
    return found;
}

std::string KeyBinds::bindName(Bind bind)
{
    if (bind.none())
        return "None";
    std::string text;
    if (bind.mods & Mod::Ctrl)
        text += "Ctrl+";
    if (bind.mods & Mod::Shift)
        text += "Shift+";
    if (bind.mods & Mod::Alt)
        text += "Alt+";
    return text + keyName(bind.key);
}

std::string KeyBinds::keyText(Action action) const
{
    for (int slot = 0; slot < kSlots; ++slot)
        if (!get(action, slot).none())
            return bindName(get(action, slot));
    return "None";
}

bool KeyBinds::parseBind(const std::string &text, Bind &bind)
{
    std::string rest = trim(text);
    if (lower(rest) == "none")
    {
        bind = Bind{};
        return true;
    }
    int mods = 0;
    for (bool more = true; more;)
    {
        more = false;
        const char *const names[] = {"Ctrl+", "Shift+", "Alt+"};
        const int bits[] = {Mod::Ctrl, Mod::Shift, Mod::Alt};
        for (int i = 0; i < 3; ++i)
            if (takePrefix(rest, names[i]))
            {
                mods |= bits[i];
                more = true;
            }
    }
    const int k = keyFromName(rest);
    if (k == 0)
        return false;
    bind.key = k;
    bind.mods = mods & ~modifierBit(k);
    return true;
}
