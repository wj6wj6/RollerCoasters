// Force-included into every tests/linux source: the parts of the MSVC C
// runtime that the sources reach without an include of their own.
#pragma once

#include <stdio.h>

inline int fopen_s(FILE **file, const char *name, const char *mode)
{
    *file = fopen(name, mode);
    return *file ? 0 : 1;
}
