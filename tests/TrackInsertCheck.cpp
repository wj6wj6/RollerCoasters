// Checks adding a control point on the track: the track still passes through the spot clicked,
// the new point goes between the two points around it with a blended tilt, the train keeps its
// place, and the spot under the mouse is found on screen.
#include "TrackInsert.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

float distance(const Pnt3f &a, const Pnt3f &b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A lumpy closed loop of six points, the third one tilted.
CTrack makeTrack()
{
    CTrack track;
    track.points.clear();
    track.points.push_back(ControlPoint(Pnt3f(50.0f, 5.0f, 0.0f)));
    track.points.push_back(ControlPoint(Pnt3f(25.0f, 12.0f, 40.0f)));
    track.points.push_back(ControlPoint(Pnt3f(-20.0f, 3.0f, 45.0f), Pnt3f(1.0f, 0.0f, 0.0f)));
    track.points.push_back(ControlPoint(Pnt3f(-50.0f, 8.0f, 0.0f)));
    track.points.push_back(ControlPoint(Pnt3f(-25.0f, 20.0f, -40.0f)));
    track.points.push_back(ControlPoint(Pnt3f(30.0f, 4.0f, -45.0f)));
    track.trainU = 0.0f;
    return track;
}

// A camera straight above the origin looking down, x right and -z up the
// screen, 800 x 600 pixels, orthographic, 1 unit = 4 pixels.
TransformGizmo::Camera topCamera()
{
    TransformGizmo::Camera camera = {};
    double *m = camera.view; // rotate -90 degrees about x, eye at y = 100
    m[0] = 1.0;
    m[6] = -1.0;
    m[9] = 1.0;
    m[14] = -100.0;
    m[15] = 1.0;
    double *p = camera.projection; // x in [-100, 100], y in [-75, 75]
    p[0] = 1.0 / 100.0;
    p[5] = 1.0 / 75.0;
    p[10] = -2.0 / 999.0;
    p[14] = -1001.0 / 999.0;
    p[15] = 1.0;
    camera.viewport[2] = 800;
    camera.viewport[3] = 600;
    return camera;
}

void checkSpline(TrackSplineType type, const char *name)
{
    char what[160];
    for (float u : {0.5f, 2.3f, 5.8f})
    {
        CTrack track = makeTrack();
        const Pnt3f spot = track.evaluatePosition(u, type);
        const int index = TrackInsert::insertAt(track, u, type);
        const int segment = static_cast<int>(u);
        std::snprintf(what, sizeof(what), "%s u=%.1f: the new point goes in after point %d", name,
                      static_cast<double>(u), segment);
        expect(index == segment + 1 && track.points.size() == 7, what);
        const Pnt3f through = track.evaluatePosition(static_cast<float>(index), type);
        std::snprintf(what, sizeof(what), "%s u=%.1f: the track still passes the spot (off %.4f)",
                      name, static_cast<double>(u), static_cast<double>(distance(through, spot)));
        expect(distance(through, spot) < 1.0e-3f, what);
    }
}
} // namespace

int main()
{
    checkSpline(TRACK_SPLINE_LINEAR, "Linear");
    checkSpline(TRACK_SPLINE_CARDINAL, "Cardinal");
    checkSpline(TRACK_SPLINE_BSPLINE, "B-spline");

    // tilt halfway between straight up (point 1) and +x (point 2)
    {
        CTrack track = makeTrack();
        const ControlPoint point = TrackInsert::pointAt(track, 1.5f, TRACK_SPLINE_CARDINAL);
        const float h = std::sqrt(0.5f);
        expect(std::fabs(point.orient.x - h) < 1.0e-4f && std::fabs(point.orient.y - h) < 1.0e-4f,
               "the tilt is blended from the points around it");
    }

    // a click right on an existing point does not stack a new one on it
    {
        CTrack track = makeTrack();
        const int index = TrackInsert::insertAt(track, 3.0f, TRACK_SPLINE_CARDINAL);
        expect(distance(track.points[static_cast<size_t>(index)].pos, track.points[3].pos) > 1.0f,
               "a click on a point goes in a little after it");
    }

    // the train stays where it was on every kind of segment
    {
        const int count = 6;
        expect(TrackInsert::trainAfterInsert(1.25f, 3.5f, count) == 1.25f,
               "train before the new point keeps its parameter");
        expect(TrackInsert::trainAfterInsert(4.25f, 3.5f, count) == 5.25f,
               "train after the new point moves up a segment");
        expect(std::fabs(TrackInsert::trainAfterInsert(3.25f, 3.5f, count) - 3.5f) < 1.0e-5f,
               "train on the split segment, before the point, stays on the first half");
        expect(std::fabs(TrackInsert::trainAfterInsert(3.75f, 3.5f, count) - 4.5f) < 1.0e-5f,
               "train on the split segment, after the point, goes to the second half");
        // the old bug: the train on the last segment jumped back to the start
        const float last = TrackInsert::trainAfterInsert(5.5f, 1.5f, count);
        expect(std::fabs(last - 6.5f) < 1.0e-5f, "train on the last segment does not wrap to 0");

        CTrack track = makeTrack();
        track.trainU = 5.5f;
        const Pnt3f before = track.evaluatePosition(track.trainU, TRACK_SPLINE_LINEAR);
        TrackInsert::insertAt(track, 1.5f, TRACK_SPLINE_LINEAR);
        const Pnt3f after = track.evaluatePosition(track.trainU, TRACK_SPLINE_LINEAR);
        expect(distance(before, after) < 1.0e-3f, "the train is in the same place after an insert");
    }

    // Fly: a point put off the track, at the crosshair
    {
        CTrack track = makeTrack();
        const Pnt3f onTrack = track.evaluatePosition(4.4f, TRACK_SPLINE_CARDINAL);
        const float u = TrackInsert::nearest(track, onTrack, TRACK_SPLINE_CARDINAL);
        expect(std::fabs(u - 4.4f) < 0.01f,
               "the nearest spot to a point on the track is that spot");

        const Pnt3f above(onTrack.x, onTrack.y + 15.0f, onTrack.z);
        track.trainU = 5.5f;
        const int index = TrackInsert::insertNear(track, above, TRACK_SPLINE_CARDINAL);
        expect(index == 5 && track.points.size() == 7,
               "a point above segment 4 goes in between points 4 and 5");
        expect(distance(track.points[5].pos, above) < 1.0e-4f, "it is exactly where it was put");
        expect(std::fabs(track.trainU - 6.5f) < 1.0e-5f, "the train after it moves up one");
    }

    // picking the track on screen
    {
        CTrack track = makeTrack();
        const TransformGizmo::Camera camera = topCamera();
        const Pnt3f spot = track.evaluatePosition(2.4f, TRACK_SPLINE_BSPLINE);
        const float position[3] = {spot.x, spot.y, spot.z};
        float sx = 0.0f, sy = 0.0f;
        TransformGizmo::project(camera, position, sx, sy);
        float u = -1.0f;
        const bool hit =
            TrackInsert::pick(track, TRACK_SPLINE_BSPLINE, camera, static_cast<int>(sx + 0.5f),
                              static_cast<int>(sy + 0.5f) + 3, 12.0f, u);
        const Pnt3f found = track.evaluatePosition(u, TRACK_SPLINE_BSPLINE);
        expect(hit && distance(found, spot) < 2.0f, "a click 3 pixels off the track finds it");
        expect(!TrackInsert::pick(track, TRACK_SPLINE_BSPLINE, camera, 400, 300, 12.0f, u),
               "a click in the middle of the loop finds nothing");
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
