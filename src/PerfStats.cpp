/************************************************************************
     File:        PerfStats.cpp

     Comment:     See PerfStats.h.

*************************************************************************/

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // keep std::min / std::max usable
#endif
#include <windows.h>
// after windows.h, which it needs
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#include "PerfStats.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
PerfStats *g_active = nullptr;

struct StageInfo
{
    const char *name;
    bool part;
    bool gpu;
};

const StageInfo kInfo[PerfStats::kStages] = {
    {"Update", false, false},    {"Setup", false, true},     {"Shadow maps", false, true},
    {"Reflection", false, true}, {"Scene", false, true},     {"Post effects", false, true},
    {"Overlay", false, true},    {"Interface", false, true}, {"Swap / wait", false, false},
    {"Land", true, true},        {"Ocean", true, true},      {"Track", true, true},
    {"Supports", true, true},    {"Train", true, true},      {"Water & glass", true, true},
};

int index(PerfStage stage)
{
    return static_cast<int>(stage);
}
} // namespace

const char *PerfStats::name(PerfStage stage)
{
    const int i = index(stage);
    return i >= 0 && i < kStages ? kInfo[i].name : "?";
}

bool PerfStats::isPart(PerfStage stage)
{
    const int i = index(stage);
    return i >= 0 && i < kStages && kInfo[i].part;
}

bool PerfStats::onGpu(PerfStage stage)
{
    const int i = index(stage);
    return i >= 0 && i < kStages && kInfo[i].gpu;
}

double PerfStats::clock()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

PerfStats *PerfStats::active()
{
    return g_active;
}

void PerfStats::setActive(PerfStats *stats)
{
    g_active = stats;
}

void PerfStats::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    // a frame cut in half by switching is dropped
    if (!on && m_inFrame)
    {
        m_inFrame = false;
        m_current = nullptr;
        m_count = std::max(0, m_count - 1);
        m_next = (m_next + kHistory - 1) % kHistory;
    }
    m_depth.fill(0);
    m_between.fill(0.0);
}

void PerfStats::setGpuStamp(void (*stamp)(void *), void *data)
{
    m_stamp = stamp;
    m_stampData = data;
}

uint64_t PerfStats::beginFrame(double now)
{
    if (!m_enabled)
        return 0;
    if (m_inFrame)
        endFrame(now);
    Record &r = m_ring[static_cast<size_t>(m_next)];
    m_next = (m_next + 1) % kHistory;
    m_count = std::min(kHistory, m_count + 1);
    r.frame = Frame();
    r.frame.id = m_nextId++;
    r.frame.start = now;
    r.marks.clear();
    // Update, timed since the last frame, belongs to this one
    r.frame.cpu = m_between;
    m_between.fill(0.0);
    m_depth.fill(0);
    m_current = &r;
    m_inFrame = true;
    return r.frame.id;
}

void PerfStats::endFrame(double now)
{
    if (!m_inFrame || !m_current)
        return;
    // anything still open ends with the frame
    for (int i = 0; i < kStages; ++i)
        if (m_depth[static_cast<size_t>(i)] > 0)
        {
            m_depth[static_cast<size_t>(i)] = 1;
            end(static_cast<PerfStage>(i), now);
        }
    Frame &f = m_current->frame;
    f.cpuWork =
        std::max(0.0, (now - f.start) * 1000.0 - f.cpu[static_cast<size_t>(PerfStage::Swap)]);
    m_inFrame = false;
    m_current = nullptr;
}

void PerfStats::begin(PerfStage stage, double now)
{
    const int i = index(stage);
    if (!m_enabled || i < 0 || i >= kStages)
        return;
    const size_t k = static_cast<size_t>(i);
    if (m_depth[k]++ > 0)
        return;
    m_began[k] = now;
    if (m_inFrame && m_stamp && onGpu(stage))
    {
        m_current->marks.push_back({i, true});
        m_stamp(m_stampData);
    }
}

