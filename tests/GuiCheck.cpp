// Checks the block-game style interface without OpenGL: the pixel font's metrics, the atlas,
// the GUI scale, and that buttons, toggles, cycle buttons, sliders and lists react to fed mouse
// input the way the game's do (a button fires on the press, the interface takes events over
// what it drew and leaves the rest to the 3D view), and that a text field takes typing.
#include "Ui/Gui.h"
#include "Ui/PixelFont.h"
#include "Ui/UiAtlas.h"

#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

InputEvent mouse(InputType type, int x, int y, int button = Mouse::Left)
{
    InputEvent e;
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = button;
    return e;
}

// Pixel position of GUI unit (x, y) at scale 2.
int px(int units)
{
    return units * 2;
}
} // namespace

int main()
{
    using namespace ui;

    // Font: proportional glyphs, the gap between them, '?' for unknown characters.
    expect(PixelFont::glyphWidth('i') == 1 && PixelFont::glyphWidth('m') == 5,
           "i is one column, m five");
    expect(PixelFont::textWidth("Hi") == 5 + 1 + 1, "\"Hi\" is H, a gap and i");
    expect(PixelFont::textWidth("") == 0, "empty text has no width");
    expect(PixelFont::glyphWidth(' ') == 3, "a space is three columns");
    expect(PixelFont::pixel('g', 0, 7) && !PixelFont::pixel('a', 0, 7),
           "g has a tail below the baseline, a does not");
    expect(PixelFont::glyphWidth('\x01') == PixelFont::glyphWidth('?'),
           "unknown characters draw as ?");
    bool everyGlyph = true;
    for (int c = '!'; c <= '~'; ++c)
    {
        bool lit = false;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                lit = lit || PixelFont::pixel(static_cast<char>(c), x, y);
        everyGlyph = everyGlyph && lit && PixelFont::glyphWidth(static_cast<char>(c)) <= 5;
    }
    expect(everyGlyph, "every printable character has a glyph at most 5 wide");

    // Atlas: glyphs are white in their cells, skins do not overlap the font.
    const std::vector<unsigned char> atlas = UiAtlas::build();
    expect(atlas.size() == static_cast<size_t>(UiAtlas::kSize) * UiAtlas::kSize * 4,
           "the atlas is 256 x 256 RGBA");
    int cx = 0, cy = 0;
    UiAtlas::fontCell('A', cx, cy);
    const unsigned char *a = &atlas[(static_cast<size_t>(cy + 3) * UiAtlas::kSize + cx) * 4];
    expect(a[0] == 255 && a[3] == 255, "A's crossbar is lit white in the atlas");
    UiAtlas::fontCell('~', cx, cy);
    expect(cy + PixelFont::kCell <= UiAtlas::skin(UiAtlas::BUTTON).y, "skins sit below the font");

    // GUI scale like the game's Auto: the largest that leaves 320 x 240 units.
    expect(Gui::autoScale(640, 480) == 2 && Gui::autoScale(1920, 1080) == 4 &&
               Gui::autoScale(1366, 768) == 3 && Gui::autoScale(300, 200) == 1,
           "auto GUI scale");

    Gui gui;
    const Rect buttonRect = {10, 10, 100, 20};
    bool flag = false;
    int option = 0;
    float speed = 0.5f;
    const std::vector<std::string> options = {"Fast", "Fancy"};
    const std::vector<std::string> rows = {"a", "b", "c", "d", "e", "f", "g", "h"};
    int selected = -1, scroll = 0;
    const Rect toggleRect = {10, 40, 100, 20};
    const Rect cycleRect = {10, 70, 100, 20};
    const Rect sliderRect = {10, 100, 108, 20};
    const Rect listRect = {130, 10, 80, 4 + 3 * Gui::kRowHeight};

    struct Frame
    {
        bool button, toggle, cycle, slider, list;
    };
    auto frame = [&]() {
        gui.beginFrame(640, 480, 2);
        Frame f;
        f.button = gui.button(buttonRect, "Run");
        f.toggle = gui.toggle(toggleRect, "Shadows", flag);
        f.cycle = gui.cycle(cycleRect, "Graphics", options, option);
        f.slider = gui.slider(sliderRect, "Speed", speed, 0.0f, 1.0f, 2);
        f.list = gui.list(listRect, rows, selected, scroll);
        return f;
    };

    Frame f = frame();
    expect(!f.button && !f.toggle && !f.cycle && !f.slider && !f.list, "nothing fires by itself");
    expect(!gui.quads().empty(), "the widgets produce quads");
    expect(gui.width() == 320 && gui.height() == 240, "640 x 480 at scale 2 is 320 x 240 units");

    // A press over the button: the interface takes it and the next frame fires it once.
    const bool taken = gui.feed(mouse(InputType::Push, px(20), px(15)));
    gui.feed(mouse(InputType::Release, px(20), px(15)));
    f = frame();
    expect(taken && f.button, "a press on the button fires it");
    f = frame();
    expect(!f.button, "... only once");

    // Clicks on empty space go to the 3D view.
    expect(!gui.feed(mouse(InputType::Push, px(300), px(200))), "empty space is not taken");
    gui.feed(mouse(InputType::Release, px(300), px(200)));
    f = frame();
    expect(!f.button && !f.toggle, "and fires nothing");

    // A disabled button draws but does not fire.
    gui.feed(mouse(InputType::Push, px(20), px(15)));
    gui.feed(mouse(InputType::Release, px(20), px(15)));
    gui.beginFrame(640, 480, 2);
    expect(!gui.button(buttonRect, "Run", false), "a disabled button does not fire");

    // Toggle flips; cycle goes forward on left click and back on right click.
    frame();
    gui.feed(mouse(InputType::Push, px(20), px(45)));
    gui.feed(mouse(InputType::Release, px(20), px(45)));
    f = frame();
    expect(f.toggle && flag, "a toggle flips on");
    gui.feed(mouse(InputType::Push, px(20), px(75)));
    gui.feed(mouse(InputType::Release, px(20), px(75)));
    f = frame();
    expect(f.cycle && option == 1, "left click picks the next option");
    gui.feed(mouse(InputType::Push, px(20), px(75)));
    gui.feed(mouse(InputType::Release, px(20), px(75)));
    f = frame();
    expect(f.cycle && option == 0, "... and wraps round");
    gui.feed(mouse(InputType::Push, px(20), px(75), Mouse::Right));
    f = frame();
    expect(f.cycle && option == 1, "right click goes back");

    // Slider: a press sets the value, a drag follows the mouse even off the slider, release ends.
    gui.feed(mouse(InputType::Push, px(sliderRect.x + 4), px(105)));
    f = frame();
    expect(f.slider && std::fabs(speed - 0.0f) < 1e-4f, "pressing the left end sets the minimum");
    const bool dragTaken = gui.feed(mouse(InputType::Drag, px(300), px(200)));
    f = frame();
    expect(dragTaken && std::fabs(speed - 1.0f) < 1e-4f,
           "dragging past the right end clamps to the maximum, and the drag is taken");
    gui.feed(mouse(InputType::Drag, px(sliderRect.x + 4 + 50), px(105)));
    f = frame();
    expect(std::fabs(speed - 0.5f) < 1e-4f, "the middle of the track is halfway");
    gui.feed(mouse(InputType::Release, px(sliderRect.x + 4 + 50), px(105)));
    frame();
    expect(!gui.feed(mouse(InputType::Drag, px(300), px(200))), "after release the drag is free");
    frame();
    expect(std::fabs(speed - 0.5f) < 1e-4f, "... and the value stays");

    // List: wheel scrolls within its range, a click selects the row under the mouse.
    gui.feed(mouse(InputType::Move, px(140), px(15)));
    InputEvent wheel = mouse(InputType::Wheel, px(140), px(15));
    wheel.dy = 1;
    for (int i = 0; i < 10; ++i)
    {
        gui.feed(wheel);
        frame();
    }
    expect(scroll == static_cast<int>(rows.size()) - 3,
           "the wheel scrolls to the last rows and stops");
    gui.feed(mouse(InputType::Push, px(140), px(12 + Gui::kRowHeight)));
    gui.feed(mouse(InputType::Release, px(140), px(12 + Gui::kRowHeight)));
    f = frame();
    expect(f.list && selected == scroll + 1, "a click selects the row under the mouse");

    // A double click on the selected row reports it (Load Track loads that track).
    bool doubleClicked = false;
    InputEvent second = mouse(InputType::Push, px(140), px(12 + Gui::kRowHeight));
    second.clicks = 1;
    gui.feed(second);
    gui.feed(mouse(InputType::Release, px(140), px(12 + Gui::kRowHeight)));
    gui.beginFrame(640, 480, 2);
    gui.list(listRect, rows, selected, scroll, &doubleClicked);
    expect(doubleClicked && selected == scroll + 1, "a double click on the row is reported");
    doubleClicked = false;
    gui.beginFrame(640, 480, 2);
    gui.list(listRect, rows, selected, scroll, &doubleClicked);
    expect(!doubleClicked, "... once");

    // An area (the world list's rows) reports a double click too.
    const Rect areaRect = {10, 160, 100, 20};
    gui.beginFrame(640, 480, 2);
    gui.area(areaRect);
    InputEvent onArea = mouse(InputType::Push, px(20), px(165));
    onArea.clicks = 1;
    gui.feed(onArea);
    gui.feed(mouse(InputType::Release, px(20), px(165)));
    gui.beginFrame(640, 480, 2);
    bool areaTwice = false;
    expect(gui.area(areaRect, &areaTwice) && areaTwice, "a double click on an area is reported");
    gui.feed(mouse(InputType::Push, px(20), px(165)));
    gui.feed(mouse(InputType::Release, px(20), px(165)));
    gui.beginFrame(640, 480, 2);
    expect(gui.area(areaRect, &areaTwice) && !areaTwice, "a single click is not");

    // Text field: printable characters and Backspace typed since the last frame, up to the limit.
    std::string name = "ab";
    auto typed = [](unsigned int codepoint) {
        InputEvent e;
        e.type = InputType::Text;
        e.key = static_cast<int>(codepoint);
        return e;
    };
    InputEvent backspace;
    backspace.type = InputType::KeyDown;
    backspace.key = Key::BackSpace;
    expect(!gui.feed(typed('c')), "typing is not taken from the caller, who decides");
    gui.feed(backspace);
    gui.feed(backspace);
    gui.feed(typed('X'));
    gui.feed(typed(0x4e2d)); // a character the pixel font cannot draw is left out
    expect(gui.pending(), "typed input waits for the next frame");
    gui.beginFrame(640, 480, 2);
    expect(gui.textField({10, 130, 100, 20}, name, 4) && name == "aX", "c, two Backspaces, then X");
    gui.beginFrame(640, 480, 2);
    expect(!gui.textField({10, 130, 100, 20}, name, 4), "nothing typed, nothing changes");
    for (char c : std::string("12345"))
        gui.feed(typed(static_cast<unsigned char>(c)));
    gui.beginFrame(640, 480, 2);
    gui.textField({10, 130, 100, 20}, name, 4);
    expect(name == "aX12", "the text stops at its limit");
    for (int i = 0; i < 6; ++i)
        gui.feed(backspace);
    gui.beginFrame(640, 480, 2);
    gui.textField({10, 130, 100, 20}, name, 4);
    expect(name.empty(), "Backspace on empty text does nothing");

    std::printf("%s\n", failures == 0 ? "GuiCheck passed" : "GuiCheck FAILED");
    return failures == 0 ? 0 : 1;
}
