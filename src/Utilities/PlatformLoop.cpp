/************************************************************************
     File:        PlatformLoop.cpp

     Comment:     Platform.H without a window toolkit: the timers are a
                  list this file keeps, run by processPending() and wait().
                  The window layer hands in how to read its events
                  (setEventPump); tests have none, so wait() just sleeps
                  until the next timer is due.

*************************************************************************/

#include "Platform.H"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
struct Timer
{
    double due; // seconds, platform::now()
    platform::TimerCallback callback;
    void *data;
};

std::vector<Timer> &timers()
{
    static std::vector<Timer> list;
    return list;
}

// While a callback runs: when it was due, so repeatTimeout() keeps the pace.
bool g_inCallback = false;
double g_callbackDue = 0.0;
platform::EventPump g_pump = nullptr;
bool g_forgetClick = false;
} // namespace

double platform::now()
{
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

void platform::addTimeout(double seconds, TimerCallback callback, void *data)
{
    timers().push_back({now() + seconds, callback, data});
}

void platform::repeatTimeout(double seconds, TimerCallback callback, void *data)
{
    if (!g_inCallback)
    {
        addTimeout(seconds, callback, data);
        return;
    }
    // From when it was due; if the loop fell far behind, from now instead of
    // running a burst of calls to catch up.
    double due = g_callbackDue + seconds;
    if (due < now())
        due = now();
    timers().push_back({due, callback, data});
}

void platform::removeTimeout(TimerCallback callback, void *data)
{
    std::vector<Timer> &list = timers();
    list.erase(
        std::remove_if(list.begin(), list.end(),
                       [&](const Timer &t) { return t.callback == callback && t.data == data; }),
        list.end());
}

bool platform::hasTimeout(TimerCallback callback, void *data)
{
    for (const Timer &t : timers())
        if (t.callback == callback && t.data == data)
            return true;
    return false;
}

double platform::secondsToNextTimeout()
{
    const std::vector<Timer> &list = timers();
    if (list.empty())
        return -1.0;
    double first = list.front().due;
    for (const Timer &t : list)
        first = std::min(first, t.due);
    return std::max(0.0, first - now());
}

void platform::runDueTimeouts()
{
    // Only the timers due when this starts: one that adds itself again for
    // "now" runs on the next call, so a zero timeout cannot loop forever.
    const double start = now();
    for (;;)
    {
        std::vector<Timer> &list = timers();
        auto next = std::min_element(list.begin(), list.end(),
                                     [](const Timer &a, const Timer &b) { return a.due < b.due; });
        if (next == list.end() || next->due > start)
            return;
        const Timer timer = *next;
        list.erase(next);
        g_inCallback = true;
        g_callbackDue = timer.due;
        timer.callback(timer.data);
        g_inCallback = false;
    }
}

void platform::setEventPump(EventPump pump)
{
    g_pump = pump;
}

void platform::processPending()
{
    if (g_pump)
        g_pump(0.0);
    runDueTimeouts();
}

void platform::wait(double seconds)
{
    const double next = secondsToNextTimeout();
    if (next >= 0.0 && next < seconds)
        seconds = next;
    if (g_pump)
        g_pump(seconds);
    else if (seconds > 0.0)
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    runDueTimeouts();
}

void platform::forgetClick()
{
    g_forgetClick = true;
}

bool platform::takeForgetClick()
{
    const bool forget = g_forgetClick;
    g_forgetClick = false;
    return forget;
}
