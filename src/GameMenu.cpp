/************************************************************************
     File:        GameMenu.cpp

     Comment:     See GameMenu.h. Layouts follow a block game's screens:
                  GUI units (Gui.h), a title near the top, settings in two
                  columns of 150 wide buttons on rows 24 apart starting a
                  sixth of the way down, and "Done" below them.

*************************************************************************/

#include "GameMenu.h"

#include <cctype>

#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "CallBacks.H"
#include "Folders.h"
#include "PerfOverlay.h"
#include "KeyBinds.h"
#include "Ui/PixelFont.h"
#include "Utilities/Platform.H"
#include "WorldSave.h"
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
    const int w = (i == n - 1) ? r.x + r.w - x : each;
    return {x, r.y, w, r.h};
}

struct Slot
{
    ui::UiAtlas::IconId icon;
    const char *name;
};

const char *const kCameraNames[] = {"World", "Train", "Chase", "Top", "Fly"};

// The hotbar's keys for the hints: "1-9", or each slot's key when they were
// changed ("12C456789"), or just where to see them.
std::string hotbarKeys(const KeyBinds &keys)
{
    std::string text;
    bool changed = false;
    for (int i = 0; i < GameMenu::kSlots; ++i)
    {
        const auto action =
            static_cast<KeyBinds::Action>(static_cast<int>(KeyBinds::Action::Slot1) + i);
        changed |= !keys.isDefault(action);
        const std::string key = keys.keyText(action);
        if (key.size() != 1)
            return "Keys in Controls:";
        text += key;
    }
    return changed ? text : "1-9";
}

// The text, shortened from the front with "..." when it is wider than `width`
// units: the end of a path or file name is the part that tells them apart.
std::string fit(const std::string &text, int width)
{
    if (ui::PixelFont::textWidth(text) <= width)
        return text;
    std::string tail = text;
    while (!tail.empty() && ui::PixelFont::textWidth("..." + tail) > width)
        tail.erase(0, 1);
    return "..." + tail;
}

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
    if ((screen == Screen::LoadTrack || screen == Screen::SaveTrack) && screen != m_screen)
    {
        // read the folder again: files put there meanwhile show up
        m_tracks = Folders::list(trackFolder(), ".txt");
        m_trackScroll = 0;
        markSaveName();
    }
    if (screen == Screen::None && !m_riding)
    {
        // Leaving the title: the ride starts looking ahead, not where the
        // title's slow turn left the view.
        m_riding = true;
        tw.trainView->headLook.reset();
    }
    // "Added ..." belongs to the screen the files were dropped on, also
    // after a notice over it about the files that were not
    const auto overlay = [](Screen s) { return s == Screen::Notice || s == Screen::Confirm; };
    if (screen != m_screen && !overlay(screen) && !overlay(m_screen))
        m_dropNote.clear();
    if (screen != m_screen)
        m_pickAction = -1; // leaving Controls drops a key being set
    m_enter = false;
    m_screen = screen;
    m_guiHasMouse = false;
    tw.trainView->damage(1);
}

void GameMenu::back(TrainWindow &tw)
{
    switch (m_screen)
    {
    case Screen::Title:
        break; // the title is where it starts; Quit is a button
    case Screen::Options:
    case Screen::SelectWorld:
        show(tw, Screen::Title);
        break;
    case Screen::None:
    case Screen::Pause:
        show(tw, Screen::None);
        break;
    case Screen::LoadTrack:
    case Screen::SaveTrack:
        show(tw, m_riding ? Screen::Track : Screen::Title);
        break;
    case Screen::Notice:
    case Screen::Confirm:
        m_yes = nullptr;
        show(tw, m_return);
        break;
    default:
        show(tw, m_riding ? Screen::Pause : Screen::Options);
        break;
    }
}

void GameMenu::notify(TrainWindow &tw, const std::string &title, const std::string &text)
{
    m_return = m_screen == Screen::Notice || m_screen == Screen::Confirm ? m_return : m_screen;
    m_title = title;
    m_lines.clear();
    size_t start = 0;
    for (size_t end; (end = text.find('\n', start)) != std::string::npos; start = end + 1)
        m_lines.push_back(text.substr(start, end - start));
    m_lines.push_back(text.substr(start));
    m_yes = nullptr;
    show(tw, Screen::Notice);
}

void GameMenu::confirm(TrainWindow &tw, const std::string &question,
                       std::function<void(TrainWindow &)> yes)
{
    m_return = m_screen == Screen::Notice || m_screen == Screen::Confirm ? m_return : m_screen;
    m_title = question;
    m_lines.clear();
    m_yes = std::move(yes);
    show(tw, Screen::Confirm);
}

