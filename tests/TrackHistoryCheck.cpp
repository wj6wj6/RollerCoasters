// Checks undo and redo for the control points: each step goes back to the track and selection
// as they were, redo puts it forward again, a new change clears redo, the train keeps its place
// unless the number of points changed, and only the newest kLimit steps are kept.
#include "TrackHistory.h"

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

CTrack makeTrack()
{
    CTrack track;
    track.points.clear();
    for (int i = 0; i < 5; ++i)
        track.points.push_back(ControlPoint(Pnt3f(static_cast<float>(i * 10), 5.0f, 0.0f)));
    track.trainU = 1.5f;
    return track;
}
} // namespace

int main()
{
    CTrack track = makeTrack();
    TrackHistory history;
    int selected = 2;
    expect(!history.canUndo() && !history.canRedo(), "a new history has nothing to do");
    expect(!history.undo(track, selected) && selected == 2,
           "undo with nothing to undo is harmless");

    // move point 2, then add a point after it
    history.record(track, selected);
    track.points[2].pos.y = 40.0f;
    history.record(track, selected);
    track.points.insert(track.points.begin() + 3, ControlPoint(Pnt3f(25.0f, 5.0f, 0.0f)));
    track.trainU = 3.5f; // insertAt moves the train along with the new segment
    selected = 3;

    // the train runs on in the meantime
    track.trainU = 4.25f;
    expect(history.undo(track, selected), "undo the added point");
    expect(track.points.size() == 5 && selected == 2, "the point is gone and point 2 is selected");
    expect(track.trainU == 1.5f, "the number of points changed: the train goes back with it");
    expect(track.points[2].pos.y == 40.0f, "the move before it is still there");

    track.trainU = 2.75f;
    expect(history.undo(track, selected), "undo the move");
    expect(track.points[2].pos.y == 5.0f, "point 2 is back where it was");
    expect(track.trainU == 2.75f, "same number of points: the train stays where it is");
    expect(!history.canUndo() && history.canRedo(), "nothing more to undo, two steps to redo");

    expect(history.redo(track, selected) && track.points[2].pos.y == 40.0f, "redo the move");
    expect(history.redo(track, selected) && track.points.size() == 6 && selected == 3,
           "redo the added point, selected again");
    expect(!history.redo(track, selected), "nothing more to redo");

    // a new change after an undo throws the redo steps away
    history.undo(track, selected);
    history.record(track, selected);
    track.points[0].pos.x = -30.0f;
    expect(!history.canRedo(), "a new change clears redo");

    // deleting the selected point and undoing brings the selection back
    selected = 4;
    history.record(track, selected);
    track.points.erase(track.points.begin() + 4);
    selected = -1;
    history.undo(track, selected);
    expect(track.points.size() == 5 && selected == 4, "undo a delete: the point and its selection");

    // only the newest steps are kept
    TrackHistory longHistory;
    CTrack counted = makeTrack();
    int none = -1;
    for (size_t i = 0; i < TrackHistory::kLimit + 25; ++i)
    {
        longHistory.record(counted, none);
        counted.points[0].pos.x = static_cast<float>(i + 1);
    }
    size_t steps = 0;
    while (longHistory.undo(counted, none))
        ++steps;
    expect(steps == TrackHistory::kLimit, "the oldest steps past the limit are dropped");
    expect(counted.points[0].pos.x == 25.0f, "the oldest step kept is the 26th change");

    std::printf("%s\n", failures == 0 ? "all passed" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
