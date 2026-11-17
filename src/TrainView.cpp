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
    if (!view->tw || !view->tw->worldCam->value() || !view->shown())
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
    // Match FLTK's signs: wheel up is -dy, tilting right is +dx.
    if (message->message == WM_MOUSEWHEEL)
        view->arcball.scroll(0, -steps, state, precise);
    else
        view->arcball.scroll(steps, 0, state, precise);
    return 1;
}

static bool touchpadAccepts(void *data)
{
    TrainView *view = static_cast<TrainView *>(data);
    return view->tw && view->tw->worldCam->value();
}

// Direct Manipulation reports finger movement and pinch together, so the
// camera orbits (Alt or Shift pans) and zooms in the same gesture.
static void touchpadMoves(void *data, float dx, float dy, float scale)
{
    if (!touchpadAccepts(data))
        return;
    const bool pan = GetKeyState(VK_MENU) < 0 || GetKeyState(VK_SHIFT) < 0;
    static_cast<TrainView *>(data)->arcball.touchpadGesture(dx, dy, scale, pan);
}

#ifdef EXAMPLE_SOLUTION
#include "TrainExample/TrainExample.H"
#endif

//************************************************************************
//
// * Constructor to set up the GL window
//========================================================================
TrainView::TrainView(int x, int y, int w, int h, const char *l) : Fl_Gl_Window(x, y, w, h, l)
//========================================================================
{
    tw = 0;
    selectedCube = -1;
    mode(FL_RGB | FL_ALPHA | FL_DOUBLE | FL_STENCIL);

    resetArcball();
    Fl::add_system_handler(touchpadWheel, this);
}

TrainView::~TrainView()
{
    Fl::remove_system_handler(touchpadWheel);
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
    arcball.setup(this, 40, 250, .2f, .4f, 0);
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

    // every time the mouse enters this window, aggressively take focus;
    // returning 1 is what makes FLTK send FL_MOVE afterwards
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
//   cameras.
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

// Keys when no edit is in progress. True if the key was used.
bool TrainView::editKey(int key)
{
    const int state = Fl::event_state();
    if (key == FL_KP + '1' || key == FL_KP + '7' || key == FL_KP + '0')
    {
        Fl_Button *camera =
            key == FL_KP + '1' ? tw->worldCam : (key == FL_KP + '7' ? tw->topCam : tw->trainCam);
        camera->setonly();
        tw->damageMe();
        return true;
    }
    if (!canEdit())
        return false;
    if (key == 'z' && (state & FL_CTRL) != 0)
    {
        undoEdit();
        return true;
    }
    if (!hasSelection())
        return false;

    if (key == 'g' && m_editCameraValid)
    {
        beginEdit(EDIT_GRAB, TransformGizmo::VIEW, Fl::event_x(), Fl::event_y());
        return true;
    }
    if (key == 'r' && m_editCameraValid)
    {
        beginEdit(EDIT_ROTATE, TransformGizmo::NONE, Fl::event_x(), Fl::event_y());
        return true;
    }
    if (key == 'd' && (state & FL_SHIFT) != 0 && m_editCameraValid)
    {
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
    if (key == FL_Delete || key == 'x')
    {
        m_undoValid = false; // the indices are about to change
        deletePointCB(nullptr, tw);
        return true;
    }
    return false;
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
    m_editCameraValid = true;

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
        tw->topCam->value() ? "Top Orthographic"
                            : (tw->trainCam->value()
                                   ? "Train Camera"
                                   : (tw->chaseCam->value() ? "Chase Camera" : "User Perspective"));
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
        return;
    }

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
    // Or we use the top cam
    else if (tw->topCam->value())
    {
        float wi, he;
        if (aspect >= 1)
        {
            wi = 110;
            he = wi / aspect;
        }
        else
        {
            he = 110;
            wi = he * aspect;
        }

        // Set up the top camera drop mode to be orthogonal and set
        // up proper projection matrix
        glMatrixMode(GL_PROJECTION);
        glOrtho(-wi, wi, -he, he, 200, -200);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotatef(-90, 1, 0, 0);
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
            // the chase camera rides behind and above the last car and looks
            // over the train
            if (tw->chaseCam->value())
                eye = position + up * 8.0f + forward * -23.0f;
        }

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        // a wide view, tipped down a little: on a climb the rider still sees
        // the land below and not only the sky
        gluPerspective(72.0, aspect, 0.5, 1000.0);
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

    // draw the train: three coupled cars with riders (TrainModel)
    if (!tw->trainCam->value() && m_pTrack->points.size() >= 4)
    {
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->splineBrowser->value());
        TrainModel::draw(*m_pTrack, splineType, doingShadows, animationTime());
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