void GameMenu::say(const std::string &text)
{
    m_slotName = text;
    m_slotNameUntil = m_now + kSlotNameSeconds;
}

//************************************************************************
//
// * Files dropped on the window, added as a block game adds the packs
//   dropped on its pack screen: copied into the folder the list shows
//========================================================================
void GameMenu::dropped(TrainWindow &tw, const std::vector<std::string> &paths)
{
    // a question waits for Yes or No first
    if (m_screen == Screen::Confirm || paths.empty())
        return;
    std::string track; // the last track added: picked in Load Track
    std::vector<std::string> tracks, packs, refused;
    for (const std::string &path : paths)
    {
        std::filesystem::path p = std::filesystem::u8path(path);
        if (!p.has_filename())
            p = p.parent_path();
        const std::string name = p.filename().u8string();
        std::string error;
        if (Folders::withExtension(name, ".txt") == name) // a track file, .txt in any case
        {
            const std::string copied = Folders::copyInto(path, trackFolder(), error);
            if (copied.empty())
                refused.push_back(name + ": " + error);
            else
            {
                track = copied;
                tracks.push_back(copied);
            }
            continue;
        }
        std::string pack;
        if (addTexturePack(&tw, path, pack, error))
            packs.push_back(pack);
        else
            refused.push_back(name + ": " + error);
    }

    auto added = [](const std::vector<std::string> &names) {
        std::string text = "Added " + names.front();
        if (names.size() > 1)
            text += " and " + std::to_string(names.size() - 1) + " more";
        return text;
    };
    if (!tracks.empty())
    {
        if (m_screen != Screen::LoadTrack)
            show(tw, Screen::LoadTrack);
        readTracks(track);
        m_dropNote = added(tracks);
    }
    else if (!packs.empty())
    {
        if (m_screen != Screen::World)
            show(tw, Screen::World);
        for (size_t i = 0; i < tw.texturePackNames.size(); ++i)
            if (tw.texturePackNames[i] == packs.back())
                m_packScroll = static_cast<int>(i);
        m_dropNote = added(packs);
    }
    if (!refused.empty())
    {
        std::string text;
        for (const std::string &line : refused)
            text += (text.empty() ? "" : "\n") + line;
        notify(tw,
               tracks.empty() && packs.empty() ? "Nothing was added" : "Not everything was added",
               text + "\nDrop track files (.txt) or texture packs (.zip or folders).");
    }
    tw.trainView->damage(1);
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
        if (pickingKey())
            return pickKey(tw, e);
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
        {
            // Backspace for the Save screen's name field, Enter for Load or
            // Save; nothing reaches the view (WASD would fly behind the menu).
            gui.feed(e);
            if (e.key == Key::Enter || e.key == Key::KeypadEnter)
                m_enter = true;
            tw.trainView->damage(1);
            return true;
        }
        using Action = KeyBinds::Action;
        const KeyBinds &keys = tw.keys;
        if (keys.matches(Action::HideHud, e))
        {
            hudHidden = !hudHidden;
            tw.trainView->damage(1);
            return true;
        }
        if (keys.matches(Action::PerfScreen, e))
        {
            perfShown = !perfShown;
            tw.trainView->damage(1);
            return true;
        }
        // a move, tilt or new point in progress keeps the keys until it ends
        if (tw.trainView->editing())
            return false;
        for (int slot = 0; slot < kSlots; ++slot)
            if (keys.matches(static_cast<Action>(static_cast<int>(Action::Slot1) + slot), e))
            {
                useSlot(tw, slot, m_now);
                return true;
            }
        if (keys.matches(Action::Run, e))
        {
            tw.settings.run = !tw.settings.run;
            tw.damageMe();
            return true;
        }
        const bool faster = keys.matches(Action::Faster, e);
        if (faster || keys.matches(Action::Slower, e))
        {
            float &speed = tw.settings.speed;
            speed += faster ? 0.5f : -0.5f;
            speed = speed < 0.0f ? 0.0f : (speed > 10.0f ? 10.0f : speed);
            tw.damageMe();
            return true;
        }
        return false;
    }
    case InputType::KeyUp:
        // a modifier let go on its own, while Controls waits: it is the key
        if (pickingKey())
        {
            if (m_pickModifier != 0 && KeyBinds::normalize(e.key) == m_pickModifier)
                setPickedKey(tw, m_pickModifier, e.state);
            return true;
        }
        return false;
    case InputType::Push:
        // a click while Controls waits for a key: keep the key it had
        if (pickingKey())
        {
            m_pickAction = -1;
            tw.trainView->damage(1);
            return true;
        }
        [[fallthrough]];
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
    case InputType::Text:
        if (!open())
            return false;
        if (pickingKey())
            return true;
        gui.feed(e);
        tw.trainView->damage(1);
        return true;
    case InputType::Unfocus:
    case InputType::Hide:
        gui.feed(e);
        m_guiHasMouse = false;
        m_pickAction = -1; // its keys' releases will not come
        return false;
    case InputType::Focus:
    case InputType::Enter:
        // back from the file manager: files put in the folder meanwhile show up
        readFolders(tw);
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
        // the world stays visible, darkened, as behind a paused game's menu;
        // with no world the dirt behind is already dark
        if (inWorld())
            gui.fill({0, 0, gui.width(), gui.height()}, ui::kDim);
        switch (m_screen)
        {
        case Screen::Title:
            drawTitle(tw, gui);
            break;
        case Screen::Options:
            drawOptions(tw, gui);
            break;
        case Screen::SelectWorld:
            drawSelectWorld(tw, gui);
            break;
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
        case Screen::Controls:
            drawControls(tw, gui);
            break;
        case Screen::LoadTrack:
            drawLoadTrack(tw, gui);
            break;
        case Screen::SaveTrack:
            drawSaveTrack(tw, gui);
            break;
        case Screen::Notice:
            drawNotice(tw, gui);
            break;
        case Screen::Confirm:
            drawConfirm(tw, gui);
            break;
        case Screen::None:
            break;
        }
        return;
    }
    if (!hudHidden)
        drawHud(tw, gui, seconds);
}

