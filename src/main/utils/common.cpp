#include <cstdarg>
#include <stdlib.h>
#include <stdio.h>
#include "threadtools.h"

void ExitError(const char* pMsg, ...)
{
	static char szBuffer[2048];
	va_list params;
	va_start(params, pMsg);
	vsnprintf(szBuffer, sizeof(szBuffer), pMsg, params);
	va_end(params);
	printf("%s", szBuffer);

	exit(1);
}

void CThreadSpinRWLock::LockForRead(const char* pFileName, int nLine)
{
	// STUB
}

void CThreadSpinRWLock::UnlockRead(const char* pFileName, int nLine)
{
	// STUB
}


LoggingResponse_t LoggingSystem_LogAssert(const char* pMessageFormat, ...)
{
	return LR_ABORT;
}