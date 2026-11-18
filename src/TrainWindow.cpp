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

// The panel's cards are laid out for this height; the window starts at it
// so the cards are never squeezed (the strip below them takes any change).
static const int kPanelHeight = 690;

//************************************************************************
//
// * Constructor
//========================================================================
TrainWindow::TrainWindow(const int x, const int y)
    : Fl_Double_Window(x, y, 800, kPanelHeight, "Train and Roller Coaster")
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

        trainView = new TrainView(5, 5, 590, kPanelHeight - 10);
        trainView->tw = this;
        trainView->m_pTrack = &m_Track;
        this->resizable(trainView);

        // to make resizing work better, put all the widgets in a group
        widgets = new Fl_Group(panelX, 0, 200, kPanelHeight);
        UiTheme::stylePanel(widgets);
        widgets->begin();

        Fl_Box *title = new Fl_Box(cardX + 2, pty, 120, 20, "Roller Coaster");
        UiTheme::styleCaption(title, UiTheme::kText, 12);
        title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        Fl_Box *subtitle = new Fl_Box(cardX + 120, pty, cardW - 122, 20, "Properties");
        UiTheme::styleCaption(subtitle, UiTheme::kTextMuted, UiTheme::kTitleSize);
        subtitle->labelfont(FL_HELVETICA);
        subtitle->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

        pty += 20 + 2;

        // ---- RIDE: run, step and speed
        Fl_Group *rideCard =
            UiTheme::beginCard(cardX, pty, cardW, titleH + 2 * rowH + gap + bottomPad, "Ride");
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

        // ---- CAMERA: the card is also the radio button group. Two rows of
        // cameras, then the Fly camera's speed and field of view.
        Fl_Group *camGroup = UiTheme::beginCard(cardX, pty, cardW,
                                                titleH + 4 * rowH + 3 * gap + bottomPad, "Camera");
        pty += titleH;
        worldCam = new Fl_Button(innerX, pty, colW, rowH, "World");
        worldCam->type(FL_RADIO_BUTTON); // radio button
        worldCam->value(1);              // turned on
        UiTheme::styleToggle(worldCam);
        worldCam->callback((Fl_Callback *)cameraCB, this);
        worldCam->tooltip("Rotate: two-finger scroll, Shift + left drag, or right drag\n"
                          "Pan: Alt + two-finger scroll (or Alt + right drag)\n"
                          "Reset: Shift + double left click (or double right click)\n"
                          "Zoom: spread fingers to zoom in; pinch to zoom out (no Ctrl needed)\n"
                          "Also: Ctrl + scroll up to zoom in; Ctrl + scroll down to zoom out");
        trainCam = new Fl_Button(col2, pty, colW, rowH, "Train");
        trainCam->type(FL_RADIO_BUTTON);
        trainCam->value(0);
        UiTheme::styleToggle(trainCam);
        trainCam->callback((Fl_Callback *)cameraCB, this);
        trainCam->tooltip("Ride the train and turn your head\n"
                          "Look around: drag the view (left or right button)\n"
                          "Touchpad: two fingers look around\n"
                          "Look ahead again: double click, R or Home");
        chaseCam = new Fl_Button(col3, pty, col3W, rowH, "Chase");
        chaseCam->type(FL_RADIO_BUTTON);
        chaseCam->value(0);
        UiTheme::styleToggle(chaseCam);
        chaseCam->callback((Fl_Callback *)cameraCB, this);
        chaseCam->tooltip("Follows behind the train: see the riders against the scenery");

        pty += rowH + gap;
        topCam = new Fl_Button(innerX, pty, halfW, rowH, "Top");
        topCam->type(FL_RADIO_BUTTON);
        topCam->value(0);
        UiTheme::styleToggle(topCam);
        topCam->callback((Fl_Callback *)cameraCB, this);
        flyCam = new Fl_Button(half2, pty, half2W, rowH, "Fly");
        flyCam->type(FL_RADIO_BUTTON);
        flyCam->value(0);
        UiTheme::styleToggle(flyCam);
        flyCam->callback((Fl_Callback *)cameraCB, this);
        flyCam->tooltip("Observer camera (spectator style), starts from the current view\n"
                        "Click the view: the mouse looks around; Esc frees the mouse\n"
                        "Move: W/A/S/D (level), Space up, Shift down, hold Ctrl to go faster\n"
                        "Speed: mouse wheel, +/- keys or the Fly slider below\n"
                        "Also: right drag or arrow keys look; E/Q up/down\n"
                        "Touchpad: two fingers look, spread / pinch fly, Alt + two fingers slide\n"
                        "Lost? F: face the train; Home: back to the World view\n"
                        "Edit on: G / R move / tilt the selected point, Alt + D adds a point,\n"
                        "Delete removes (Shift + D and X fly or do nothing here)");

        // observer camera speed (world units per second) and field of view
        pty += rowH + gap;
        flySpeed = new Fl_Value_Slider(sliderX, pty, sliderW, rowH, "Fly");
        UiTheme::styleSlider(flySpeed);
        flySpeed->range(FlyCam::minSpeed, FlyCam::maxSpeed);
        flySpeed->step(1);
        flySpeed->value(trainView->flyCam.speed());
        flySpeed->tooltip("How fast the Fly camera moves, in world units per second");
        flySpeed->callback((Fl_Callback *)flySpeedCB, this);

        pty += rowH + gap;
        fovSlider = new Fl_Value_Slider(sliderX, pty, sliderW, rowH, "FOV");
        UiTheme::styleSlider(fovSlider);
        fovSlider->range(FlyCam::minFieldOfView, FlyCam::maxFieldOfView);
        fovSlider->step(1);
        fovSlider->value(trainView->m_fieldOfView);
        fovSlider->tooltip("Field of view of the World, Train, Chase and Fly cameras, in degrees\n"
                           "(Top is a flat orthographic view)");
        fovSlider->callback((Fl_Callback *)fovCB, this);

        pty += rowH + bottomPad;
        camGroup->end();
        pty += cardGap;

        // ---- TRACK: spline type and control point editing
        const int browserLabelH = 13;
        const int browserH = 62;
        Fl_Group *trackCard = UiTheme::beginCard(
            cardX, pty, cardW, titleH + browserLabelH + browserH + 4 * (gap + rowH) + bottomPad,
            "Track");
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

        // show the control points for editing; add and delete points
        editButton = new Fl_Button(innerX, pty, colW, rowH, "Edit");
        togglify(editButton);
        editButton->tooltip("Show the control points and their move handles.\n"
                            "Off for the Grand Tour: the scenery is built around that ride.");
        Fl_Button *ap = new Fl_Button(col2, pty, colW, rowH, "Add Pt");
        UiTheme::styleButton(ap);
        ap->tooltip("Then click the track where the new point goes (hold to drag it).\n"
                    "Esc or a right click cancels. Shortcut: Shift + D (Alt + D in Fly).");
        ap->callback((Fl_Callback *)addPointCB, this);
        Fl_Button *dp = new Fl_Button(col3, pty, col3W, rowH, "Del Pt");
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

        pty += rowH + gap;
        // step back and forward through the changes to the points
        undoButton = new Fl_Button(innerX, pty, halfW, rowH, "Undo");
        UiTheme::styleButton(undoButton);
        undoButton->tooltip(
            "Take back the last change to the control points (Ctrl + Z):\n"
            "adding, deleting, moving or tilting a point, Load, Reset, Grand Tour.");
        undoButton->callback((Fl_Callback *)undoCB, this);
        redoButton = new Fl_Button(half2, pty, half2W, rowH, "Redo");
        UiTheme::styleButton(redoButton);
        redoButton->tooltip("Put back a change taken back (Ctrl + Y or Ctrl + Shift + Z).");
        redoButton->callback((Fl_Callback *)redoCB, this);
        updateHistoryButtons();

        pty += rowH + bottomPad;
        trackCard->end();
        pty += cardGap;

        // ---- Transform: where the selected control point is. The view also
        // moves it with the handles drawn on it and with G / R (TrainView.cpp)
        Fl_Group *transformCard =
            UiTheme::beginCard(cardX, pty, cardW, titleH + rowH + bottomPad, "Transform");
        pty += titleH;
        {
            const int labelW = 12;
            const int fieldW = (innerW - 2 * gap) / 3 - labelW;
            Fl_Value_Input **fields[3] = {&pointX, &pointY, &pointZ};
            const char *names[3] = {"X", "Y", "Z"};
            const Fl_Color colours[3] = {UiTheme::kAxisX, UiTheme::kAxisY, UiTheme::kAxisZ};
            for (int axis = 0; axis < 3; ++axis)
            {
                const int fx = innerX + axis * (fieldW + labelW + gap) + labelW;
                Fl_Value_Input *field = new Fl_Value_Input(fx, pty, fieldW, rowH, names[axis]);
                UiTheme::styleNumberField(field, colours[axis]);
                field->range(-1000.0, 1000.0);
                field->step(0.1);
                field->callback(transformCB, this);
                field->tooltip("Position of the selected control point.\n"
                               "In the view: drag an arrow or a square, or press G to move,\n"
                               "then X / Y / Z to keep to one axis (Shift: the other two),\n"
                               "R to tilt, Ctrl to snap, Esc or right click to cancel.");
                field->deactivate();
                *fields[axis] = field;
            }
        }
        pty += rowH + bottomPad;
        transformCard->end();
        pty += cardGap;

        // ---- LIGHTING: shader lighting and screen effects
        Fl_Group *lightCard = UiTheme::beginCard(
            cardX, pty, cardW, titleH + 3 * rowH + 2 * gap + bottomPad, "Lighting");
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

        // ---- WORLD: water, weather, the scenery and the demo ride
        const int oceanH = 26;
        Fl_Group *worldCard = UiTheme::beginCard(cardX, pty, cardW,
                                                 titleH + rowH + gap + oceanH + bottomPad, "World");
        pty += titleH;

        waterButton = new Fl_Button(innerX, pty, colW, rowH, "Water");
        togglify(waterButton, 1);
        waterButton->tooltip("Ocean with reflections, refraction and foam (needs Light+)");
        rainButton = new Fl_Button(col2, pty, colW, rowH, "Rain");
        togglify(rainButton);
        rainButton->tooltip("Rain: wet reflective ground, rain streaks and ripples on the water "
                            "(needs Light+)");
        sceneryButton = new Fl_Button(col3, pty, col3W, rowH, "Scenery");
        togglify(sceneryButton, 1);
        sceneryButton->tooltip("The island's mountains, canyon, volcano and village.\n"
                               "Off: level grass, for editing a track of your own.");

        pty += rowH + gap;
        Fl_Button *ocean = new Fl_Button(innerX, pty, innerW, oceanH, "Grand Tour");
        UiTheme::styleAccentButton(ocean);
        ocean->callback((Fl_Callback *)grandTourCB, this);
        ocean->tooltip("Ride the whole island: up the snow mountain, through the canyon,\n"
                       "over the volcano's crater, through the mine and under the sea.");

        pty += oceanH + bottomPad;
        worldCard->end();
        pty += cardGap;

        // TODO: add widgets for all of your fancier features here