//************************************************************************
//
// * The performance screen (F3)
//========================================================================
void GameMenu::drawPerf(TrainWindow &tw, ui::Gui &gui)
{
    const TrainView &view = *tw.trainView;
    PerfInfo info;
    info.version = std::string("RollerCoasters ") + ROLLERCOASTERS_VERSION;
    info.renderer = view.glRenderer;
    info.glVersion = view.glVersionText;
    info.lines = PerfOverlay::sceneLines(tw.settings);
    info.vsync = true;
    info.gpuTimer = view.perfGpuOn;
    info.memory = PerfStats::memoryLine(PerfStats::memory());
    PerfOverlay::draw(gui, view.perf, info, PerfStats::clock());
}

void GameMenu::drawHud(TrainWindow &tw, ui::Gui &gui, double seconds)
{
    const SceneSettings &s = tw.settings;

    // Key hints, top left, each line on a faint dark strip.
    // the keys as set in Controls
    using Action = KeyBinds::Action;
    const KeyBinds &keys = tw.keys;
    char speed[64];
    std::snprintf(speed, sizeof(speed), "%s %s  Speed %.1f", keys.keyText(Action::Slower).c_str(),
                  keys.keyText(Action::Faster).c_str(), static_cast<double>(s.speed));
    const std::vector<std::string> hints = {
        "Esc  Menu", keys.keyText(Action::Run) + (s.run ? "  Stop the train" : "  Run the train"),
        speed, hotbarKeys(keys) + "  Hotbar", keys.keyText(Action::HideHud) + "  Hide this"};
    // the performance screen takes the corner, as a block game's debug screen
    if (perfShown)
        drawPerf(tw, gui);
    for (size_t i = 0; i < hints.size() && !perfShown; ++i)
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
        // the slot's key, small in the corner, as an item's count (if it fits)
        const std::string key =
            keys.keyText(static_cast<Action>(static_cast<int>(Action::Slot1) + i));
        if (ui::PixelFont::textWidth(key) <= 16)
            gui.text(r.x + 19, r.y + 12, key, ui::kWhite, ui::Align::Right);
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

//************************************************************************
//
// * The title screen
//========================================================================
namespace
{
// Text in blocks: every lit pixel of the font is a block `size` units
// square, its face lighter at the top and speckled like stone, over a darker
// side and bottom, so the letters stand out of the screen.
void blockText(ui::Gui &gui, int centerX, int y, const std::string &text, int size)
{
    const int width = ui::PixelFont::textWidth(text) * size;
    const int depth = size / 2 > 0 ? size / 2 : 1;
    for (int pass = 0; pass < 2; ++pass)
    {
        int pen = centerX - width / 2;
        for (char c : text)
        {
            const int glyph = ui::PixelFont::glyphWidth(c);
            for (int row = 0; row < ui::PixelFont::kCell; ++row)
                for (int column = 0; column < glyph; ++column)
                {
                    if (!ui::PixelFont::pixel(c, column, row))
                        continue;
                    const int x = pen + column * size, rowY = y + row * size;
                    if (pass == 0)
                    {
                        gui.fill({x + depth, rowY + depth, size, size}, {40, 40, 46, 255});
                        continue;
                    }
                    // a few shades, picked by position, as stone has
                    const unsigned hash = (static_cast<unsigned>(x) * 73856093u) ^
                                          (static_cast<unsigned>(rowY) * 19349663u);
                    const int light = 228 - row * 12 - static_cast<int>(hash % 3) * 10;
                    const auto shade = static_cast<unsigned char>(light);
                    gui.fill({x, rowY, size, size},
                             {shade, shade, static_cast<unsigned char>(light + 8), 255});
                }
            pen += (glyph + 1) * size;
        }
    }
}
} // namespace

void GameMenu::drawTitle(TrainWindow &tw, ui::Gui &gui)
{
    // No world is loaded behind the title (TrainView::worldReady()): it is
    // drawn over the dirt.
    const int cx = gui.width() / 2;
    const std::string name = "RollerCoasters";
    const int size = (gui.width() - 40) / ui::PixelFont::textWidth(name) < 4
                         ? (gui.width() - 40) / ui::PixelFont::textWidth(name)
                         : 4;
    const int logoY = gui.height() / 4 - 4 * size - 6;
    blockText(gui, cx, logoY, name, size);
    gui.text(cx, logoY + 8 * size + 6, "Train and Roller Coaster", ui::kYellow, ui::Align::Center);

    const int y = gui.height() / 4 + 40;
    if (gui.button({cx - 100, y, 200, kButtonH}, "Play") || m_enter)
    {
        m_enter = false;
        m_worldSelected = tw.settings.scenery ? 0 : 1; // the world it was last in
        show(tw, Screen::SelectWorld);
        return;
    }
    if (gui.button({cx - 100, y + 24, 200, kButtonH}, "Load Track..."))
        show(tw, Screen::LoadTrack);
    if (gui.button({cx - 100, y + 60, 98, kButtonH}, "Options..."))
        show(tw, Screen::Options);
    if (gui.button({cx + 2, y + 60, 98, kButtonH}, "Quit"))
        tw.close();

    gui.text(2, gui.height() - 10, std::string("RollerCoasters ") + ROLLERCOASTERS_VERSION,
             ui::kWhite);
}

//************************************************************************
//
// * Select World, as a block game lists its worlds: a picture, the name and
//   a line about each; a click selects, a double click or Enter plays
//========================================================================
namespace
{
struct WorldEntry
{
    const char *name;
    const char *about;
    const char *save; // WorldSave name
    bool flat;
};
const WorldEntry kWorlds[] = {
    {"Grand Tour Island", "The island, the sea and the ride round them", "island", false},
    {"Flat Island", "Level grass to build a track of your own", "flat", true},
};

// A small picture of the world: sky, then the island over the sea or the
// flat grass on dirt.
void worldPicture(ui::Gui &gui, int x, int y, bool flat)
{
    gui.fill({x, y, 32, 32}, {120, 170, 255, 255});
    if (flat)
    {
        gui.fill({x, y + 18, 32, 4}, {96, 168, 64, 255});
        gui.fill({x, y + 22, 32, 10}, {134, 96, 67, 255});
        return;
    }
    gui.fill({x, y + 20, 32, 12}, {48, 96, 200, 255});
    gui.fill({x + 6, y + 16, 20, 6}, {96, 168, 64, 255});
    gui.fill({x + 11, y + 11, 10, 5}, {96, 168, 64, 255});
    gui.fill({x + 4, y + 21, 24, 2}, {218, 206, 150, 255});
}
} // namespace

void GameMenu::playWorld(TrainWindow &tw, int world)
{
    if (world == 1)
        playFlat(&tw);
    else
        playIsland(&tw);
    show(tw, Screen::None); // the loading screen shows while that world is built
}

void GameMenu::drawSelectWorld(TrainWindow &tw, ui::Gui &gui)
{
    title(gui, "Select World");
    const int cx = gui.width() / 2;
    int i = 0;
    for (const WorldEntry &world : kWorlds)
    {
        const Rect row = {cx - 135, top(gui) + i * 40, 270, 36};
        bool twice = false;
        if (gui.area(row, &twice))
        {
            if (twice && m_worldSelected == i)
            {
                playWorld(tw, i);
                return;
            }
            m_worldSelected = i;
        }
        if (m_worldSelected == i)
        {
            // the selected world has a white frame, as in the game
            gui.fill(row, {255, 255, 255, 255});
            gui.fill({row.x + 1, row.y + 1, row.w - 2, row.h - 2}, {0, 0, 0, 255});
        }
        else if (gui.isHovered(row))
            gui.fill(row, {255, 255, 255, 40});
        worldPicture(gui, row.x + 2, row.y + 2, world.flat);
        gui.text(row.x + 38, row.y + 3, world.name, ui::kWhite);
        gui.text(row.x + 38, row.y + 14, world.about, ui::kGrey);
        const bool saved = Folders::exists(WorldSave::path(world.save));
        gui.text(row.x + 38, row.y + 25,
                 saved ? "Saved: opens quickly" : "New: made the first time you play", ui::kGrey);
        ++i;
    }
    if (gui.button(left(gui, 4), "Play Selected World") || m_enter)
    {
        m_enter = false;
        playWorld(tw, m_worldSelected);
        return;
    }
    if (gui.button(right(gui, 4), "Cancel"))
        back(tw);
    gui.text(cx, top(gui) + 5 * kRowStep + 6, "A track of your own goes with you to either world",
             ui::kGrey, ui::Align::Center);
}

void GameMenu::drawOptions(TrainWindow &tw, ui::Gui &gui)
{
    title(gui, "Options");
    if (gui.button(left(gui, 0), "Ride..."))
        show(tw, Screen::Ride);
    if (gui.button(right(gui, 0), "Camera..."))
        show(tw, Screen::Camera);
    if (gui.button(left(gui, 1), "Lighting..."))
        show(tw, Screen::Lighting);
    if (gui.button(right(gui, 1), "World..."))
        show(tw, Screen::World);
    if (gui.button(wide(gui, 2), "Controls..."))
        show(tw, Screen::Controls);
    done(tw, gui, 3);
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
    if (gui.button({cx + 4, y + 72, 98, kButtonH}, "Controls..."))
        show(tw, Screen::Controls);
    if (gui.button({cx - 102, y + 96, 204, kButtonH}, "Grand Tour"))
    {
        grandTourCB(&tw);
        show(tw, Screen::None);
    }
    // as a block game's "Quit to Title": the program ends from the title
    if (gui.button({cx - 102, y + 132, 204, kButtonH}, "Quit to Title"))
    {
        m_riding = false;
        show(tw, Screen::Title);
    }
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
    const KeyBinds &keys = tw.keys;
    gui.text(gui.width() / 2, top(gui) + 3 * kRowStep + 6,
             "While riding: " + keys.keyText(KeyBinds::Action::Run) + " runs and stops, " +
                 keys.keyText(KeyBinds::Action::Slower) + " and " +
                 keys.keyText(KeyBinds::Action::Faster) + " change the speed",
             ui::kGrey, ui::Align::Center);
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

    // How the chosen camera is steered, in place of the old buttons' tooltips,
    // with the keys as set in Controls.
    using Action = KeyBinds::Action;
    const auto key = [&tw](Action action) { return tw.keys.keyText(action); };
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
                "Look ahead again: double click or " + key(Action::LookAhead)};
        break;
    case CameraView::Chase:
        help = {"Follows behind the train, looking over it"};
        break;
    case CameraView::Top:
        help = {"Straight down, without perspective (FOV does not apply)"};
        break;
    case CameraView::Fly:
        help = {"Click the view to steer with the mouse; Esc frees it",
                key(Action::FlyForward) + " " + key(Action::FlyLeft) + " " + key(Action::FlyBack) +
                    " " + key(Action::FlyRight) + " move, " + key(Action::FlyUp) + " up, " +
                    key(Action::FlyDown) + " down, " + key(Action::FlyFast) + " faster",
                "Wheel or " + key(Action::FlySpeedUp) + " / " + key(Action::FlySpeedDown) +
                    " set the speed   " + key(Action::FlyFaceTrain) + ": face the train   " +
                    key(Action::FlyToWorld) + ": World view",
                "With Edit on: left removes a point, right places one, " + key(Action::Grab) +
                    " carries"};
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
        show(tw, Screen::LoadTrack);
    if (gui.button(right(gui, 3), "Save..."))
        show(tw, Screen::SaveTrack);

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
            tw.nudgeSelected(i / 2, (i % 2 != 0) ? 1.0f : -1.0f);
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
    // as a block game's Video Settings: Fullscreen sits with the picture's settings (F11 too)
    bool fullscreen = tw.fullscreen();
    if (gui.toggle(left(gui, 3), "Fullscreen", fullscreen))
        tw.setFullscreen(fullscreen);
    gui.text(right(gui, 3).x + 4, right(gui, 3).y + 6,
             tw.keys.keyText(KeyBinds::Action::Fullscreen) + " anywhere", ui::kGrey);
    gui.text(gui.width() / 2, top(gui) + 4 * kRowStep + 6,
             "Light+ is per-pixel lighting with shadow maps, sky and fog;", ui::kGrey,
             ui::Align::Center);
    gui.text(gui.width() / 2, top(gui) + 4 * kRowStep + 17,
             "the other effects need it. Off: the original fixed-function lighting.", ui::kGrey,
             ui::Align::Center);
    if (changed)
        tw.damageMe();
    done(tw, gui, 5);
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
    if (gui.button(wide(gui, 4), "Open Pack Folder"))
        openTexturePackFolder(&tw);
    gui.text(gui.width() / 2, top(gui) + 5 * kRowStep + 1,
             fit(m_dropNote.empty() ? "Drop texture packs on this window to add them" : m_dropNote,
                 gui.width() - 20),
             m_dropNote.empty() ? ui::kGrey : ui::kWhite, ui::Align::Center);
    done(tw, gui, 6);
}

