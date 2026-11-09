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

// Preserve the Ctrl flag supplied by Windows pinch-to-zoom messages.
// Reading only the physical keyboard state can lose this synthetic flag.
static int touchpadZoom(void *event, void *data)
{
    MSG *message = static_cast<MSG *>(event);
    TrainView *view = static_cast<TrainView *>(data);
    if (message->message != WM_MOUSEWHEEL || !(LOWORD(message->wParam) & MK_CONTROL) || !view->tw ||
        !view->tw->worldCam->value() || !view->shown())
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
    view->arcball.zoomByWheel((float)(short)HIWORD(message->wParam) / WHEEL_DELTA);
    return 1;
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
    Fl::add_system_handler(touchpadZoom, this);
}

TrainView::~TrainView()
{
    Fl::remove_system_handler(touchpadZoom);
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
            doPick();
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
    case FL_ENTER:
        focus(this);
        break;

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

    // A new GL context starts without our textures, shaders and frame
    // buffers; let the track and the lighting rebuild them.
    if (!context_valid())
    {
        m_trackRenderer.releaseTextures();
        m_ground.releaseTextures();
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
        return;
    }

    setupFloor();
    glDisable(GL_LIGHTING);
    drawGround();

    //*********************************************************************
    // now draw the object and we need to do it twice
    // once for real, and then once for shadows
    //*********************************************************************
    glEnable(GL_LIGHTING);
    setupObjects();

    drawStuff();

    // this time drawing is for shadows (except for top view)
    if (tw->shadowButton->value() && !tw->topCam->value())
    {
        setupShadows();
        drawStuff(true);
        unsetupShadows();
    }
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
    drawTree(-82.0f, -68.0f, doingShadows);
    drawTree(-78.0f, 70.0f, doingShadows);
    drawTree(82.0f, -70.0f, doingShadows);
    drawTree(78.0f, 68.0f, doingShadows);
    drawTree(0.0f, 88.0f, doingShadows);

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
