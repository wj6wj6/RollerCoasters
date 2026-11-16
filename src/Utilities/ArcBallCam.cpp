/************************************************************************
     File:        ArcBallCam.h

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu
     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu

     Comment:    
						This is a camera UI for FlTk OpenGL Applications.

	 
						An arcball is a user interface for rotating an object 
						around its very useful for looking around the world.

						The actual mathematics to implement it are elegant, 
						but beyond the scope of CS559. This example code is 
						provided for you to use
						without having to worry about the details of 
						how it works.

     Note:
						1.	A quickly hacked together implementation of ArcBall, 
							with lots of bits lifted from Ken Shoemake's original 
							source in Graphics Gems	made into a nice C++ thing
						2.	It is simplified a lot - the controller is always 
						   at the origin,
						3.	It has a fixed radius, it doesn't have constraints

						4.	Added the double right click and the mousewheel - 
							to make a much	better UI.

					   5. the "ball" also stores all of the other camera 
							parameters (position, FoV)

						6. We take over the right mouse button - pass us your 
							events, and we'll take the ones we want.
						7. the ALT button lets you do panning
							double right click resets the arcball if you get lost

						To use it:
						1) instantiate it in your Fl_Gl_Window class
						2) call setup in the constructor of your window class
						3) call its handle method inside of your handle method
						4) use its SetProjection method when you need to set the 
							camera (like when drawing)

     Platform:    Visio Studio.Net 2003 (converted to 2005)

*************************************************************************/

#include "ArcBallCam.H"

#include <math.h>
#include <windows.h>

// the FlTk headers have lots of warnings - these are bad, but there's not
// much we can do about them
#pragma warning(push)
#pragma warning(disable : 4311) // convert void* to long
#pragma warning(disable : 4312) // convert long to void*
#include <FL/Fl_Gl_Window.H>
#include <FL/Fl.H>
#include <GL/GL.h>
#include <GL/GLU.h>
#include <FL/Fl_Double_Window.H>
#pragma warning(pop)

#include "stdio.h"

//**************************************************************************
//
// * Constructor
//==========================================================================
ArcBallCam::ArcBallCam()
    : mode(None), eyeX(0), eyeY(0), eyeZ(20), initEyeZ(20), fieldOfView(40), isx(0), isy(0), isz(0),
      wind(0)
//==========================================================================
{
}

ArcBallCam::~ArcBallCam()
{
    stopGesture();
}

void ArcBallCam::stopGesture()
{
    Fl::remove_timeout(animateGesture, this);
    gestureActive = false;
    pendingGestureX = pendingGestureY = 0;
    pendingZoom = 0;
}

void ArcBallCam::startGesture()
{
    if (!gestureActive)
    {
        gestureActive = true;
        gestureTime = std::chrono::steady_clock::now();
        Fl::add_timeout(1.0 / 60.0, animateGesture, this);
    }
}

void ArcBallCam::zoomByWheel(float steps, bool precise)
{
    // Zoom only changes the distance, so it may run during a drag or an orbit.
    if (steps == 0)
        return;
    if (precise)
    {
        applyZoom(steps);
        wind->damage(1);
        return;
    }
    pendingZoom += steps;
    startGesture();
}

void ArcBallCam::applyZoom(float steps)
{
    eyeZ *= (float)pow(1.1, -steps);
    if (eyeZ < 1.0f)
        eyeZ = 1.0f;
    if (eyeZ > 900.0f)
        eyeZ = 900.0f;
}

void ArcBallCam::scroll(float dx, float dy, int state, bool precise)
{
    if (state & FL_CTRL)
    {
        zoomByWheel(-dy, precise);
    }
    else if (mode != None)
    {
        // Do not change the drag's coordinate frame mid-drag.
        return;
    }
    else if (state & (FL_ALT | FL_SHIFT))
    {
        stopGesture();
        eyeX += dx * eyeZ * .02f;
        eyeY -= dy * eyeZ * .02f;
    }
    else if (precise)
    {
        // Follow the fingers directly; extra smoothing only adds lag.
        orbit(-dx * .035f, -dy * .035f);
    }
    else
    {
        // Limit large scroll bursts to keep rotation controllable.
        if (dx > 10)
            dx = 10;
        if (dx < -10)
            dx = -10;
        if (dy > 10)
            dy = 10;
        if (dy < -10)
            dy = -10;
        pendingGestureX += dx;
        pendingGestureY += dy;
        if (dx != 0 || dy != 0)
            startGesture();
    }
    wind->damage(1);
}