//************************************************************************
//
// * Controls: the key binds, as a block game's Key Binds screen
//========================================================================
namespace
{
// A row of the list: a group's heading, or a command.
struct KeyRow
{
    bool heading;
    KeyBinds::Group group;
    KeyBinds::Action action;
};

std::vector<KeyRow> keyRows()
{
    std::vector<KeyRow> rows;
    for (int i = 0; i < KeyBinds::kActions; ++i)
    {
        const auto action = static_cast<KeyBinds::Action>(i);
        const KeyBinds::Group group = KeyBinds::group(action);
        if (rows.empty() || rows.back().group != group)
            rows.push_back({true, group, action});
        rows.push_back({false, group, action});
    }
    return rows;
}

constexpr int kKeyRowStep = 20;
constexpr ui::Color kClash = {255, 85, 85, 255};
} // namespace

bool GameMenu::pickKey(TrainWindow &tw, const InputEvent &e)
{
    const auto action = static_cast<KeyBinds::Action>(m_pickAction);
    if (e.key == Key::Escape)
    {
        setPickedKey(tw, 0, 0); // Esc: no key, as in the game
        return true;
    }
    const int key = KeyBinds::normalize(e.key);
    // Ctrl, Shift or Alt may start a combination: wait for the next key, or
    // for it to be let go alone (input()'s KeyUp). The Fly camera's own keys
    // take no modifiers, so there a modifier is the key at once.
    if (KeyBinds::modifierBit(key) != 0 && !KeyBinds::held(action))
    {
        m_pickModifier = key;
        tw.trainView->damage(1);
        return true;
    }
    setPickedKey(tw, key, e.state);
    return true;
}

