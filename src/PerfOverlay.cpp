/************************************************************************
     File:        PerfOverlay.cpp

     Comment:     See PerfOverlay.h.

*************************************************************************/

#include "PerfOverlay.h"

#include <algorithm>
#include <cstdio>

#include "Ui/PixelFont.h"

namespace
{
using ui::Color;
using ui::Rect;

const Color kStrip = {80, 80, 80, 144}; // a block game's debug text background
const Color kGreen = {90, 220, 90, 255};
const Color kAmber = {240, 200, 60, 255};
const Color kRed = {235, 80, 70, 255};
const int kLine = 9; // units from one line to the next
// The table's columns: name, processor ms, graphics card ms, share bar.
const int kNameW = 74, kNumW = 28, kBarW = 28;
const int kTableW = kNameW + kNumW * 2 + kBarW + 6;

// Each stage's colour in the table's bars, as a profiler's pie.
const Color kStageColor[PerfStats::kStages] = {
    {150, 150, 150, 255}, // Update
    {120, 160, 220, 255}, // Setup
    {150, 90, 200, 255},  // Shadow maps
    {80, 190, 220, 255},  // Reflection
    {90, 200, 110, 255},  // Scene
    {230, 150, 60, 255},  // Post effects
    {220, 220, 100, 255}, // Overlay
    {230, 110, 160, 255}, // Interface
    {110, 110, 110, 255}, // Swap / wait
    {120, 190, 80, 255},  // Land
    {60, 120, 210, 255},  // Ocean
    {190, 150, 110, 255}, // Track
    {160, 120, 80, 255},  // Supports
    {220, 80, 70, 255},   // Train
    {120, 200, 230, 255}, // Water & glass
};

std::string format(const char *pattern, double a)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, pattern, a);
    return buf;
}

std::string ms(double value)
{
    return format(value >= 100.0 ? "%.0f" : "%.1f", value);
}

void line(ui::Gui &gui, int x, int y, const std::string &text, Color c, bool right)
{
    const int w = ui::PixelFont::textWidth(text);
    const int left = right ? x - w - 1 : x;
    gui.fill({left, y - 1, w + 2, kLine}, kStrip);
    gui.text(left + 1, y, text, c, ui::Align::Left, true);
}

Color costColor(double cost)
{
    if (cost <= 1000.0 / 60.0 + 0.05)
        return kGreen;
    if (cost <= 1000.0 / 30.0 + 0.05)
        return kAmber;
    return kRed;
}

// Cuts text to fit `width` units, with ".." at the end.
std::string fit(std::string text, int width)
{
    if (ui::PixelFont::textWidth(text) <= width)
        return text;
    while (!text.empty() && ui::PixelFont::textWidth(text + "..") > width)
        text.pop_back();
    return text + "..";
}

void drawTable(ui::Gui &gui, const PerfStats::Summary &s, bool gpuTimer)
{
    const int right = gui.width() - 2;
    const int nameW = kNameW, numW = kNumW, barW = kBarW;
    const int width = kTableW;
    const int x = right - width;
    int y = 2;
    const double frame = std::max(0.001, s.avgCost);

    gui.fill({x, y - 1, width + 1, kLine}, kStrip);
    gui.text(x + 1, y, "Stage", ui::kWhite);
    gui.text(x + nameW + numW, y, "CPU", ui::kGrey, ui::Align::Right);
    gui.text(x + nameW + numW * 2, y, "GPU", ui::kGrey, ui::Align::Right);
    gui.text(right, y, "share", ui::kGrey, ui::Align::Right);
    y += kLine;
    for (int i = 0; i < PerfStats::kStages; ++i)
    {
        const PerfStage stage = static_cast<PerfStage>(i);
        if (stage == PerfStage::Land)
        {
            gui.fill({x, y - 1, width + 1, kLine}, kStrip);
            gui.text(x + 1, y, "Parts, every pass", ui::kGrey);
            y += kLine;
        }
        const size_t k = static_cast<size_t>(i);
        const bool gpu = gpuTimer && PerfStats::onGpu(stage) && s.gpuFrames > 0;
        const double biggest = std::max(s.cpu[k], gpu ? s.gpu[k] : 0.0);
        gui.fill({x, y - 1, width + 1, kLine}, kStrip);
        gui.fill({x + 1, y + 1, 5, 5}, kStageColor[k]);
        gui.text(x + 8, y, PerfStats::name(stage), ui::kWhite);
        gui.text(x + nameW + numW, y, ms(s.cpu[k]), ui::kWhite, ui::Align::Right);
        gui.text(x + nameW + numW * 2, y, gpu ? ms(s.gpu[k]) : "-", gpu ? ui::kWhite : ui::kGrey,
                 ui::Align::Right);
        // the share of the frame's cost, at most the whole bar
        const int bar = static_cast<int>(std::min(1.0, biggest / frame) * (barW - 2) + 0.5);
        gui.fill({right - barW + 1, y + 1, barW - 2, 5}, {0, 0, 0, 90});
        if (bar > 0 && stage != PerfStage::Swap)
            gui.fill({right - barW + 1, y + 1, bar, 5}, kStageColor[k]);
        y += kLine;
    }
}

