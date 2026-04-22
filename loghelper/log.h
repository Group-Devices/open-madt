#ifndef MADT_LOGHELPER_LOG_H
#define MADT_LOGHELPER_LOG_H

#include <cstddef>

#define LOGHELPER_FATAL    -3
#define LOGHELPER_ALERT    -3
#define LOGHELPER_CRITICAL -3
#define LOGHELPER_ERROR    -2
#define LOGHELPER_WARNING  -1
#define LOGHELPER_NOTICE   0
#define LOGHELPER_INFO     0
#define LOGHELPER_DEBUG    1
#define LOGHELPER_TRACE    2

#define xstr(s) zstr(s)
#define zstr(s) #s

#ifndef MODULE_NAME
#define MODULE_NAME unknown
#endif

extern "C" void cwrap_log(int verbosity, const char* file, unsigned int line, const char* fmt, ...);
extern "C" void
DumpHex(int verbosity, const char* file, unsigned int line, const void* data, unsigned int size);

#define MADT_LOG_LEVEL_DEBUG 0
#define MADT_LOG_LEVEL_INFO 1
#define MADT_LOG_LEVEL_WARN 2
#define MADT_LOG_LEVEL_ERROR 3

#ifndef MADT_LOG_LEVEL
#define MADT_LOG_LEVEL MADT_LOG_LEVEL_INFO
#endif

#if MADT_LOG_LEVEL <= MADT_LOG_LEVEL_DEBUG
#define DLOG(...) cwrap_log(LOGHELPER_DEBUG, __FILE__ "@" xstr(MODULE_NAME), __LINE__, __VA_ARGS__)
#else
#define DLOG(...) ((void)0)
#endif

#if MADT_LOG_LEVEL <= MADT_LOG_LEVEL_DEBUG
#define TLOG(...) cwrap_log(LOGHELPER_TRACE, __FILE__ "@" xstr(MODULE_NAME), __LINE__, __VA_ARGS__)
#else
#define TLOG(...) ((void)0)
#endif

#if MADT_LOG_LEVEL <= MADT_LOG_LEVEL_INFO
#define ILOG(...) cwrap_log(LOGHELPER_INFO, __FILE__ "@" xstr(MODULE_NAME), __LINE__, __VA_ARGS__)
#else
#define ILOG(...) ((void)0)
#endif

#if MADT_LOG_LEVEL <= MADT_LOG_LEVEL_WARN
#define WLOG(...) cwrap_log(LOGHELPER_WARNING, __FILE__ "@" xstr(MODULE_NAME), __LINE__, __VA_ARGS__)
#else
#define WLOG(...) ((void)0)
#endif

#if MADT_LOG_LEVEL <= MADT_LOG_LEVEL_ERROR
#define ELOG(...) cwrap_log(LOGHELPER_ERROR, __FILE__ "@" xstr(MODULE_NAME), __LINE__, __VA_ARGS__)
#else
#define ELOG(...) ((void)0)
#endif

#endif