void ArcBallCam::touchpadGesture(float dx, float dy, float scale, bool pan)
{
    if (scale > 0 && scale != 1)
        applyZoom((float)(log(scale) / log(1.1)));
    // A mouse drag owns the view direction until it ends.
    if (mode == None && (dx != 0 || dy != 0))
    {
        // Same feel as dragging: the scene follows the fingers.
        const float ndcX = dx * 2.0f / (float)wind->w();
        const float ndcY = -dy * 2.0f / (float)wind->h();
        if (pan)
        {
            eyeX -= ndcX * eyeZ;
            eyeY -= ndcY * eyeZ;
        }
        else
        {
            orbit(ndcX * 1.5f, -ndcY * 1.5f);
        }
    }
    wind->damage(1);
}

void ArcBallCam::beginDrag(bool pan)
{
    stopGesture();
    float x, y;
    getMouseNDC(x, y);
    down(x, y);
    mode = pan ? Pan : Rotate;
    wind->damage(1);
}

void ArcBallCam::orbit(float yawDelta, float pitchDelta)
{
    HMatrix matrix;
    getMatrix(matrix);
    // Extract the viewing direction, then rebuild with world Y as up.
    float yaw = (float)atan2(-matrix[0][2], matrix[2][2]) + yawDelta;
    float pitch = (float)atan2(matrix[1][2],
                               sqrt(matrix[0][2] * matrix[0][2] + matrix[2][2] * matrix[2][2])) +
                  pitchDelta;
    const float limit = 1.48352986f; // 85 degrees: never flip over the poles.
    if (pitch > limit)
        pitch = limit;
    if (pitch < -limit)
        pitch = -limit;
    Quat yawRotation(0, (float)sin(yaw * .5f), 0, (float)cos(yaw * .5f));
    Quat pitchRotation((float)sin(pitch * .5f), 0, 0, (float)cos(pitch * .5f));
    start = pitchRotation * yawRotation;
    start.renorm();
    now = Quat();
}

void ArcBallCam::animateGesture(void *data)
{
    ArcBallCam *camera = static_cast<ArcBallCam *>(data);
    const auto currentTime = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(currentTime - camera->gestureTime).count();
    camera->gestureTime = currentTime;
    // Keep a delayed frame from consuming the entire remaining motion.
    if (dt > 1.0 / 60.0)
        dt = 1.0 / 60.0;
    float blend = (float)(1.0 - exp(-dt / .060));
    if (fabs(camera->pendingGestureX) + fabs(camera->pendingGestureY) + fabs(camera->pendingZoom) <
        .001f)
        blend = 1;
    const float dx = camera->pendingGestureX * blend;
    const float dy = camera->pendingGestureY * blend;
    camera->pendingGestureX -= dx;
    camera->pendingGestureY -= dy;
    if (dx != 0 || dy != 0)
        camera->orbit(-dx * .035f, -dy * .035f);
    const float zoom = camera->pendingZoom * blend;
    camera->pendingZoom -= zoom;
    camera->applyZoom(zoom);
    camera->wind->damage(1);
    if (blend == 1)
    {
        camera->gestureActive = false;
    }
    else
    {
        Fl::repeat_timeout(1.0 / 60.0, animateGesture, camera);
    }
}

//**************************************************************************
//
// * really do the setup stuff, once you have the window
//==========================================================================
void ArcBallCam::setup(Fl_Gl_Window *_wind, float _fieldOfView, float _eyeZ, float _isx, float _isy,
                       float _isz)
//==========================================================================
{
    // set up the basic information
    stopGesture();
    wind = _wind;
    fieldOfView = _fieldOfView;
    eyeZ = _eyeZ;
    initEyeZ = _eyeZ;
    eyeX = 0;
    eyeY = 0;
    isx = _isx;
    isy = _isy;
    isz = _isz;

    reset();
    spin(isx, isy, isz);
    orbit(0, 0);
}

