// Minimal stand-ins for the engine services interpreter.cc links against, so
// interpreter_fatal_test stays independent of the rest of the engine. No file
// can be opened here: every script is "missing".

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "db.h"
#include "debug.h"
#include "memory_manager.h"

namespace fallout {

int debugPrint(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int rc = vfprintf(stderr, format, args);
    va_end(args);
    return rc;
}

File* fileOpen(const char* /* filename */, const char* /* mode */)
{
    return nullptr;
}

int fileClose(File* /* stream */)
{
    return 0;
}

int fileGetSize(File* /* stream */)
{
    return 0;
}

size_t fileRead(void* /* buf */, size_t /* size */, size_t /* count */, File* /* stream */)
{
    return 0;
}

void* internal_malloc_safe(size_t size, const char* /* file */, int /* line */)
{
    return malloc(size);
}

} // namespace fallout
