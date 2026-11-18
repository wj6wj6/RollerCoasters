/************************************************************************
     File:        EditKeys.cpp

     Comment:     See EditKeys.h.

*************************************************************************/

#include "EditKeys.h"

// The same guard as the other FLTK includes: FLTK's headers convert between
// pointers and long (C4311, C4312); it covers this one include only.
#pragma warning(push)
#pragma warning(disable : 4311)
#pragma warning(disable : 4312)
#include <FL/Enumerations.H>
#pragma warning(pop)

EditKeys::Action EditKeys::forKey(int key, int state, bool fly)
{
    if (key == FL_KP + '1')
        return CAMERA_WORLD;
    if (key == FL_KP + '7')
        return CAMERA_TOP;
    if (key == FL_KP + '0')
        return CAMERA_TRAIN;
    if (key == 'z' && (state & FL_CTRL) != 0)
        return UNDO;
    if (key == 'g')
        return GRAB;
    if (key == 'r')
        return TILT;
    if (key == 'd' && (state & (fly ? FL_ALT : FL_SHIFT)) != 0)
        return DUPLICATE_POINT;
    if (key == FL_Delete || (key == 'x' && !fly))
        return REMOVE;
    return NONE;
}
