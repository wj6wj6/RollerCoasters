/************************************************************************
     File:        TrainView.cpp

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     
						The TrainView is the window that actually shows the 
						train. Its a
						GL display canvas (Fl_Gl_Window).  It is held within 
						a TrainWindow
						that is the outer window with all the widgets. 
						The TrainView needs 
						to be aware of the window - since it might need to 
						check the widgets to see how to draw

	  Note:        we need to have pointers to this, but maybe not know 
						about it (beware circular references)

     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/

#include <chrono>
#include <iostream>
#include <math.h>
#include <FL/Fl.H>

// we will need OpenGL, and OpenGL needs windows.h
#include <windows.h>
#include <imm.h>
//#include "GL/gl.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "GL/GLU.h"

#include "TrainView.H"
#include "TrainWindow.H"
#include "Utilities/3DUtils.h"
#include "TrackRenderer.H"
#include "CallBacks.H"
#include "TrainModel.h"
#include "EditKeys.h"
#include <FL/x.H>
#include <FL/gl.h>

// Touchpads send wheel deltas smaller than one notch. A burst that contains any
// such delta stays precise for a moment, so a stray whole notch is not smoothed.
static bool isPreciseWheel(int delta)
{
    static std::chrono::steady_clock::time_point lastPrecise;
    const auto now = std::chrono::steady_clock::now();
    if (delta % WHEEL_DELTA != 0)
    {
        lastPrecise = now;
        return true;
    }
    return now - lastPrecise < std::chrono::milliseconds(300);
}

// FLTK 1.3 rounds wheel messages to whole notches, which makes touchpad scrolling
// and pinching move in coarse, delayed steps. Read the exact delta here instead.
// The modifier flags come from the message, keeping the synthetic Ctrl of pinch.
static int touchpadWheel(void *event, void *data)
{
    MSG *message = static_cast<MSG *>(event);
    if (message->message != WM_MOUSEWHEEL && message->message != WM_MOUSEHWHEEL)
        return 0;
    TrainView *view = static_cast<TrainView *>(data);
    if (!view->tw || !view->shown())
        return 0;
    const bool fly = view->tw->flyCam->value() != 0;
    const bool train = view->tw->trainCam->value() != 0;
    if (!view->tw->worldCam->value() && !fly && !train)
        return 0;
    HWND canvas = fl_xid(view);
    if (!canvas || GetAncestor(message->hwnd, GA_ROOT) != GetAncestor(canvas, GA_ROOT))
        return 0;
    RECT bounds;
    if (!GetWindowRect(canvas, &bounds))
        return 0;
    POINT position = {(short)LOWORD(message->lParam), (short)HIWORD(message->lParam)};
    if (!PtInRect(&bounds, position))
        return 0;
    const int delta = (short)HIWORD(message->wParam);
    const WORD keys = LOWORD(message->wParam);
    int state = 0;
    if (keys & MK_CONTROL)
        state |= FL_CTRL;
    if (keys & MK_SHIFT)
        state |= FL_SHIFT;
    if (GetKeyState(VK_MENU) < 0)
        state |= FL_ALT;
    const float steps = (float)delta / WHEEL_DELTA;
    const bool precise = isPreciseWheel(delta);
    // Riding the train, touchpad scrolling turns the head; wheel notches do nothing.
    if (train && (!precise || (state & FL_CTRL)))
        return 1;
    // Match FLTK's signs: wheel up is -dy, tilting right is +dx.
    const float dx = message->message == WM_MOUSEWHEEL ? 0.0f : steps;
    const float dy = message->message == WM_MOUSEWHEEL ? -steps : 0.0f;
    if (train)
    {
        view->headLook.scroll(dx, dy);
        view->damage(1);
    }
    else if (fly)
    {
        view->flyCam.scroll(dx, dy, state, precise);
        // The mouse wheel changes the flying speed; keep the slider in step.
        view->tw->flySpeed->value(view->flyCam.speed());
    }
    else
        view->arcball.scroll(dx, dy, state, precise);
    return 1;
}

// In Fly mode, take the flying keys before FLTK and the input method see them
// and hand them to the view as FLTK key events.
static int flyKeys(void *event, void *data)
{
    MSG *message = static_cast<MSG *>(event);
    if (message->message != WM_KEYDOWN && message->message != WM_KEYUP)
        return 0;
    TrainView *view = static_cast<TrainView *>(data);
    if (!view->tw || !view->shown())
        return 0;
    const bool train = view->tw->trainCam->value() != 0;
    if (!view->tw->flyCam->value() && !train)
        return 0;
    HWND canvas = fl_xid(view);
    if (!canvas || GetAncestor(message->hwnd, GA_ROOT) != GetAncestor(canvas, GA_ROOT))
        return 0;
    // Only when the view has the keyboard, so typing in a panel widget still works.
    if (train && Fl::focus() != view)
        return 0;
    const unsigned scan = (unsigned)(message->lParam >> 16) & 0xFF;
    const bool extended = ((message->lParam >> 24) & 1) != 0;
    const int key = FlyCam::keyForScanCode(scan, extended);
    // Riding the train, only R (look ahead again) is read this way.
    if (!key || (train && key != 'r'))
        return 0;
    if (train)
    {
        if (message->message == WM_KEYDOWN)
            view->recenterHead();
        return 1;
    }
    Fl::e_keysym = key;
    // FLTK never saw this message, so take the modifiers from the keyboard:
    // editing commands such as Ctrl + snapping read them.
    int state = Fl::e_state & ~(FL_SHIFT | FL_CTRL | FL_ALT);
    if (GetKeyState(VK_SHIFT) < 0)
        state |= FL_SHIFT;
    if (GetKeyState(VK_CONTROL) < 0)
        state |= FL_CTRL;
    if (GetKeyState(VK_MENU) < 0)
        state |= FL_ALT;
    Fl::e_state = state;
    view->handle(message->message == WM_KEYDOWN ? FL_KEYDOWN : FL_KEYUP);
    return 1;
}

static bool touchpadAccepts(void *data)
{
    TrainView *view = static_cast<TrainView *>(data);
    return view->tw && (view->tw->worldCam->value() || view->tw->flyCam->value() ||
                        view->tw->trainCam->value());
}

// Direct Manipulation reports finger movement and pinch together, so the
// camera orbits (Alt or Shift pans) and zooms in the same gesture.
static void touchpadMoves(void *data, float dx, float dy, float scale)
{
    if (!touchpadAccepts(data))
        return;
    const bool alt = GetKeyState(VK_MENU) < 0;
    const bool pan = alt || GetKeyState(VK_SHIFT) < 0;
    TrainView *view = static_cast<TrainView *>(data);
    // Riding the train, two fingers turn the head; spreading does nothing.
    if (view->tw->trainCam->value())
    {
        if (!view->headDragging && (dx != 0 || dy != 0))
        {
            view->headLook.touchpadGesture(dx, dy);
            view->damage(1);
        }
    }
    // In Fly mode Shift sinks the camera, so only Alt slides.
    else if (view->tw->flyCam->value())
        view->flyCam.touchpadGesture(dx, dy, scale, alt);
    else
        view->arcball.touchpadGesture(dx, dy, scale, pan);
}

#ifdef EXAMPLE_SOLUTION
#include "TrainExample/TrainExample.H"
#endif

// The field of view the World, Train, Chase and Fly cameras start with, in
// degrees; the FOV slider changes all of them (TrainView::setFieldOfView).
static const float startFieldOfView = 70.0f;
// The World camera's start: this far away with a 40 degree view frames the
// editor's island; a wider view starts as much closer. The scenery island is
// WorldShape::kScale times wider, so the camera starts that much further out
// (a little more, for the mountains in front).
static const float worldStartDistance = 250.0f;
static const float sceneryStartDistance = 750.0f;
static const float worldStartFieldOfView = 40.0f;
// The lowest the observer camera can fly. Tracks may dive below the floor at
// y = 0 (into the sea, say), so the camera can follow them well under it.
static const float flyLowest = -200.0f;

//************************************************************************
//
// * Constructor to set up the GL window
//========================================================================
TrainView::TrainView(int x, int y, int w, int h, const char *l) : Fl_Gl_Window(x, y, w, h, l)
//========================================================================
{
    tw = 0;
    selectedCube = -1;
    mouseCaptured = false;
    m_fieldOfView = startFieldOfView;
    headDragging = false;
    headDragX = 0;
    headDragY = 0;
    mode(FL_RGB | FL_ALPHA | FL_DOUBLE | FL_STENCIL);

    resetArcball();
    flyCam.setup(this, m_fieldOfView);
    flyCam.setGround(flyLowest - FlyCam::minHeight); // the eye stays minHeight above it
    Fl::add_system_handler(touchpadWheel, this);
    Fl::add_system_handler(flyKeys, this);
}

TrainView::~TrainView()
{
    if (mouseCaptured)
        ClipCursor(nullptr);
    Fl::remove_system_handler(touchpadWheel);
    Fl::remove_system_handler(flyKeys);
}

//************************************************************************
//
// * Reset the camera to look at the world
//========================================================================
void TrainView::resetArcball()
//========================================================================
{
    // Set up the camera to look at the world
    // these parameters might seem magical, and they kindof are
    // a little trial and error goes a long way
    const float halfAngle = 3.14159265f / 360.0f;
    const float start = tw && sceneryOn() ? sceneryStartDistance : worldStartDistance;
    const float distance =
        start * tanf(worldStartFieldOfView * halfAngle) / tanf(m_fieldOfView * halfAngle);
    arcball.setup(this, m_fieldOfView, distance, .2f, .4f, 0);
}

//************************************************************************
//
// * The FOV slider: one field of view for every perspective camera (the Top
//   camera is orthographic)
//========================================================================
void TrainView::setFieldOfView(float degrees)
//========================================================================
{
    m_fieldOfView = degrees;
    arcball.setFieldOfView(degrees);
    flyCam.setFieldOfView(degrees);
    damage(1);
}

//************************************************************************
//
// * Start the observer camera exactly where the last frame not drawn by Fly
//   was seen from (World, Train or Chase), so switching to Fly does not
//   jump. The view keeps its direction; only a banked train view loses its
//   roll, since Fly keeps the horizon level.
//========================================================================
void TrainView::placeFlyCamAtCurrentView()
//========================================================================
{
    if (!m_flyHandoverValid)
    {
        placeFlyCamAtWorldView();
        return;
    }
    if (m_flyHandoverTop)
    {
        // The Top view has no eye point: look straight down from the height
        // at which the same width of ground fills the view.
        const float aspect = h() > 0 ? static_cast<float>(w()) / static_cast<float>(h()) : 1.0f;
        const float half = topViewHalf();
        const float halfHeight = aspect >= 1.0f ? half : half / aspect;
        const float height = halfHeight / tanf(m_fieldOfView * 3.14159265f / 360.0f);
        float centreX, centreZ;
        topViewCentre(centreX, centreZ);
        flyCam.setPose(centreX, height, centreZ, 0.0f, -89.0f);
    }
    else
    {
        GLfloat view[16];
        for (int i = 0; i < 16; ++i)
            view[i] = static_cast<GLfloat>(m_flyHandover[i]);
        flyCam.setFromViewMatrix(view);
    }
    damage(1);
}

//************************************************************************
//
// * Start the observer camera where the world camera currently looks from
//========================================================================
void TrainView::placeFlyCamAtWorldView()
//========================================================================
{
    make_current();
    arcball.setProjection();
    GLfloat view[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    flyCam.setFromViewMatrix(view); // the same field of view: no correction
    damage(1);
}

//========================================================================
void TrainView::centerCursor()
//========================================================================
{
    POINT center = {w() / 2, h() / 2};
    HWND canvas = fl_xid(this);
    if (canvas && ClientToScreen(canvas, &center))
        SetCursorPos(center.x, center.y);
}

//========================================================================
void TrainView::setMouseCaptured(bool captured)
//========================================================================
{
    if (captured == mouseCaptured)
        return;
    mouseCaptured = captured;
    HWND canvas = fl_xid(this);
    if (captured)
    {
        cursor(FL_CURSOR_NONE);
        // Keep the hidden cursor over the view so no move is lost at an edge.
        RECT bounds;
        if (canvas && GetWindowRect(canvas, &bounds))
            ClipCursor(&bounds);
        centerCursor();
    }
    else
    {
        ClipCursor(nullptr);
        cursor(FL_CURSOR_DEFAULT);
    }
    damage(1);
}

//========================================================================
void TrainView::cameraChanged()
//========================================================================
{
    // A first-person view that keeps spinning after a flick is disorienting.
    touchpad.inertia = !tw->flyCam->value() && !tw->trainCam->value();
    setInputMethodEnabled(!tw->flyCam->value());
    // Each ride starts looking ahead.
    headDragging = false;
    headLook.reset();
    if (!tw->flyCam->value())
    {
        setMouseCaptured(false);
        flyCam.stop();
        return;
    }
    placeFlyCamAtCurrentView();
    // Take keyboard focus from the camera button so WASD works at once.
    Fl::focus(this);
}

//========================================================================
void TrainView::setInputMethodEnabled(bool enabled)
//========================================================================
{
    // A Chinese input method would catch WASD for its composition window, so
    // switch it off for this window while flying and back on afterwards.
    HWND canvas = fl_xid(this);
    if (!canvas)
        return;
    const DWORD flags = enabled ? IACE_DEFAULT : 0;
    // A null context is documented for these flags (0 detaches the input
    // method, IACE_DEFAULT restores it), but the SAL annotation says _In_.
#pragma warning(suppress : 6387)
    (void)ImmAssociateContextEx(canvas, nullptr, flags);
#pragma warning(suppress : 6387)
    (void)ImmAssociateContextEx(GetAncestor(canvas, GA_ROOT), nullptr, flags);
}

//========================================================================
void TrainView::recenterHead()
//========================================================================
{
    headLook.reset();
    damage(1);
}

//************************************************************************
//
// * Mouse input for the Train camera: any drag turns the rider's head, a
//   double click looks ahead again. Control points are hidden while riding,
//   so clicks never pick them. Returns 1 when the event was used.
//========================================================================
int TrainView::handleTrainLook(int event)
//========================================================================
{
    switch (event)
    {
    case FL_PUSH:
        if (Fl::event_button() == FL_MIDDLE_MOUSE)
            break;
        Fl::focus(this);
        if (Fl::event_clicks())
        {
            recenterHead();
            headDragging = false;
            // The next press starts a new drag rather than counting as a third click.
            Fl::event_is_click(0);
            return 1;
        }
        headDragging = true;
        headDragX = Fl::event_x();
        headDragY = Fl::event_y();
        return 1;
    case FL_DRAG:
        if (!headDragging)
            break;
        headLook.drag((float)(Fl::event_x() - headDragX), (float)(Fl::event_y() - headDragY));
        headDragX = Fl::event_x();
        headDragY = Fl::event_y();
        damage(1);
        return 1;
    case FL_RELEASE:
        if (!headDragging)
            break;
        headDragging = false;
        return 1;
    case FL_KEYBOARD:
        // R (or Home) looks ahead again; the scan-code handler covers input methods.
        if (Fl::event_key() == 'r' || Fl::event_key() == FL_Home)
        {
            recenterHead();
            return 1;
        }
        break;
    }
    return 0;
}

//************************************************************************
//
// * FlTk Event handler for the window
//########################################################################
// TODO:
//       if you want to make the train respond to other events
//       (like key presses), you might want to hack this.
//########################################################################
//========================================================================
int TrainView::handle(int event)
{
    // an edit in progress sees every event before the camera does
    if (m_editMode != EDIT_IDLE && editEvent(event))
        return 1;

    // see if the ArcBall will handle the event - if it does,
    // then we're done
    // note: the arcball only gets the event if we're in world view
    if (tw->worldCam->value())
        if (arcball.handle(event))
            return 1;

    // Riding the train, drags turn the rider's head.
    if (tw->trainCam->value() && handleTrainLook(event))
        return 1;

    // The observer camera takes held keys, right drags and the wheel.
    if (tw->flyCam->value())
    {
        if (mouseCaptured)
        {
            switch (event)
            {
            case FL_MOVE:
            case FL_DRAG: {
                // The cursor is hidden and kept at the centre; its offset is the turn.
                const int dx = Fl::event_x() - w() / 2;
                const int dy = Fl::event_y() - h() / 2;
                if (dx != 0 || dy != 0)
                {
                    flyCam.mouseLook((float)dx, (float)dy);
                    centerCursor();
                }
                return 1;
            }
            // Clicks do nothing while the mouse steers the view.
            case FL_PUSH:
            case FL_RELEASE:
                return 1;
            case FL_UNFOCUS:
            case FL_HIDE:
                setMouseCaptured(false);
                break;
            }
        }
        // Esc frees the mouse; it must not reach the window, which would close.
        if (event == FL_KEYBOARD && Fl::event_key() == FL_Escape)
        {
            setMouseCaptured(false);
            return 1;
        }
        // Editing commands first: none of them is a flying key (see
        // EditKeys.h), so G, R, Alt + D, Delete and Ctrl + Z still work here.
        if (event == FL_KEYBOARD && editKey(Fl::event_key()))
            return 1;
        const float speedBefore = flyCam.speed();
        const int used = flyCam.handle(event);
        if (flyCam.speed() != speedBefore)
            tw->flySpeed->value(flyCam.speed());
        if (used)
            return 1;
        // Home puts the observer back at the world camera's viewpoint (R
        // tilts the selected control point).
        if (event == FL_KEYBOARD && Fl::event_key() == FL_Home)
        {
            placeFlyCamAtWorldView();
            return 1;
        }
        // F turns to face the train, so it is easy to find again.
        if (event == FL_KEYBOARD && Fl::event_key() == 'f')
        {
            const Pnt3f target = trainPosition();
            flyCam.lookAt(target.x, target.y, target.z);
            return 1;
        }
    }

    // remember what button was used
    static int last_push;

    switch (event)
    {
    // Mouse button being pushed event
    case FL_PUSH:
        last_push = Fl::event_button();
        // if the left button be pushed is left mouse button
        if (last_push == FL_LEFT_MOUSE)
        {
            const int mx = Fl::event_x();
            const int my = Fl::event_y();
            // a handle of the selected point wins over picking another point
            if (canEdit() && hasSelection() && m_editCameraValid)
            {
                const Pnt3f &p = m_pTrack->points[static_cast<size_t>(selectedCube)].pos;
                const float position[3] = {p.x, p.y, p.z};
                const TransformGizmo::Handle handle =
                    TransformGizmo::pick(m_editCamera, position, m_gizmoSize, mx, my);
                if (handle != TransformGizmo::NONE)
                {
                    beginEdit(EDIT_DRAG, handle, mx, my);
                    return 1;
                }
            }
            doPick();
            // dragging a point by its body moves it parallel to the screen;
            // dragging empty space orbits (Alt pans), so one finger is enough on a touchpad
            if (canEdit() && hasSelection() && m_editCameraValid)
                beginEdit(EDIT_DRAG, TransformGizmo::VIEW, mx, my);
            else if (selectedCube < 0 && tw->worldCam->value())
                arcball.beginDrag((Fl::event_state() & FL_ALT) != 0);
            // In the observer camera a click on empty space captures the mouse
            // for spectator-style mouse look (Esc frees it).
            else if (selectedCube < 0 && tw->flyCam->value())
                setMouseCaptured(true);
            damage(1);
            return 1;
        };
        break;

        // Mouse button release event
    case FL_RELEASE: // button release
        damage(1);
        last_push = 0;
        return 1;

    // highlight the handle under the mouse
    case FL_MOVE:
        if (canEdit() && hasSelection() && m_editCameraValid)
        {
            const Pnt3f &p = m_pTrack->points[static_cast<size_t>(selectedCube)].pos;
            const float position[3] = {p.x, p.y, p.z};
            const TransformGizmo::Handle handle = TransformGizmo::pick(
                m_editCamera, position, m_gizmoSize, Fl::event_x(), Fl::event_y());
            if (handle != m_hoverHandle)
            {
                m_hoverHandle = handle;
                damage(1);
            }
        }
        return 1;

    // in order to get keyboard events, we need to accept focus
    case FL_FOCUS:
        return 1;

    // every time the mouse enters this window, aggressively take focus
    // (returning 1 also asks FLTK for FL_MOVE events, used by mouse look)
    case FL_ENTER:
        focus(this);
        return 1;

    case FL_KEYBOARD:
        int k = Fl::event_key();
        if (editKey(k))
            return 1;
        if (k == 'p')
        {
            // Print out the selected control point information
            if (selectedCube >= 0)
                printf("Selected(%d) (%g %g %g) (%g %g %g)\n", selectedCube,
                       m_pTrack->points[selectedCube].pos.x, m_pTrack->points[selectedCube].pos.y,
                       m_pTrack->points[selectedCube].pos.z,
                       m_pTrack->points[selectedCube].orient.x,
                       m_pTrack->points[selectedCube].orient.y,
                       m_pTrack->points[selectedCube].orient.z);
            else
                printf("Nothing Selected\n");

            return 1;
        };
        break;
    }

    return Fl_Gl_Window::handle(event);
}

//************************************************************************
//
// * Control point editing
//
//   Left click selects a point. On the selected point: drag an arrow to
//   move along that axis, a square to move in that plane, the point itself
//   to move parallel to the screen. From the keyboard, as in a 3D modelling
//   program: G moves and R tilts until a left click or Enter accepts, or a
//   right click or Esc puts the point back; X / Y / Z keep to that axis and
//   Shift + X / Y / Z to the other two. Ctrl snaps to whole units (5 degrees
//   when tilting). Shift + D duplicates, Delete or X removes, Ctrl + Z takes
//   back the last change. Numpad 1 / 7 / 0 switch to the world, top and train
//   cameras. In Fly, Alt + D duplicates and only Delete removes, so the
//   flying keys are left alone (EditKeys.h).
//========================================================================
bool TrainView::canEdit() const
{
    // the control points are shown with Edit on, and hidden while riding the train
    return tw->editButton->value() && !tw->trainCam->value() && !tw->chaseCam->value();
}

bool TrainView::hasSelection() const
{
    return selectedCube >= 0 && selectedCube < static_cast<int>(m_pTrack->points.size());
}

void TrainView::beginEdit(EditMode mode, TransformGizmo::Handle handle, int mouseX, int mouseY)
{
    if (!hasSelection())
        return;
    m_editStart = m_pTrack->points[static_cast<size_t>(selectedCube)];
    m_undoPoint = m_editStart;
    m_undoIndex = selectedCube;
    m_undoValid = true;
    m_editMode = mode;
    m_editHandle = handle;
    // In Fly the camera holds still while a point is moved or tilted, so the
    // point follows the mouse and Shift can lock a plane (it sinks the camera
    // otherwise). Keys still held fly again once the edit ends.
    if (tw->flyCam->value())
    {
        flyCam.stop();
        setMouseCaptured(false);
    }
    m_editStartX = mouseX;
    m_editStartY = mouseY;
    damage(1);
}

void TrainView::updateEdit(int mouseX, int mouseY)
{
    if (!hasSelection() || !m_editCameraValid)
        return;
    ControlPoint &point = m_pTrack->points[static_cast<size_t>(selectedCube)];
    const bool snap = (Fl::event_state() & FL_CTRL) != 0;
    const float start[3] = {m_editStart.pos.x, m_editStart.pos.y, m_editStart.pos.z};

    if (m_editMode == EDIT_ROTATE)
    {
        // The angle the mouse has swept around the point on screen turns the
        // point's up direction about the chosen axis, or about the line of
        // sight when none is chosen.
        float cx = 0.0f, cy = 0.0f;
        if (!TransformGizmo::project(m_editCamera, start, cx, cy))
            return;
        const float before =
            atan2f(static_cast<float>(m_editStartY) - cy, static_cast<float>(m_editStartX) - cx);
        const float now = atan2f(static_cast<float>(mouseY) - cy, static_cast<float>(mouseX) - cx);
        float angle = now - before;
        if (snap)
        {
            const float step = 5.0f * 3.14159265f / 180.0f;
            angle = floorf(angle / step + 0.5f) * step;
        }
        float axis[3] = {0.0f, 0.0f, 0.0f};
        TransformGizmo::viewDirection(m_editCamera, start, axis);
        if (m_editHandle >= TransformGizmo::AXIS_X && m_editHandle <= TransformGizmo::AXIS_Z)
        {
            // turn the way the mouse goes whichever side the axis is seen from
            const int index = m_editHandle - TransformGizmo::AXIS_X;
            const float towards = axis[index];
            axis[0] = axis[1] = axis[2] = 0.0f;
            axis[index] = towards >= 0.0f ? 1.0f : -1.0f;
        }
        const float up[3] = {m_editStart.orient.x, m_editStart.orient.y, m_editStart.orient.z};
        float turned[3] = {0.0f, 0.0f, 0.0f};
        TransformGizmo::rotate(up, axis, angle, turned);
        point.orient = Pnt3f(turned[0], turned[1], turned[2]);
        point.orient.normalize();
    }
    else
    {
        float moved[3] = {0.0f, 0.0f, 0.0f};
        if (!TransformGizmo::drag(m_editHandle, m_editCamera, start, m_editStartX, m_editStartY,
                                  mouseX, mouseY, moved))
            return;
        if (snap)
            for (int k = 0; k < 3; ++k)
                moved[k] = start[k] + floorf(moved[k] - start[k] + 0.5f);
        point.pos = Pnt3f(moved[0], moved[1], moved[2]);
    }
    damage(1);
}

void TrainView::endEdit(bool keep)
{
    if (!keep && hasSelection())
        m_pTrack->points[static_cast<size_t>(selectedCube)] = m_editStart;
    m_editMode = EDIT_IDLE;
    m_editHandle = TransformGizmo::NONE;
    m_hoverHandle = TransformGizmo::NONE;
    damage(1);
}

void TrainView::undoEdit()
{
    if (!m_undoValid || m_undoIndex < 0 || m_undoIndex >= static_cast<int>(m_pTrack->points.size()))
        return;
    // swap, so a second Ctrl + Z brings the change back
    ControlPoint &point = m_pTrack->points[static_cast<size_t>(m_undoIndex)];
    const ControlPoint current = point;
    point = m_undoPoint;
    m_undoPoint = current;
    selectedCube = m_undoIndex;
    damage(1);
}

// Events while a point is being moved or tilted. True if the event was used.
bool TrainView::editEvent(int event)
{
    if (!hasSelection() || !canEdit())
    {
        endEdit(false);
        return false;
    }
    const bool held = m_editMode == EDIT_DRAG;
    switch (event)
    {
    case FL_DRAG:
    case FL_MOVE:
        updateEdit(Fl::event_x(), Fl::event_y());
        return true;
    case FL_PUSH:
        if (held)
            return true;
        // left click accepts, any other button puts the point back
        endEdit(Fl::event_button() == FL_LEFT_MOUSE);
        return true;
    case FL_RELEASE:
        if (held)
            endEdit(true);
        return true;
    case FL_KEYBOARD: {
        const int key = Fl::event_key();
        if (key == FL_Escape)
        {
            endEdit(false);
            return true;
        }
        if (key == FL_Enter || key == FL_KP_Enter)
        {
            endEdit(true);
            return true;
        }
        if (!held && (key == 'x' || key == 'y' || key == 'z'))
        {
            // start over from where the point was, held to the new axis
            const int axis = key - 'x';
            const bool plane = (Fl::event_state() & FL_SHIFT) != 0 && m_editMode == EDIT_GRAB;
            const TransformGizmo::Handle wanted = static_cast<TransformGizmo::Handle>(
                (plane ? TransformGizmo::PLANE_YZ : TransformGizmo::AXIS_X) + axis);
            const TransformGizmo::Handle free =
                m_editMode == EDIT_GRAB ? TransformGizmo::VIEW : TransformGizmo::NONE;
            m_pTrack->points[static_cast<size_t>(selectedCube)] = m_editStart;
            m_editHandle = m_editHandle == wanted ? free : wanted;
            updateEdit(Fl::event_x(), Fl::event_y());
            damage(1);
        }
        return true; // no other key does anything in the middle of an edit
    }
    default:
        return false;
    }
}

// Keys when no edit is in progress. True if the key was used. Which key is
// which command, in Fly and in the other cameras, is in EditKeys.h.
bool TrainView::editKey(int key)
{
    const EditKeys::Action action =
        EditKeys::forKey(key, Fl::event_state(), tw->flyCam->value() != 0);
    if (action == EditKeys::CAMERA_WORLD || action == EditKeys::CAMERA_TOP ||
        action == EditKeys::CAMERA_TRAIN)
    {
        Fl_Button *camera = action == EditKeys::CAMERA_WORLD
                                ? tw->worldCam
                                : (action == EditKeys::CAMERA_TOP ? tw->topCam : tw->trainCam);
        camera->setonly();
        cameraChanged();
        tw->damageMe();
        return true;
    }
    if (action == EditKeys::NONE || !canEdit())
        return false;
    if (action == EditKeys::UNDO)
    {
        undoEdit();
        return true;
    }
    if (!hasSelection())
        return false;

    switch (action)
    {
    case EditKeys::GRAB:
        if (!m_editCameraValid)
            return false;
        beginEdit(EDIT_GRAB, TransformGizmo::VIEW, Fl::event_x(), Fl::event_y());
        return true;
    case EditKeys::TILT:
        if (!m_editCameraValid)
            return false;
        beginEdit(EDIT_ROTATE, TransformGizmo::NONE, Fl::event_x(), Fl::event_y());
        return true;
    case EditKeys::DUPLICATE_POINT: {
        if (!m_editCameraValid)
            return false;
        // the new point goes in after the selected one; pick it up at once
        const size_t before = m_pTrack->points.size();
        addPointCB(nullptr, tw);
        if (m_pTrack->points.size() > before)
        {
            selectedCube = (selectedCube + 1) % static_cast<int>(m_pTrack->points.size());
            beginEdit(EDIT_GRAB, TransformGizmo::VIEW, Fl::event_x(), Fl::event_y());
        }
        return true;
    }
    case EditKeys::REMOVE:
        m_undoValid = false; // the indices are about to change
        deletePointCB(nullptr, tw);
        return true;
    default:
        return false;
    }
}

// Remembers the camera for the mouse maths, then draws the handles on the
// selected point and a line of text saying what is going on.
void TrainView::drawEditOverlay()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glViewport(0, 0, w(), h());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    setProjection();
    glGetDoublev(GL_MODELVIEW_MATRIX, m_editCamera.view);
    glGetDoublev(GL_PROJECTION_MATRIX, m_editCamera.projection);
    m_editCamera.viewport[0] = 0;
    m_editCamera.viewport[1] = 0;
    m_editCamera.viewport[2] = w();
    m_editCamera.viewport[3] = h();
    m_editCameraOrthographic = tw->topCam->value() != 0;
    m_editCameraValid = true;

    // Remember the last frame a camera other than Fly drew, for switching to
    // Fly. Which camera drew it follows setProjection(): World first, then
    // Fly. While the Fly button is held down both it and the old camera's
    // button are on, and a redraw then may already be a Fly frame (from the
    // old Fly pose); those must not count.
    const bool drawnByFly = !tw->worldCam->value() && tw->flyCam->value();
    if (!drawnByFly)
    {
        for (int i = 0; i < 16; ++i)
            m_flyHandover[i] = m_editCamera.view[i];
        m_flyHandoverTop = !tw->worldCam->value() && tw->topCam->value();
        m_flyHandoverValid = true;
    }

    tw->syncTransform();

    const bool selected = canEdit() && hasSelection();
    if (selected)
    {
        const ControlPoint &point = m_pTrack->points[static_cast<size_t>(selectedCube)];
        const float position[3] = {point.pos.x, point.pos.y, point.pos.z};
        m_gizmoSize = TransformGizmo::worldSize(m_editCamera, position, 90.0f);
        const bool modal = m_editMode == EDIT_GRAB || m_editMode == EDIT_ROTATE;
        TransformGizmo::draw(position, m_gizmoSize,
                             m_editMode == EDIT_IDLE ? m_hoverHandle : m_editHandle, modal);
    }

    // one line of text in the top left corner, as a 3D view has
    char text[160] = "";
    const char *camera =
        tw->topCam->value()
            ? "Top Orthographic"
            : (tw->trainCam->value()
                   ? "Train Camera"
                   : (tw->chaseCam->value()
                          ? "Chase Camera"
                          : (tw->flyCam->value() ? "Fly Camera" : "User Perspective")));
    if (!selected)
        snprintf(text, sizeof(text), "%s", camera);
    else
    {
        const ControlPoint &point = m_pTrack->points[static_cast<size_t>(selectedCube)];
        const char *axes[8] = {"",       " along X", " along Y", " along Z",
                               " in YZ", " in XZ",   " in XY",   ""};
        if (m_editMode == EDIT_GRAB || m_editMode == EDIT_DRAG)
            snprintf(text, sizeof(text), "Move%s   D: %.2f  %.2f  %.2f   [X/Y/Z] axis  [Ctrl] snap",
                     axes[m_editHandle], static_cast<double>(point.pos.x - m_editStart.pos.x),
                     static_cast<double>(point.pos.y - m_editStart.pos.y),
                     static_cast<double>(point.pos.z - m_editStart.pos.z));
        else if (m_editMode == EDIT_ROTATE)
            snprintf(text, sizeof(text),
                     "Tilt%s   up: %.2f  %.2f  %.2f   [X/Y/Z] axis  [Ctrl] snap",
                     axes[m_editHandle], static_cast<double>(point.orient.x),
                     static_cast<double>(point.orient.y), static_cast<double>(point.orient.z));
        else
            snprintf(text, sizeof(text), "%s  |  Point %d   %.1f  %.1f  %.1f   [G] move  [R] tilt",
                     camera, selectedCube, static_cast<double>(point.pos.x),
                     static_cast<double>(point.pos.y), static_cast<double>(point.pos.z));
    }
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(w()), 0.0, static_cast<double>(h()), -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    gl_font(FL_HELVETICA, 12);
    glColor3f(0.0f, 0.0f, 0.0f);
    gl_draw(text, 11.0f, static_cast<float>(h() - 21));
    glColor3f(0.92f, 0.92f, 0.92f);
    gl_draw(text, 10.0f, static_cast<float>(h() - 20));
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

//************************************************************************
//
// * this is the code that actually draws the window
//   it puts a lot of the work into other routines to simplify things
//========================================================================
void TrainView::draw()
{

    //*********************************************************************
    //
    // * Set up basic opengl informaiton
    //
    //**********************************************************************
    //initialized glad
    if (gladLoadGL())
    {
        //initiailize VAO, VBO, Shader...
    }
    else
        throw std::runtime_error("Could not initialize GLAD!");

    // FLTK may recreate the canvas window, so (re)attach to the current one.
    touchpad.attach(fl_xid(this), touchpadAccepts, touchpadMoves, this);

    // A new GL context starts without our textures, shaders and frame
    // buffers; let the track and the lighting rebuild them.
    if (!context_valid())
    {
        m_trackRenderer.releaseTextures();
        m_land.releaseResources();
        m_ocean.releaseResources();
        lighting.invalidate();
        effects.invalidate();
    }
    const bool advanced = advancedLightingOn();

    // Set up the view port
    glViewport(0, 0, w(), h());

    // clear the window, be sure to clear the Z-Buffer too
    if (advanced)
    {
        float sky[3];
        AdvancedLighting::skyColor(static_cast<float>(tw->timeOfDay->value()), sky);
        glClearColor(sky[0], sky[1], sky[2], 0);
    }
    else
        glClearColor(0, 0, .3f, 0); // background should be blue

    // we need to clear out the stencil buffer since we'll use
    // it for shadows
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH);

    // Blayne prefers GL_DIFFUSE
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // prepare for projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    setProjection(); // put the code to set up matrices here

    //######################################################################
    // TODO:
    // you might want to set the lighting up differently. if you do,
    // we need to set up the lights AFTER setting up the projection
    //######################################################################
    // enable the lighting
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    // top view only needs one light
    if (tw->topCam->value())
    {
        glDisable(GL_LIGHT1);
        glDisable(GL_LIGHT2);
    }
    else
    {
        glEnable(GL_LIGHT1);
        glEnable(GL_LIGHT2);
    }

    //*********************************************************************
    //
    // * set the light parameters
    //
    //**********************************************************************
    GLfloat lightPosition1[] = {0, 1, 1, 0}; // {50, 200.0, 50, 1.0};
    GLfloat lightPosition2[] = {1, 0, 0, 0};
    GLfloat lightPosition3[] = {0, -1, 0, 0};
    GLfloat yellowLight[] = {0.5f, 0.5f, .1f, 1.0};
    GLfloat whiteLight[] = {1.0f, 1.0f, 1.0f, 1.0};
    GLfloat blueLight[] = {.1f, .1f, .3f, 1.0};
    GLfloat grayLight[] = {.3f, .3f, .3f, 1.0};

    glLightfv(GL_LIGHT0, GL_POSITION, lightPosition1);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, whiteLight);
    glLightfv(GL_LIGHT0, GL_AMBIENT, grayLight);

    glLightfv(GL_LIGHT1, GL_POSITION, lightPosition2);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, yellowLight);

    glLightfv(GL_LIGHT2, GL_POSITION, lightPosition3);
    glLightfv(GL_LIGHT2, GL_DIFFUSE, blueLight);

    //*********************************************************************
    // now draw the ground plane
    //*********************************************************************
    // set to opengl fixed pipeline(use opengl 1.x draw function)
    glUseProgram(0);

    // Shadow maps, per-pixel lighting, water and post effects
    if (advanced)
    {
        drawAdvanced();
        drawEditOverlay();
        if (tw->flyCam->value())
            drawFlyOverlay();
        return;
    }

    if (tw->flyCam->value())
        drawFlyHorizon();

    // the island is lit like everything else: its cliffs need the shading
    setupFloor();
    drawGround();

    //*********************************************************************
    // now draw the object and we need to do it twice
    // once for real, and then once for shadows
    //*********************************************************************
    setupObjects();

    drawStuff();

    // this time drawing is for shadows (except for top view). They are
    // flattened onto y = 0, which is only right on the level editor island.
    if (tw->shadowButton->value() && !tw->topCam->value() && !sceneryOn())
    {
        setupShadows();
        drawStuff(true);
        unsetupShadows();
    }
    drawEditOverlay();

    if (tw->flyCam->value())
        drawFlyOverlay();
}

//========================================================================
Pnt3f TrainView::trainPosition() const
//========================================================================
{
    if (m_pTrack->points.size() < 4)
        return Pnt3f(0, 0, 0);
    const TrackSplineType splineType = static_cast<TrackSplineType>(tw->splineBrowser->value());
    return m_pTrack->evaluatePosition(m_pTrack->trainU, splineType);
}

//************************************************************************
//
// * Ground out to the horizon for the observer camera. The checkered floor
//   ends at 100 units; past it the sky colour would fill the whole view, so
//   flying out or looking down far would lose any sense of up and down.
//========================================================================
void TrainView::drawFlyHorizon()
//========================================================================
{
    const float extent = 1500.0f;
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_STENCIL_TEST); // keep shadows on the checkered floor only
    // Drawn first without writing depth, so the floor and everything else simply
    // paint over it; a depth-tested plane this large would z-fight the floor.
    glDepthMask(GL_FALSE);
    glColor3f(.22f, .32f, .2f);
    glBegin(GL_QUADS);
    glNormal3f(0, 1, 0);
    glVertex3f(-extent, 0, -extent);
    glVertex3f(-extent, 0, extent);
    glVertex3f(extent, 0, extent);
    glVertex3f(extent, 0, -extent);
    glEnd();
    glPopAttrib();
}

//************************************************************************
//
// * Overlay for the observer camera: a crosshair at the view centre.
//========================================================================
void TrainView::drawFlyOverlay()
//========================================================================
{
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_VIEWPORT_BIT | GL_LINE_BIT |
                 GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Crosshair in window pixels.
    glViewport(0, 0, w(), h());
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w(), 0, h(), -1, 1);
    glMatrixMode(GL_MODELVIEW);
    const float cx = w() * .5f, cy = h() * .5f;
    glLineWidth(2);
    glColor4f(1, 1, 1, .7f);
    glBegin(GL_LINES);
    glVertex2f(cx - 8, cy);
    glVertex2f(cx - 3, cy);
    glVertex2f(cx + 3, cy);
    glVertex2f(cx + 8, cy);
    glVertex2f(cx, cy - 8);
    glVertex2f(cx, cy - 3);
    glVertex2f(cx, cy + 3);
    glVertex2f(cx, cy + 8);
    glEnd();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

//************************************************************************
//
// * This sets up both the Projection and the ModelView matrices
//   HOWEVER: it doesn't clear the projection first (the caller handles
//   that) - its important for picking
//========================================================================
void TrainView::setProjection()
//========================================================================
{
    // Compute the aspect ratio (we'll need it)
    float aspect = static_cast<float>(w()) / static_cast<float>(h());

    // Check whether we use the world camp
    if (tw->worldCam->value())
        arcball.setProjection(false);
    // The free-moving observer camera
    else if (tw->flyCam->value())
        flyCam.setProjection(false);
    // Or we use the top cam
    else if (tw->topCam->value())
    {
        // The shorter side of the window shows the whole island, so none of
        // it is cut off whatever the window's shape.
        float wi, he;
        if (aspect >= 1)
        {
            he = topViewHalf();
            wi = he * aspect;
        }
        else
        {
            wi = topViewHalf();
            he = wi / aspect;
        }

        // Set up the top camera drop mode to be orthogonal and set
        // up proper projection matrix
        // The view looks down from y = 230, just under the block clouds
        // (LandWorld draws them from y = 235), which would hide the map;
        // the snow peak and the balloons are lower.
        glMatrixMode(GL_PROJECTION);
        glOrtho(-wi, wi, -he, he, 230, -400);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotatef(-90, 1, 0, 0);
        // centred on the scenery, which reaches further to the north-east
        float centreX, centreZ;
        topViewCentre(centreX, centreZ);
        glTranslatef(-centreX, 0.0f, -centreZ);
    }
    else
    {
        Pnt3f eye(0.0f, 2.0f, 0.0f);
        Pnt3f forward(0.0f, 0.0f, 1.0f);
        Pnt3f up(0.0f, 1.0f, 0.0f);

        if (m_pTrack->points.size() >= 4)
        {
            const TrackSplineType splineType =
                static_cast<TrackSplineType>(tw->splineBrowser->value());
            const Pnt3f position = m_pTrack->evaluatePosition(m_pTrack->trainU, splineType);
            forward = m_pTrack->evaluateTangent(m_pTrack->trainU, splineType);
            Pnt3f side;
            makeTrackFrame(forward, side, up);
            eye = position + up * 3.0f + forward * 3.0f;
            // The chase camera rides above the track a little way behind the
            // last cart and looks along the train. Following the track keeps
            // it inside the room carved for the ride, out of the rock.
            if (tw->chaseCam->value())
            {
                const float behind = TrainModel::parameterBehind(
                    *m_pTrack, m_pTrack->trainU, TrainModel::length() + 9.0f, splineType);
                Pnt3f backForward = m_pTrack->evaluateTangent(behind, splineType);
                Pnt3f backSide, backUp;
                makeTrackFrame(backForward, backSide, backUp);
                eye = m_pTrack->evaluatePosition(behind, splineType) + backUp * 6.5f;
                const Pnt3f look = position + up * 1.0f;
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                gluPerspective(m_fieldOfView, aspect, 0.5, 3000.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                gluLookAt(eye.x, eye.y, eye.z, look.x, look.y, look.z, 0.0, 1.0, 0.0);
                return;
            }
            // The rider may turn their head; the view still moves with the train.
            Pnt3f viewForward, viewUp;
            headLook.apply(forward, side, up, viewForward, viewUp);
            forward = viewForward;
            up = viewUp;
        }

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        // tipped down a little: on a climb the rider still sees the land
        // below and not only the sky
        gluPerspective(m_fieldOfView, aspect, 0.5, 3000.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const Pnt3f target = eye + forward * 15.0f + up * -3.6f;
        gluLookAt(eye.x, eye.y, eye.z, target.x, target.y, target.z, up.x, up.y, up.z);
    }
}

//************************************************************************
//
// * this draws all of the stuff in the world
//
//	NOTE: if you're drawing shadows, DO NOT set colors (otherwise, you get
//       colored shadows). this gets called twice per draw
//       -- once for the objects, once for the shadows
//########################################################################
// TODO:
// if you have other objects in the world, make sure to draw them
//########################################################################

//========================================================================
void TrainView::drawStuff(bool doingShadows)
{
    // Draw the control points
    // don't draw the control points if you're driving
    // (otherwise you get sea-sick as you drive through them)
    // they are editing aids rather than part of the scene, so they cast no
    // shadows; the selected one is orange. Hidden until Edit is switched on.
    if (canEdit() && !doingShadows)
    {
        for (size_t i = 0; i < m_pTrack->points.size(); ++i)
        {
            if (((int)i) != selectedCube)
                glColor3ub(215, 215, 215);
            else
                glColor3ub(255, 150, 30);
            m_pTrack->points[i].draw();
        }
    }
    // draw the track: blocky pixel-art sleepers (evenly spaced by arc length)
    // and square iron rails, see TrackRenderer
    if (m_pTrack->points.size() >= 4)
    {
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->splineBrowser->value());
        m_trackRenderer.draw(*m_pTrack, splineType, doingShadows);
    }

#ifdef EXAMPLE_SOLUTION
    drawTrack(this, doingShadows);
#endif

    // wooden trestles where the track is high above the scenery
    if (sceneryOn() && m_pTrack->points.size() >= 4)
        drawSupports(doingShadows);

    // draw the train: a locomotive and five carts with riders (TrainModel)
    if (!tw->trainCam->value() && m_pTrack->points.size() >= 4)
    {
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->splineBrowser->value());
        TrainModel::draw(*m_pTrack, splineType, doingShadows, animationTime());
    }

    // the island's see-through water last, blended over everything behind it
    if (!doingShadows)
    {
        lighting.setTranslucent(true);
        m_land.drawWater(!sceneryOn());
        lighting.setTranslucent(false);
    }
}

//
//************************************************************************
//
// * this tries to see which control point is under the mouse
//	  (for when the mouse is clicked)
//		it uses OpenGL picking - which is always a trick
//########################################################################
// TODO:
//		if you want to pick things other than control points, or you
//		changed how control points are drawn, you might need to change this
//########################################################################
//========================================================================
void TrainView::doPick()
//========================================================================
{
    // since we'll need to do some GL stuff so we make this window as
    // active window
    make_current();

    // where is the mouse?
    int mx = Fl::event_x();
    int my = Fl::event_y();

    // get the viewport - most reliable way to turn mouse coords into GL coords
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Set up the pick matrix on the stack - remember, FlTk is
    // upside down!
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPickMatrix((double)mx, (double)(viewport[3] - my), 5, 5, viewport);

    // now set up the projection
    setProjection();

    // now draw the objects - but really only see what we hit
    GLuint buf[100];
    glSelectBuffer(100, buf);
    glRenderMode(GL_SELECT);
    glInitNames();
    glPushName(0);

    // draw the cubes, loading the names as we go
    for (size_t i = 0; i < m_pTrack->points.size(); ++i)
    {
        glLoadName((GLuint)(i + 1));
        m_pTrack->points[i].draw();
    }

    // go back to drawing mode, and see how picking did
    int hits = glRenderMode(GL_RENDER);
    if (hits)
    {
        // warning; this just grabs the first object hit - if there
        // are multiple objects, you really want to pick the closest
        // one - see the OpenGL manual
        // remember: we load names that are one more than the index
        selectedCube = buf[3] - 1;
    }
    else // nothing hit, nothing selected
        selectedCube = -1;

    printf("Selected Cube %d\n", selectedCube);
}
