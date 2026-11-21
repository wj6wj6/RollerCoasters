/************************************************************************
     File:        GameMenu.cpp

     Comment:     See GameMenu.h. Layouts follow a block game's screens:
                  GUI units (Gui.h), a title near the top, settings in two
                  columns of 150 wide buttons on rows 24 apart starting a
                  sixth of the way down, and "Done" below them.

*************************************************************************/

#include "GameMenu.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "CallBacks.H"
#include "Ui/PixelFont.h"
#include "TrainView.H"
#include "TrainWindow.H"

namespace
{
using ui::Rect;

constexpr int kRowStep = 24;
constexpr int kButtonH = 20;
constexpr double kSlotNameSeconds = 2.0;

// The settings screens' grid.
int top(const ui::Gui &gui)
{
    return gui.height() / 6;
}
Rect left(const ui::Gui &gui, int row)
{
    return {gui.width() / 2 - 155, top(gui) + row * kRowStep, 150, kButtonH};
}
Rect right(const ui::Gui &gui, int row)
{
    return {gui.width() / 2 + 5, top(gui) + row * kRowStep, 150, kButtonH};
}
Rect wide(const ui::Gui &gui, int row)
{
    return {gui.width() / 2 - 155, top(gui) + row * kRowStep, 310, kButtonH};
}
// The row split into n equal buttons with 2 units between them; column i.
Rect part(const ui::Gui &gui, int row, int n, int i)
{
    const Rect r = wide(gui, row);
    const int gap = 2;
    const int each = (r.w - gap * (n - 1)) / n;
    const int x = r.x + i * (each + gap);
    const int w = i == n - 1 ? r.x + r.w - x : each;
    return {x, r.y, w, r.h};
}

struct Slot
{
    ui::UiAtlas::IconId icon;
    const char *name;
};

const char *const kCameraNames[] = {"World", "Train", "Chase", "Top", "Fly"};

std::string hoursText(float hours)
{
    const int minutes = static_cast<int>(hours * 60.0f + 0.5f);
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", (minutes / 60) % 25, minutes % 60);
    return text;
}
} // namespace

//************************************************************************
//
// * Opening, closing and going back
//========================================================================
void GameMenu::show(TrainWindow &tw, Screen screen)
{
    if (screen != Screen::None && m_screen == Screen::None)
    {
        // The mouse is for the menu now, and a held flying key stops.
        tw.trainView->setMouseCaptured(false);
        tw.trainView->flyCam.stop();
    }
    if (screen == Screen::World)
        tw.listTexturePacks(tw.texturePackPath); // packs put in the folder meanwhile show up
    m_screen = screen;
    m_guiHasMouse = false;
    tw.trainView->damage(1);
}

void GameMenu::back(TrainWindow &tw)
{
    show(tw, m_screen == Screen::Pause || m_screen == Screen::None ? Screen::None : Screen::Pause);
}

//************************************************************************
//
// * Events before the view: true when the interface took them
//========================================================================
bool GameMenu::input(TrainWindow &tw, const InputEvent &e)
{
    ui::Gui &gui = tw.trainView->m_gui;
    switch (e.type)
    {
    case InputType::KeyDown: {
        if (e.key == Key::Escape)
        {
            if (open())
            {
                back(tw);
                return true;
            }
            // a move, tilt or new point in progress: Esc cancels that first
            if (tw.trainView->editing())
                return false;
            show(tw, Screen::Pause);
            return true;
        }
        if (open())
            return true; // the menu has no keyboard controls; keep WASD from flying behind it
        const bool plain = (e.state & (Mod::Ctrl | Mod::Alt)) == 0;
        if (e.key == Key::F + 1)
        {
            hudHidden = !hudHidden;
            tw.trainView->damage(1);
            return true;
        }
        // a move, tilt or new point in progress keeps the keys until it ends
        if (tw.trainView->editing())
            return false;
        if (plain && e.key >= '1' && e.key <= '9')
        {
            useSlot(tw, e.key - '1', m_now);
            return true;
        }
        if (plain && e.key == 'p')
        {
            tw.settings.run = !tw.settings.run;
            tw.damageMe();
            return true;
        }
        if (plain && (e.key == '[' || e.key == ']'))
        {
            float &speed = tw.settings.speed;
            speed += e.key == ']' ? 0.5f : -0.5f;
            speed = speed < 0.0f ? 0.0f : (speed > 10.0f ? 10.0f : speed);
            tw.damageMe();
            return true;
        }
        return false;
    }
    case InputType::Push:
    case InputType::Release:
    case InputType::Drag:
    case InputType::Move:
    case InputType::Wheel: {
        if (open())
        {
            gui.feed(e);
            tw.trainView->damage(1);
            return true;
        }
        // Riding: only a press on the hotbar is the interface's, and then
        // its drag and release too. Anything else, also a drag that started
        // in the view and crosses the hotbar, is the view's.
        if (e.type == InputType::Push && gui.covers(e.x, e.y))
        {
            m_guiHasMouse = true;
            gui.feed(e);
            tw.trainView->damage(1);
            return true;
        }
        if (m_guiHasMouse && (e.type == InputType::Drag || e.type == InputType::Release))
        {
            gui.feed(e);
            if (e.type == InputType::Release)
                m_guiHasMouse = false;
            return true;
        }
        if (e.type == InputType::Move && !hudHidden)
        {
            gui.feed(e); // the slot under the mouse lights up; the view still gets the move
            tw.trainView->damage(1);
        }
        return false;
    }
    case InputType::Unfocus:
    case InputType::Hide:
        gui.feed(e);
        m_guiHasMouse = false;
        return false;
    default:
        return false;
    }
}

