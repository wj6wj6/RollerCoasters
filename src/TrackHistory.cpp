/************************************************************************
     File:        TrackHistory.cpp

     Comment:     See TrackHistory.h.

*************************************************************************/

#include "TrackHistory.h"

TrackHistory::Snapshot TrackHistory::capture(const CTrack &track, int selected)
{
    Snapshot snapshot;
    snapshot.points = track.points;
    snapshot.trainU = track.trainU;
    snapshot.selected = selected;
    return snapshot;
}

void TrackHistory::push(const Snapshot &before)
{
    m_undo.push_back(before);
    if (m_undo.size() > kLimit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void TrackHistory::restore(const Snapshot &snapshot, CTrack &track, int &selected)
{
    // with the same points the train carries on where it is; otherwise its
    // parameter would land somewhere else on the track, so it goes back too
    if (snapshot.points.size() != track.points.size())
        track.trainU = snapshot.trainU;
    track.points = snapshot.points;
    selected = snapshot.selected;
    if (selected >= static_cast<int>(track.points.size()))
        selected = -1;
}

bool TrackHistory::undo(CTrack &track, int &selected)
{
    if (m_undo.empty())
        return false;
    m_redo.push_back(capture(track, selected));
    const Snapshot before = m_undo.back();
    m_undo.pop_back();
    restore(before, track, selected);
    return true;
}

bool TrackHistory::redo(CTrack &track, int &selected)
{
    if (m_redo.empty())
        return false;
    m_undo.push_back(capture(track, selected));
    const Snapshot after = m_redo.back();
    m_redo.pop_back();
    restore(after, track, selected);
    return true;
}

void TrackHistory::clear()
{
    m_undo.clear();
    m_redo.clear();
}
