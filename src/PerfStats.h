/************************************************************************
     File:        PerfStats.h

     Comment:     Where each frame's time goes, for the performance screen
                  (F3, as a block game's debug screen; PerfOverlay.h) and
                  for --benchmark (AppControl.h).

                  A frame is split into phases that follow one another
                  (Setup, Shadow maps, Reflection, Scene, Post effects,
                  Overlay, Interface, then Swap, where the driver waits
                  for the screen), and into parts that are drawn in
                  several phases (the land is drawn into the shadow maps,
                  the reflection and the scene): a part's time is added
                  up over the whole frame. Update is the train's step and
                  the timers between frames.

                  Every phase and part is timed on the processor (a steady
                  clock) and, when an OpenGL timer is attached, on the
                  graphics card: a timestamp at each begin and end, read
                  back a few frames later so nothing waits for it
                  (PerfGpu.h). This file has no OpenGL; it gets the
                  timestamps back as numbers (gpuResults), so
                  tests/PerfStatsCheck.cpp checks it on its own.

                  PerfScope times one phase or part of the frame being
                  drawn, through PerfStats::active(). With no active
                  PerfStats, or one switched off, it does nothing.

*************************************************************************/
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class PerfStage
{
    // between frames
    Update,
    // the phases of a frame, in order
    Setup,
    Shadows,
    Reflection,
    Scene,
    Post,
    Overlay,
    Interface,
    Swap,
    // parts, added up over the phases they are drawn in
    Land,
    Ocean,
    Track,
    Supports,
    Train,
    Water,
    Count
};

class PerfStats
{
  public:
    static constexpr int kStages = static_cast<int>(PerfStage::Count);
    static constexpr int kHistory = 600; // frames kept: the graph, --benchmark's 600 at most

    static const char *name(PerfStage stage);
    static bool isPart(PerfStage stage); // drawn in several phases
    static bool onGpu(PerfStage stage);  // has a time on the graphics card too
    static double clock();               // seconds, a steady clock
    static PerfStats *active();          // the one PerfScope reports to
    static void setActive(PerfStats *stats);

    struct Frame
    {
        uint64_t id = 0;
        double start = 0.0;                // clock() when it began
        std::array<double, kStages> cpu{}; // milliseconds
        std::array<double, kStages> gpu{}; // milliseconds, once read back
        bool gpuDone = false;              // the graphics card's times came back
        double cpuWork = 0.0;              // ms from the start to the swap
        double gpuWork = 0.0;              // ms, the phases added up
        // The time the frame costs: the processor or the graphics card,
        // whichever took longer, without waiting for the screen.
        double cost() const
        {
            return cpuWork > gpuWork ? cpuWork : gpuWork;
        }
    };

    // Averages over the last frames (at most kHistory, at most `seconds` old).
    struct Summary
    {
        int frames = 0;
        double fps = 0.0; // frames drawn in the last second
        double avgCost = 0.0, minCost = 0.0, maxCost = 0.0, p95Cost = 0.0; // ms
        double avgCpu = 0.0, avgGpu = 0.0;                                 // ms
        std::array<double, kStages> cpu{};                                 // ms per frame
        std::array<double, kStages> gpu{};                                 // ms per frame
        int gpuFrames = 0; // frames whose graphics card times came back
    };

    // Off: begin() and end() do nothing, and no frame is recorded.
    void setEnabled(bool on);
    bool enabled() const
    {
        return m_enabled;
    }

    // A function that puts a timestamp into the graphics card's command
    // stream (PerfGpu::stamp); nullptr for processor times only.
    void setGpuStamp(void (*stamp)(void *), void *data);

    // A frame: begin it before drawing, end it after the swap. Its number
    // is what gpuResults() is given back.
    uint64_t beginFrame(double now);
    void endFrame(double now);
    bool inFrame() const
    {
        return m_inFrame;
    }
    uint64_t currentId() const // the frame being drawn, 0 outside one
    {
        return m_inFrame && m_current ? m_current->frame.id : 0;
    }

    // A phase or part starts or ends (now from clock()). Outside a frame
    // (Update) the time goes to the next frame. A stage begun again before
    // it ended is counted once.
    void begin(PerfStage stage, double now);
    void end(PerfStage stage, double now);

    // The graphics card's timestamps (nanoseconds) for frame `id`, one for
    // each stamp the frame made, in order. Too old a frame is ignored.
    void gpuResults(uint64_t id, const std::vector<uint64_t> &nanoseconds);

    // The recorded frames, oldest first.
    std::vector<const Frame *> frames() const;
    Summary summary(double now, double seconds = 1e9) const;
    void clear();

    // Text for --benchmark: the averages, the phases and parts with both
    // times, and the five biggest costs. `about` lines go at the top.
    static std::string report(const Summary &s, const std::vector<std::string> &about);

  private:
    struct Mark
    {
        int stage;
        bool begin;
    };
    struct Record
    {
        Frame frame;
        std::vector<Mark> marks; // the order the stamps were made in
    };
    Record *find(uint64_t id);

    bool m_enabled = false;
    void (*m_stamp)(void *) = nullptr;
    void *m_stampData = nullptr;
    std::array<Record, kHistory> m_ring{};
    int m_count = 0; // records in use
    int m_next = 0;  // where the next frame goes
    uint64_t m_nextId = 1;
    bool m_inFrame = false;
    Record *m_current = nullptr;
    std::array<int, kStages> m_depth{};
    std::array<double, kStages> m_began{};
    std::array<double, kStages> m_between{}; // ms timed outside a frame
};

// Times one phase or part of a frame, while it is in scope.
class PerfScope
{
  public:
    explicit PerfScope(PerfStage stage) : m_stage(stage), m_stats(PerfStats::active())
    {
        if (m_stats && m_stats->enabled())
            m_stats->begin(m_stage, PerfStats::clock());
        else
            m_stats = nullptr;
    }
    ~PerfScope()
    {
        if (m_stats)
            m_stats->end(m_stage, PerfStats::clock());
    }
    PerfScope(const PerfScope &) = delete;
    PerfScope &operator=(const PerfScope &) = delete;

  private:
    PerfStage m_stage;
    PerfStats *m_stats;
};
