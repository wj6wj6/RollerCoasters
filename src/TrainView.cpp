/************************************************************************
     File:        TrainView.cpp

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     See TrainView.H.

     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/

#include <algorithm>
#include <chrono>
#include <iostream>
#include <math.h>

// we will need OpenGL, and OpenGL needs windows.h
#include <windows.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "GL/GLU.h"

#include "TrainView.H"
#include "TrainWindow.H"
#include "Utilities/3DUtils.h"
#include "TrackRenderer.H"
#include "CallBacks.H"
#include "ChaseRig.h"
#include "TrainModel.h"
#include "EditKeys.h"
#include "TrackInsert.h"
#include "Utilities/Platform.H"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

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

// GLFW turns wheel messages into scroll events without the keys held with
// them, so read the exact delta and the modifier flags from the message here:
// the synthetic Ctrl of a touchpad pinch is only in the message.
bool TrainView::systemWheel(unsigned message, unsigned long long wParam, long long lParam)
{
    if (message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL)
        return false;
    if (!tw || !m_hwnd)
        return false;
    const bool fly = tw->settings.camera == CameraView::Fly;
    HeadLook *const look = turningLook();
    const bool train = look != nullptr; // riding, or chasing the train
    if (tw->settings.camera != CameraView::World && !fly && !train)
        return false;
    HWND canvas = static_cast<HWND>(m_hwnd);
    RECT bounds;
    if (!GetClientRect(canvas, &bounds))
        return false;
    POINT corner = {bounds.left, bounds.top};
    POINT opposite = {bounds.right, bounds.bottom};
    ClientToScreen(canvas, &corner);
    ClientToScreen(canvas, &opposite);
    bounds = {corner.x, corner.y, opposite.x, opposite.y};
    POINT position = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
    if (!PtInRect(&bounds, position))
        return false;
    TrainView *view = this;
    const int delta = (short)HIWORD(wParam);
    const WORD keys = LOWORD(wParam);
    int state = 0;
    if (keys & MK_CONTROL)
        state |= Mod::Ctrl;
    if (keys & MK_SHIFT)
        state |= Mod::Shift;
    if (GetKeyState(VK_MENU) < 0)
        state |= Mod::Alt;
    const float steps = (float)delta / WHEEL_DELTA;
    const bool precise = isPreciseWheel(delta);
    // Riding the train, touchpad scrolling turns the head (in Chase, the
    // camera round the train); wheel notches do nothing.
    if (train && (!precise || (state & Mod::Ctrl)))
        return true;
    // Match InputEvent's signs: wheel up is -dy, tilting right is +dx.
    const float dx = message == WM_MOUSEWHEEL ? 0.0f : steps;
    const float dy = message == WM_MOUSEWHEEL ? -steps : 0.0f;
    if (train)
    {
        look->scroll(dx, dy);
        view->damage(1);
    }
    else if (fly && view->wheelSetsDepth())
        view->adjustPlaceDepth(dy); // adding a point: the wheel sets its distance
    else if (fly)
    {
        view->flyCam.scroll(dx, dy, state, precise);
        // The mouse wheel changes the flying speed (the menu shows it).
        view->damage(1);
    }
    else
        view->arcball.scroll(dx, dy, state, precise);
    return true;
}

static bool touchpadAccepts(void *data)
{
    TrainView *view = static_cast<TrainView *>(data);
    return view->tw && ((view->tw->settings.camera == CameraView::World) ||
                        (view->tw->settings.camera == CameraView::Fly) ||
                        (view->tw->settings.camera == CameraView::Train) ||
                        (view->tw->settings.camera == CameraView::Chase));
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
    // Riding the train, two fingers turn the head (in Chase, the camera
    // round the train); spreading does nothing.
    if (HeadLook *look = view->turningLook())
    {
        if (!view->headDragging && (dx != 0 || dy != 0))
        {
            look->touchpadGesture(dx, dy);
            view->damage(1);
        }
    }
    // In Fly mode Shift sinks the camera, so only Alt slides.
    else if (view->tw->settings.camera == CameraView::Fly)
        view->flyCam.touchpadGesture(dx, dy, scale, alt);
    else
        view->arcball.touchpadGesture(dx, dy, scale, pan);
}

#ifdef EXAMPLE_SOLUTION
#include "TrainExample/TrainExample.H"
#endif

// The field of view the World, Train, Chase and Fly cameras start with, in
// degrees; the FOV slider changes all of them (TrainView::setFieldOfView).
// An OpenGL string, empty when there is none.
static std::string glText(GLenum name)
{
    const GLubyte *text = glGetString(name);
    return text ? reinterpret_cast<const char *>(text) : "";
}

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
// * Constructor: the cameras; the window comes later (attach())
//========================================================================
TrainView::TrainView()
//========================================================================
{
    tw = 0;
    selectedCube = -1;
    mouseCaptured = false;
    m_fieldOfView = startFieldOfView;
    headDragging = false;
    headDragX = 0;
    headDragY = 0;

    resetArcball();
    flyCam.setup(this, m_fieldOfView);
    flyCam.setGround(flyLowest - FlyCam::minHeight); // the eye stays minHeight above it
    PerfStats::setActive(&perf);
}

TrainView::~TrainView()
{
    if (mouseCaptured)
        ClipCursor(nullptr);
    touchpad.detach();
    platform::removeTimeout(redrawSoon, this);
    if (PerfStats::active() == &perf)
        PerfStats::setActive(nullptr);
}

void TrainView::attach(GLFWwindow *window)
{
    m_window = window;
    m_hwnd = window ? glfwGetWin32Window(window) : nullptr;
    m_contextValid = false;
    damage(1);
}

void TrainView::resize(int width, int height)
{
    m_width = width > 0 ? width : 1;
    m_height = height > 0 ? height : 1;
    damage(1);
}

void TrainView::make_current()
{
    if (m_window)
        glfwMakeContextCurrent(m_window);
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
    if (m_window)
        glfwSetCursorPos(m_window, w() / 2, h() / 2);
}

//========================================================================
void TrainView::setMouseCaptured(bool captured)
//========================================================================
{
    if (captured == mouseCaptured)
        return;
    mouseCaptured = captured;
    HWND canvas = static_cast<HWND>(m_hwnd);
    if (captured)
    {
        if (m_window)
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        // Keep the hidden cursor over the view so no move is lost at an edge.
        RECT bounds;
        if (canvas && GetClientRect(canvas, &bounds))
        {
            POINT corner = {bounds.left, bounds.top};
            POINT opposite = {bounds.right, bounds.bottom};
            ClientToScreen(canvas, &corner);
            ClientToScreen(canvas, &opposite);
            bounds = {corner.x, corner.y, opposite.x, opposite.y};
            ClipCursor(&bounds);
        }
        centerCursor();
    }
    else
    {
        ClipCursor(nullptr);
        if (m_window)
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    damage(1);
}

//========================================================================
void TrainView::cameraChanged()
//========================================================================
{
    // A first-person view that keeps spinning after a flick is disorienting.
    touchpad.inertia = tw->settings.camera != CameraView::Fly &&
                       tw->settings.camera != CameraView::Train &&
                       tw->settings.camera != CameraView::Chase;
    // Each ride starts looking ahead, and the Chase camera from behind.
    headDragging = false;
    headLook.reset();
    chaseLook.reset();
    if (tw->settings.camera != CameraView::Fly)
    {
        setMouseCaptured(false);
        flyCam.stop();
        return;
    }
    placeFlyCamAtCurrentView();
}

//========================================================================
void TrainView::recenterHead()
//========================================================================
{
    headLook.reset();
    chaseLook.reset();
    damage(1);
}

//========================================================================
HeadLook *TrainView::turningLook()
//========================================================================
{
    if (!tw)
        return nullptr;
    if (tw->settings.camera == CameraView::Train)
        return &headLook;
    if (tw->settings.camera == CameraView::Chase)
        return &chaseLook;
    return nullptr;
}

//************************************************************************
//
// * Mouse input for the Train and Chase cameras: any drag turns the
//   rider's head, or swings the Chase camera round the train as a block
//   game's third person view (F5) turns round the player; a double click,
//   R or Home looks ahead from behind again. Control points are hidden
//   while riding, so clicks never pick them. Returns 1 when the event was
//   used.
//========================================================================
int TrainView::handleTrainLook(const InputEvent &e)
//========================================================================
{
    switch (e.type)
    {
    case InputType::Push:
        if (e.button == Mouse::Middle)
            break;
        if (e.clicks)
        {
            recenterHead();
            headDragging = false;
            // The next press starts a new drag rather than counting as a third click.
            platform::forgetClick();
            return 1;
        }
        headDragging = true;
        headDragX = e.x;
        headDragY = e.y;
        return 1;
    case InputType::Drag:
        if (!headDragging)
            break;
        if (HeadLook *look = turningLook())
            look->drag((float)(e.x - headDragX), (float)(e.y - headDragY));
        headDragX = e.x;
        headDragY = e.y;
        damage(1);
        return 1;
    case InputType::Release:
        if (!headDragging)
            break;
        headDragging = false;
        return 1;
    case InputType::KeyDown:
        // R (or Home) looks ahead again, or the keys set in Controls
        if (tw->keys.matches(KeyBinds::Action::LookAhead, e))
        {
            recenterHead();
            return 1;
        }
        break;
    default:
        break;
    }
    return 0;
}

//************************************************************************
//
// * Mouse and keyboard input the interface did not take
//========================================================================
int TrainView::handleInput(const InputEvent &e)
{
    // an edit in progress sees every event before the camera does
    if (m_editMode != EDIT_IDLE && editEvent(e))
        return 1;
    // so does adding a point, so a click on the track is not an orbit
    if (m_placing && placeEvent(e))
        return 1;
    // and carrying a point along in Fly (G)
    if (m_carrying && carryEvent(e))
        return 1;

    // see if the ArcBall will handle the event - if it does,
    // then we're done
    // note: the arcball only gets the event if we're in world view
    if (tw->settings.camera == CameraView::World)
        if (arcball.handle(e))
            return 1;

    // Riding the train, drags turn the rider's head; chasing it, they swing
    // the camera round it.
    if (turningLook() && handleTrainLook(e))
        return 1;

    // The observer camera takes held keys, right drags and the wheel.
    if (tw->settings.camera == CameraView::Fly)
    {
        if (mouseCaptured)
        {
            switch (e.type)
            {
            case InputType::Move:
            case InputType::Drag: {
                // The cursor is hidden and kept at the centre; its offset is the turn.
                const int dx = e.x - w() / 2;
                const int dy = e.y - h() / 2;
                if (dx != 0 || dy != 0)
                {
                    flyCam.mouseLook((float)dx, (float)dy);
                    centerCursor();
                }
                return 1;
            }
            // With Edit on the mouse buttons build, as in Minecraft: left
            // removes the point at the crosshair, right places a new one,
            // middle selects (picks) it. With Edit off they do nothing.
            case InputType::Push:
                if (canEdit())
                    flyBuildClick(e.button);
                return 1;
            case InputType::Release:
                return 1;
            case InputType::Unfocus:
            case InputType::Hide:
                setMouseCaptured(false);
                break;
            default:
                break;
            }
        }
        // Esc frees the mouse; it must not reach the window, which would close.
        if (e.type == InputType::KeyDown && e.key == Key::Escape)
        {
            setMouseCaptured(false);
            return 1;
        }
        // Editing commands first: none of them is a flying key (see
        // EditKeys.h), so G, R, Alt + D, Delete and Ctrl + Z still work here.
        if (e.type == InputType::KeyDown && editKey(e))
            return 1;
        // building: the wheel sets how far ahead a new point goes
        if (e.type == InputType::Wheel && flyBuilding())
        {
            adjustPlaceDepth(e.dy);
            return 1;
        }
        const float speedBefore = flyCam.speed();
        const int used = flyCam.handle(e);
        if (flyCam.speed() != speedBefore)
            damage(1); // the panel shows the new speed
        if (used)
            return 1;
        // Home puts the observer back at the world camera's viewpoint (R
        // tilts the selected control point).
        if (e.type == InputType::KeyDown && tw->keys.matches(KeyBinds::Action::FlyToWorld, e))
        {
            placeFlyCamAtWorldView();
            return 1;
        }
        // F turns to face the train, so it is easy to find again.
        if (e.type == InputType::KeyDown && tw->keys.matches(KeyBinds::Action::FlyFaceTrain, e))
        {
            const Pnt3f target = trainPosition();
            flyCam.lookAt(target.x, target.y, target.z);
            return 1;
        }
    }

    // remember what button was used
    static int last_push;

    switch (e.type)
    {
    // Mouse button being pushed event
    case InputType::Push:
        last_push = e.button;
        // if the left button be pushed is left mouse button
        if (last_push == Mouse::Left)
        {
            const int mx = e.x;
            const int my = e.y;
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
            doPick(mx, my);
            // dragging a point by its body moves it parallel to the screen;
            // dragging empty space orbits (Alt pans), so one finger is enough on a touchpad
            if (canEdit() && hasSelection() && m_editCameraValid)
                beginEdit(EDIT_DRAG, TransformGizmo::VIEW, mx, my);
            else if (selectedCube < 0 && tw->settings.camera == CameraView::World)
                arcball.beginDrag(mx, my, (e.state & Mod::Alt) != 0);
            // In the observer camera a click on empty space captures the mouse
            // for spectator-style mouse look (Esc frees it).
            else if (selectedCube < 0 && tw->settings.camera == CameraView::Fly)
                setMouseCaptured(true);
            damage(1);
            return 1;
        }
        // the middle button selects the point under the mouse (picks it, as
        // in a block game), without moving anything; C does the same
        if (last_push == Mouse::Middle && canEdit())
        {
            selectedCube = pointUnderMouse(e.x, e.y);
            tw->damageMe();
            return 1;
        }
        break;

        // Mouse button release event
    case InputType::Release: // button release
        damage(1);
        last_push = 0;
        return 1;

    // highlight the handle under the mouse
    case InputType::Move:
        if (canEdit() && hasSelection() && m_editCameraValid)
        {
            const Pnt3f &p = m_pTrack->points[static_cast<size_t>(selectedCube)].pos;
            const float position[3] = {p.x, p.y, p.z};
            const TransformGizmo::Handle handle =
                TransformGizmo::pick(m_editCamera, position, m_gizmoSize, e.x, e.y);
            if (handle != m_hoverHandle)
            {
                m_hoverHandle = handle;
                damage(1);
            }
        }
        return 1;

    // in order to get keyboard events, we need to accept focus
    case InputType::Focus:
        return 1;

    case InputType::Enter:
        return 1;

    case InputType::KeyDown:
        if (editKey(e))
            return 1;
        if (e.key == 'p')
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
    default:
        break;
    }

    return 0;
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
//   when tilting). Shift + D (or Add Pt) adds a point where the track is
//   then clicked, Delete or X removes, Ctrl + Z undoes and Ctrl + Y or
//   Ctrl + Shift + Z redoes (TrackHistory.h).
//   Numpad 1 / 7 / 0 switch to the world, top and train cameras. In Fly,
//   Alt + D adds and only Delete removes, so the flying keys are left alone
//   (EditKeys.h).
//========================================================================
bool TrainView::canEdit() const
{
    // the control points are shown with Edit on, and hidden while riding the train
    return tw->settings.edit && tw->settings.camera != CameraView::Train &&
           tw->settings.camera != CameraView::Chase;
}

bool TrainView::hasSelection() const
{
    return selectedCube >= 0 && selectedCube < static_cast<int>(m_pTrack->points.size());
}

void TrainView::beginEdit(EditMode mode, TransformGizmo::Handle handle, int mouseX, int mouseY)
{
    if (!hasSelection())
        return;
    endCarry(true); // R while carrying: put it down first
    m_editStart = m_pTrack->points[static_cast<size_t>(selectedCube)];
    m_editBefore = TrackHistory::capture(*m_pTrack, selectedCube);
    m_editMode = mode;
    m_editHandle = handle;
    // In Fly the camera holds still while a point is moved or tilted, so the
    // point follows the mouse and Shift can lock a plane (it sinks the camera
    // otherwise). Keys still held fly again once the edit ends.
    if (tw->settings.camera == CameraView::Fly)
    {
        flyCam.stop();
        setMouseCaptured(false);
    }
    m_editStartX = mouseX;
    m_editStartY = mouseY;
    damage(1);
}

void TrainView::updateEdit(int mouseX, int mouseY, bool snap)
{
    if (!hasSelection() || !m_editCameraValid)
        return;
    ControlPoint &point = m_pTrack->points[static_cast<size_t>(selectedCube)];
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
    // a point that really moved or tilted is one step for Ctrl + Z; a click
    // that only selected it is not, nor the drag that places a new point
    // (adding it was the step)
    if (keep && hasSelection() && !m_editJoinsInsert)
    {
        const ControlPoint &now = m_pTrack->points[static_cast<size_t>(selectedCube)];
        const ControlPoint &was = m_editStart;
        if (now.pos.x != was.pos.x || now.pos.y != was.pos.y || now.pos.z != was.pos.z ||
            now.orient.x != was.orient.x || now.orient.y != was.orient.y ||
            now.orient.z != was.orient.z)
            tw->pushHistory(m_editBefore);
    }
    m_editJoinsInsert = false;
    m_editMode = EDIT_IDLE;
    m_editHandle = TransformGizmo::NONE;
    m_hoverHandle = TransformGizmo::NONE;
    damage(1);
}

void TrainView::finishEdits()
{
    if (m_editMode != EDIT_IDLE)
        endEdit(true);
    if (m_placing)
        endPlacePoint();
    endCarry(true);
}

// The Fly camera's right, up and forward directions.
void TrainView::flyAxes(float right[3], float up[3], float forward[3]) const
{
    flyCam.forward(forward[0], forward[1], forward[2]);
    // right = forward x world up, level; up = right x forward
    right[0] = -forward[2];
    right[1] = 0.0f;
    right[2] = forward[0];
    float length = sqrtf(right[0] * right[0] + right[2] * right[2]);
    if (length < 1.0e-4f)
    {
        const float yaw = flyCam.yaw() * 3.14159265f / 180.0f;
        right[0] = cosf(yaw);
        right[2] = sinf(yaw);
        length = 1.0f;
    }
    right[0] /= length;
    right[2] /= length;
    up[0] = right[1] * forward[2] - right[2] * forward[1];
    up[1] = right[2] * forward[0] - right[0] * forward[2];
    up[2] = right[0] * forward[1] - right[1] * forward[0];
}

// G in Fly: pick up the point at the crosshair (else the selected one) and
// bring it onto the crosshair at the distance it is. The view keeps turning
// and flying and the point goes along, until G again or a left click puts it
// down.
void TrainView::beginCarry()
{
    const int aimed = crosshairPoint();
    if (aimed >= 0)
        selectedCube = aimed;
    if (!hasSelection())
        return;
    m_carryBefore = TrackHistory::capture(*m_pTrack, selectedCube);
    const Pnt3f &p = m_pTrack->points[static_cast<size_t>(selectedCube)].pos;
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    flyCam.position(ex, ey, ez);
    // the point jumps onto the crosshair, as far away as it is now
    const float d[3] = {p.x - ex, p.y - ey, p.z - ez};
    float away = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (away < kPlaceDepthMin)
        away = kPlaceDepthMin;
    if (away > kPlaceDepthMax)
        away = kPlaceDepthMax;
    m_carryOffset[0] = 0.0f;
    m_carryOffset[1] = 0.0f;
    m_carryOffset[2] = away;
    updateCarry();
    m_carrying = true;
    setMouseCaptured(true); // the mouse turns the view, so the point follows the crosshair
    tw->damageMe();
}

// Keeps the carried point where it was in the view.
void TrainView::updateCarry()
{
    if (!hasSelection())
        return;
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    flyCam.position(ex, ey, ez);
    float axes[3][3];
    flyAxes(axes[0], axes[1], axes[2]);
    float world[3] = {ex, ey, ez};
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i)
            world[i] += axes[k][i] * m_carryOffset[k];
    m_pTrack->points[static_cast<size_t>(selectedCube)].pos = Pnt3f(world[0], world[1], world[2]);
}

// G again or a left click puts the point down (one undo step); Esc puts it back.
void TrainView::endCarry(bool keep)
{
    if (!m_carrying)
        return;
    m_carrying = false;
    const int index = m_carryBefore.selected;
    if (index < 0 || index >= static_cast<int>(m_pTrack->points.size()) ||
        m_carryBefore.points.size() != m_pTrack->points.size())
        return;
    ControlPoint &point = m_pTrack->points[static_cast<size_t>(index)];
    const ControlPoint &was = m_carryBefore.points[static_cast<size_t>(index)];
    if (!keep)
        point = was;
    else if (point.pos.x != was.pos.x || point.pos.y != was.pos.y || point.pos.z != was.pos.z)
        tw->pushHistory(m_carryBefore);
    tw->damageMe();
}

// Events while carrying a point in Fly. The mouse still looks and the keys
// still fly; the wheel sets the distance, a left click puts it down.
bool TrainView::carryEvent(const InputEvent &e)
{
    if (!canEdit() || tw->settings.camera != CameraView::Fly || !hasSelection())
    {
        endCarry(true);
        return false;
    }
    switch (e.type)
    {
    case InputType::Wheel:
        adjustPlaceDepth(e.dy);
        return true;
    case InputType::Push:
        if (e.button == Mouse::Left)
            endCarry(true);
        return true; // the other buttons do nothing meanwhile
    case InputType::Release:
        return true;
    case InputType::KeyDown:
        if (e.key == Key::Escape)
        {
            endCarry(false);
            return true;
        }
        return false;
    case InputType::Unfocus:
    case InputType::Hide:
        endCarry(true);
        return false;
    default:
        return false;
    }
}

// The control point the middle button or C selects: at the crosshair while
// Fly holds the mouse, else under the mouse; -1 when none is near.
int TrainView::pointUnderMouse(int mouseX, int mouseY) const
{
    if (tw->settings.camera == CameraView::Fly && mouseCaptured)
        return crosshairPoint();
    if (!m_editCameraValid)
        return -1;
    return TrackInsert::pickPoint(*m_pTrack, m_editCamera, mouseX, mouseY, kCrosshairReach);
}

// The control point at the Fly crosshair, or -1.
int TrainView::crosshairPoint() const
{
    if (!m_editCameraValid)
        return -1;
    return TrackInsert::pickPoint(*m_pTrack, m_editCamera, w() / 2, h() / 2, kCrosshairReach);
}

// Add Pt, Shift + D (Alt + D in Fly): wait for a click on the track.
void TrainView::beginPlacePoint()
{
    if (m_pTrack->points.size() < 4)
        return;
    // the points must be showing, and cannot be edited from the train
    tw->settings.edit = true;
    if (tw->settings.camera == CameraView::Train || tw->settings.camera == CameraView::Chase)
    {
        tw->settings.camera = CameraView::World;
        cameraChanged();
    }
    if (m_editMode != EDIT_IDLE)
        endEdit(true);
    // In Fly, building is on whenever the mouse steers the view: right click
    // places a point at the crosshair, so this only takes the mouse.
    if (tw->settings.camera == CameraView::Fly)
    {
        setMouseCaptured(true);
        tw->damageMe();
        return;
    }
    setMouseCaptured(false);
    m_placing = true;
    m_placeHit = false;
    tw->damageMe();
}

// Fly with the mouse captured and Edit on: the mouse buttons build.
bool TrainView::flyBuilding() const
{
    return (tw->settings.camera == CameraView::Fly) && mouseCaptured && canEdit() &&
           m_editMode == EDIT_IDLE && !m_placing && !m_carrying;
}

// The wheel sets a distance (not the flying speed) while building or carrying.
bool TrainView::wheelSetsDepth() const
{
    return flyBuilding() || m_carrying;
}

// A mouse button while building in Fly, as in Minecraft: left removes the
// point at the crosshair, right places a new point at the crosshair (the wheel
// sets how far ahead), middle selects the point at the crosshair.
void TrainView::flyBuildClick(int button)
{
    const int aimed = crosshairPoint();
    if (button == Mouse::Middle)
        selectedCube = aimed;
    else if (button == Mouse::Left && aimed >= 0)
    {
        const TrackHistory::Snapshot before = TrackHistory::capture(*m_pTrack, selectedCube);
        if (TrackInsert::removeAt(*m_pTrack, aimed))
        {
            tw->pushHistory(before);
            if (selectedCube == aimed)
                selectedCube = -1;
            else if (selectedCube > aimed)
                --selectedCube;
        }
    }
    else if (button == Mouse::Right)
    {
        updateFlyPlace();
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
        const TrackHistory::Snapshot before = TrackHistory::capture(*m_pTrack, selectedCube);
        const int index = TrackInsert::insertNear(*m_pTrack, m_placeSpot, splineType);
        if (index >= 0)
        {
            tw->pushHistory(before);
            selectedCube = index; // G moves it, R tilts it
        }
    }
    tw->damageMe();
}

// The wheel while building or carrying in Fly: up pushes the point further away.
void TrainView::adjustPlaceDepth(float wheelDy)
{
    if (m_carrying)
    {
        // the carried point slides along the line from the eye through it
        const float length =
            sqrtf(m_carryOffset[0] * m_carryOffset[0] + m_carryOffset[1] * m_carryOffset[1] +
                  m_carryOffset[2] * m_carryOffset[2]);
        float wanted = length * powf(1.15f, -wheelDy);
        if (wanted < kPlaceDepthMin)
            wanted = kPlaceDepthMin;
        if (wanted > kPlaceDepthMax)
            wanted = kPlaceDepthMax;
        if (length > 1.0e-4f)
            for (float &v : m_carryOffset)
                v *= wanted / length;
        updateCarry();
        damage(1);
        return;
    }
    m_placeDepth *= powf(1.15f, -wheelDy);
    if (m_placeDepth < kPlaceDepthMin)
        m_placeDepth = kPlaceDepthMin;
    if (m_placeDepth > kPlaceDepthMax)
        m_placeDepth = kPlaceDepthMax;
    damage(1);
}

// Where a point placed in Fly would go: at the crosshair, m_placeDepth ahead
// of the eye; and the spot on the track it would be linked in at.
void TrainView::updateFlyPlace()
{
    float ex = 0.0f, ey = 0.0f, ez = 0.0f, fx = 0.0f, fy = 0.0f, fz = 0.0f;
    flyCam.position(ex, ey, ez);
    flyCam.forward(fx, fy, fz);
    m_placeSpot = Pnt3f(ex + fx * m_placeDepth, ey + fy * m_placeDepth, ez + fz * m_placeDepth);
    const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
    m_placeU = TrackInsert::nearest(*m_pTrack, m_placeSpot, splineType);
}

void TrainView::endPlacePoint()
{
    m_placing = false;
    m_placeHit = false;
    damage(1);
}

void TrainView::updatePlaceHover(int mouseX, int mouseY)
{
    const bool was = m_placeHit;
    m_placeHit = false;
    if (m_editCameraValid)
    {
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
        float u = 0.0f;
        if (TrackInsert::pick(*m_pTrack, splineType, m_editCamera, mouseX, mouseY, 14.0f, u))
        {
            m_placeHit = true;
            m_placeU = u;
            m_placeSpot = m_pTrack->evaluatePosition(u, splineType);
        }
    }
    if (m_placeHit || was)
        damage(1);
}

// Events while adding a point. True if the event was used; the rest go on
// as usual, so the camera can still be turned and zoomed.
bool TrainView::placeEvent(const InputEvent &e)
{
    // gave up when Edit goes off or the camera changes to Fly
    if (!canEdit() || tw->settings.camera == CameraView::Fly)
    {
        endPlacePoint();
        return false;
    }
    switch (e.type)
    {
    case InputType::Move:
        updatePlaceHover(e.x, e.y);
        return false; // the handle highlighting still runs
    case InputType::Push: {
        if (e.button != Mouse::Left)
        {
            endPlacePoint();
            return true;
        }
        const int mx = e.x;
        const int my = e.y;
        updatePlaceHover(mx, my);
        if (!m_placeHit)
            return false; // off the track: orbit or pick a point as usual
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
        const TrackHistory::Snapshot before = TrackHistory::capture(*m_pTrack, selectedCube);
        const int index = TrackInsert::insertAt(*m_pTrack, m_placeU, splineType);
        endPlacePoint();
        if (index < 0)
            return true;
        tw->pushHistory(before);
        // select the new point and keep it under the mouse while the button is held
        selectedCube = index;
        beginEdit(EDIT_DRAG, TransformGizmo::VIEW, mx, my);
        m_editJoinsInsert = true;
        tw->damageMe();
        return true;
    }
    case InputType::KeyDown:
        if (e.key == Key::Escape)
        {
            endPlacePoint();
            return true;
        }
        return false;
    default:
        return false;
    }
}

// Events while a point is being moved or tilted. True if the event was used.
bool TrainView::editEvent(const InputEvent &e)
{
    if (!hasSelection() || !canEdit())
    {
        endEdit(false);
        return false;
    }
    const bool held = m_editMode == EDIT_DRAG;
    const bool snap = (e.state & Mod::Ctrl) != 0;
    switch (e.type)
    {
    case InputType::Drag:
    case InputType::Move:
        updateEdit(e.x, e.y, snap);
        return true;
    case InputType::Push:
        if (held)
            return true;
        // left click accepts, any other button puts the point back
        endEdit(e.button == Mouse::Left);
        return true;
    case InputType::Release:
        if (held)
            endEdit(true);
        return true;
    case InputType::KeyDown: {
        const int key = e.key;
        if (key == Key::Escape)
        {
            endEdit(false);
            return true;
        }
        if (key == Key::Enter || key == Key::KeypadEnter)
        {
            endEdit(true);
            return true;
        }
        if (!held && (key == 'x' || key == 'y' || key == 'z'))
        {
            // start over from where the point was, held to the new axis
            const int axis = key - 'x';
            const bool plane = (e.state & Mod::Shift) != 0 && m_editMode == EDIT_GRAB;
            const TransformGizmo::Handle wanted = static_cast<TransformGizmo::Handle>(
                (plane ? TransformGizmo::PLANE_YZ : TransformGizmo::AXIS_X) + axis);
            const TransformGizmo::Handle free =
                m_editMode == EDIT_GRAB ? TransformGizmo::VIEW : TransformGizmo::NONE;
            m_pTrack->points[static_cast<size_t>(selectedCube)] = m_editStart;
            m_editHandle = m_editHandle == wanted ? free : wanted;
            updateEdit(e.x, e.y, snap);
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
bool TrainView::editKey(const InputEvent &e)
{
    const EditKeys::Action action =
        EditKeys::forKey(tw->keys, e.key, e.state, tw->settings.camera == CameraView::Fly);
    if (action == EditKeys::CAMERA_WORLD || action == EditKeys::CAMERA_TOP ||
        action == EditKeys::CAMERA_TRAIN)
    {
        tw->settings.camera = action == EditKeys::CAMERA_WORLD ? CameraView::World
                              : action == EditKeys::CAMERA_TOP ? CameraView::Top
                                                               : CameraView::Train;
        cameraChanged();
        tw->damageMe();
        return true;
    }
    if (action == EditKeys::TOGGLE_EDIT)
    {
        // F4: show or hide the control points and their editing, as the Edit
        // button does; whatever is in progress is kept first
        finishEdits();
        tw->settings.edit = !tw->settings.edit;
        tw->damageMe();
        return true;
    }
    if (action == EditKeys::NONE || !canEdit())
        return false;
    if (action == EditKeys::UNDO)
    {
        tw->undoEdit();
        return true;
    }
    if (action == EditKeys::REDO)
    {
        tw->redoEdit();
        return true;
    }
    if (action == EditKeys::PICK_POINT)
    {
        // C: select the point at the crosshair in Fly, else under the mouse
        // (the middle button's job, for a touchpad that has none)
        selectedCube = pointUnderMouse(e.x, e.y);
        tw->damageMe();
        return true;
    }
    if (action == EditKeys::ADD_POINT)
    {
        // no selection needed: the new point goes where the track is clicked
        beginPlacePoint();
        return true;
    }
    // In Fly, G picks up a point to carry along with the view (the view does
    // not stop), and G again puts it down.
    if (action == EditKeys::GRAB && tw->settings.camera == CameraView::Fly)
    {
        if (m_carrying)
            endCarry(true);
        else
            beginCarry();
        return true;
    }
    if (!hasSelection())
        return false;

    switch (action)
    {
    case EditKeys::GRAB:
        if (!m_editCameraValid)
            return false;
        beginEdit(EDIT_GRAB, TransformGizmo::VIEW, e.x, e.y);
        return true;
    case EditKeys::TILT:
        if (!m_editCameraValid)
            return false;
        beginEdit(EDIT_ROTATE, TransformGizmo::NONE, e.x, e.y);
        return true;
    case EditKeys::REMOVE:
        deletePointCB(tw);
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
    m_editCameraOrthographic = tw->settings.camera == CameraView::Top;
    m_editCameraValid = true;

    // Remember the last frame a camera other than Fly drew, for switching to
    // Fly. Which camera drew it follows setProjection(): World first, then
    // Fly. While the Fly button is held down both it and the old camera's
    // button are on, and a redraw then may already be a Fly frame (from the
    // old Fly pose); those must not count.
    const bool drawnByFly = tw->settings.camera == CameraView::Fly;
    if (!drawnByFly)
    {
        for (int i = 0; i < 16; ++i)
            m_flyHandover[i] = m_editCamera.view[i];
        m_flyHandoverTop = tw->settings.camera == CameraView::Top;
        m_flyHandoverValid = true;
    }

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

    // Track > Samples: where the rails were subdivided, over everything
    if (tw->settings.showSamples && m_pTrack->points.size() >= 4)
        m_trackRenderer.drawSamples();

    // adding a point: a marker where it would go, drawn over everything
    const bool placing = m_placing && canEdit();
    // building in Fly: a ghost point where a right click would put one, and a
    // white box on the point a left click would remove
    const bool building = flyBuilding();
    const int aimed = building ? crosshairPoint() : -1;
    if (building)
        updateFlyPlace();
    if (aimed >= 0)
    {
        const Pnt3f &p = m_pTrack->points[static_cast<size_t>(aimed)].pos;
        const float e = 2.6f; // a little larger than the point's cube
        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        for (int y = -1; y <= 1; y += 2)
        {
            glBegin(GL_LINE_LOOP);
            glVertex3f(p.x - e, p.y + y * e, p.z - e);
            glVertex3f(p.x + e, p.y + y * e, p.z - e);
            glVertex3f(p.x + e, p.y + y * e, p.z + e);
            glVertex3f(p.x - e, p.y + y * e, p.z + e);
            glEnd();
        }
        glBegin(GL_LINES);
        for (int x = -1; x <= 1; x += 2)
            for (int z = -1; z <= 1; z += 2)
            {
                glVertex3f(p.x + x * e, p.y - e, p.z + z * e);
                glVertex3f(p.x + x * e, p.y + e, p.z + z * e);
            }
        glEnd();
        glPopAttrib();
    }
    if ((placing && m_placeHit) || building)
    {
        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_POINT_BIT | GL_LINE_BIT);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
        // In Fly the dot is off the track: a line shows where it joins the
        // track, and a nearer dot is drawn bigger, to judge the distance.
        float dotSize = 10.0f;
        if (building)
        {
            const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
            const Pnt3f joint = m_pTrack->evaluatePosition(m_placeU, splineType);
            glLineWidth(2.0f);
            glColor3f(1.0f, 0.85f, 0.1f);
            glBegin(GL_LINES);
            glVertex3f(m_placeSpot.x, m_placeSpot.y, m_placeSpot.z);
            glVertex3f(joint.x, joint.y, joint.z);
            glEnd();
            dotSize = 600.0f / m_placeDepth;
            if (dotSize < 6.0f)
                dotSize = 6.0f;
            if (dotSize > 24.0f)
                dotSize = 24.0f;
        }
        glPointSize(dotSize + 5.0f);
        glColor3f(0.05f, 0.05f, 0.05f);
        glBegin(GL_POINTS);
        glVertex3f(m_placeSpot.x, m_placeSpot.y, m_placeSpot.z);
        glEnd();
        glPointSize(dotSize);
        glColor3f(1.0f, 0.85f, 0.1f);
        glBegin(GL_POINTS);
        glVertex3f(m_placeSpot.x, m_placeSpot.y, m_placeSpot.z);
        glEnd();
        glPopAttrib();
    }

    // one line about what the view is doing
    char text[200] = "";
    const char *camera =
        (tw->settings.camera == CameraView::Top)
            ? "Top Orthographic"
            : ((tw->settings.camera == CameraView::Train)
                   ? "Train Camera"
                   : ((tw->settings.camera == CameraView::Chase)
                          ? "Chase Camera"
                          : ((tw->settings.camera == CameraView::Fly) ? "Fly Camera"
                                                                      : "User Perspective")));
    // the keys as set in Options > Controls
    const auto keyOf = [this](KeyBinds::Action action) { return tw->keys.keyText(action); };
    const std::string grabKey = keyOf(KeyBinds::Action::Grab);
    if (m_carrying && hasSelection())
    {
        const float away =
            sqrtf(m_carryOffset[0] * m_carryOffset[0] + m_carryOffset[1] * m_carryOffset[1] +
                  m_carryOffset[2] * m_carryOffset[2]);
        snprintf(text, sizeof(text),
                 "Carry point %d: %.1f away   [Wheel] nearer / farther   [%s] or [Left] put down   "
                 "[Esc] put back",
                 selectedCube, static_cast<double>(away), grabKey.c_str());
    }
    else if (building && aimed >= 0)
        snprintf(text, sizeof(text),
                 "Point %d   [Left] remove   [Middle / %s] select   [%s] carry   "
                 "[Right] place a point %.1f ahead",
                 aimed, keyOf(KeyBinds::Action::Pick).c_str(), grabKey.c_str(),
                 static_cast<double>(m_placeDepth));
    else if (building)
        snprintf(text, sizeof(text),
                 "%s  |  [Right] place a point %.1f ahead   [Wheel] nearer / farther", camera,
                 static_cast<double>(m_placeDepth));
    else if (placing)
        snprintf(text, sizeof(text), "Add point: %s   [Esc] cancel",
                 m_placeHit ? "click to put it here, hold to drag it"
                            : "point at the track where it should go");
    else if (!selected)
        text[0] = '\0'; // the hotbar already shows the camera
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
            snprintf(text, sizeof(text),
                     "%s  |  Point %d   %.1f  %.1f  %.1f   [%s] move  [%s] tilt", camera,
                     selectedCube, static_cast<double>(point.pos.x),
                     static_cast<double>(point.pos.y), static_cast<double>(point.pos.z),
                     grabKey.c_str(), keyOf(KeyBinds::Action::Tilt).c_str());
    }
    // the interface shows it above the hotbar (GameMenu.cpp)
    m_statusText = text;
}

//************************************************************************
//
// * this is the code that actually draws the window
//   it puts a lot of the work into other routines to simplify things
//========================================================================
void TrainView::draw()
{
    // a request made while drawing (a timer, the interface) asks for another frame
    m_needsDraw = false;
    drawFrame();
    // the title and the screens before Play show over the dirt, with no world
    if (tw && (frameShowsScene || !tw->menu.inWorld()))
        tw->drawInterface();
    finishAutomatedFrame();
}

//************************************************************************
//
// * Draw one frame of the scene from the selected camera
//========================================================================
void TrainView::drawFrame()
{
    // a point carried in Fly follows the camera, which may have flown since
    if (m_carrying)
        updateCarry();
    // Setup's processor time includes loading the OpenGL functions
    const double setupStart = PerfStats::clock();

    // A new GL context may come with a new window, so (re)attach to the current one.
    // The window proc is wrapped once the window exists (attach()).
    if (m_hwnd)
        touchpad.attach(m_hwnd, touchpadAccepts, touchpadMoves, this);

    // A new GL context starts without our textures, shaders and frame
    // buffers; let the track and the lighting rebuild them.
    if (!m_contextValid)
    {
        // The OpenGL functions are looked up once per context: looking them
        // up again every frame cost about 7 ms on a laptop.
        if (!gladLoadGL())
            throw std::runtime_error("Could not initialize GLAD!");
        m_contextValid = true;
        m_trackRenderer.releaseResources();
        TrainModel::releaseTextures();
        m_land.releaseResources();
        m_ocean.releaseResources();
        lighting.invalidate();
        effects.invalidate();
        m_loadingTexture = 0;
        m_guiRenderer.forget();
        perfGpu.forget();
        glRenderer = glText(GL_RENDERER);
        glVersionText = glText(GL_VERSION);
    }

    // The graphics card's timestamps go into this frame's set of queries
    // (PerfGpu.h); the frame was begun by TrainWindow::drawNow().
    perfGpuOn = perf.enabled() && PerfGpu::available();
    if (perfGpuOn)
    {
        perfGpu.attach(&perf);
        perfGpu.beginFrame(perf, perf.currentId());
    }
    else
        perf.setGpuStamp(nullptr, nullptr);
    perf.begin(PerfStage::Setup, setupStart);

    // The first frames show how far the world is built, instead of a white
    // window that does not answer until it is done.
    frameShowsScene = false;
    if (!worldReady())
    {
        perf.end(PerfStage::Setup, PerfStats::clock());
        return;
    }
    frameShowsScene = true;
    const bool advanced = advancedLightingOn();

    // Set up the view port
    glViewport(0, 0, w(), h());

    // clear the window, be sure to clear the Z-Buffer too
    if (advanced)
    {
        float sky[3];
        AdvancedLighting::skyColor(tw->settings.timeOfDay, sky);
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
    if (tw->settings.camera == CameraView::Top)
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
    perf.end(PerfStage::Setup, PerfStats::clock());

    // Shadow maps, per-pixel lighting, water and post effects
    if (advanced)
    {
        drawAdvanced();
        PerfScope overlay(PerfStage::Overlay);
        drawEditOverlay();
        if (tw->settings.camera == CameraView::Fly)
            drawFlyOverlay();
        return;
    }

    {
        PerfScope scene(PerfStage::Scene);
        if (tw->settings.camera == CameraView::Fly)
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
    }

    // this time drawing is for shadows (except for top view). They are
    // flattened onto y = 0, which is only right on the level editor island.
    if (tw->settings.shadows && tw->settings.camera != CameraView::Top && !sceneryOn())
    {
        PerfScope shadows(PerfStage::Shadows);
        setupShadows();
        drawStuff(true);
        unsetupShadows();
    }
    PerfScope overlay(PerfStage::Overlay);
    drawEditOverlay();

    if (tw->settings.camera == CameraView::Fly)
        drawFlyOverlay();
}

//========================================================================
Pnt3f TrainView::trainPosition() const
//========================================================================
{
    if (m_pTrack->points.size() < 4)
        return Pnt3f(0, 0, 0);
    const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
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
    if (tw->settings.camera == CameraView::World)
        arcball.setProjection(false);
    // The free-moving observer camera
    else if (tw->settings.camera == CameraView::Fly)
        flyCam.setProjection(false);
    // Or we use the top cam
    else if (tw->settings.camera == CameraView::Top)
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
            const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
            const Pnt3f position = m_pTrack->evaluatePosition(m_pTrack->trainU, splineType);
            forward = m_pTrack->evaluateTangent(m_pTrack->trainU, splineType);
            Pnt3f side;
            makeTrackFrame(forward, side, up);
            eye = position + up * 3.0f + forward * 3.0f;
            // The Chase camera follows the train from behind and above, like a
            // third person view: the whole train and the track ahead in view,
            // the last rider low in the picture (ChaseRig works out where
            // from the train's length and the riders' height). It stays over
            // the track behind the last cart, so it keeps to the room carved
            // for the ride; where a tunnel's roof or an overhang leaves less
            // room it comes lower and nearer, and it moves there smoothly,
            // dropping at once rather than going through the rock.
            if (tw->settings.camera == CameraView::Chase)
            {
                const ChaseRig::Train train = {TrainModel::length(), TrainModel::riderTop()};
                const float fovY = m_fieldOfView;
                const ChaseRig::Placement open = ChaseRig::plan(train, fovY);
                const float uLast = TrainModel::parameterBehind(*m_pTrack, m_pTrack->trainU,
                                                                train.length, splineType);
                Pnt3f lastForward = m_pTrack->evaluateTangent(uLast, splineType);
                Pnt3f lastSide, lastUp;
                makeTrackFrame(lastForward, lastSide, lastUp);
                const Pnt3f lastHead =
                    m_pTrack->evaluatePosition(uLast, splineType) + lastUp * train.riderTop;
                const float minBehind = 0.6f * train.riderTop;
                // the camera's place over the track `behind` back, and the room there
                auto place = [&](float behind, Pnt3f &base, Pnt3f &baseUp) {
                    const float u =
                        TrainModel::parameterBehind(*m_pTrack, uLast, behind, splineType);
                    Pnt3f f = m_pTrack->evaluateTangent(u, splineType);
                    Pnt3f s;
                    makeTrackFrame(f, s, baseUp);
                    base = m_pTrack->evaluatePosition(u, splineType);
                };
                ChaseRig::Placement want = open;
                for (int pass = 0; pass < 4; ++pass)
                {
                    Pnt3f base, baseUp;
                    place(want.behind, base, baseUp);
                    const float room = chaseRoom(base, baseUp, open.height);
                    want = ChaseRig::fit(open, std::min(room, open.height), minBehind, train, fovY);
                    // and a clear line from there to the last rider, or nearer
                    place(want.behind, base, baseUp);
                    const Pnt3f from = base + baseUp * want.height;
                    bool clear = true;
                    for (int k = 1; k < 12 && clear; ++k)
                    {
                        const float t = static_cast<float>(k) / 12.0f;
                        clear = !chaseBlocked(from * (1.0f - t) + lastHead * t);
                    }
                    if (clear)
                        break;
                    want.behind = std::max(minBehind, want.behind * 0.6f);
                    want.height = std::min(want.height, open.height);
                }
                // follow smoothly: on along the track and up gently, down at once
                const float now = animationTime();
                const float dt = now - m_chaseTime;
                m_chaseTime = now;
                if (!m_chaseValid || dt < 0.0f || dt > 1.0f)
                {
                    m_chaseBehind = want.behind;
                    m_chaseHeight = want.height;
                    m_chaseValid = true;
                }
                else
                {
                    m_chaseBehind = ChaseRig::approach(m_chaseBehind, want.behind, dt, 3.0f);
                    m_chaseHeight = ChaseRig::approach(m_chaseHeight, want.height, dt,
                                                       want.height < m_chaseHeight ? 12.0f : 2.5f);
                }
                Pnt3f base, baseUp;
                place(m_chaseBehind, base, baseUp);
                // never higher than the room where the camera is now
                const float height =
                    std::min(m_chaseHeight, chaseRoom(base, baseUp, m_chaseHeight));
                eye = base + baseUp * height;
                Pnt3f ahead = forward;
                ahead.normalize();
                Pnt3f look =
                    position + ahead * (open.lookAhead - train.length) + up * open.lookHeight;
                // Dragged round: the view swings round the middle of the
                // train, and where blocks stand between the train and the
                // camera it comes in front of them, as a block game's third
                // person camera does.
                if (!chaseLook.centered())
                {
                    const float uMid = TrainModel::parameterBehind(*m_pTrack, m_pTrack->trainU,
                                                                   0.5f * train.length, splineType);
                    Pnt3f midForward = m_pTrack->evaluateTangent(uMid, splineType);
                    Pnt3f midSide, midUp;
                    makeTrackFrame(midForward, midSide, midUp);
                    const Pnt3f pivot = m_pTrack->evaluatePosition(uMid, splineType) +
                                        midUp * (0.6f * train.riderTop);
                    ChaseRig::Point e = {eye.x, eye.y, eye.z};
                    ChaseRig::Point l = {look.x, look.y, look.z};
                    ChaseRig::orbit({pivot.x, pivot.y, pivot.z}, e, l, chaseLook.yaw(),
                                    chaseLook.pitch());
                    eye = Pnt3f(e.x, e.y, e.z);
                    look = Pnt3f(l.x, l.y, l.z);
                    Pnt3f out = eye + pivot * -1.0f;
                    const float reach = sqrtf(out.x * out.x + out.y * out.y + out.z * out.z);
                    if (reach > 1e-3f)
                    {
                        out = out * (1.0f / reach);
                        const float margin = 1.0f;
                        for (float d = 0.5f; d <= reach + margin; d += 0.5f)
                            if (chaseBlocked(pivot + out * d))
                            {
                                const float nearer = std::max(0.5f, d - margin);
                                if (nearer < reach)
                                    eye = pivot + out * nearer;
                                break;
                            }
                    }
                }
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                gluPerspective(m_fieldOfView, aspect, 0.5, 3000.0);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                gluLookAt(eye.x, eye.y, eye.z, look.x, look.y, look.z, 0.0, 1.0, 0.0);
                return;
            }
            m_chaseValid = false;
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
// * Is there a block of the island or the sea floor at p? (for the Chase camera)
//========================================================================
bool TrainView::chaseBlocked(const Pnt3f &p) const
{
    return m_land.solidAt(!sceneryOn(), p.x, p.y, p.z) || m_ocean.solidAt(p.x, p.y, p.z);
}

//************************************************************************
//
// * How high above base, along up, the Chase camera can go before it is
//   within a block and a half of a block (at most upTo)
//========================================================================
float TrainView::chaseRoom(const Pnt3f &base, const Pnt3f &up, float upTo) const
{
    const float margin = 1.5f;
    for (float h = 1.0f; h <= upTo + margin; h += 0.5f)
        if (chaseBlocked(base + up * h))
            return std::max(1.0f, h - margin);
    return upTo;
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
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
        PerfScope part(PerfStage::Track);
        m_trackRenderer.draw(*m_pTrack, splineType, doingShadows, tw->settings.adaptiveTrack);
    }

#ifdef EXAMPLE_SOLUTION
    drawTrack(this, doingShadows);
#endif

    // wooden trestles where the track is high above the scenery
    if (sceneryOn() && m_pTrack->points.size() >= 4)
    {
        PerfScope part(PerfStage::Supports);
        drawSupports(doingShadows);
    }

    // draw the train: a locomotive and five carts with riders (TrainModel)
    if (tw->settings.camera != CameraView::Train && m_pTrack->points.size() >= 4)
    {
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->settings.spline);
        PerfScope part(PerfStage::Train);
        TrainModel::draw(*m_pTrack, splineType, doingShadows, animationTime());
    }

    // the island's see-through water and glass last, blended over everything
    // behind them (only the glass when the sea's shader draws the water after
    // the scene: landWaterLater)
    if (!doingShadows)
    {
        PerfScope part(PerfStage::Water);
        lighting.setTranslucent(true);
        if (landWaterLater)
            m_land.drawGlass(!sceneryOn());
        else
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
void TrainView::doPick(int mx, int my)
//========================================================================
{
    // since we'll need to do some GL stuff so we make this window as
    // active window
    make_current();

    // get the viewport - most reliable way to turn mouse coords into GL coords
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Set up the pick matrix on the stack - remember, window y is
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
