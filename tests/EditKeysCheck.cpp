// Checks the control point editing keys against the Fly camera's keys: in Fly no flying key is
// an editing command (so X, Shift + D and the like cannot delete or add a point while
// flying), every editing command in Fly is a key the real FlyCam leaves alone, and the World
// and Top keys are unchanged.
#include "EditKeys.h"
#include "FlyCam.H"

#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>

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
    Fl::e_keysym = key;
    Fl::e_state = state;
    const int used = camera.handle(FL_KEYDOWN);
    camera.handle(FL_KEYUP);
    camera.stop();
    Fl::e_state = 0;
    return used != 0;
}
} // namespace

int main()
{
    using EditKeys::forKey;

    // World and Top: as before.
    expect(forKey('g', 0, false) == EditKeys::GRAB, "World: G moves");
    expect(forKey('r', 0, false) == EditKeys::TILT, "World: R tilts");
    expect(forKey('d', FL_SHIFT, false) == EditKeys::ADD_POINT, "World: Shift + D adds a point");
    expect(forKey('d', 0, false) == EditKeys::NONE, "World: D alone does nothing");
    expect(forKey('x', 0, false) == EditKeys::REMOVE, "World: X removes");
    expect(forKey(FL_Delete, 0, false) == EditKeys::REMOVE, "World: Delete removes");
    expect(forKey('z', FL_CTRL, false) == EditKeys::UNDO, "World: Ctrl + Z undoes");
    expect(forKey(FL_KP + '7', 0, false) == EditKeys::CAMERA_TOP, "World: numpad 7 is Top");

    // Fly: the editing commands that do not clash.
    expect(forKey('g', 0, true) == EditKeys::GRAB, "Fly: G moves");
    expect(forKey('r', 0, true) == EditKeys::TILT, "Fly: R tilts");
    expect(forKey('d', FL_ALT, true) == EditKeys::ADD_POINT, "Fly: Alt + D adds a point");
    expect(forKey(FL_Delete, 0, true) == EditKeys::REMOVE, "Fly: Delete removes");
    expect(forKey('z', FL_CTRL, true) == EditKeys::UNDO, "Fly: Ctrl + Z undoes");
    expect(forKey('x', 0, true) == EditKeys::NONE, "Fly: X does not remove a point");
    expect(forKey('d', FL_SHIFT, true) == EditKeys::NONE,
           "Fly: Shift + D flies (sink and right), it does not add a point");
    expect(forKey('d', FL_CTRL, true) == EditKeys::NONE,
           "Fly: Ctrl + D flies right faster, it does not add a point");

    // No flying key is an editing command in Fly, whatever the modifiers.
    const int flying[] = {'w',          'a',        's',          'd',        ' ',
                          'e',          'q',        FL_Shift_L,   FL_Shift_R, FL_Control_L,
                          FL_Control_R, FL_Page_Up, FL_Page_Down, FL_Left,    FL_Right,
                          FL_Up,        FL_Down,    'f',          FL_Home};
    const int modifiers[] = {0, FL_SHIFT, FL_CTRL, FL_SHIFT | FL_CTRL};
    bool clear = true;
    for (int key : flying)
        for (int state : modifiers)
            if (forKey(key, state, true) != EditKeys::NONE)
                clear = false;
    expect(clear, "Fly: W / A / S / D, Space, Shift, Ctrl, E / Q, F, Home and arrows stay flying");

    // ... and the real FlyCam leaves every Fly editing key alone, so TrainView can hand it to
    // editing without the camera also moving. (D itself is a flying key: with Alt, TrainView
    // asks EditKeys before FlyCam, which is why Alt was chosen.)
    Fl_Gl_Window window(400, 300);
    FlyCam camera;
    camera.setup(&window, 90);
    const int editing[] = {'g', 'r', FL_Delete, 'z', FL_KP + '1', FL_KP + '7', FL_KP + '0'};
    bool untouched = true;
    for (int key : editing)
        if (flyTakes(camera, key, 0))
            untouched = false;
    expect(untouched, "FlyCam does not use G, R, Delete, Z or the numpad camera keys");
    expect(flyTakes(camera, 'w', 0), "FlyCam still takes W (the check can see a used key)");

    // Key releases always reach FlyCam: a release of a held key stops the camera.
    Fl::e_keysym = 'w';
    camera.handle(FL_KEYDOWN);
    const bool movingWhileHeld = camera.moving();
    Fl::e_keysym = 'w';
    camera.handle(FL_KEYUP);
    float x0, y0, z0, x1, y1, z1;
    camera.position(x0, y0, z0);
    for (int i = 0; i < 10; ++i)
        Fl::wait(0.02);
    camera.position(x1, y1, z1);
    expect(movingWhileHeld && x0 == x1 && y0 == y1 && z0 == z1,
           "a released W stops the camera on the next tick");
    camera.stop();

    std::printf("%s\n", failures == 0 ? "EditKeysCheck passed" : "EditKeysCheck FAILED");
    return failures == 0 ? 0 : 1;
}
