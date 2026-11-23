/************************************************************************
     File:        EditKeys.cpp

     Comment:     See EditKeys.h.

*************************************************************************/

#include "EditKeys.h"
#include "KeyBinds.h"

EditKeys::Action EditKeys::forKey(const KeyBinds &keys, int key, int state, bool fly)
{
    using Bind = KeyBinds::Action;
    // in this order, so a key bound to two of them does the first
    const struct
    {
        Bind bind;
        Action action;
    } table[] = {{Bind::CameraWorld, CAMERA_WORLD},
                 {Bind::CameraTop, CAMERA_TOP},
                 {Bind::CameraTrain, CAMERA_TRAIN},
                 // like the game mode switch of a block game: building on or off
                 {Bind::ToggleEdit, TOGGLE_EDIT},
                 {Bind::Undo, UNDO},
                 {Bind::Redo, REDO},
                 // what the middle button does, for touchpads without one
                 {Bind::Pick, PICK_POINT},
                 {Bind::Grab, GRAB},
                 {Bind::Tilt, TILT},
                 {fly ? Bind::FlyAddPoint : Bind::AddPoint, ADD_POINT},
                 {fly ? Bind::FlyRemovePoint : Bind::RemovePoint, REMOVE}};
    for (const auto &entry : table)
        if (keys.matches(entry.bind, key, state))
            return entry.action;
    return NONE;
}
