/************************************************************************
     File:        Platform.H

     Comment:     Timers and the event loop, for code that should not depend
                  on the window toolkit (the cameras, the touchpad, the
                  view's animations). PlatformLoop.cpp implements them; the
                  window (TrainWindow.cpp) hands it how to read the window's
                  events and runs the due timers from its loop.

                  A callback runs once, after at least the given time, from
                  the event loop. Inside the callback, repeatTimeout()
                  schedules it again measured from when it was due, so a
                  steady rate does not drift.

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
// For the window layer, which counts the clicks: was forgetClick() called
// since the last time this was asked?
bool takeForgetClick();

// Seconds since the program started.
double now();
// For the window layer's loop: how long until the next timer is due (0 when
// one is due now, negative when there is none), and run the due ones.
double secondsToNextTimeout();
void runDueTimeouts();
// How the window's events are read: wait up to seconds for one (0: only
// what is there now) and dispatch them. Without one, wait() sleeps.
using EventPump = void (*)(double seconds);
void setEventPump(EventPump pump);
} // namespace platform