void GameMenu::setPickedKey(TrainWindow &tw, int key, int mods)
{
    if (m_pickAction < 0)
        return;
    tw.keys.set(static_cast<KeyBinds::Action>(m_pickAction), m_pickSlot, {key, mods});
    tw.keysChanged();
    m_pickAction = -1;
    m_pickModifier = 0;
    tw.trainView->damage(1);
}

void GameMenu::drawControls(TrainWindow &tw, ui::Gui &gui)
{
    KeyBinds &keys = tw.keys;
    const int cx = gui.width() / 2;
    gui.text(cx, 6, "Key Binds", ui::kWhite, ui::Align::Center);
    gui.text(cx, 17,
             pickingKey() ? "Press the new key (with Ctrl, Shift or Alt if wanted); Esc: no key"
                          : "Click a key, then press the new one. Red keys clash.",
             ui::kGrey, ui::Align::Center);

    // The list: name, key, second key, Reset; rows that fit, scrolled by the wheel.
    const int x0 = cx - 157;
    const int listTop = 30;
    const int listBottom = gui.height() - 52;
    const ui::Rect area = {x0 - 2, listTop - 1, 320, listBottom - listTop + 2};
    const std::vector<KeyRow> rows = keyRows();
    const int count = static_cast<int>(rows.size());
    const int visible =
        (listBottom - listTop) / kKeyRowStep > 0 ? (listBottom - listTop) / kKeyRowStep : 1;
    const float wheel = gui.wheel(area);
    if (wheel != 0.0f)
        m_keyScroll += wheel > 0.0f ? 1 : -1;
    const int most = count > visible ? count - visible : 0;
    m_keyScroll = m_keyScroll < 0 ? 0 : (m_keyScroll > most ? most : m_keyScroll);
    gui.fill(area, {0, 0, 0, 90});

    for (int i = 0; i < visible && m_keyScroll + i < count; ++i)
    {
        const KeyRow &row = rows[static_cast<size_t>(m_keyScroll + i)];
        const int y = listTop + i * kKeyRowStep;
        if (row.heading)
        {
            gui.text(cx, y + 7, KeyBinds::groupName(row.group), ui::kYellow, ui::Align::Center);
            continue;
        }
        const int index = static_cast<int>(row.action);
        gui.text(x0, y + 6, fit(KeyBinds::name(row.action), 136), ui::kWhite);
        for (int slot = 0; slot < KeyBinds::kSlots; ++slot)
        {
            const ui::Rect r = {x0 + 140 + slot * 64, y + 1, 62, kKeyRowStep - 2};
            const bool waiting = m_pickAction == index && m_pickSlot == slot;
            std::string label = KeyBinds::bindName(keys.get(row.action, slot));
            const bool none = keys.get(row.action, slot).none();
            ui::Color color =
                keys.clashes(row.action, slot) ? kClash : (none ? ui::kGrey : ui::kWhite);
            if (waiting)
            {
                label = m_pickModifier != 0 ? KeyBinds::keyName(m_pickModifier) + "+..." : label;
                label = "> " + label + " <";
                color = ui::kYellow;
            }
            if (gui.button(r, fit(label, r.w - 4), true, color))
            {
                m_pickAction = index;
                m_pickSlot = slot;
                m_pickModifier = 0;
            }
        }
        if (gui.button({x0 + 270, y + 1, 42, kKeyRowStep - 2}, "Reset",
                       !keys.isDefault(row.action)))
        {
            keys.reset(row.action);
            tw.keysChanged();
        }
    }
    if (count > visible)
    {
        // a thin bar showing which part of the list is in view
        const int trackH = listBottom - listTop;
        const int thumbH = trackH * visible / count > 6 ? trackH * visible / count : 6;
        const int thumbY = listTop + (trackH - thumbH) * m_keyScroll / most;
        gui.fill({x0 + 314, listTop, 3, trackH}, {0, 0, 0, 120});
        gui.fill({x0 + 314, thumbY, 3, thumbH}, {200, 200, 210, 255});
    }

    // Not a key, but where a block game's players look for keyboard
    // settings: the Windows key does nothing while playing (WindowsKey.h).
    if (gui.toggle({cx - 155, gui.height() - 47, 310, kButtonH}, "Lock Windows Key",
                   keys.lockWindowsKey))
        tw.keysChanged();
    const int buttonsY = gui.height() - 25;
    if (gui.button({cx - 155, buttonsY, 150, kButtonH}, "Reset All", !keys.allDefault()))
    {
        keys.resetAll();
        tw.keysChanged();
    }
    if (gui.button({cx + 5, buttonsY, 150, kButtonH}, "Done"))
        back(tw);
}

