#include "log.h"

#include <cstdarg>
#include <cstdio>

namespace {

const char* levelName(int verbosity)
{
	switch (verbosity) {
		case LOGHELPER_FATAL:
			return "FATAL";
		case LOGHELPER_ERROR:
			return "ERROR";
		case LOGHELPER_WARNING:
			return "WARN";
		case LOGHELPER_INFO:
			return "INFO";
		case LOGHELPER_DEBUG:
			return "DEBUG";
		case LOGHELPER_TRACE:
			return "TRACE";
		default:
			return "LOG";
	}
}

} // namespace

extern "C" void cwrap_log(int verbosity, const char* file, unsigned int line, const char* fmt, ...)
{
	std::fprintf(stderr, "[%s] %s:%u ", levelName(verbosity), file != nullptr ? file : "unknown", line);

	va_list args;
	va_start(args, fmt);
	std::vfprintf(stderr, fmt, args);
	va_end(args);

	std::fputc('\n', stderr);
}

extern "C" void
DumpHex(int verbosity, const char* file, unsigned int line, const void* data, unsigned int size)
{
	const auto* bytes = static_cast<const unsigned char*>(data);
	if (bytes == nullptr) {
		cwrap_log(verbosity, file, line, "DumpHex called with null data");
		return;
	}

	for (unsigned int offset = 0; offset < size; offset += 16) {
		char hexbuf[16 * 3 + 1] = { 0 };
		for (unsigned int i = 0; i < 16 && offset + i < size; ++i) {
			std::snprintf(&hexbuf[i * 3], 4, "%02X ", bytes[offset + i]);
		}
		cwrap_log(verbosity, file, line, "%s", hexbuf);
	}
}