#ifdef EXAMPLE_SOLUTION
        makeExampleWidgets(this, pty);
#endif

        // we need to make a little phantom widget to have things resize correctly:
        // the cards keep their size and only this strip below them stretches
        Fl_Box *resizebox = new Fl_Box(panelX, pty, 200, kPanelHeight - pty);
        widgets->resizable(resizebox);

        widgets->end();
    }
    end(); // done adding to this widget

    // set up callback on idle
    Fl::add_idle((void (*)(void *))runButtonCB, this);

    // keep the water, clouds and plants moving even when the train is stopped
    Fl::add_timeout(1.0 / 30.0, animateEffectsCB, this);

    // The program opens on the demo: the Grand Tour seen from the train, in
    // a window wide enough to enjoy the view. Run starts the ride.
    resize(x, y, 1180, kPanelHeight);
    grandTourCB(nullptr, this);
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
// * One of the Transform fields was edited: move the selected control point
//========================================================================
void TrainWindow::transformCB(Fl_Widget *, void *data)
{
    TrainWindow *tw = static_cast<TrainWindow *>(data);
    const int selected = tw->trainView->selectedCube;
    if (selected < 0 || selected >= static_cast<int>(tw->m_Track.points.size()))
        return;
    const Pnt3f typed(static_cast<float>(tw->pointX->value()),
                      static_cast<float>(tw->pointY->value()),
                      static_cast<float>(tw->pointZ->value()));
    Pnt3f &pos = tw->m_Track.points[static_cast<size_t>(selected)].pos;
    if (typed.x == pos.x && typed.y == pos.y && typed.z == pos.z)
        return;
    tw->recordHistory();
    pos = typed;
    tw->trainView->damage(1);
}

