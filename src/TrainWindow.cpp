/************************************************************************
     File:        TrainWindow.H

     Author:     
                  Michael Gleicher, gleicher@cs.wisc.edu

     Modifier
                  Yu-Chi Lai, yu-chi@cs.wisc.edu
     
     Comment:     
						this class defines the window in which the project 
						runs - its the outer windows that contain all of 
						the widgets, including the "TrainView" which has the 
						actual OpenGL window in which the train is drawn

						You might want to modify this class to add new widgets
						for controlling	your train

						This takes care of lots of things - including installing 
						itself into the FlTk "idle" loop so that we get periodic 
						updates (if we're running the train).


     Platform:    Visio Studio.Net 2003/2005

*************************************************************************/

#include <FL/Fl.H>
#include <FL/Fl_Box.H>

// for using the real time clock
#include <time.h>
#include <math.h>

#include "TrainWindow.H"
#include "TrainView.H"
#include "CallBacks.H"

//************************************************************************
//
// * Constructor
//========================================================================
TrainWindow::TrainWindow(const int x, const int y)
    : Fl_Double_Window(x, y, 800, 600, "Train and Roller Coaster")
//========================================================================
{
    // make all of the widgets
    begin(); // add to this widget
    {
        int pty = 5; // where the last widgets were drawn

        trainView = new TrainView(5, 5, 590, 590);
        trainView->tw = this;
        trainView->m_pTrack = &m_Track;
        this->resizable(trainView);

        // to make resizing work better, put all the widgets in a group
        widgets = new Fl_Group(600, 5, 190, 590);
        widgets->begin();

        runButton = new Fl_Button(605, pty, 60, 20, "Run");
        togglify(runButton);

        Fl_Button *fb = new Fl_Button(700, pty, 25, 20, "@>>");
        fb->callback((Fl_Callback *)forwCB, this);
        Fl_Button *rb = new Fl_Button(670, pty, 25, 20, "@<<");
        rb->callback((Fl_Callback *)backCB, this);

        arcLength = new Fl_Button(730, pty, 65, 20, "ArcLength");
        togglify(arcLength, 1);

        pty += 25;
        speed = new Fl_Value_Slider(655, pty, 140, 20, "speed");
        speed->range(0, 10);
        speed->value(2);
        speed->align(FL_ALIGN_LEFT);
        speed->type(FL_HORIZONTAL);

        pty += 30;

        // camera buttons - in a radio button group
        Fl_Group *camGroup = new Fl_Group(600, pty, 195, 20);
        camGroup->begin();
        worldCam = new Fl_Button(605, pty, 60, 20, "World");
        worldCam->type(FL_RADIO_BUTTON);        // radio button
        worldCam->value(1);                     // turned on
        worldCam->selection_color((Fl_Color)3); // yellow when pressed
        worldCam->callback((Fl_Callback *)damageCB, this);
        worldCam->tooltip("Rotate: two-finger scroll, Shift + left drag, or right drag\n"
                          "Pan: Alt + two-finger scroll (or Alt + right drag)\n"
                          "Reset: Shift + double left click (or double right click)\n"
                          "Zoom: spread fingers to zoom in; pinch to zoom out (no Ctrl needed)\n"
                          "Also: Ctrl + scroll up to zoom in; Ctrl + scroll down to zoom out");
        trainCam = new Fl_Button(670, pty, 60, 20, "Train");
        trainCam->type(FL_RADIO_BUTTON);
        trainCam->value(0);
        trainCam->selection_color((Fl_Color)3);
        trainCam->callback((Fl_Callback *)damageCB, this);
        topCam = new Fl_Button(735, pty, 60, 20, "Top");
        topCam->type(FL_RADIO_BUTTON);
        topCam->value(0);
        topCam->selection_color((Fl_Color)3);
        topCam->callback((Fl_Callback *)damageCB, this);
        camGroup->end();

        pty += 30;

        // browser to select spline types
        splineBrowser = new Fl_Browser(605, pty, 120, 75, "Spline Type");
        splineBrowser->type(2); // select
        splineBrowser->callback((Fl_Callback *)damageCB, this);
        splineBrowser->add("Linear");
        splineBrowser->add("Cardinal Cubic");
        splineBrowser->add("Cubic B-Spline");
        splineBrowser->select(3);

        pty += 110;

        // add and delete points
        Fl_Button *ap = new Fl_Button(605, pty, 80, 20, "Add Point");
        ap->callback((Fl_Callback *)addPointCB, this);
        Fl_Button *dp = new Fl_Button(690, pty, 80, 20, "Delete Point");
        dp->callback((Fl_Callback *)deletePointCB, this);

        pty += 25;
        // reset the points
        resetButton = new Fl_Button(735, pty, 60, 20, "Reset");
        resetButton->callback((Fl_Callback *)resetCB, this);
        Fl_Button *loadb = new Fl_Button(605, pty, 60, 20, "Load");
        loadb->callback((Fl_Callback *)loadCB, this);
        Fl_Button *saveb = new Fl_Button(670, pty, 60, 20, "Save");
        saveb->callback((Fl_Callback *)saveCB, this);

        pty += 25;
        // roll the points
        Fl_Button *rx = new Fl_Button(605, pty, 30, 20, "R+X");
        rx->callback((Fl_Callback *)rpxCB, this);
        Fl_Button *rxp = new Fl_Button(635, pty, 30, 20, "R-X");
        rxp->callback((Fl_Callback *)rmxCB, this);
        Fl_Button *rz = new Fl_Button(670, pty, 30, 20, "R+Z");
        rz->callback((Fl_Callback *)rpzCB, this);
        Fl_Button *rzp = new Fl_Button(700, pty, 30, 20, "R-Z");
        rzp->callback((Fl_Callback *)rmzCB, this);

        pty += 30;

        // Shader lighting and effects
        lightingButton = new Fl_Button(605, pty, 60, 20, "Light+");
        togglify(lightingButton, 1);
        lightingButton->tooltip("Per-pixel lighting with shadow mapping, sky and fog.\n"
                                "Turn off to get the original fixed-function lighting.");
        shadowButton = new Fl_Button(670, pty, 60, 20, "Shadows");
        togglify(shadowButton, 1);
        headlightButton = new Fl_Button(735, pty, 60, 20, "Headlight");
        togglify(headlightButton, 1);
        headlightButton->labelsize(12);
        headlightButton->tooltip("Spotlight on the front of the train (needs Light+).\n"
                                 "Slide Time towards night to see it best.");

        pty += 25;
        waterButton = new Fl_Button(605, pty, 60, 20, "Water");
        togglify(waterButton, 1);
        waterButton->tooltip("Ocean with reflections, refraction and foam (needs Light+)");
        bloomButton = new Fl_Button(670, pty, 60, 20, "Bloom");
        togglify(bloomButton, 1);
        bloomButton->tooltip("Bloom glow and god rays from the sun (needs Light+)");
        ssaoButton = new Fl_Button(735, pty, 60, 20, "SSAO");
        togglify(ssaoButton, 1);
        ssaoButton->tooltip("Screen space ambient occlusion (needs Light+)");

        pty += 25;
        timeOfDay = new Fl_Value_Slider(655, pty, 140, 20, "Time");
        timeOfDay->range(0, 24);
        timeOfDay->step(0.25);
        timeOfDay->value(15);
        timeOfDay->align(FL_ALIGN_LEFT);
        timeOfDay->type(FL_HORIZONTAL);
        timeOfDay->callback((Fl_Callback *)damageCB, this);
        timeOfDay->tooltip("Hour of the day: moves the sun and its shadows (needs Light+)");

        pty += 30;

        // TODO: add widgets for all of your fancier features here
#ifdef EXAMPLE_SOLUTION
        makeExampleWidgets(this, pty);
#endif

        // we need to make a little phantom widget to have things resize correctly
        Fl_Box *resizebox = new Fl_Box(600, 595, 200, 5);
        widgets->resizable(resizebox);

        widgets->end();
    }
    end(); // done adding to this widget

    // set up callback on idle
    Fl::add_idle((void (*)(void *))runButtonCB, this);

    // keep the water, clouds and leaves moving even when the train is stopped
    Fl::add_timeout(1.0 / 30.0, animateEffectsCB, this);
}

