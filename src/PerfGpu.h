/************************************************************************
     File:        PerfGpu.h

     Comment:     The graphics card's side of PerfStats: an OpenGL
                  timestamp query (glQueryCounter, OpenGL 3.3) at every
                  stamp, in four rotating sets so a frame's answers are
                  read three frames later, when they are ready, instead of
                  making the processor wait for the card. A set that is
                  still not ready is skipped: that frame shows processor
                  times only.

                  The queries belong to the OpenGL context: forget() when
                  the context is gone (TrainView's m_contextValid),
                  release() while it is still current.

*************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

class PerfStats;

class PerfGpu
{
  public:
    // Does this OpenGL have timestamp queries? (needs gladLoadGL first)
    static bool available();

    // Becomes stats' stamp function.
    void attach(PerfStats *stats);

    // After PerfStats::beginFrame() gave the frame number id (0: none):
    // reads the set this frame will reuse, hands its times to stats, and
    // makes the frame's stamps go into that set.
    void beginFrame(PerfStats &stats, uint64_t id);

    void forget();  // the context is gone: the names mean nothing now
    void release(); // delete the queries (context current)

  private:
    static void stampCB(void *self);
    void stamp();

    struct Set
    {
        std::vector<unsigned> queries;
        int used = 0;
        uint64_t frame = 0; // 0: nothing waiting
    };
    static constexpr int kSets = 4;
    static constexpr int kMaxStamps = 512; // a frame's stamps beyond this are not made
    Set m_sets[kSets];
    int m_current = 0;
};