void drawGraph(ui::Gui &gui, const PerfStats &stats)
{
    const std::vector<const PerfStats::Frame *> frames = stats.frames();
    // as wide as fits left of the hotbar (182 units in the middle)
    const int room = (gui.width() - 182) / 2 - 6;
    const int width = std::min(PerfStats::kHistory, room);
    const int height = 50;
    if (width < 40 || gui.height() < height + 80)
        return;
    const int x = 2;
    const int bottom = gui.height() - 2;
    const int count = std::min(width, static_cast<int>(frames.size()));
    // the top of the graph: 50 ms, or 100, 200, 400 when frames take longer
    double slowest = 0.0;
    for (int n = 0; n < count; ++n)
        slowest = std::max(slowest, frames[frames.size() - static_cast<size_t>(n + 1)]->cost());
    double top = 50.0;
    while (top < slowest && top < 400.0)
        top *= 2.0;
    auto heightOf = [&](double cost) {
        return std::max(1, std::min(height, static_cast<int>(cost * height / top + 0.5)));
    };
    gui.fill({x, bottom - height, width, height}, {0, 0, 0, 100});
    for (int n = 0; n < count; ++n)
    {
        const PerfStats::Frame *f = frames[frames.size() - static_cast<size_t>(count - n)];
        const double cost = f->cost();
        const int h = heightOf(cost);
        gui.fill({x + width - count + n, bottom - h, 1, h}, costColor(cost));
    }
    // 60 and 30 frames a second
    const int at60 = bottom - heightOf(1000.0 / 60.0);
    const int at30 = bottom - heightOf(1000.0 / 30.0);
    gui.fill({x, at60, width, 1}, {255, 255, 255, 110});
    gui.fill({x, at30, width, 1}, {255, 255, 255, 110});
    if (at60 - at30 >= 9)
        gui.text(x + 1, at60 - 8, "60 FPS", ui::kWhite);
    gui.text(x + 1, at30 - 8, "30 FPS", ui::kWhite);
    gui.text(x + 1, bottom - height - 9, format("Frame cost (ms), top %.0f", top), ui::kWhite);
}
} // namespace

std::vector<std::string> PerfOverlay::sceneLines(const SceneSettings &s)
{
    static const char *const kCameras[] = {"World", "Train", "Chase", "Top", "Fly"};
    const int camera = static_cast<int>(s.camera);
    std::vector<std::string> out;
    out.push_back(std::string("Camera: ") + (camera >= 0 && camera < 5 ? kCameras[camera] : "?"));
    std::string light = "Light+: off";
    if (s.lighting)
    {
        light = "Light+: on";
        std::string with;
        auto add = [&with](bool on, const char *name) {
            if (on)
                with += (with.empty() ? "" : ", ") + std::string(name);
        };
        add(s.shadows, "shadows");
        add(s.water, "water");
        add(s.bloom, "bloom");
        add(s.ssao, "SSAO");
        add(s.rain, "rain");
        if (!with.empty())
            light += " (" + with + ")";
    }
    out.push_back(light);
    out.push_back(std::string("World: ") + (s.scenery ? "island" : "flat grass") +
                  (s.run ? ", train running" : ", train stopped"));
    return out;
}

std::vector<std::string> PerfOverlay::leftLines(const PerfStats::Summary &s, const PerfInfo &info,
                                                int pixelW, int pixelH)
{
    std::vector<std::string> out;
    out.push_back(info.version);
    char buf[160];
    std::snprintf(buf, sizeof buf, "%.0f fps (T: %s, train %.0f/s)", s.fps,
                  info.vsync ? "vsync" : "no vsync", info.trainSteps);
    out.push_back(buf);
    if (s.frames > 0)
    {
        const double possible = s.avgCost > 0.0 ? 1000.0 / s.avgCost : 0.0;
        std::snprintf(buf, sizeof buf, "Frame cost %.1f ms (could run %.0f fps)", s.avgCost,
                      possible);
        out.push_back(buf);
        if (info.gpuTimer && s.gpuFrames > 0)
            std::snprintf(buf, sizeof buf, "CPU %.1f ms  GPU %.1f ms", s.avgCpu, s.avgGpu);
        else
            std::snprintf(buf, sizeof buf, "CPU %.1f ms  GPU: no timer", s.avgCpu);
        out.push_back(buf);
        std::snprintf(buf, sizeof buf, "min %.1f  avg %.1f  max %.1f ms (%d frames)", s.minCost,
                      s.avgCost, s.maxCost, s.frames);
        out.push_back(buf);
    }
    else
        out.push_back("Measuring...");
    out.push_back("");
    std::snprintf(buf, sizeof buf, "Window %dx%d", pixelW, pixelH);
    out.push_back(buf);
    if (!info.memory.empty())
        out.push_back(info.memory);
    for (const std::string &l : info.lines)
        out.push_back(l);
    out.push_back("");
    if (!info.renderer.empty())
        out.push_back(info.renderer);
    if (!info.glVersion.empty())
        out.push_back("OpenGL " + info.glVersion);
    return out;
}

void PerfOverlay::draw(ui::Gui &gui, const PerfStats &stats, const PerfInfo &info, double now)
{
    // the last second's frames; when the view stood still, whatever there is
    PerfStats::Summary s = stats.summary(now, 1.0);
    if (s.frames == 0)
        s = stats.summary(now);
    s.fps = stats.summary(now, 1.0).fps;

    const int leftW = std::max(80, gui.width() - kTableW - 12);
    const std::vector<std::string> lines = leftLines(s, info, gui.pixelWidth(), gui.pixelHeight());
    int y = 2;
    for (size_t i = 0; i < lines.size(); ++i, y += kLine)
    {
        if (lines[i].empty())
            continue;
        Color c = ui::kWhite;
        if (i == 2 && s.frames > 0)
            c = costColor(s.avgCost);
        line(gui, 2, y, fit(lines[i], leftW), c, false);
    }
    drawTable(gui, s, info.gpuTimer);
    drawGraph(gui, stats);
}