//************************************************************************
//
// * The hotbar: 1-5 cameras, 6 Edit, 7 Add point, 8 Delete point, 9 Run
//========================================================================
void GameMenu::useSlot(TrainWindow &tw, int slot, double seconds)
{
    static const std::array<const char *, kSlots> names = {
        "World Camera", "Train Camera", "Chase Camera", "Top View",  "Fly Camera",
        "Edit Points",  "Add Point",    "Delete Point", "Run / Stop"};
    if (slot < 0 || slot >= kSlots)
        return;
    switch (slot)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
        tw.setCamera(static_cast<CameraView>(slot));
        break;
    case 5:
        tw.trainView->finishEdits();
        tw.settings.edit = !tw.settings.edit;
        break;
    case 6:
        addPointCB(&tw);
        break;
    case 7:
        deletePointCB(&tw);
        break;
    case 8:
        tw.settings.run = !tw.settings.run;
        break;
    default:
        break;
    }
    m_slotName = names.at(static_cast<size_t>(slot));
    if (slot == 5)
        m_slotName += tw.settings.edit ? ": ON" : ": OFF";
    m_slotNameUntil = seconds + kSlotNameSeconds;
    tw.damageMe();
}

//************************************************************************
//
// * Every frame
//========================================================================
void GameMenu::draw(TrainWindow &tw, ui::Gui &gui, double seconds)
{
    m_now = seconds;
    if (open())
    {
        // the world stays visible, darkened, as behind a paused game's menu
        gui.fill({0, 0, gui.width(), gui.height()}, ui::kDim);
        switch (m_screen)
        {
        case Screen::Pause:
            drawPause(tw, gui);
            break;
        case Screen::Ride:
            drawRide(tw, gui);
            break;
        case Screen::Camera:
            drawCamera(tw, gui);
            break;
        case Screen::Track:
            drawTrack(tw, gui);
            break;
        case Screen::Lighting:
            drawLighting(tw, gui);
            break;
        case Screen::World:
            drawWorld(tw, gui);
            break;
        case Screen::None:
            break;
        }
        return;
    }
    if (!hudHidden)
        drawHud(tw, gui, seconds);
}