//************************************************************************
//
// * Undo and redo (TrackHistory.h)
//========================================================================
void TrainWindow::recordHistory()
{
    m_history.record(m_Track, trainView->selectedCube);
    updateHistoryButtons();
}

void TrainWindow::pushHistory(const TrackHistory::Snapshot &before)
{
    m_history.push(before);
    updateHistoryButtons();
}

void TrainWindow::undoEdit()
{
    trainView->finishEdits(); // a move in progress becomes a step of its own first
    if (m_history.undo(m_Track, trainView->selectedCube))
        damageMe();
    updateHistoryButtons();
}

void TrainWindow::redoEdit()
{
    trainView->finishEdits();
    if (m_history.redo(m_Track, trainView->selectedCube))
        damageMe();
    updateHistoryButtons();
}

void TrainWindow::updateHistoryButtons()
{
    if (!undoButton || !redoButton)
        return;
    if (m_history.canUndo())
        undoButton->activate();
    else
        undoButton->deactivate();
    if (m_history.canRedo())
        redoButton->activate();
    else
        redoButton->deactivate();
}

//************************************************************************
//
// * Shows the selected control point in the Transform fields, greyed out
//   when nothing is selected. A field being typed in is left alone.
//========================================================================
void TrainWindow::syncTransform()
{
    const int selected = trainView->selectedCube;
    const bool valid = selected >= 0 && selected < static_cast<int>(m_Track.points.size());
    Fl_Value_Input *fields[3] = {pointX, pointY, pointZ};
    float values[3] = {0.0f, 0.0f, 0.0f};
    if (valid)
    {
        const Pnt3f &pos = m_Track.points[static_cast<size_t>(selected)].pos;
        values[0] = pos.x;
        values[1] = pos.y;
        values[2] = pos.z;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        Fl_Value_Input *field = fields[axis];
        if (valid != (field->active() != 0))
        {
            if (valid)
                field->activate();
            else
                field->deactivate();
        }
        if (Fl::focus() == &field->input)
            continue;
        // one decimal is what the field shows
        const double shown = floor(static_cast<double>(values[axis]) * 10.0 + 0.5) / 10.0;
        if (field->value() != shown)
            field->value(shown);
    }
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