//**************************************************************************
//
// * Set up the camera projection
//==========================================================================
void ArcBallCam::setProjection(bool doClear)
//==========================================================================
{
    glMatrixMode(GL_PROJECTION);
    if (doClear)
        glLoadIdentity();

    // Compute the aspect ratio so we don't distort things
    double aspect = ((double)wind->w()) / ((double)wind->h());
    gluPerspective(fieldOfView, aspect, .1, 1000);

    // Put the camera where we want it to be
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Use the transformation in the ArcBall
    glTranslatef(-eyeX, -eyeY, -eyeZ);
    multMatrix();
}

//**************************************************************************
//
// * Handle the event happen to this camera
//==========================================================================
int ArcBallCam::handle(int e)
//==========================================================================
{
    switch (e)
    {
    case FL_PUSH:
        stopGesture();
        mode = None;
        // Shift-left drag also controls the camera on touchpads.
        if (Fl::event_button() == FL_RIGHT_MOUSE ||
            (Fl::event_button() == FL_LEFT_MOUSE && (Fl::event_state() & FL_SHIFT)))
        {
            // double click? do a reset
            if (Fl::event_clicks())
            {
                setup(wind, fieldOfView, initEyeZ, isx, isy, isz);
                wind->damage(1);
                return 1;
            }

            // Get the mouse position
            float x, y;
            getMouseNDC(x, y);

            // Compute the mouse position
            down(x, y);

            // Tell window to refresh
            wind->damage(1);

            // Set up the mode
            mode = (Fl::event_state() & FL_ALT) ? Pan : Rotate;
            return 1;
        };
        break;

    case FL_RELEASE:
        if (mode != None)
        {
            wind->damage(1);
            mode = None;
            return 1;
        }
        break;

    case FL_DRAG: // if the user drags the mouse
        if (mode != None)
        { // we're taking the drags
            float x, y;
            getMouseNDC(x, y);
            computeNow(x, y);
            wind->damage(1);
            return 1;
        };
        break;
    case FL_MOUSEWHEEL:
        // FLTK rounds wheel input to whole notches; Windows touchpads reach
        // scroll() with their exact deltas through TrainView's system handler.
        scroll((float)Fl::event_dx(), (float)Fl::event_dy(), Fl::event_state(), false);
        return 1;
    }
    return 0;
}

//**************************************************************************
//
// * Get the mouse in NDC
//==========================================================================
void ArcBallCam::getMouseNDC(float &x, float &y)
//==========================================================================
{
    // notice, we put everything into doubles so we can do the math
    float mx = (float)Fl::event_x(); // remeber where the mouse went down
    float my = (float)Fl::event_y();

    // we will assume that the viewport is the same as the window size
    float wd = (float)wind->w();
    float hd = (float)wind->h();

    // remember that FlTk has Y going the wrong way!
    my = hd - my;

    x = (mx / wd) * 2.0f - 1.f;
    y = (my / hd) * 2.0f - 1.f;
}

//**************************************************************************
//
// * When the mouse goes down, remember where. also, clear out the now
//   position by putting it into start
//==========================================================================
void ArcBallCam::down(const float x, const float y)
//==========================================================================
{
    start = now * start;
    now = Quat(); // identity

    downX = x;
    downY = y;
}

//**************************************************************************
//
// * Get the whole matrix!
//==========================================================================
void ArcBallCam::getMatrix(HMatrix m) const
//==========================================================================
{
    Quat qAll = now * start;
    qAll = qAll.conjugate(); // since Ken does everything transposed
    qAll.toMatrix(m);
}

//**************************************************************************
//
// * a simplified interface - so you never see the insides of arcball
//==========================================================================
void ArcBallCam::multMatrix()
//==========================================================================
{
    HMatrix m;
    getMatrix(m);
    glMultMatrixf((float *)m);
}

//**************************************************************************
//
// * Reset the camera's control related parameters
//==========================================================================
void ArcBallCam::reset()
//==========================================================================
{
    start.x = 0;
    start.y = 0;
    start.z = 0;
    start.w = 1;

    now.x = now.y = now.z = 0;
    now.w = 1;
}