void GameMenu::drawHud(TrainWindow &tw, ui::Gui &gui, double seconds)
{
    const SceneSettings &s = tw.settings;

    // Key hints, top left, each line on a faint dark strip.
    char speed[48];
    std::snprintf(speed, sizeof(speed), "[ ]  Speed %.1f", static_cast<double>(s.speed));
    const std::vector<std::string> hints = {"Esc  Menu",
                                            s.run ? "P  Stop the train" : "P  Run the train", speed,
                                            "1-9  Hotbar", "F1  Hide this"};
    for (size_t i = 0; i < hints.size(); ++i)
    {
        const int y = 2 + static_cast<int>(i) * 10;
        gui.fill({1, y, ui::PixelFont::textWidth(hints[i]) + 3, 10}, {0, 0, 0, 90});
        gui.text(2, y + 1, hints[i], i == 1 ? ui::kYellow : ui::kWhite);
    }

    // The hotbar: nine slots 20 units apart, like a block game's.
    const int barW = 182;
    const int x0 = gui.width() / 2 - barW / 2;
    const int y0 = gui.height() - 22;
    const Slot slots[kSlots] = {{ui::UiAtlas::ICON_WORLD, "World"},
                                {ui::UiAtlas::ICON_TRAIN, "Train"},
                                {ui::UiAtlas::ICON_CHASE, "Chase"},
                                {ui::UiAtlas::ICON_TOP, "Top"},
                                {ui::UiAtlas::ICON_FLY, "Fly"},
                                {ui::UiAtlas::ICON_EDIT, "Edit"},
                                {ui::UiAtlas::ICON_ADD, "Add"},
                                {ui::UiAtlas::ICON_DELETE, "Delete"},
                                {s.run ? ui::UiAtlas::ICON_STOP : ui::UiAtlas::ICON_RUN, "Run"}};
    const int chosen = static_cast<int>(s.camera);
    for (int i = 0; i < kSlots; ++i)
    {
        const Rect r = {x0 + i * 20, y0, 22, 22};
        if (gui.area(r))
            useSlot(tw, i, seconds);
        gui.skin(r, ui::UiAtlas::SLOT);
        // a switch that is on (Edit, Run) glows green under its icon
        const bool lit = (i == 5 && s.edit) || (i == 8 && s.run);
        if (lit)
            gui.fill({r.x + 2, r.y + 2, 18, 18}, {90, 200, 90, 110});
        if (gui.isHovered(r))
            gui.fill({r.x + 2, r.y + 2, 18, 18}, {255, 255, 255, 60});
        gui.icon(r.x + 3, r.y + 3, slots[i].icon);
        // the slot's key, small in the corner, as an item's count
        gui.text(r.x + 19, r.y + 12, std::string(1, static_cast<char>('1' + i)), ui::kWhite,
                 ui::Align::Right);
    }
    gui.skin({x0 + chosen * 20 - 1, y0 - 1, 24, 24}, ui::UiAtlas::SLOT_SELECTED);

    // Above the hotbar: the slot just used, then what the view is doing.
    int lineY = y0 - 12;
    if (seconds < m_slotNameUntil && !m_slotName.empty())
    {
        gui.text(gui.width() / 2, lineY, m_slotName, ui::kWhite, ui::Align::Center);
        lineY -= 12;
        tw.trainView->damage(1); // draw again so it goes away on time
    }
    const std::string &status = tw.trainView->m_statusText;
    if (!status.empty())
        gui.text(gui.width() / 2, lineY, status, ui::kWhite, ui::Align::Center);
}

//************************************************************************
//
// * The screens
//========================================================================
void GameMenu::title(ui::Gui &gui, const std::string &text)
{
    gui.text(gui.width() / 2, top(gui) - 22, text, ui::kWhite, ui::Align::Center);
}

bool GameMenu::done(TrainWindow &tw, ui::Gui &gui, int row)
{
    const Rect r = {gui.width() / 2 - 100, top(gui) + row * kRowStep + 8, 200, kButtonH};
    if (!gui.button(r, "Done"))
        return false;
    back(tw);
    return true;
}

void GameMenu::drawPause(TrainWindow &tw, ui::Gui &gui)
{
    const int cx = gui.width() / 2;
    const int y = gui.height() / 4 + 8;
    gui.text(cx, y - 24, "Game Menu", ui::kWhite, ui::Align::Center);
    if (gui.button({cx - 102, y, 204, kButtonH}, "Back to Ride"))
        show(tw, Screen::None);
    if (gui.button({cx - 102, y + 24, 98, kButtonH}, "Ride..."))
        show(tw, Screen::Ride);
    if (gui.button({cx + 4, y + 24, 98, kButtonH}, "Camera..."))
        show(tw, Screen::Camera);
    if (gui.button({cx - 102, y + 48, 98, kButtonH}, "Track..."))
        show(tw, Screen::Track);
    if (gui.button({cx + 4, y + 48, 98, kButtonH}, "Lighting..."))
        show(tw, Screen::Lighting);
    if (gui.button({cx - 102, y + 72, 98, kButtonH}, "World..."))
        show(tw, Screen::World);
    if (gui.button({cx + 4, y + 72, 98, kButtonH}, "Grand Tour"))
    {
        grandTourCB(&tw);
        show(tw, Screen::None);
    }
    if (gui.button({cx - 102, y + 108, 204, kButtonH}, "Quit"))
        tw.close();
}

