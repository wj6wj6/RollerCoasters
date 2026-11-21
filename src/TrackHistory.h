/************************************************************************
     File:        TrackHistory.h

     Comment:     Undo and redo for the control points (Ctrl + Z,
                  Ctrl + Y or Ctrl + Shift + Z, the Undo and Redo
                  buttons).

                  Every change to the points saves the whole track as it
                  was just before: adding, deleting, moving and tilting a
                  point, the X/Y/Z nudges, R+X ... R-Z, Load, Reset
                  and the Grand Tour. A track is a few dozen points, so a
                  copy each time is cheap and cannot get out of step the
                  way a list of individual changes could.

                  The selected point goes back with the track, so the
                  change undone is the one in view. The train stays where
                  it is unless the number of points changed, when it goes
                  back to where it was with that track.

                  No window or OpenGL is needed: tests/TrackHistoryCheck.cpp.
*************************************************************************/
#pragma once

#include <cstddef>
#include <vector>

#include "Track.H"

class TrackHistory
{
  public:
    // The most steps kept; the oldest is dropped after that.
    static const size_t kLimit = 200;

    struct Snapshot
    {
        std::vector<ControlPoint> points;
        float trainU = 0.0f;
        int selected = -1;
    };

    static Snapshot capture(const CTrack &track, int selected);

    // Remember `before`, the track just before a change. Clears redo.
    void push(const Snapshot &before);
    // The same with the track as it is now, for a change about to be made.
    void record(const CTrack &track, int selected)
    {
        push(capture(track, selected));
    }

    // Go one step back or forward. False (and nothing changed) at either end.
    bool undo(CTrack &track, int &selected);
    bool redo(CTrack &track, int &selected);

    bool canUndo() const
    {
        return !m_undo.empty();
    }
    bool canRedo() const
    {
        return !m_redo.empty();
    }
    void clear();

  private:
    static void restore(const Snapshot &snapshot, CTrack &track, int &selected);

    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
};
