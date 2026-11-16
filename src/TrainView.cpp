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
    if (!view->tw->worldCam->value() && !fly)
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
    // A touchpad gesture that reaches here bypassed Direct Manipulation.
    static std::chrono::steady_clock::time_point lastLog;
    const auto now = std::chrono::steady_clock::now();
    if (now - lastLog > std::chrono::milliseconds(500))
        touchpadLog("wheel message: %s delta %d%s%s",
                    message->message == WM_MOUSEWHEEL ? "vertical" : "horizontal", delta,
                    (keys & MK_CONTROL) ? ", Ctrl (pinch)" : "", precise ? ", precise" : "");
    lastLog = now;
    // Match FLTK's signs: wheel up is -dy, tilting right is +dx.
    const float dx = message->message == WM_MOUSEWHEEL ? 0.0f : steps;
    const float dy = message->message == WM_MOUSEWHEEL ? -steps : 0.0f;
    if (fly)
    {
        view->flyCam.scroll(dx, dy, state, precise);
        // The mouse wheel changes the flying speed; keep the slider in step.
        view->tw->flySpeed->value(view->flyCam.speed());
    }
    else
        view->arcball.scroll(dx, dy, state, precise);
    return 1;
}

static bool touchpadAccepts(void *data)
{
    TrainView *view = static_cast<TrainView *>(data);
    return view->tw && (view->tw->worldCam->value() || view->tw->flyCam->value());
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
    // In Fly mode Shift sinks the camera, so only Alt slides.
    if (view->tw->flyCam->value())
        view->flyCam.touchpadGesture(dx, dy, scale, alt);
    else
        view->arcball.touchpadGesture(dx, dy, scale, pan);
}

#ifdef EXAMPLE_SOLUTION
#include "TrainExample/TrainExample.H"
#endif

// Field of view of the world camera, in degrees.
static const float worldFieldOfView = 40.0f;

// Scenery positions (x, z).
static const float treePositions[][2] = {
    {-82.0f, -68.0f}, {-78.0f, 70.0f}, {82.0f, -70.0f}, {78.0f, 68.0f}, {0.0f, 88.0f}};
static const int treeCount = (int)(sizeof(treePositions) / sizeof(treePositions[0]));

//************************************************************************
//
// * Constructor to set up the GL window
//========================================================================
TrainView::TrainView(int x, int y, int w, int h, const char *l) : Fl_Gl_Window(x, y, w, h, l)
//========================================================================
{
    tw = 0;
    selectedCube = -1;
    flyCamPlaced = false;
    mouseCaptured = false;
    mode(FL_RGB | FL_ALPHA | FL_DOUBLE | FL_STENCIL);

    resetArcball();
    flyCam.setup(this);
    Fl::add_system_handler(touchpadWheel, this);
}