//************************************************************************
//
// * Load Track and Save Track: the tracks in TrackFiles\, as a block game
//   lists its worlds
//========================================================================
void GameMenu::readTracks(const std::string &pick)
{
    std::string keep = pick;
    if (keep.empty() && m_trackSelected >= 0 && m_trackSelected < static_cast<int>(m_tracks.size()))
        keep = m_tracks[static_cast<size_t>(m_trackSelected)];
    m_tracks = Folders::list(trackFolder(), ".txt");
    m_trackSelected = -1;
    for (size_t i = 0; i < m_tracks.size(); ++i)
        if (m_tracks[i] == keep)
            m_trackSelected = static_cast<int>(i);
    // a track just added scrolls into view (Gui::list keeps the list full)
    if (!pick.empty() && m_trackSelected >= 0)
        m_trackScroll = m_trackSelected;
}

void GameMenu::readFolders(TrainWindow &tw)
{
    if (m_screen == Screen::LoadTrack || m_screen == Screen::SaveTrack)
        readTracks();
    else if (m_screen == Screen::World)
        tw.listTexturePacks(tw.texturePackPath);
    else
        return;
    tw.trainView->damage(1);
}

void GameMenu::markSaveName()
{
    // file names on Windows ignore case, so "Track" is the same file as "track.txt"
    auto lower = [](std::string text) {
        for (char &c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    const std::string name = lower(Folders::withExtension(m_saveName, ".txt"));
    m_trackSelected = -1;
    for (size_t i = 0; i < m_tracks.size(); ++i)
        if (lower(m_tracks[i]) == name)
            m_trackSelected = static_cast<int>(i);
}

void GameMenu::loadSelected(TrainWindow &tw)
{
    if (m_trackSelected < 0 || m_trackSelected >= static_cast<int>(m_tracks.size()))
        return;
    const std::string name = m_tracks[static_cast<size_t>(m_trackSelected)];
    std::string error;
    if (!loadTrack(&tw, trackFolder() + "/" + name, error))
    {
        notify(tw, "Could not load " + name, error + "\nThe track was not changed.");
        return;
    }
    m_saveName = name.substr(0, name.size() - 4); // Save offers the same name
    show(tw, Screen::None);
    say("Loaded " + name);
}

void GameMenu::drawLoadTrack(TrainWindow &tw, ui::Gui &gui)
{
    title(gui, "Load Track");
    const Rect list = {wide(gui, 0).x, wide(gui, 0).y, 310, 4 * kRowStep - 4};
    bool doubleClicked = false;
    gui.list(list, m_tracks, m_trackSelected, m_trackScroll, &doubleClicked);
    if (m_tracks.empty())
    {
        gui.text(gui.width() / 2, list.y + 20, "No tracks yet. Save one, or drop", ui::kGrey,
                 ui::Align::Center);
        gui.text(gui.width() / 2, list.y + 31, "track files (.txt) on this window.", ui::kGrey,
                 ui::Align::Center);
    }
    const bool picked = m_trackSelected >= 0;
    if ((gui.button(left(gui, 4), "Load", picked) || doubleClicked || (m_enter && picked)))
    {
        m_enter = false;
        loadSelected(tw);
        return;
    }
    m_enter = false;
    if (gui.button(right(gui, 4), "Open Folder"))
        Folders::show(trackFolder());
    // what a drop did, or that dropping is how files get here
    gui.text(gui.width() / 2, top(gui) + 5 * kRowStep + 1,
             fit(m_dropNote.empty() ? "Drop track files on this window to add them" : m_dropNote,
                 gui.width() - 20),
             m_dropNote.empty() ? ui::kGrey : ui::kWhite, ui::Align::Center);
    gui.text(gui.width() / 2, top(gui) + 5 * kRowStep + 12, fit(trackFolder(), gui.width() - 20),
             ui::kGrey, ui::Align::Center);
    if (gui.button({gui.width() / 2 - 100, top(gui) + 6 * kRowStep + 8, 200, kButtonH}, "Cancel"))
        back(tw);
}

void GameMenu::save(TrainWindow &tw)
{
    const std::string name = Folders::withExtension(m_saveName, ".txt");
    const std::string path = trackFolder() + "/" + name;
    auto write = [this, name, path](TrainWindow &window) {
        std::string error;
        if (!saveTrack(&window, path, error))
        {
            notify(window, "Could not save " + name, error);
            return;
        }
        show(window, Screen::None);
        say("Saved " + name);
    };
    if (Folders::exists(path))
        confirm(tw, "Replace " + name + "?", write);
    else
        write(tw);
}

void GameMenu::drawSaveTrack(TrainWindow &tw, ui::Gui &gui)
{
    title(gui, "Save Track");
    const Rect field = wide(gui, 0);
    gui.text(field.x, field.y - 10, "Name", ui::kGrey);
    if (gui.textField(field, m_saveName, 60))
        markSaveName();
    const bool valid = Folders::validName(m_saveName);
    const std::string name = Folders::withExtension(m_saveName, ".txt");
    std::string note;
    if (m_saveName.empty())
        note = "Type a name for the track";
    else if (!valid)
        note = "A name cannot have \\ / : * ? \" < > | or end in a dot";
    else if (Folders::exists(trackFolder() + "/" + name))
        note = "Saves over " + name;
    else
        note = "Saves as " + name;
    gui.text(field.x, field.y + kButtonH + 4, fit(note, field.w), valid ? ui::kGrey : ui::kYellow);

    // a click on a saved track takes its name, to save over it
    const Rect list = {field.x, top(gui) + 2 * kRowStep - 4, 310, 3 * kRowStep - 4};
    if (gui.list(list, m_tracks, m_trackSelected, m_trackScroll) && m_trackSelected >= 0)
    {
        const std::string &picked = m_tracks[static_cast<size_t>(m_trackSelected)];
        m_saveName = picked.substr(0, picked.size() - 4);
    }
    if (gui.button(left(gui, 5), "Save", valid) || (m_enter && valid))
    {
        m_enter = false;
        save(tw);
        return;
    }
    m_enter = false;
    if (gui.button(right(gui, 5), "Cancel"))
        back(tw);
}

//************************************************************************
//
// * A notice and a question, in place of message boxes
//========================================================================
void GameMenu::drawNotice(TrainWindow &tw, ui::Gui &gui)
{
    title(gui, m_title);
    for (size_t i = 0; i < m_lines.size(); ++i)
        gui.text(gui.width() / 2, top(gui) + 6 + static_cast<int>(i) * 11,
                 fit(m_lines[i], gui.width() - 20), ui::kWhite, ui::Align::Center);
    const int row = 1 + static_cast<int>(m_lines.size()) / 2;
    if (gui.button({gui.width() / 2 - 100, top(gui) + row * kRowStep + 8, 200, kButtonH}, "OK") ||
        m_enter)
    {
        m_enter = false;
        back(tw);
    }
}

void GameMenu::drawConfirm(TrainWindow &tw, ui::Gui &gui)
{
    title(gui, m_title);
    if (gui.button(left(gui, 1), "Yes") || m_enter)
    {
        m_enter = false;
        std::function<void(TrainWindow &)> yes = std::move(m_yes);
        m_yes = nullptr;
        m_screen = m_return; // where Yes leads from, unless it shows something else
        if (yes)
            yes(tw);
        tw.trainView->damage(1);
        return;
    }
    if (gui.button(right(gui, 1), "No"))
        back(tw);
}
