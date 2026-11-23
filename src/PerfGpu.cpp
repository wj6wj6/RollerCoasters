/************************************************************************
     File:        PerfGpu.cpp

     Comment:     See PerfGpu.h.

*************************************************************************/

#include "PerfGpu.h"
#include "PerfStats.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <glad/glad.h>

bool PerfGpu::available()
{
    return GLAD_GL_VERSION_3_3 && glQueryCounter && glGetQueryObjectui64v && glGenQueries;
}

void PerfGpu::attach(PerfStats *stats)
{
    if (stats)
        stats->setGpuStamp(stampCB, this);
}

void PerfGpu::stampCB(void *self)
{
    static_cast<PerfGpu *>(self)->stamp();
}

void PerfGpu::beginFrame(PerfStats &stats, uint64_t id)
{
    if (id == 0)
        return;
    Set &set = m_sets[id % kSets];
    // the frame that used this set last: its answers, if they are in
    if (set.frame != 0 && set.used > 0)
    {
        GLint ready = 0;
        glGetQueryObjectiv(set.queries[static_cast<size_t>(set.used - 1)],
                           GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready)
        {
            std::vector<uint64_t> times(static_cast<size_t>(set.used));
            for (int i = 0; i < set.used; ++i)
            {
                GLuint64 t = 0;
                glGetQueryObjectui64v(set.queries[static_cast<size_t>(i)], GL_QUERY_RESULT, &t);
                times[static_cast<size_t>(i)] = t;
            }
            stats.gpuResults(set.frame, times);
        }
    }
    set.frame = id;
    set.used = 0;
    m_current = static_cast<int>(id % kSets);
}

void PerfGpu::stamp()
{
    Set &set = m_sets[m_current];
    // too many: the count no longer matches the frame's, which drops its times
    if (set.used >= kMaxStamps)
        return;
    if (set.used >= static_cast<int>(set.queries.size()))
    {
        // grow by a batch: a frame's stamps are about the same every frame
        const size_t had = set.queries.size();
        set.queries.resize(had + 32);
        glGenQueries(32, &set.queries[had]);
    }
    glQueryCounter(set.queries[static_cast<size_t>(set.used++)], GL_TIMESTAMP);
}

void PerfGpu::forget()
{
    for (Set &set : m_sets)
        set = Set();
}

void PerfGpu::release()
{
    for (Set &set : m_sets)
        if (!set.queries.empty())
            glDeleteQueries(static_cast<GLsizei>(set.queries.size()), set.queries.data());
    forget();
}