TrainView::~TrainView()
{
    if (mouseCaptured)
        ClipCursor(nullptr);
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
    arcball.setup(this, worldFieldOfView, 250, .2f, .4f, 0);
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
    flyCam.setFromViewMatrix(view, worldFieldOfView);
    flyCamPlaced = true;
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
    touchpad.inertia = !tw->flyCam->value();
    if (!tw->flyCam->value())
    {
        setMouseCaptured(false);
        flyCam.stop();
        return;
    }
    if (!flyCamPlaced)
        placeFlyCamAtWorldView();
    // Take keyboard focus from the camera button so WASD works at once.
    Fl::focus(this);
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
    // see if the ArcBall will handle the event - if it does,
    // then we're done
    // note: the arcball only gets the event if we're in world view
    if (tw->worldCam->value())
        if (arcball.handle(event))
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
        const float speedBefore = flyCam.speed();
        const int used = flyCam.handle(event);
        if (flyCam.speed() != speedBefore)
            tw->flySpeed->value(flyCam.speed());
        if (used)
            return 1;
        // R puts the observer back at the world camera's viewpoint.
        if (event == FL_KEYBOARD && Fl::event_key() == 'r')
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
            doPick();
            // Dragging empty space orbits (Alt pans), so one finger is enough on a touchpad.
            if (selectedCube < 0 && tw->worldCam->value())
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

    // Mouse button drag event
    case FL_DRAG:

        // Compute the new control point position
        if ((last_push == FL_LEFT_MOUSE) && (selectedCube >= 0))
        {
            ControlPoint *cp = &m_pTrack->points[selectedCube];

            double r1x, r1y, r1z, r2x, r2y, r2z;
            getMouseLine(r1x, r1y, r1z, r2x, r2y, r2z);

            double rx, ry, rz;
            mousePoleGo(r1x, r1y, r1z, r2x, r2y, r2z, static_cast<double>(cp->pos.x),
                        static_cast<double>(cp->pos.y), static_cast<double>(cp->pos.z), rx, ry, rz,
                        (Fl::event_state() & FL_CTRL) != 0);

            cp->pos.x = (float)rx;
            cp->pos.y = (float)ry;
            cp->pos.z = (float)rz;
            damage(1);
        }
        break;

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

    // A new GL context starts without our textures; let the track rebuild them.
    if (!context_valid())
        m_trackRenderer.releaseTextures();

    // Set up the view port
    glViewport(0, 0, w(), h());

    // clear the window, be sure to clear the Z-Buffer too
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

    if (tw->flyCam->value())
        drawFlyHorizon();

    setupFloor();
    glDisable(GL_LIGHTING);
    drawFloor(200, 10);

    //*********************************************************************
    // now draw the object and we need to do it twice
    // once for real, and then once for shadows
    //*********************************************************************
    glEnable(GL_LIGHTING);
    setupObjects();

    drawStuff();

    // this time drawing is for shadows (except for top view)
    if (!tw->topCam->value())
    {
        setupShadows();
        drawStuff(true);
        unsetupShadows();
    }

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
// * Overlay for the observer camera: a crosshair at the view centre and a
//   controls hint along the bottom.
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

    // Controls hint along the bottom of the view.
    const char *lines[2] = {mouseCaptured ? "Mouse looks around  -  Esc frees the mouse"
                                          : "Click the view to look with the mouse",
                            "WASD move  Space/Shift up/down  Ctrl fast  Wheel speed  F train"};
    // A dim band keeps the text readable over the light floor.
    glColor4f(0, 0, 0, .45f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f((float)w(), 0);
    glVertex2f((float)w(), 40);
    glVertex2f(0, 40);
    glEnd();
    gl_font(FL_HELVETICA, 12);
    for (int i = 0; i < 2; ++i)
    {
        const float x = (w() - (float)gl_width(lines[i])) * .5f;
        const float y = 10.0f + 16.0f * (1 - i);
        glColor4f(0, 0, 0, .7f);
        gl_draw(lines[i], x + 1, y - 1);
        glColor4f(1, 1, 1, .9f);
        gl_draw(lines[i], x, y);
    }

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
            eye = position + up * 2.2f + forward * 3.0f;
        }

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(60.0, aspect, 0.5, 1000.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        const Pnt3f target = eye + forward * 15.0f;
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
static void drawTrainBox(float halfX, float halfY, float halfZ)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glVertex3f(-halfX, -halfY, halfZ);
    glVertex3f(halfX, -halfY, halfZ);
    glVertex3f(halfX, halfY, halfZ);
    glVertex3f(-halfX, halfY, halfZ);
    glNormal3f(0, 0, -1);
    glVertex3f(halfX, -halfY, -halfZ);
    glVertex3f(-halfX, -halfY, -halfZ);
    glVertex3f(-halfX, halfY, -halfZ);
    glVertex3f(halfX, halfY, -halfZ);
    glNormal3f(0, 1, 0);
    glVertex3f(-halfX, halfY, halfZ);
    glVertex3f(halfX, halfY, halfZ);
    glVertex3f(halfX, halfY, -halfZ);
    glVertex3f(-halfX, halfY, -halfZ);
    glNormal3f(0, -1, 0);
    glVertex3f(-halfX, -halfY, -halfZ);
    glVertex3f(halfX, -halfY, -halfZ);
    glVertex3f(halfX, -halfY, halfZ);
    glVertex3f(-halfX, -halfY, halfZ);
    glNormal3f(1, 0, 0);
    glVertex3f(halfX, -halfY, halfZ);
    glVertex3f(halfX, -halfY, -halfZ);
    glVertex3f(halfX, halfY, -halfZ);
    glVertex3f(halfX, halfY, halfZ);
    glNormal3f(-1, 0, 0);
    glVertex3f(-halfX, -halfY, -halfZ);
    glVertex3f(-halfX, -halfY, halfZ);
    glVertex3f(-halfX, halfY, halfZ);
    glVertex3f(-halfX, halfY, -halfZ);
    glEnd();
}

static void drawTreeCone(float radius, float height)
{
    const int segments = 12;
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = 6.2831853f * i / segments;
        const float a1 = 6.2831853f * (i + 1) / segments;
        const float mid = (a0 + a1) * 0.5f;
        const float nx = cosf(mid);
        const float nz = sinf(mid);
        const float normalLength = sqrtf(1.0f + (radius / height) * (radius / height));
        glNormal3f(nx / normalLength, (radius / height) / normalLength, nz / normalLength);
        glVertex3f(radius * cosf(a0), 0.0f, radius * sinf(a0));
        glVertex3f(0.0f, height, 0.0f);
        glVertex3f(radius * cosf(a1), 0.0f, radius * sinf(a1));
    }
    glEnd();
}

