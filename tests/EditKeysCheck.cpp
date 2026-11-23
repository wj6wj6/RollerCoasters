// Checks the control point editing keys against the Fly camera's keys: in Fly no flying key is
// an editing command (so X, Shift + D and the like cannot delete or add a point while
// flying), every editing command in Fly is a key the real FlyCam leaves alone, and the World
// and Top keys are unchanged. Rebinding a command (KeyBinds) moves it to the new key.
#include "EditKeys.h"
#include "KeyBinds.h"
#include "FlyCam.H"
#include "Platform.H"
#include "TestViewport.h"

#include <cstdio>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

// Does the real FlyCam take this key press? Released again at once.
bool flyTakes(FlyCam &camera, int key, int state)
{
    const int used = camera.handle(keyEvent(InputType::KeyDown, key, state));
    camera.handle(keyEvent(InputType::KeyUp, key, state));
    camera.stop();
    return used != 0;
}
} // namespace

int main()
{
    KeyBinds keys; // the defaults
    auto forKey = [&keys](int key, int state, bool fly) {
        return EditKeys::forKey(keys, key, state, fly);
    };

    // World and Top: as before.
    expect(forKey('g', 0, false) == EditKeys::GRAB, "World: G moves");
    expect(forKey('r', 0, false) == EditKeys::TILT, "World: R tilts");
    expect(forKey('d', Mod::Shift, false) == EditKeys::ADD_POINT, "World: Shift + D adds a point");
    expect(forKey('d', 0, false) == EditKeys::NONE, "World: D alone does nothing");
    expect(forKey('x', 0, false) == EditKeys::REMOVE, "World: X removes");
    expect(forKey(Key::Delete, 0, false) == EditKeys::REMOVE, "World: Delete removes");
    expect(forKey('y', Mod::Ctrl, false) == EditKeys::REDO, "World: Ctrl + Y redoes");
    expect(forKey('z', Mod::Ctrl | Mod::Shift, false) == EditKeys::REDO,
           "World: Ctrl + Shift + Z redoes");
    expect(forKey('y', 0, false) == EditKeys::NONE, "World: Y alone does nothing");
    expect(forKey(Key::F + 4, 0, false) == EditKeys::TOGGLE_EDIT, "World: F4 turns Edit on / off");
    expect(forKey('c', 0, false) == EditKeys::PICK_POINT, "World: C selects (middle button)");
    expect(forKey('c', 0, true) == EditKeys::PICK_POINT, "Fly: C selects (middle button)");
    expect(forKey('c', Mod::Ctrl, false) == EditKeys::NONE, "Ctrl + C is not a selection");
    expect(forKey(Key::F + 4, 0, true) == EditKeys::TOGGLE_EDIT, "Fly: F4 turns Edit on / off");
    expect(forKey('z', Mod::Ctrl, false) == EditKeys::UNDO, "World: Ctrl + Z undoes");
    expect(forKey(Key::Keypad + '7', 0, false) == EditKeys::CAMERA_TOP, "World: numpad 7 is Top");

    // Fly: the editing commands that do not clash.
    expect(forKey('g', 0, true) == EditKeys::GRAB, "Fly: G moves");
    expect(forKey('r', 0, true) == EditKeys::TILT, "Fly: R tilts");
    expect(forKey('d', Mod::Alt, true) == EditKeys::ADD_POINT, "Fly: Alt + D adds a point");
    expect(forKey(Key::Delete, 0, true) == EditKeys::REMOVE, "Fly: Delete removes");
    expect(forKey('z', Mod::Ctrl, true) == EditKeys::UNDO, "Fly: Ctrl + Z undoes");
    expect(forKey('y', Mod::Ctrl, true) == EditKeys::REDO, "Fly: Ctrl + Y redoes");
    expect(forKey('z', Mod::Ctrl | Mod::Shift, true) == EditKeys::REDO,
           "Fly: Ctrl + Shift + Z redoes");
    expect(forKey('x', 0, true) == EditKeys::NONE, "Fly: X does not remove a point");
    expect(forKey('d', Mod::Shift, true) == EditKeys::NONE,
           "Fly: Shift + D flies (sink and right), it does not add a point");
    expect(forKey('d', Mod::Ctrl, true) == EditKeys::NONE,
           "Fly: Ctrl + D flies right faster, it does not add a point");

    // No flying key is an editing command in Fly, whatever the modifiers.
    const int flying[] = {'w',           'a',         's',           'd',         ' ',
                          'e',           'q',         Key::ShiftL,   Key::ShiftR, Key::ControlL,
                          Key::ControlR, Key::PageUp, Key::PageDown, Key::Left,   Key::Right,
                          Key::Up,       Key::Down,   'f',           Key::Home};
    const int modifiers[] = {0, Mod::Shift, Mod::Ctrl, Mod::Shift | Mod::Ctrl};
    bool clear = true;
    for (int key : flying)
        for (int state : modifiers)
            if (forKey(key, state, true) != EditKeys::NONE)
                clear = false;
    expect(clear, "Fly: W / A / S / D, Space, Shift, Ctrl, E / Q, F, Home and arrows stay flying");

    // ... and the real FlyCam leaves every Fly editing key alone, so TrainView can hand it to
    // editing without the camera also moving. (D itself is a flying key: with Alt, TrainView
    // asks EditKeys before FlyCam, which is why Alt was chosen.)
    TestViewport window(400, 300);
    FlyCam camera;
    camera.setup(&window, 90);
    const int editing[] = {'g',
                           'r',
                           Key::Delete,
                           'z',
                           'y',
                           Key::Keypad + '1',
                           Key::Keypad + '7',
                           Key::Keypad + '0',
                           Key::F + 4,
                           'c'};
    bool untouched = true;
    for (int key : editing)
        if (flyTakes(camera, key, 0))
            untouched = false;
    expect(untouched, "FlyCam does not use G, R, C, Delete, Z, Y, F4 or the numpad camera keys");
    expect(flyTakes(camera, 'w', 0), "FlyCam still takes W (the check can see a used key)");

    // Key releases always reach FlyCam: a release of a held key stops the camera.
    camera.handle(keyEvent(InputType::KeyDown, 'w'));
    const bool movingWhileHeld = camera.moving();
    camera.handle(keyEvent(InputType::KeyUp, 'w'));
    float x0, y0, z0, x1, y1, z1;
    camera.position(x0, y0, z0);
    for (int i = 0; i < 10; ++i)
        platform::wait(0.02);
    camera.position(x1, y1, z1);
    expect(movingWhileHeld && x0 == x1 && y0 == y1 && z0 == z1,
           "a released W stops the camera on the next tick");
    camera.stop();

    // Rebinding: Tilt on T, Add Point (Fly) on Alt + F, Delete Point without X.
    keys.set(KeyBinds::Action::Tilt, 0, KeyBinds::Bind{'t', 0});
    keys.set(KeyBinds::Action::FlyAddPoint, 0, KeyBinds::Bind{'f', Mod::Alt});
    keys.set(KeyBinds::Action::RemovePoint, 1, KeyBinds::Bind{});
    expect(forKey('t', 0, false) == EditKeys::TILT && forKey('r', 0, false) == EditKeys::NONE,
           "rebound: T tilts and R no longer does");
    expect(forKey('f', Mod::Alt, true) == EditKeys::ADD_POINT &&
               forKey('d', Mod::Alt, true) == EditKeys::NONE,
           "rebound: Alt + F adds a point in Fly, Alt + D no longer does");
    expect(forKey('x', 0, false) == EditKeys::NONE &&
               forKey(Key::Delete, 0, false) == EditKeys::REMOVE,
           "rebound: X cleared, Delete still removes");
    expect(forKey('d', Mod::Shift, false) == EditKeys::ADD_POINT,
           "rebound: the commands not changed keep their keys");

    std::printf("%s\n", failures == 0 ? "EditKeysCheck passed" : "EditKeysCheck FAILED");
    return failures == 0 ? 0 : 1;
}