void GameMenu::drawRide(TrainWindow &tw, ui::Gui &gui)
{
    SceneSettings &s = tw.settings;
    title(gui, "Ride");
    if (gui.toggle(left(gui, 0), "Run", s.run))
        tw.damageMe();
    if (gui.toggle(right(gui, 0), "Arc Length", s.arcLength))
        tw.damageMe();
    if (gui.slider(wide(gui, 1), "Speed", s.speed, 0.0f, 10.0f, 1))
        tw.damageMe();
    if (gui.button(left(gui, 2), "<< Step Back"))
        backCB(&tw);
    if (gui.button(right(gui, 2), "Step Forward >>"))
        forwCB(&tw);
    gui.text(gui.width() / 2, top(gui) + 3 * kRowStep + 6,
             "While riding: P runs and stops, [ and ] change the speed", ui::kGrey,
             ui::Align::Center);
    done(tw, gui, 4);
}

void GameMenu::drawCamera(TrainWindow &tw, ui::Gui &gui)
{
    TrainView &view = *tw.trainView;
    title(gui, "Camera");
    const std::vector<std::string> cameras(std::begin(kCameraNames), std::end(kCameraNames));
    int camera = static_cast<int>(tw.settings.camera);
    if (gui.cycle(left(gui, 0), "Camera", cameras, camera))
        tw.setCamera(static_cast<CameraView>(camera));
    float fov = view.m_fieldOfView;
    if (gui.slider(right(gui, 0), "FOV", fov, FlyCam::minFieldOfView, FlyCam::maxFieldOfView))
        view.setFieldOfView(fov);
    float flySpeed = view.flyCam.speed();
    if (gui.slider(left(gui, 1), "Fly Speed", flySpeed, FlyCam::minSpeed, FlyCam::maxSpeed))
    {
        view.flyCam.setSpeed(flySpeed);
        view.damage(1);
    }

    // How the chosen camera is steered, in place of the old buttons' tooltips.
    std::vector<std::string> help;
    switch (tw.settings.camera)
    {
    case CameraView::World:
        help = {"Rotate: right drag, Shift + left drag or two-finger scroll",
                "Pan: Alt + right drag   Zoom: Ctrl + scroll or pinch",
                "Reset: double right click"};
        break;
    case CameraView::Train:
        help = {"Look around: drag the view or two fingers",
                "Look ahead again: double click, R or Home"};
        break;
    case CameraView::Chase:
        help = {"Follows behind the train, looking over it"};
        break;
    case CameraView::Top:
        help = {"Straight down, without perspective (FOV does not apply)"};
        break;
    case CameraView::Fly:
        help = {"Click the view to steer with the mouse; Esc frees it",
                "W A S D move, Space up, Shift down, Ctrl faster",
                "Wheel or + / - set the speed   F: face the train   Home: World view",
                "With Edit on: left removes a point, right places one, G carries"};
        break;
    }
    for (size_t i = 0; i < help.size(); ++i)
        gui.text(gui.width() / 2, top(gui) + 2 * kRowStep + 6 + static_cast<int>(i) * 11, help[i],
                 ui::kGrey, ui::Align::Center);
    done(tw, gui, 4);
}

