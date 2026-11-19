/************************************************************************
     File:        TrackInsert.cpp

     Comment:     See TrackInsert.h.

*************************************************************************/

#include "TrackInsert.h"

#include <cmath>
#include <initializer_list>

namespace
{
// u brought into [0, count).
float wrap(float u, int count)
{
    const float n = static_cast<float>(count);
    float wrapped = u - std::floor(u / n) * n;
    if (wrapped >= n)
        wrapped = 0.0f;
    return wrapped;
}

// The segment u is on and how far along it, kept off the two ends.
void split(float u, int count, int &segment, float &t)
{
    const float wrapped = wrap(u, count);
    segment = static_cast<int>(wrapped);
    if (segment >= count)
        segment = count - 1;
    t = wrapped - static_cast<float>(segment);
    if (t < TrackInsert::kEdgeMargin)
        t = TrackInsert::kEdgeMargin;
    if (t > 1.0f - TrackInsert::kEdgeMargin)
        t = 1.0f - TrackInsert::kEdgeMargin;
}

float distanceSquared(float ax, float ay, float bx, float by)
{
    return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
}

float distanceSquared(const Pnt3f &a, const Pnt3f &b)
{
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
}

// The tilt halfway along: blended from the two points around it
// (normalize() turns a zero vector straight up).
Pnt3f blendedUp(const ControlPoint &before, const ControlPoint &after, float t)
{
    Pnt3f up = before.orient * (1.0f - t) + after.orient * t;
    up.normalize();
    return up;
}
} // namespace

ControlPoint TrackInsert::pointAt(const CTrack &track, float u, TrackSplineType splineType)
{
    const int count = static_cast<int>(track.points.size());
    if (count < 4)
        return ControlPoint();
    int segment = 0;
    float t = 0.0f;
    split(u, count, segment, t);
    const ControlPoint &before = track.points[static_cast<size_t>(segment)];
    const ControlPoint &after = track.points[static_cast<size_t>((segment + 1) % count)];
    const Pnt3f spot = track.evaluatePosition(static_cast<float>(segment) + t, splineType);

    // Linear and Cardinal tracks pass through their control points, so the
    // point goes on the spot. A B-spline passes (P[i-1] + 4 P[i] + P[i+1]) / 6
    // at point i; the new point sits between `before` and `after`, so solving
    // for it puts the track through the spot.
    Pnt3f pos = spot;
    if (splineType != TRACK_SPLINE_LINEAR && splineType != TRACK_SPLINE_CARDINAL)
        pos = Pnt3f((6.0f * spot.x - before.pos.x - after.pos.x) * 0.25f,
                    (6.0f * spot.y - before.pos.y - after.pos.y) * 0.25f,
                    (6.0f * spot.z - before.pos.z - after.pos.z) * 0.25f);

    return ControlPoint(pos, blendedUp(before, after, t));
}

float TrackInsert::trainAfterInsert(float trainU, float u, int count)
{
    if (count < 1)
        return trainU;
    int segment = 0;
    float t = 0.0f;
    split(u, count, segment, t);
    const float train = wrap(trainU, count);
    const int trainSegment = static_cast<int>(train);
    const float along = train - static_cast<float>(trainSegment);
    if (trainSegment < segment)
        return train;
    if (trainSegment > segment)
        return train + 1.0f; // never past count + 1, the new number of points
    // on the segment that is split: the part before the new point becomes
    // this segment, the part after it the next one
    if (along <= t)
        return static_cast<float>(segment) + along / t;
    return static_cast<float>(segment + 1) + (along - t) / (1.0f - t);
}

int TrackInsert::insertAt(CTrack &track, float u, TrackSplineType splineType)
{
    const int count = static_cast<int>(track.points.size());
    if (count < 4)
        return -1;
    int segment = 0;
    float t = 0.0f;
    split(u, count, segment, t);
    const ControlPoint point = pointAt(track, u, splineType);
    track.trainU = trainAfterInsert(track.trainU, u, count);
    const int index = segment + 1;
    track.points.insert(track.points.begin() + index, point);
    return index;
}

float TrackInsert::nearest(const CTrack &track, const Pnt3f &position, TrackSplineType splineType)
{
    const int count = static_cast<int>(track.points.size());
    if (count < 4)
        return 0.0f;
    // the nearest of evenly spaced samples, then halve the step around it
    const int steps = 48;
    float step = 1.0f / static_cast<float>(steps);
    float bestU = 0.0f;
    float best = distanceSquared(track.evaluatePosition(0.0f, splineType), position);
    for (int i = 1; i < count * steps; ++i)
    {
        const float u = static_cast<float>(i) * step;
        const float d = distanceSquared(track.evaluatePosition(u, splineType), position);
        if (d < best)
        {
            best = d;
            bestU = u;
        }
    }
    for (int round = 0; round < 12; ++round)
    {
        step *= 0.5f;
        for (const float u : {bestU - step, bestU + step})
        {
            const float d = distanceSquared(track.evaluatePosition(u, splineType), position);
            if (d < best)
            {
                best = d;
                bestU = u;
            }
        }
    }
    return wrap(bestU, count);
}

