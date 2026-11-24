// Checks the key binds (src/KeyBinds.*): the defaults are the keys the program had, a command
// matches only with exactly its modifiers (the Fly camera's keys ignore them), left and right
// modifier keys are the same, the defaults do not clash while a shared key in the same camera
// does, every key name reads back as the same key, data/keybinds.txt keeps every change and
// skips lines it cannot read, and the real FlyCam flies on the keys it is given.
#include "KeyBinds.h"
#include "FlyCam.H"
#include "Platform.H"
#include "TestViewport.h"

#include <cstdio>
#include <string>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

using A = KeyBinds::Action;
using Bind = KeyBinds::Bind;

bool anyClash(const KeyBinds &keys)
{
    for (int i = 0; i < KeyBinds::kActions; ++i)
        for (int slot = 0; slot < KeyBinds::kSlots; ++slot)
            if (keys.clashes(static_cast<A>(i), slot))
            {
                std::printf("  clash: %s %s\n", KeyBinds::name(static_cast<A>(i)),
                            KeyBinds::bindName(keys.get(static_cast<A>(i), slot)).c_str());
                return true;
            }
    return false;
}

// Does the FlyCam move while this key is held for a moment?
bool flies(FlyCam &camera, int key)
{
    float x0, y0, z0, x1, y1, z1;
    camera.position(x0, y0, z0);
    camera.handle(keyEvent(InputType::KeyDown, key));
    for (int i = 0; i < 5; ++i)
        platform::wait(0.02);
    camera.handle(keyEvent(InputType::KeyUp, key));
    camera.position(x1, y1, z1);
    camera.stop();
    return x0 != x1 || y0 != y1 || z0 != z1;
}
} // namespace