void PerfStats::end(PerfStage stage, double now)
{
    const int i = index(stage);
    if (!m_enabled || i < 0 || i >= kStages)
        return;
    const size_t k = static_cast<size_t>(i);
    if (m_depth[k] == 0 || --m_depth[k] > 0)
        return;
    const double ms = std::max(0.0, (now - m_began[k]) * 1000.0);
    if (!m_inFrame)
    {
        m_between[k] += ms;
        return;
    }
    m_current->frame.cpu[k] += ms;
    if (m_stamp && onGpu(stage))
    {
        m_current->marks.push_back({i, false});
        m_stamp(m_stampData);
    }
}

PerfStats::Record *PerfStats::find(uint64_t id)
{
    for (int n = 0; n < m_count; ++n)
    {
        Record &r = m_ring[static_cast<size_t>((m_next + kHistory - 1 - n) % kHistory)];
        if (r.frame.id == id)
            return &r;
    }
    return nullptr;
}

void PerfStats::gpuResults(uint64_t id, const std::vector<uint64_t> &nanoseconds)
{
    Record *r = find(id);
    if (!r || r->frame.gpuDone || nanoseconds.size() != r->marks.size())
        return;
    Frame &f = r->frame;
    std::array<uint64_t, kStages> began{};
    for (size_t n = 0; n < r->marks.size(); ++n)
    {
        const Mark &m = r->marks[n];
        const size_t k = static_cast<size_t>(m.stage);
        if (m.begin)
            began[k] = nanoseconds[n];
        else if (nanoseconds[n] >= began[k])
            f.gpu[k] += static_cast<double>(nanoseconds[n] - began[k]) / 1.0e6;
    }
    f.gpuWork = 0.0;
    for (int i = 0; i < kStages; ++i)
        if (onGpu(static_cast<PerfStage>(i)) && !isPart(static_cast<PerfStage>(i)))
            f.gpuWork += f.gpu[static_cast<size_t>(i)];
    f.gpuDone = true;
}

std::vector<const PerfStats::Frame *> PerfStats::frames() const
{
    std::vector<const Frame *> out;
    // the frame being drawn is not finished yet
    const int finished = m_inFrame ? m_count - 1 : m_count;
    const int skipNewest = m_inFrame ? 1 : 0;
    for (int n = finished - 1; n >= 0; --n)
        out.push_back(
            &m_ring[static_cast<size_t>((m_next + kHistory - 1 - n - skipNewest) % kHistory)]
                 .frame);
    return out;
}

PerfStats::Summary PerfStats::summary(double now, double seconds) const
{
    Summary s;
    std::vector<double> costs;
    for (const Frame *f : frames())
    {
        const double age = now - f->start;
        if (age <= 1.0)
            s.fps += 1.0;
        if (age > seconds)
            continue;
        ++s.frames;
        for (int i = 0; i < kStages; ++i)
            s.cpu[static_cast<size_t>(i)] += f->cpu[static_cast<size_t>(i)];
        s.avgCpu += f->cpuWork;
        if (f->gpuDone)
        {
            ++s.gpuFrames;
            for (int i = 0; i < kStages; ++i)
                s.gpu[static_cast<size_t>(i)] += f->gpu[static_cast<size_t>(i)];
            s.avgGpu += f->gpuWork;
        }
        costs.push_back(f->cost());
    }
    if (s.frames == 0)
        return s;
    for (double &v : s.cpu)
        v /= s.frames;
    s.avgCpu /= s.frames;
    if (s.gpuFrames > 0)
    {
        for (double &v : s.gpu)
            v /= s.gpuFrames;
        s.avgGpu /= s.gpuFrames;
    }
    std::sort(costs.begin(), costs.end());
    s.minCost = costs.front();
    s.maxCost = costs.back();
    double sum = 0.0;
    for (double c : costs)
        sum += c;
    s.avgCost = sum / static_cast<double>(costs.size());
    const size_t p95 = (costs.size() * 95 + 99) / 100;
    s.p95Cost = costs[std::min(costs.size() - 1, p95 > 0 ? p95 - 1 : 0)];
    return s;
}

