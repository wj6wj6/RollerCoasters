/************************************************************************
     File:        PlatformFltk.H

     Comment:     The FLTK side of Input.H: turns the FLTK event being
                  handled into an InputEvent. Only the FLTK window classes
                  include this; everything else reads InputEvent.

*************************************************************************/
#pragma once

#include "Input.H"

namespace platform
{
// The event FLTK is delivering now (Fl::event_x() and so on), as an InputEvent
// of the given FLTK event type. Unknown types give InputType::None.
InputEvent fltkEvent(int flEvent);
} // namespace platform