//************************************************************************
//
// * Redraw about 30 times a second while the shader effects are on
//========================================================================
void TrainWindow::animateEffectsCB(void *data)
{
    TrainWindow *tw = static_cast<TrainWindow *>(data);
    if (tw->lightingButton->value() && !tw->runButton->value())
        tw->trainView->damage(1);
    Fl::repeat_timeout(1.0 / 30.0, animateEffectsCB, data);
}

//************************************************************************
//
// * handy utility to make a button into a toggle
//========================================================================
void TrainWindow::togglify(Fl_Button *b, int val)
//========================================================================
{
    b->type(FL_TOGGLE_BUTTON);       // toggle
    b->value(val);                   // turned off
    b->selection_color((Fl_Color)3); // yellow when pressed
    b->callback((Fl_Callback *)damageCB, this);
}

//************************************************************************
//
// *
//========================================================================
void TrainWindow::damageMe()
//========================================================================
{
    if (trainView->selectedCube >= ((int)m_Track.points.size()))
        trainView->selectedCube = 0;
    trainView->damage(1);
}

static float wrapTrackParameter(float u, float period)
{
    return u - floorf(u / period) * period;
}

static float advanceByArcLength(const CTrack &track, float u, float distance,
                                TrackSplineType splineType)
{
    const float period = static_cast<float>(track.points.size());
    const float direction = distance < 0.0f ? -1.0f : 1.0f;
    float remaining = fabsf(distance);
    const float parameterStep = 0.005f;
    u = wrapTrackParameter(u, period);

    for (int i = 0; i < 20000 && remaining > 0.0001f; ++i)
    {
        const float nextU = wrapTrackParameter(u + direction * parameterStep, period);
        const Pnt3f current = track.evaluatePosition(u, splineType);
        const Pnt3f next = track.evaluatePosition(nextU, splineType);
        const float dx = next.x - current.x;
        const float dy = next.y - current.y;
        const float dz = next.z - current.z;
        const float segmentLength = sqrtf(dx * dx + dy * dy + dz * dz);

        if (segmentLength <= 0.000001f)
        {
            u = nextU;
            continue;
        }

        if (segmentLength >= remaining)
        {
            u = wrapTrackParameter(u + direction * parameterStep * (remaining / segmentLength),
                                   period);
            remaining = 0.0f;
        }
        else
        {
            remaining -= segmentLength;
            u = nextU;
        }
    }

    return u;
}

//************************************************************************
//
// * This will get called (approximately) 30 times per second
//   if the run button is pressed
//========================================================================
void TrainWindow::advanceTrain(float dir)
//========================================================================
{
    const int pointCount = static_cast<int>(m_Track.points.size());
    if (pointCount < 4)
        return;

    const TrackSplineType splineType = static_cast<TrackSplineType>(splineBrowser->value());
    const float step = dir * static_cast<float>(speed->value());
    if (arcLength->value())
        m_Track.trainU = advanceByArcLength(m_Track, m_Track.trainU, step, splineType);
    else
        m_Track.trainU =
            wrapTrackParameter(m_Track.trainU + step * 0.01f, static_cast<float>(pointCount));
}
