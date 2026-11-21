/************************************************************************
     File:        EditKeys.cpp

     Comment:     See EditKeys.h.

*************************************************************************/

#include "EditKeys.h"
#include "Utilities/Input.H"

EditKeys::Action EditKeys::forKey(int key, int state, bool fly)
{
    if (key == Key::Keypad + '1')
        return CAMERA_WORLD;
    if (key == Key::Keypad + '7')
        return CAMERA_TOP;
    if (key == Key::Keypad + '0')
        return CAMERA_TRAIN;
    // like the game mode switch of a block game: building on or off; a
    // function key, so an input method such as Zhuyin cannot swallow it
    if (key == Key::F + 4)
        return TOGGLE_EDIT;
    if (key == 'z' && (state & Mod::Ctrl) != 0)
        return (state & Mod::Shift) != 0 ? REDO : UNDO;
    if (key == 'y' && (state & Mod::Ctrl) != 0)
        return REDO;
    // what the middle button does, for touchpads without one
    if (key == 'c' && (state & (Mod::Ctrl | Mod::Alt)) == 0)
        return PICK_POINT;
    if (key == 'g')
        return GRAB;
    if (key == 'r')
        return TILT;
    if (key == 'd' && (state & (fly ? Mod::Alt : Mod::Shift)) != 0)
        return ADD_POINT;
    if (key == Key::Delete || (key == 'x' && !fly))
        return REMOVE;
    return NONE;
}
