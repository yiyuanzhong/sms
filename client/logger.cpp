extern "C" {
#include "logger.h"
} // extern "C"

#include <unistd.h>

#include <flinter/logger.h>

int logger_initialize(const char *filename)
{
    if (isatty(STDIN_FILENO)) {
        return 0;
    }

    flinter::Logger::SetColorful(false);
    return !flinter::Logger::ProcessAttach(
            filename, flinter::Logger::kLevelTrace);
}

int logger_shutdown(void)
{
    flinter::Logger::ProcessDetach();
    return 0;
}

#define L(n,l) \
int logger_##n(const char *file, int line, const char *fmt, ...) \
{ \
    va_list ap; \
    va_start(ap, fmt); \
    bool ret = flinter::CLogger::VLog(flinter::CLogger::kLevel##l, file, line, fmt, ap); \
    va_end(ap); \
    return ret ? 0 : -1; \
}

L(fatal, Fatal);
L(error, Error);
L(warn , Warn );
L(info , Info );
L(debug, Debug);
L(trace, Trace);