void PerfStats::clear()
{
    m_count = 0;
    m_next = 0;
    m_inFrame = false;
    m_current = nullptr;
    m_depth.fill(0);
    m_between.fill(0.0);
}

std::string PerfStats::report(const Summary &s, const std::vector<std::string> &about)
{
    std::string out;
    char line[160];
    for (const std::string &a : about)
        out += a + "\n";
    std::snprintf(line, sizeof line, "frames measured: %d (graphics card times for %d)\n", s.frames,
                  s.gpuFrames);
    out += line;
    const double possible = s.avgCost > 0.0 ? 1000.0 / s.avgCost : 0.0;
    std::snprintf(
        line, sizeof line,
        "frame cost: avg %.2f ms, min %.2f, 95%% %.2f, max %.2f (could run at %.0f fps)\n",
        s.avgCost, s.minCost, s.p95Cost, s.maxCost, possible);
    out += line;
    std::snprintf(line, sizeof line, "processor %.2f ms, graphics card %.2f ms a frame\n", s.avgCpu,
                  s.avgGpu);
    out += line;
    out += "\nstage                 cpu ms   gpu ms\n";
    for (int i = 0; i < kStages; ++i)
    {
        const PerfStage stage = static_cast<PerfStage>(i);
        if (i == index(PerfStage::Land))
            out += "parts, over every pass:\n";
        if (onGpu(stage) && s.gpuFrames > 0)
            std::snprintf(line, sizeof line, "  %-18s %7.2f  %7.2f\n", name(stage),
                          s.cpu[static_cast<size_t>(i)], s.gpu[static_cast<size_t>(i)]);
        else
            std::snprintf(line, sizeof line, "  %-18s %7.2f        -\n", name(stage),
                          s.cpu[static_cast<size_t>(i)]);
        out += line;
    }
    // the biggest costs: the larger of each stage's two times
    std::vector<std::pair<double, int>> top;
    for (int i = 0; i < kStages; ++i)
    {
        const PerfStage stage = static_cast<PerfStage>(i);
        if (stage == PerfStage::Swap || stage == PerfStage::Update)
            continue;
        top.push_back({std::max(s.cpu[static_cast<size_t>(i)], s.gpu[static_cast<size_t>(i)]), i});
    }
    std::sort(top.begin(), top.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    out += "\nbiggest:\n";
    for (size_t n = 0; n < top.size() && n < 5; ++n)
    {
        const size_t k = static_cast<size_t>(top[n].second);
        const bool gpuBigger = s.gpu[k] > s.cpu[k];
        std::snprintf(line, sizeof line, "  %zu. %-18s %.2f ms (%s)\n", n + 1,
                      name(static_cast<PerfStage>(top[n].second)), top[n].first,
                      gpuBigger ? "graphics card" : "processor");
        out += line;
    }
    return out;
}

PerfStats::Memory PerfStats::memory()
{
    Memory m;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters)))
    {
        m.workingSet = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
        m.committed = static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
    }
#else
    if (FILE *f = std::fopen("/proc/self/status", "r"))
    {
        char line[256];
        while (std::fgets(line, sizeof line, f))
            if (std::strncmp(line, "VmRSS:", 6) == 0)
                m.workingSet = m.committed = std::atof(line + 6) / 1024.0;
        std::fclose(f);
    }
#endif
    return m;
}

std::string PerfStats::memoryLine(const Memory &m)
{
    if (m.workingSet < 0.0)
        return "";
    char buf[96];
    std::snprintf(buf, sizeof buf, "Mem: %.0f MB (committed %.0f MB)", m.workingSet, m.committed);
    return buf;
}