static void drawTree(float x, float z, bool doingShadows)
{
    const int trunkSegments = 10;
    const float trunkRadius = 1.1f;
    const float trunkHeight = 7.0f;
    glPushAttrib(GL_CURRENT_BIT);
    glPushMatrix();
    glTranslatef(x, 0.0f, z);
    if (!doingShadows)
        glColor3ub(105, 65, 35);
    glBegin(GL_QUADS);
    for (int i = 0; i < trunkSegments; ++i)
    {
        const float a0 = 6.2831853f * i / trunkSegments;
        const float a1 = 6.2831853f * (i + 1) / trunkSegments;
        const float nx = cosf((a0 + a1) * 0.5f);
        const float nz = sinf((a0 + a1) * 0.5f);
        glNormal3f(nx, 0.0f, nz);
        glVertex3f(trunkRadius * cosf(a0), 0.0f, trunkRadius * sinf(a0));
        glVertex3f(trunkRadius * cosf(a0), trunkHeight, trunkRadius * sinf(a0));
        glVertex3f(trunkRadius * cosf(a1), trunkHeight, trunkRadius * sinf(a1));
        glVertex3f(trunkRadius * cosf(a1), 0.0f, trunkRadius * sinf(a1));
    }
    glEnd();

    if (!doingShadows)
        glColor3ub(35, 125, 55);
    glPushMatrix();
    glTranslatef(0.0f, 4.0f, 0.0f);
    drawTreeCone(5.0f, 11.0f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0.0f, 9.0f, 0.0f);
    drawTreeCone(3.8f, 9.0f);
    glPopMatrix();
    glPopMatrix();
    glPopAttrib();
}

//========================================================================
void TrainView::drawStuff(bool doingShadows)
{
    // Draw the control points
    // don't draw the control points if you're driving
    // (otherwise you get sea-sick as you drive through them)
    if (!tw->trainCam->value())
    {
        for (size_t i = 0; i < m_pTrack->points.size(); ++i)
        {
            if (!doingShadows)
            {
                if (((int)i) != selectedCube)
                    glColor3ub(240, 60, 60);
                else
                    glColor3ub(240, 240, 30);
            }
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

    // A small ring of trees gives the track a visible setting beyond the ground.
    for (int i = 0; i < treeCount; ++i)
        drawTree(treePositions[i][0], treePositions[i][1], doingShadows);

    // draw the train
    if (!tw->trainCam->value() && m_pTrack->points.size() >= 4)
    {
        const TrackSplineType splineType = static_cast<TrackSplineType>(tw->splineBrowser->value());
        const Pnt3f position = m_pTrack->evaluatePosition(m_pTrack->trainU, splineType);
        Pnt3f forward = m_pTrack->evaluateTangent(m_pTrack->trainU, splineType);
        Pnt3f side;
        Pnt3f up;
        makeTrackFrame(forward, side, up);

        const GLfloat frame[16] = {side.x,     side.y,     side.z,     0.0f,      up.x,      up.y,
                                   up.z,       0.0f,       forward.x,  forward.y, forward.z, 0.0f,
                                   position.x, position.y, position.z, 1.0f};

        glPushAttrib(GL_CURRENT_BIT);
        glPushMatrix();
        glMultMatrixf(frame);
        glTranslatef(0.0f, 1.1f, 0.0f);
        if (!doingShadows)
            glColor3ub(35, 110, 190);
        drawTrainBox(1.5f, 0.65f, 2.4f);

        glPushMatrix();
        glTranslatef(0.0f, 0.85f, -0.25f);
        if (!doingShadows)
            glColor3ub(55, 155, 220);
        drawTrainBox(0.95f, 0.55f, 1.1f);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(0.0f, -0.05f, 2.45f);
        if (!doingShadows)
            glColor3ub(230, 75, 35);
        drawTrainBox(1.15f, 0.45f, 0.35f);
        glPopMatrix();

        glPopMatrix();
        glPopAttrib();
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