void GameMenu::drawTrack(TrainWindow &tw, ui::Gui &gui)
{
    TrainView &view = *tw.trainView;
    SceneSettings &s = tw.settings;
    title(gui, "Track");
    static const std::vector<std::string> splines = {"Linear", "Cardinal Cubic", "Cubic B-Spline"};
    int spline = s.spline - 1;
    if (gui.cycle(left(gui, 0), "Spline", splines, spline))
    {
        s.spline = spline + 1;
        tw.damageMe();
    }
    bool edit = s.edit;
    if (gui.toggle(right(gui, 0), "Edit Points", edit))
    {
        view.finishEdits();
        s.edit = edit;
        tw.damageMe();
    }
    if (gui.button(left(gui, 1), "Add Point"))
    {
        show(tw, Screen::None); // the next click on the track puts it there
        addPointCB(&tw);
    }
    if (gui.button(right(gui, 1), "Delete Point"))
        deletePointCB(&tw);
    if (gui.button(left(gui, 2), "Undo", tw.m_history.canUndo()))
        tw.undoEdit();
    if (gui.button(right(gui, 2), "Redo", tw.m_history.canRedo()))
        tw.redoEdit();
    if (gui.button(left(gui, 3), "Load..."))
        loadCB(&tw);
    if (gui.button(right(gui, 3), "Save..."))
        saveCB(&tw);

    // The selected point: roll it, or nudge it a unit along an axis.
    const bool selected = view.hasSelection();
    if (gui.button(part(gui, 4, 4, 0), "Roll +X", selected))
        rpxCB(&tw);
    if (gui.button(part(gui, 4, 4, 1), "Roll -X", selected))
        rmxCB(&tw);
    if (gui.button(part(gui, 4, 4, 2), "Roll +Z", selected))
        rpzCB(&tw);
    if (gui.button(part(gui, 4, 4, 3), "Roll -Z", selected))
        rmzCB(&tw);
    static const char *const nudges[6] = {"X-", "X+", "Y-", "Y+", "Z-", "Z+"};
    for (int i = 0; i < 6; ++i)
        if (gui.button(part(gui, 5, 6, i), nudges[i], selected))
            tw.nudgeSelected(i / 2, i % 2 ? 1.0f : -1.0f);
    if (gui.button(left(gui, 6), "Reset Track"))
        resetCB(&tw);
    std::string where = "No point selected";
    if (selected)
    {
        const Pnt3f &p = tw.m_Track.points[static_cast<size_t>(view.selectedCube)].pos;
        char text[64];
        std::snprintf(text, sizeof(text), "Point %d: %.1f  %.1f  %.1f", view.selectedCube,
                      static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z));
        where = text;
    }
    gui.text(right(gui, 6).x + 4, right(gui, 6).y + 6, where, ui::kGrey);
    done(tw, gui, 7);
}

void GameMenu::drawLighting(TrainWindow &tw, ui::Gui &gui)
{
    SceneSettings &s = tw.settings;
    title(gui, "Lighting");
    bool changed = gui.toggle(left(gui, 0), "Light+", s.lighting);
    changed |= gui.toggle(right(gui, 0), "Shadows", s.shadows);
    changed |= gui.toggle(left(gui, 1), "Headlight", s.headlight);
    changed |= gui.toggle(right(gui, 1), "Bloom", s.bloom);
    changed |= gui.toggle(left(gui, 2), "SSAO", s.ssao);
    float hours = s.timeOfDay;
    if (gui.slider(right(gui, 2), "Time: " + hoursText(hours), hours, 0.0f, 24.0f, -1))
    {
        s.timeOfDay = static_cast<float>(static_cast<int>(hours * 4.0f + 0.5f)) / 4.0f;
        changed = true;
    }
    gui.text(gui.width() / 2, top(gui) + 3 * kRowStep + 6,
             "Light+ is per-pixel lighting with shadow maps, sky and fog;", ui::kGrey,
             ui::Align::Center);
    gui.text(gui.width() / 2, top(gui) + 3 * kRowStep + 17,
             "the other effects need it. Off: the original fixed-function lighting.", ui::kGrey,
             ui::Align::Center);
    if (changed)
        tw.damageMe();
    done(tw, gui, 4);
}

void GameMenu::drawWorld(TrainWindow &tw, ui::Gui &gui)
{
    SceneSettings &s = tw.settings;
    title(gui, "World");
    bool changed = gui.toggle(left(gui, 0), "Water", s.water);
    changed |= gui.toggle(right(gui, 0), "Rain", s.rain);
    changed |= gui.toggle(left(gui, 1), "Scenery", s.scenery);
    if (changed)
        tw.damageMe();
    if (gui.button(right(gui, 1), "Grand Tour"))
    {
        grandTourCB(&tw);
        show(tw, Screen::None);
        return;
    }

    gui.text(wide(gui, 2).x, wide(gui, 2).y + 2, "Texture Pack", ui::kWhite);
    gui.text(wide(gui, 2).x + wide(gui, 2).w, wide(gui, 2).y + 2, tw.texturePackNote, ui::kGrey,
             ui::Align::Right);
    int selected = 0;
    for (size_t i = 0; i < tw.texturePackPaths.size(); ++i)
        if (tw.texturePackPaths[i] == tw.texturePackPath)
            selected = static_cast<int>(i);
    const Rect list = {wide(gui, 2).x, wide(gui, 2).y + 12, 310, 2 * kRowStep - 14};
    if (gui.list(list, tw.texturePackNames, selected, m_packScroll))
        useTexturePack(&tw, static_cast<size_t>(selected));
    if (gui.button(left(gui, 4), "Open Pack..."))
        openTexturePack(&tw);
    if (gui.button(right(gui, 4), "Open Packs Folder"))
        openTexturePackFolder(&tw);
    done(tw, gui, 5);
}
