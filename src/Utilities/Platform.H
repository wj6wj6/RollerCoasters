/************************************************************************
     File:        Platform.H

     Comment:     Timers and the event loop, for code that should not depend
                  on the window toolkit (the cameras, the touchpad, the
                  view's animations). PlatformFltk.cpp implements them with
                  FLTK; a later window layer only has to provide this file's
                  functions again.

                  The timers follow FLTK's model: a callback runs once, after
                  at least the given time, from the event loop. Inside the
                  callback, repeatTimeout() schedules it again measured from
                  when it was due, so a steady rate does not drift.

*************************************************************************/
#pragma once

namespace platform
{
using TimerCallback = void (*)(void *data);

void addTimeout(double seconds, TimerCallback callback, void *data);
// Only from inside a timer callback; elsewhere it is the same as addTimeout.
void repeatTimeout(double seconds, TimerCallback callback, void *data);
// Cancels every pending call of callback with this data.
void removeTimeout(TimerCallback callback, void *data);
bool hasTimeout(TimerCallback callback, void *data);

// Run whatever events and timers are due now, without waiting. Tests use it as
// the event loop; the program's own loop runs the timers by itself.
void processPending();
// Wait up to seconds for events or timers, then run what is due.
void wait(double seconds);

// The next Push does not count as a double click of the last one.
void forgetClick();
} // namespace platform