int TrackInsert::insertNear(CTrack &track, const Pnt3f &position, TrackSplineType splineType)
{
    const int count = static_cast<int>(track.points.size());
    if (count < 4)
        return -1;
    const float u = nearest(track, position, splineType);
    int segment = 0;
    float t = 0.0f;
    split(u, count, segment, t);
    const ControlPoint &before = track.points[static_cast<size_t>(segment)];
    const ControlPoint &after = track.points[static_cast<size_t>((segment + 1) % count)];
    const ControlPoint point(position, blendedUp(before, after, t));
    track.trainU = trainAfterInsert(track.trainU, u, count);
    const int index = segment + 1;
    track.points.insert(track.points.begin() + index, point);
    return index;
}

bool TrackInsert::pick(const CTrack &track, TrackSplineType splineType,
                       const TransformGizmo::Camera &camera, int mouseX, int mouseY,
                       float maxPixels, float &u)
{
    const int count = static_cast<int>(track.points.size());
    if (count < 4)
        return false;
    // Walk the track in short pieces and find the piece that passes
    // nearest the mouse on screen; pieces behind the camera are skipped.
    const int steps = 48;
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    float best = maxPixels * maxPixels;
    bool found = false;
    float lastX = 0.0f, lastY = 0.0f;
    bool lastSeen = false;
    for (int i = 0; i <= count * steps; ++i)
    {
        const float here = static_cast<float>(i) / static_cast<float>(steps);
        const Pnt3f p = track.evaluatePosition(here, splineType);
        const float position[3] = {p.x, p.y, p.z};
        float sx = 0.0f, sy = 0.0f;
        const bool seen = TransformGizmo::project(camera, position, sx, sy);
        if (seen && lastSeen)
        {
            // nearest spot on the piece from (lastX, lastY) to (sx, sy)
            const float dx = sx - lastX;
            const float dy = sy - lastY;
            const float lengthSquared = dx * dx + dy * dy;
            float along = 0.0f;
            if (lengthSquared > 1.0e-6f)
                along = ((mx - lastX) * dx + (my - lastY) * dy) / lengthSquared;
            if (along < 0.0f)
                along = 0.0f;
            if (along > 1.0f)
                along = 1.0f;
            const float d = distanceSquared(mx, my, lastX + dx * along, lastY + dy * along);
            if (d <= best)
            {
                best = d;
                found = true;
                u = (static_cast<float>(i - 1) + along) / static_cast<float>(steps);
            }
        }
        lastX = sx;
        lastY = sy;
        lastSeen = seen;
    }
    if (found)
        u = wrap(u, count);
    return found;
}

int TrackInsert::pickPoint(const CTrack &track, const TransformGizmo::Camera &camera, int mouseX,
                           int mouseY, float maxPixels)
{
    int found = -1;
    float best = maxPixels * maxPixels;
    for (size_t i = 0; i < track.points.size(); ++i)
    {
        const Pnt3f &p = track.points[i].pos;
        const float position[3] = {p.x, p.y, p.z};
        float sx = 0.0f, sy = 0.0f;
        if (!TransformGizmo::project(camera, position, sx, sy))
            continue;
        const float d =
            distanceSquared(sx, sy, static_cast<float>(mouseX), static_cast<float>(mouseY));
        if (d <= best)
        {
            best = d;
            found = static_cast<int>(i);
        }
    }
    return found;
}

bool TrackInsert::removeAt(CTrack &track, int index)
{
    const int count = static_cast<int>(track.points.size());
    if (count <= 4 || index < 0 || index >= count)
        return false;
    // The segments before and after the point become one: the train on
    // either half goes to the same half of the joined segment.
    const int before = (index + count - 1) % count;
    const int joined = before > index ? before - 1 : before;
    const float train = wrap(track.trainU, count);
    const int segment = static_cast<int>(train);
    const float along = train - static_cast<float>(segment);
    float moved = 0.0f;
    if (segment == before)
        moved = static_cast<float>(joined) + along * 0.5f;
    else if (segment == index)
        moved = static_cast<float>(joined) + 0.5f + along * 0.5f;
    else
        moved = static_cast<float>(segment > index ? segment - 1 : segment) + along;
    track.points.erase(track.points.begin() + index);
    track.trainU = wrap(moved, count - 1);
    return true;
}
