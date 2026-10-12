#include "common.h"
#include <cstdarg>
#include <stdlib.h>
#include <stdio.h>
#include "tier0/logging.h"
#include <spdlog/spdlog.h>

const char* SimpleCUtlString::Get() {
	return m_pString;
}

void ExitError(const char* pMsg, ...)
{
	static char szBuffer[2048];
	va_list params;
	va_start(params, pMsg);
	vsnprintf(szBuffer, sizeof(szBuffer), pMsg, params);
	va_end(params);
	spdlog::critical("{}", szBuffer);

	exit(1);
}

// tier0 is not linked: the SDK's declarations of it go out of date when the game updates, and calling them would crash
// instead of failing on a validated read. So memory the game allocates is never freed, as that needs tier0's allocator,
// and what the SDK code calls is stubbed: Plat_ExitProcess from interfaces.cpp, LoggingSystem_LogAssert from asserts in debug builds.

void Plat_ExitProcess(int) {
	// STUB
}

LoggingResponse_t LoggingSystem_LogAssert(const char* pMessageFormat, ...)
{
	return LR_ABORT;
}
