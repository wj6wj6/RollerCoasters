// Checks the performance numbers without OpenGL: a frame's phases and parts add up on the
// processor, a part drawn in several passes is added up, a stage begun again inside itself
// counts once, Update timed between frames goes to the next frame, the graphics card's
// timestamps are matched to the stamps in the order they were made (and a wrong count or a
// frame too old is ignored), the summary's frames a second, averages, minimum, maximum and 95th
// percentile, nothing is recorded while switched off, PerfScope reports to the active
// PerfStats, the --benchmark report names the biggest cost, and the F3 screen lays out inside
// the window at the smallest GUI size.
#include "PerfOverlay.h"
#include "PerfStats.h"
#include "Ui/Gui.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok)
        ++failures;
}

bool same(double a, double b)
{
    return std::fabs(a - b) < 1e-6;
}

// Counts the stamps, as PerfGpu would put queries in the card's stream.
int stamps = 0;
void countStamp(void *)
{
    ++stamps;
}

double ms(double milliseconds)
{
    return milliseconds / 1000.0;
}
} // namespace

int main()
{
    using S = PerfStage;
    // 600 frames of history: too big for the stack (/analyze C6262)
    const auto owned = std::make_unique<PerfStats>();
    PerfStats &perf = *owned;

    // Off: nothing at all.
    expect(perf.beginFrame(1.0) == 0, "switched off, no frame begins");
    perf.begin(S::Scene, 1.0);
    perf.end(S::Scene, 1.1);
    expect(perf.frames().empty(), "switched off, nothing is recorded");

    perf.setEnabled(true);
    perf.setGpuStamp(countStamp, nullptr);

    // Update between frames belongs to the next frame.
    perf.begin(S::Update, 0.0);
    perf.end(S::Update, ms(2));
    expect(!perf.inFrame() && stamps == 0, "Update outside a frame makes no stamps");

    // A frame: Setup 1 ms, Shadows 3 ms with the land in them for 2, Scene 5 ms with the land
    // again for 1, a nested Scene that must not count twice, then Swap 4 ms.
    const double t = 10.0;
    const uint64_t id = perf.beginFrame(t);
    expect(id != 0 && perf.inFrame() && perf.currentId() == id, "a frame begins");
    perf.begin(S::Setup, t);
    perf.end(S::Setup, t + ms(1));
    perf.begin(S::Shadows, t + ms(1));
    perf.begin(S::Land, t + ms(1));
    perf.end(S::Land, t + ms(3));
    perf.end(S::Shadows, t + ms(4));
    perf.begin(S::Scene, t + ms(4));
    perf.begin(S::Scene, t + ms(5)); // inside itself
    perf.begin(S::Land, t + ms(5));
    perf.end(S::Land, t + ms(6));
    perf.end(S::Scene, t + ms(8));
    perf.end(S::Scene, t + ms(9));
    perf.begin(S::Swap, t + ms(9));
    perf.end(S::Swap, t + ms(13));
    perf.endFrame(t + ms(13));

    std::vector<const PerfStats::Frame *> frames = perf.frames();
    expect(frames.size() == 1, "one frame recorded");
    const PerfStats::Frame &f = *frames.front();
    expect(same(f.cpu[static_cast<size_t>(S::Update)], 2.0), "Update goes to the next frame");
    expect(same(f.cpu[static_cast<size_t>(S::Shadows)], 3.0), "Shadows 3 ms");
    expect(same(f.cpu[static_cast<size_t>(S::Scene)], 5.0),
           "a stage begun inside itself counts once");
    expect(same(f.cpu[static_cast<size_t>(S::Land)], 3.0), "a part adds up over the passes");
    expect(same(f.cpuWork, 9.0), "the work is up to the swap, without it");
    expect(same(f.cost(), 9.0) && !f.gpuDone, "the cost is the processor's until the card answers");
    // Setup, Shadows, Land, Land, Scene: a begin and an end each; Swap has no card time.
    expect(stamps == 10, "a stamp at each begin and end of what the card draws");

    // The card's answers, in the order of the stamps (nanoseconds).
    std::vector<uint64_t> gpu = {0,          1'000'000,   // Setup 1 ms
                                 1'000'000,  2'000'000,   // Shadows begins, Land begins
                                 6'000'000,  12'000'000,  // Land 4 ms, Shadows 11 ms
                                 12'000'000, 13'000'000,  // Scene begins, Land begins
                                 14'000'000, 20'000'000}; // Land 1 ms, Scene 8 ms
    perf.gpuResults(id, std::vector<uint64_t>(gpu.begin(), gpu.end() - 1));
    expect(!perf.frames().front()->gpuDone, "a wrong number of answers is ignored");
    perf.gpuResults(id, gpu);
    const PerfStats::Frame &g = *perf.frames().front();
    expect(g.gpuDone && same(g.gpu[static_cast<size_t>(S::Shadows)], 11.0) &&
               same(g.gpu[static_cast<size_t>(S::Land)], 5.0) &&
               same(g.gpu[static_cast<size_t>(S::Scene)], 8.0),
           "the card's times are matched to the stamps");
    expect(same(g.gpuWork, 20.0) && same(g.cost(), 20.0),
           "the card's work is its phases, and the slower side is the cost");

    // More frames for the summary: costs 10, 20, 30 ms, a frame every 0.25 s.
    perf.clear();
    for (int n = 0; n < 3; ++n)
    {
        const double start = 100.0 + 0.25 * n;
        perf.beginFrame(start);
        perf.begin(S::Scene, start);
        perf.end(S::Scene, start + ms(10.0 * (n + 1)));
        perf.endFrame(start + ms(10.0 * (n + 1)));
    }
    PerfStats::Summary s = perf.summary(100.6);
    expect(s.frames == 3 && same(s.fps, 3.0), "three frames in the last second");
    expect(same(s.avgCost, 20.0) && same(s.minCost, 10.0) && same(s.maxCost, 30.0) &&
               same(s.p95Cost, 30.0),
           "average, minimum, maximum and 95th percentile");
    expect(same(s.cpu[static_cast<size_t>(S::Scene)], 20.0), "a stage's average a frame");
    s = perf.summary(101.2, 0.8); // the frames are 1.2, 0.95 and 0.7 s old
    expect(s.frames == 1 && same(s.fps, 2.0), "only the frames inside the time asked for");

    // A frame long gone is not found any more.
    const uint64_t old = perf.frames().front()->id;
    for (int n = 0; n < PerfStats::kHistory; ++n)
    {
        perf.beginFrame(200.0 + n);
        perf.endFrame(200.0 + n);
    }
    stamps = 0;
    perf.gpuResults(old, {});
    expect(perf.frames().size() == static_cast<size_t>(PerfStats::kHistory),
           "the history keeps its last frames");

    // PerfScope goes through the active PerfStats.
    perf.clear();
    PerfStats::setActive(&perf);
    perf.beginFrame(PerfStats::clock());
    {
        PerfScope scope(S::Interface);
    }
    perf.endFrame(PerfStats::clock());
    expect(perf.frames().size() == 1 &&
               perf.frames().front()->cpu[static_cast<size_t>(S::Interface)] >= 0.0,
           "PerfScope reports to the active PerfStats");
    perf.setEnabled(false);
    {
        PerfScope scope(S::Interface); // does nothing
    }
    PerfStats::setActive(nullptr);
    {
        PerfScope scope(S::Interface); // nothing active: does nothing
    }

    // The --benchmark report.
    PerfStats::Summary r;
    r.frames = 10;
    r.gpuFrames = 10;
    r.avgCost = 8.0;
    r.cpu[static_cast<size_t>(S::Scene)] = 2.0;
    r.gpu[static_cast<size_t>(S::Shadows)] = 5.0;
    const std::string text = PerfStats::report(r, {"about this run"});
    expect(text.find("about this run") == 0, "the report starts with what was measured");
    expect(text.find("1. Shadow maps") != std::string::npos &&
               text.find("graphics card") != std::string::npos,
           "the report names the biggest cost and where it is");
    expect(text.find("could run at 125 fps") != std::string::npos, "8 ms is 125 fps");

    // The F3 screen at the smallest GUI (320 x 240 units) stays inside the window.
    perf.setEnabled(true);
    perf.clear();
    for (int n = 0; n < 300; ++n)
    {
        perf.beginFrame(n / 60.0);
        perf.begin(S::Scene, n / 60.0);
        perf.end(S::Scene, n / 60.0 + ms(n % 50));
        perf.endFrame(n / 60.0 + ms(n % 50));
    }
    PerfInfo info;
    info.version = "RollerCoasters test";
    info.renderer = "A graphics card with a very long name that will not fit on one line at all";
    info.glVersion = "4.6";
    SceneSettings settings;
    info.lines = PerfOverlay::sceneLines(settings);
    expect(info.lines.size() == 3 && info.lines[0] == "Camera: World" &&
               info.lines[1].find("shadows") != std::string::npos,
           "the scene's lines name the camera and the switches");
    ui::Gui gui;
    gui.beginFrame(640, 480, 2);
    PerfOverlay::draw(gui, perf, info, 300 / 60.0);
    bool inside = !gui.quads().empty();
    for (const ui::Quad &q : gui.quads())
        inside = inside && q.x0 >= 0 && q.y0 >= 0 && q.x1 <= 640.0f && q.y1 <= 480.0f;
    expect(inside, "the F3 screen draws inside a 320 x 240 unit window");
    const std::vector<std::string> left =
        PerfOverlay::leftLines(perf.summary(300 / 60.0, 1.0), info, 640, 480);
    expect(left.size() > 5 && left[1].find("fps") != std::string::npos &&
               left[2].find("Frame cost") == 0,
           "the left column starts with the frame rate and the frame cost");

    // the program's memory: read on this system, and shown on the F3 screen
    const PerfStats::Memory memory = PerfStats::memory();
    // (a small test program may have committed less than a megabyte)
    std::printf("memory: working set %.2f MB, committed %.2f MB\n", memory.workingSet,
                memory.committed);
    expect(memory.workingSet > 0.0 && memory.committed > 0.0, "the program's memory is read");
    PerfStats::Memory known;
    known.workingSet = 312.4;
    known.committed = 340.0;
    expect(PerfStats::memoryLine(known) == "Mem: 312 MB (committed 340 MB)", "the memory line");
    expect(PerfStats::memoryLine(PerfStats::Memory()).empty(), "no line when unknown");
    info.memory = PerfStats::memoryLine(known);
    bool shown = false;
    for (const std::string &l :
         PerfOverlay::leftLines(perf.summary(300 / 60.0, 1.0), info, 640, 480))
        shown = shown || l == info.memory;
    expect(shown, "F3 shows the memory");

    std::printf("%s\n", failures == 0 ? "PerfStatsCheck passed" : "PerfStatsCheck FAILED");
    return failures == 0 ? 0 : 1;
}
