/************************************************************************
     File:        EditKeys.h

     Comment:     Which control point editing command a key press is, for
                  TrainView::editKey(). Kept apart from the view so the
                  key map can be tested without a window
                  (tests/EditKeysCheck.cpp).

                  The Fly camera owns W / A / S / D, Space, Shift, Ctrl,
                  E / Q, F, Home and the arrow keys, so in Fly the editing
                  commands avoid them:

                                      World / Top        Fly
                    move (grab)       G                  G
                    tilt              R                  R
                    add a point       Shift + D          Alt + D
                    remove            Delete or X        Delete
                    undo              Ctrl + Z           Ctrl + Z
                    redo              Ctrl + Y or        Ctrl + Y or
                                      Ctrl + Shift + Z   Ctrl + Shift + Z
                    cameras           numpad 1 / 7 / 0   numpad 1 / 7 / 0

                  In Fly, Shift + D would sink and slide the camera and
                  Ctrl + D would fly right faster, so adding takes Alt,
                  which Fly does not use; X is dropped so a stray press
                  while flying cannot delete a point.

*************************************************************************/
#pragma once

namespace EditKeys
{
enum Action
{
    NONE = 0,
    CAMERA_WORLD,
    CAMERA_TOP,
    CAMERA_TRAIN,
    UNDO,
    REDO,
    GRAB,
    TILT,
    ADD_POINT,
    REMOVE
};

// key: an FLTK key (Fl::event_key()); state: Fl::event_state(); fly: the Fly
// camera is on.
Action forKey(int key, int state, bool fly);
} // namespace EditKeys