//**************************************************************************
//
// *
//==========================================================================
void ArcBallCam::spin(float x, float y, float z)
//==========================================================================
{
    // printf("\nstart was %g %g %g %g\n",start.x,start.y,start.z,start.w);
    // first, get rid of anything cached
    start = now * start;
    now = Quat();

    float iw = x * x + y * y + z * z;
    if (iw < 1)
        iw = sqrtf(1 - iw);
    else
        iw = 0;
    Quat newq(x, y, z, iw);

    newq.renorm();
    start = newq * start;
    // printf(" (no norm) %g %g %g %g\n",start.x,start.y,start.z,start.w);

    start.renorm();
    // printf("start is %g %g %g %g\n",start.x,start.y,start.z,start.w);
}

//**************************************************************************
//
// *
//==========================================================================
void ArcBallCam::computeNow(const float nowX, const float nowY)
//==========================================================================
{
    if (mode == Rotate)
    {
        orbit((nowX - downX) * 1.5f, -(nowY - downY) * 1.5f);
        downX = nowX;
        downY = nowY;
    }
    else if (mode == Pan)
    {
        // Pan step by step so zooming mid-drag does not make the view jump.
        eyeX -= (nowX - downX) * eyeZ;
        eyeY -= (nowY - downY) * eyeZ;
        downX = nowX;
        downY = nowY;
    }
}

//*****************************************************************************
//
// Minimal Quaternion Class - if you don't know what a quaternion is, don't
// worry. if you do know what a quaternion is, you probably want a more complete
// implementation
//
//*****************************************************************************

//**************************************************************************
//
// * Set up constructor
//==========================================================================
Quat::Quat(float ix, float iy, float iz, float iw) : x(ix), y(iy), z(iz), w(iw)
//==========================================================================
{
}

//**************************************************************************
//
// * Default constructor
//==========================================================================
Quat::Quat() : x(0), y(0), z(0), w(1)
//==========================================================================
{
}

//**************************************************************************
//
// * Copy constructor
//==========================================================================
Quat::Quat(const Quat &q) : x(q.x), y(q.y), z(q.z), w(q.w)
//==========================================================================
{
}

//**************************************************************************
//
// * Renormalize, in case things got messed up
//==========================================================================
void Quat::renorm()
//==========================================================================
{
    float Nq = 1.f / (float)sqrt(x * x + y * y + z * z + w * w);
    x *= Nq;
    y *= Nq;
    z *= Nq;
    w *= Nq;
}

//**************************************************************************
//
// * Conversions to a 4x4 matrix
//==========================================================================
void Quat::toMatrix(HMatrix out) const
//==========================================================================
{
    float Nq = x * x + y * y + z * z + w * w;
    float s = (Nq > 0.f) ? (2.0f / Nq) : 0.f;
    float xs = x * s, ys = y * s, zs = z * s;
    float wx = w * xs, wy = w * ys, wz = w * zs;
    float xx = x * xs, xy = x * ys, xz = x * zs;
    float yy = y * ys, yz = y * zs, zz = z * zs;
    out[X][X] = 1.0f - (yy + zz);
    out[Y][X] = xy + wz;
    out[Z][X] = xz - wy;
    out[X][Y] = xy - wz;
    out[Y][Y] = 1.0f - (xx + zz);
    out[Z][Y] = yz + wx;
    out[X][Z] = xz + wy;
    out[Y][Z] = yz - wx;
    out[Z][Z] = 1.0f - (xx + yy);
    out[X][W] = out[Y][W] = out[Z][W] = out[W][X] = out[W][Y] = out[W][Z] = 0.0f;
    out[W][W] = 1.0f;
}

//**************************************************************************
//
// * Find the conjugate of the quaternion
//==========================================================================
Quat Quat::conjugate() const
//==========================================================================
{
    return Quat(-x, -y, -z, w);
}

//**************************************************************************
//
// *
//==========================================================================
Quat Quat::operator*(const Quat &qR) const
//==========================================================================
{
    Quat qq;
    qq.w = w * qR.w - x * qR.x - y * qR.y - z * qR.z;
    qq.x = w * qR.x + x * qR.w + y * qR.z - z * qR.y;
    qq.y = w * qR.y + y * qR.w + z * qR.x - x * qR.z;
    qq.z = w * qR.z + z * qR.w + x * qR.y - y * qR.x;
    return (qq);
}
