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
#include "UiTheme.h"

//************************************************************************
//
// * Constructor
//========================================================================
TrainWindow::TrainWindow(const int x, const int y)
    : Fl_Double_Window(x, y, 800, 600, "Train and Roller Coaster")
//========================================================================
{
    // dark neon look for the control panel (see UiTheme.h); has to come
    // before the widgets are made
    UiTheme::apply();
    color(UiTheme::kPanel);

    // make all of the widgets
    begin(); // add to this widget
    {
        // The panel is a column of cards. Every card has a title strip and
        // rows of widgets on a three column grid.
        const int panelX = 600;
        const int cardX = panelX + 4;
        const int cardW = 192;
        const int innerX = cardX + 6;
        const int innerW = cardW - 12;
        const int rowH = 22;
        const int gap = 4;
        const int cardGap = 5;
        const int titleH = UiTheme::kCardTitleHeight;
        const int bottomPad = 6;
        // three columns, and two halves for the rows with two widgets
        const int colW = (innerW - 2 * gap) / 3;
        const int col2 = innerX + colW + gap;
        const int col3 = innerX + 2 * (colW + gap);
        const int col3W = innerX + innerW - col3;
        const int halfW = (innerW - gap) / 2;
        const int half2 = innerX + halfW + gap;
        const int half2W = innerX + innerW - half2;
        // sliders leave room on the left for their label
        const int sliderX = innerX + 38;
        const int sliderW = innerW - 38;

        int pty = 5; // where the next widget goes

        trainView = new TrainView(5, 5, 590, 590);
        trainView->tw = this;
        trainView->m_pTrack = &m_Track;
        this->resizable(trainView);

        // to make resizing work better, put all the widgets in a group
        widgets = new Fl_Group(panelX, 0, 200, 600);
        UiTheme::stylePanel(widgets);
        widgets->begin();

        Fl_Box *title = new Fl_Box(cardX, pty, 120, 22, "ROLLER COASTER");
        UiTheme::styleCaption(title, UiTheme::kText, 12);
        title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        Fl_Box *subtitle = new Fl_Box(cardX + 120, pty, cardW - 120, 22, "DEEP SEA");
        UiTheme::styleCaption(subtitle, UiTheme::kAccent2, UiTheme::kTitleSize);
        subtitle->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

        pty += 22 + gap;

        // ---- RIDE: run, step and speed
        Fl_Group *rideCard =
            UiTheme::beginCard(cardX, pty, cardW, titleH + 2 * rowH + gap + bottomPad, "RIDE");
        pty += titleH;

        runButton = new Fl_Button(innerX, pty, colW, rowH, "Run");
        togglify(runButton);

        const int stepW = (colW - gap) / 2;
        Fl_Button *rb = new Fl_Button(col2, pty, stepW, rowH, "@<<");
        UiTheme::styleButton(rb);
        rb->callback((Fl_Callback *)backCB, this);
        Fl_Button *fb = new Fl_Button(col2 + stepW + gap, pty, colW - stepW - gap, rowH, "@>>");
        UiTheme::styleButton(fb);
        fb->callback((Fl_Callback *)forwCB, this);

        arcLength = new Fl_Button(col3, pty, col3W, rowH, "ArcLength");
        togglify(arcLength, 1);

        pty += rowH + gap;
        speed = new Fl_Value_Slider(sliderX, pty, sliderW, rowH, "Speed");
        UiTheme::styleSlider(speed);
        speed->range(0, 10);
        speed->value(2);

        pty += rowH + bottomPad;
        rideCard->end();
        pty += cardGap;

        // ---- CAMERA: the card is also the radio button group
        Fl_Group *camGroup =
            UiTheme::beginCard(cardX, pty, cardW, titleH + rowH + bottomPad, "CAMERA");
        pty += titleH;
        worldCam = new Fl_Button(innerX, pty, colW, rowH, "World");
        worldCam->type(FL_RADIO_BUTTON); // radio button
        worldCam->value(1);              // turned on
        UiTheme::styleToggle(worldCam);
        worldCam->callback((Fl_Callback *)damageCB, this);
        worldCam->tooltip("Rotate: two-finger scroll, Shift + left drag, or right drag\n"
                          "Pan: Alt + two-finger scroll (or Alt + right drag)\n"
                          "Reset: Shift + double left click (or double right click)\n"
                          "Zoom: spread fingers to zoom in; pinch to zoom out (no Ctrl needed)\n"
                          "Also: Ctrl + scroll up to zoom in; Ctrl + scroll down to zoom out");
        trainCam = new Fl_Button(col2, pty, colW, rowH, "Train");
        trainCam->type(FL_RADIO_BUTTON);
        trainCam->value(0);
        UiTheme::styleToggle(trainCam);
        trainCam->callback((Fl_Callback *)damageCB, this);
        topCam = new Fl_Button(col3, pty, col3W, rowH, "Top");
        topCam->type(FL_RADIO_BUTTON);
        topCam->value(0);
        UiTheme::styleToggle(topCam);
        topCam->callback((Fl_Callback *)damageCB, this);
        pty += rowH + bottomPad;
        camGroup->end();
        pty += cardGap;

        // ---- TRACK: spline type and control point editing
        const int browserLabelH = 13;
        const int browserH = 64;
        Fl_Group *trackCard = UiTheme::beginCard(
            cardX, pty, cardW, titleH + browserLabelH + browserH + 3 * (gap + rowH) + bottomPad,
            "TRACK");
        pty += titleH + browserLabelH;

        // browser to select spline types
        splineBrowser = new Fl_Browser(innerX, pty, innerW, browserH, "Spline Type");
        splineBrowser->type(2); // select
        UiTheme::styleBrowser(splineBrowser);
        splineBrowser->callback((Fl_Callback *)damageCB, this);
        splineBrowser->add("Linear");
        splineBrowser->add("Cardinal Cubic");
        splineBrowser->add("Cubic B-Spline");
        splineBrowser->select(3);

        pty += browserH + gap;

        // add and delete points
        Fl_Button *ap = new Fl_Button(innerX, pty, halfW, rowH, "Add Point");
        UiTheme::styleButton(ap);
        ap->callback((Fl_Callback *)addPointCB, this);
        Fl_Button *dp = new Fl_Button(half2, pty, half2W, rowH, "Delete Point");
        UiTheme::styleButton(dp);
        dp->callback((Fl_Callback *)deletePointCB, this);

        pty += rowH + gap;
        // load, save and reset the points
        Fl_Button *loadb = new Fl_Button(innerX, pty, colW, rowH, "Load");
        UiTheme::styleButton(loadb);
        loadb->callback((Fl_Callback *)loadCB, this);
        Fl_Button *saveb = new Fl_Button(col2, pty, colW, rowH, "Save");
        UiTheme::styleButton(saveb);
        saveb->callback((Fl_Callback *)saveCB, this);
        resetButton = new Fl_Button(col3, pty, col3W, rowH, "Reset");
        UiTheme::styleButton(resetButton);
        resetButton->callback((Fl_Callback *)resetCB, this);

        pty += rowH + gap;
        // roll the points
        const int rollW = (innerW - 3 * gap) / 4;
        Fl_Button *rx = new Fl_Button(innerX, pty, rollW, rowH, "R+X");
        UiTheme::styleButton(rx);
        rx->callback((Fl_Callback *)rpxCB, this);
        Fl_Button *rxp = new Fl_Button(innerX + rollW + gap, pty, rollW, rowH, "R-X");
        UiTheme::styleButton(rxp);
        rxp->callback((Fl_Callback *)rmxCB, this);
        Fl_Button *rz = new Fl_Button(innerX + 2 * (rollW + gap), pty, rollW, rowH, "R+Z");
        UiTheme::styleButton(rz);
        rz->callback((Fl_Callback *)rpzCB, this);
        const int roll4 = innerX + 3 * (rollW + gap);
        Fl_Button *rzp = new Fl_Button(roll4, pty, innerX + innerW - roll4, rowH, "R-Z");
        UiTheme::styleButton(rzp);
        rzp->callback((Fl_Callback *)rmzCB, this);

        pty += rowH + bottomPad;
        trackCard->end();
        pty += cardGap;

        // ---- LIGHTING: shader lighting and screen effects
        Fl_Group *lightCard = UiTheme::beginCard(
            cardX, pty, cardW, titleH + 3 * rowH + 2 * gap + bottomPad, "LIGHTING");
        pty += titleH;

        lightingButton = new Fl_Button(innerX, pty, colW, rowH, "Light+");
        togglify(lightingButton, 1);
        lightingButton->tooltip("Per-pixel lighting with shadow mapping, sky and fog.\n"
                                "Turn off to get the original fixed-function lighting.");
        shadowButton = new Fl_Button(col2, pty, colW, rowH, "Shadows");
        togglify(shadowButton, 1);
        headlightButton = new Fl_Button(col3, pty, col3W, rowH, "Headlight");
        togglify(headlightButton, 1);
        headlightButton->tooltip("Spotlight on the front of the train (needs Light+).\n"
                                 "Slide Time towards night to see it best.");

        pty += rowH + gap;
        bloomButton = new Fl_Button(innerX, pty, halfW, rowH, "Bloom");
        togglify(bloomButton, 1);
        bloomButton->tooltip("Bloom glow and god rays from the sun (needs Light+)");
        ssaoButton = new Fl_Button(half2, pty, half2W, rowH, "SSAO");
        togglify(ssaoButton, 1);
        ssaoButton->tooltip("Screen space ambient occlusion (needs Light+)");

        pty += rowH + gap;
        timeOfDay = new Fl_Value_Slider(sliderX, pty, sliderW, rowH, "Time");
        UiTheme::styleSlider(timeOfDay);
        timeOfDay->range(0, 24);
        timeOfDay->step(0.25);
        timeOfDay->value(15);
        timeOfDay->callback((Fl_Callback *)damageCB, this);
        timeOfDay->tooltip("Hour of the day: moves the sun and its shadows (needs Light+)");

        pty += rowH + bottomPad;
        lightCard->end();
        pty += cardGap;

        // ---- WORLD: water, weather and the underwater demo ride
        const int oceanH = 30;
        Fl_Group *worldCard = UiTheme::beginCard(cardX, pty, cardW,
                                                 titleH + rowH + gap + oceanH + bottomPad, "WORLD");
        pty += titleH;

        waterButton = new Fl_Button(innerX, pty, halfW, rowH, "Water");
        togglify(waterButton, 1);
        waterButton->tooltip("Ocean with reflections, refraction and foam (needs Light+)");
        rainButton = new Fl_Button(half2, pty, half2W, rowH, "Rain");
        togglify(rainButton);
        rainButton->tooltip("Rain: wet reflective ground, rain streaks and ripples on the water "
                            "(needs Light+)");

        pty += rowH + gap;
        Fl_Button *ocean = new Fl_Button(innerX, pty, innerW, oceanH, "Ocean Ride");
        UiTheme::styleAccentButton(ocean);
        ocean->callback((Fl_Callback *)oceanRideCB, this);
        ocean->tooltip("Load a ride that dives under the sea: reef, kelp, ruins, a wreck\n"
                       "and a crystal tunnel. Watch it from the Train view.");

        pty += oceanH + bottomPad;
        worldCard->end();
        pty += cardGap;

        // TODO: add widgets for all of your fancier features here
#ifdef EXAMPLE_SOLUTION
        makeExampleWidgets(this, pty);
#endif

        // we need to make a little phantom widget to have things resize correctly:
        // the cards keep their size and only this strip below them stretches
        Fl_Box *resizebox = new Fl_Box(panelX, pty, 200, 600 - pty);
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
    b->type(FL_TOGGLE_BUTTON); // toggle
    b->value(val);             // turned off
    UiTheme::styleToggle(b);   // neon cyan when on, dark when off
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
