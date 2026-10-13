#include "common.h"
#include <cstdarg>
#include <stdlib.h>
#include <stdio.h>
#include "tier0/logging.h"
#include "utlstring.h"
#include "buffer_string.h"
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
// and what the SDK headers call is stubbed, like LoggingSystem_LogAssert from asserts in debug builds.

LoggingResponse_t LoggingSystem_LogAssert(const char* pMessageFormat, ...)
{
	return LR_ABORT;
}

// The rest are referenced by SDK header code that is never called, like the convar type traits, which unoptimized builds
// still compile in. Reaching one means the dumper calls into tier0 through the SDK.

static void Tier0NotLinked(const char* pFunction)
{
	ExitError("%s is in tier0, which is not linked", pFunction);
}

#ifdef _MSC_VER
// The SDK declares its tier0 classes dllimport regardless of STATIC_TIER0
#pragma warning(disable : 4273)
#endif

bool CUtlString::operator==(const CUtlString&) const
{
	Tier0NotLinked(__func__);
	return false;
}

void CUtlString::Purge()
{
	Tier0NotLinked(__func__);
}

void CUtlString::Set(const char*)
{
	Tier0NotLinked(__func__);
}

void CUtlString::SetDirect(const char*, int)
{
	Tier0NotLinked(__func__);
}

const char* CBufferString::Insert(int, const char*, int, bool)
{
	Tier0NotLinked(__func__);
	return nullptr;
}

int CBufferString::Format(const char*, ...)
{
	Tier0NotLinked(__func__);
	return 0;
}

int V_tier0_strlen(const char*)
{
	Tier0NotLinked(__func__);
	return 0;
}

bool V_StringToBool(const char*, bool, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return false;
}

int16 V_StringToInt16(const char*, int16, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

uint16 V_StringToUint16(const char*, uint16, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

int32 V_StringToInt32(const char*, int32, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

uint32 V_StringToUint32(const char*, uint32, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

int64 V_StringToInt64(const char*, int64, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

uint64 V_StringToUint64(const char*, uint64, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

float32 V_StringToFloat32(const char*, float32, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

float64 V_StringToFloat64(const char*, float64, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
	return 0;
}

void V_StringToVector(const char*, Vector&, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
}

void V_StringToVector2D(const char*, Vector2D&, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
}

void V_StringToVector4D(const char*, Vector4D&, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
}

void V_StringToVectorWS(const char*, VectorWS&, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
}

void V_StringToColor(const char*, Color&, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
}

void V_StringToQAngle(const char*, QAngle&, bool*, char**, uint, IErrorListener*)
{
	Tier0NotLinked(__func__);
}