int main()
{
    KeyBinds keys;

    // The defaults: the keys the program always had.
    expect(keys.matches(A::Fullscreen, Key::F + 11, 0), "F11 is Fullscreen");
    expect(keys.matches(A::HideHud, Key::F + 1, 0), "F1 hides the hotbar and hints");
    expect(keys.matches(A::PerfScreen, Key::F + 3, 0), "F3 shows the performance screen");
    expect(keys.matches(A::Slot1, '1', 0) && keys.matches(A::Slot9, '9', 0), "1-9 are the hotbar");
    expect(keys.matches(A::Run, 'p', 0), "P runs and stops");
    expect(keys.matches(A::Slower, '[', 0) && keys.matches(A::Faster, ']', 0), "[ ] the speed");
    expect(keys.matches(A::LookAhead, 'r', 0) && keys.matches(A::LookAhead, Key::Home, 0),
           "R and Home look ahead");
    expect(keys.matches(A::Undo, 'z', Mod::Ctrl), "Ctrl + Z undoes");
    expect(keys.matches(A::Redo, 'y', Mod::Ctrl) &&
               keys.matches(A::Redo, 'z', Mod::Ctrl | Mod::Shift),
           "Ctrl + Y and Ctrl + Shift + Z redo");
    expect(keys.matches(A::FlyUp, ' ', 0) && keys.matches(A::FlyUp, 'e', 0), "Space and E rise");
    expect(keys.matches(A::FlyDown, Key::ShiftL, Mod::Shift) && keys.matches(A::FlyDown, 'q', 0),
           "Shift and Q sink");
    expect(keys.allDefault(), "a new table is all defaults");

    // Exact modifiers for commands; none needed for the Fly camera's keys.
    expect(!keys.matches(A::Undo, 'z', Mod::Ctrl | Mod::Shift), "Ctrl + Shift + Z is not Undo");
    expect(!keys.matches(A::Undo, 'z', 0), "Z alone is not Undo");
    expect(!keys.matches(A::Run, 'p', Mod::Ctrl), "Ctrl + P is not Run");
    expect(keys.matches(A::FlyForward, 'w', Mod::Ctrl | Mod::Shift),
           "Ctrl + Shift + W still flies");
    expect(keys.matches(A::FlySpeedUp, Key::Keypad + '+', Mod::Ctrl), "numpad + with Ctrl");
    expect(keys.matches(A::FlyDown, Key::ShiftR, Mod::Shift), "the right Shift sinks too");
    expect(keys.matches(A::FlyFast, Key::ControlR, Mod::Ctrl), "the right Ctrl is Faster too");

    // Clashes.
    expect(!anyClash(keys), "the defaults do not clash");
    keys.set(A::Slot1, 0, Bind{'w', 0});
    expect(keys.clashes(A::Slot1, 0) && keys.clashes(A::FlyForward, 0),
           "W on the hotbar and on Fly Forward clash");
    keys.reset(A::Slot1);
    expect(!keys.clashes(A::FlyForward, 0) && keys.isDefault(A::Slot1), "Reset takes it back");
    expect(!keys.clashes(A::LookAhead, 0) && !keys.clashes(A::Tilt, 0),
           "R in the Train camera and R for Tilt do not clash (never both usable)");
    keys.set(A::Grab, 0, Bind{'r', 0});
    expect(keys.clashes(A::Grab, 0) && keys.clashes(A::Tilt, 0),
           "G moved onto R clashes with Tilt");
    keys.set(A::Grab, 0, Bind{'r', Mod::Shift});
    expect(!keys.clashes(A::Grab, 0), "Shift + R is another key than R");
    keys.resetAll();

    // set(): a modifier key alone, and the Fly camera's keys lose their modifiers.
    keys.set(A::Run, 0, Bind{Key::ControlR, Mod::Ctrl});
    expect(keys.get(A::Run, 0) == (Bind{Key::ControlL, 0}) &&
               keys.matches(A::Run, Key::ControlL, Mod::Ctrl),
           "Ctrl alone can be a key");
    keys.set(A::FlyForward, 0, Bind{'i', Mod::Alt});
    expect(keys.get(A::FlyForward, 0) == (Bind{'i', 0}), "a Fly motion keeps no modifiers");
    keys.resetAll();

    // Names read back as the same key.
    bool names = true;
    const int sample[] = {'a',
                          'z',
                          '0',
                          '9',
                          ' ',
                          '[',
                          ']',
                          '\\',
                          ',',
                          '=',
                          '-',
                          '`',
                          '\'',
                          Key::Enter,
                          Key::Tab,
                          Key::BackSpace,
                          Key::Home,
                          Key::End,
                          Key::Insert,
                          Key::Delete,
                          Key::PageUp,
                          Key::PageDown,
                          Key::Left,
                          Key::Up,
                          Key::Right,
                          Key::Down,
                          Key::ShiftL,
                          Key::ControlL,
                          Key::AltL,
                          Key::KeypadEnter,
                          Key::Keypad + '0',
                          Key::Keypad + '9',
                          Key::Keypad + '+',
                          Key::Keypad + '.',
                          Key::F + 1,
                          Key::F + 12,
                          Key::F + 25};
    for (int k : sample)
        if (KeyBinds::keyFromName(KeyBinds::keyName(k)) != k)
        {
            std::printf("  %d (%s) does not read back\n", k, KeyBinds::keyName(k).c_str());
            names = false;
        }
    expect(names, "every key name reads back as its key");
    expect(KeyBinds::bindName(Bind{'z', Mod::Ctrl | Mod::Shift}) == "Ctrl+Shift+Z", "Ctrl+Shift+Z");
    expect(KeyBinds::bindName(Bind{}) == "None", "no key is None");
    Bind parsed;
    expect(KeyBinds::parseBind(" ctrl+numpad + ", parsed) &&
               parsed == (Bind{Key::Keypad + '+', Mod::Ctrl}),
           "Ctrl+Numpad + reads back, any case");
    expect(!KeyBinds::parseBind("Hyper+Q", parsed) && !KeyBinds::parseBind("", parsed),
           "unknown names are refused");

    // data/keybinds.txt: everything changed comes back, and lines it cannot read are skipped.
    keys.set(A::FlyForward, 0, Bind{'i', 0});
    keys.set(A::FlyForward, 1, Bind{Key::Up, 0});
    keys.set(A::Undo, 0, Bind{'u', Mod::Ctrl | Mod::Alt});
    keys.set(A::Redo, 1, Bind{});
    keys.set(A::HideHud, 0, Bind{});
    const std::string text = keys.save();
    KeyBinds read;
    read.load(text);
    bool same = true;
    for (int i = 0; i < KeyBinds::kActions; ++i)
        for (int slot = 0; slot < KeyBinds::kSlots; ++slot)
            if (read.get(static_cast<A>(i), slot) != keys.get(static_cast<A>(i), slot))
                same = false;
    expect(same, "save() then load() gives the same keys");
    expect(KeyBinds().lockWindowsKey && read.lockWindowsKey, "Lock Windows Key is on by default");
    keys.lockWindowsKey = false;
    KeyBinds unlocked;
    unlocked.load(keys.save());
    expect(!unlocked.lockWindowsKey, "Lock Windows Key off is saved and read back");
    unlocked.resetAll();
    expect(!unlocked.lockWindowsKey, "Reset All leaves Lock Windows Key as it is");
    unlocked.load("lockWindowsKey=maybe\n");
    expect(!unlocked.lockWindowsKey, "a value it cannot read keeps the switch");
    KeyBinds older;
    older.load("fly.back=K\n");
    expect(older.lockWindowsKey, "a file from before the switch locks the key");
    keys.lockWindowsKey = true;
    KeyBinds partial;
    partial.load("# a comment\nfly.back=K\nfly.left=NoSuchKey\nno.such.command=J\n"
                 "garbage line\r\nfly.right.alt=L\r\n");
    expect(partial.get(A::FlyBack, 0) == (Bind{'k', 0}), "a known line is read");
    expect(partial.isDefault(A::FlyLeft), "a key it does not know keeps the default");
    expect(partial.get(A::FlyRight, 1) == (Bind{'l', 0}), "an .alt line with Windows line ends");

    const std::string path = "keybinds-check/keybinds.txt";
    expect(keys.saveFile(path), "saveFile() makes the folder and writes");
    KeyBinds fromFile;
    expect(fromFile.loadFile(path) && fromFile.get(A::Undo, 0) == keys.get(A::Undo, 0),
           "loadFile() reads it back");
    std::remove(path.c_str());
    std::remove("keybinds-check");
    expect(!fromFile.loadFile("keybinds-check/missing.txt"), "no file: false, keys unchanged");

    // The real FlyCam flies on the keys it is given.
    TestViewport window(400, 300);
    FlyCam camera;
    camera.setup(&window, 90);
    expect(flies(camera, 'w'), "FlyCam's own defaults: W flies");
    KeyBinds defaults;
    defaults.applyTo(camera);
    expect(flies(camera, 'w') && flies(camera, 'e') && flies(camera, Key::ShiftR),
           "with the default binds: W, E and the right Shift fly");
    keys.applyTo(camera); // forward on I and Up
    expect(flies(camera, 'i') && !flies(camera, 'w'), "rebound: I flies forward, W does nothing");
    const float speed = camera.speed();
    camera.handle(keyEvent(InputType::KeyDown, '='));
    expect(camera.speed() > speed, "= still speeds up");

    std::printf("%s\n", failures == 0 ? "KeyBindsCheck passed" : "KeyBindsCheck FAILED");
    return failures == 0 ? 0 : 1;
}
