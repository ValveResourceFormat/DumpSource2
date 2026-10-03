#include "common.h"
#include <cstdarg>
#include <stdlib.h>
#include <stdio.h>
#include "tier0/logging.h"
#include "tier0/memalloc.h"
#include "utlstring.h"
#include "bufferstring.h"
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

// Stubs for tier0 functions, which is only linked on Windows (for memoverride.cpp).
// The SDK interfaces library calls Plat_ExitProcess, the rest are called from SDK headers in debug builds.
#ifndef _WIN32

// Set to tier0's when it's loaded, the SDK's containers free memory the game allocates with it
IMemAlloc* g_pMemAlloc = nullptr;

void Plat_ExitProcess(int) {
	// STUB
}

LoggingResponse_t LoggingSystem_LogAssert(const char* pMessageFormat, ...)
{
	return LR_ABORT;
}

bool CUtlString::operator==(const CUtlString& src) const {
	// STUB
	return false;
}

const char* CBufferString::Insert(int, char const *, int, bool) {
	// STUB
	return nullptr;
}

int CBufferString::Format(char const*, ...) {
	// STUB
	return 0;
}

void CUtlString::Purge() {
	// STUB
}

void CUtlString::Set(const char*) {
	// STUB
}

void CUtlString::SetDirect(const char*, int) {
	// STUB
}

bool V_StringToBool(const char*, bool, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
	return false;
}

void V_StringToColor(const char*, Color&, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
}

float32 V_StringToFloat32(const char*, float32, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
	return 0;
}

float64 V_StringToFloat64(const char*, float64, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
	return 0;
}

int16 V_StringToInt16(const char*, int16, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
	return 0;
}

int32 V_StringToInt32(const char*, int32, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
	return 0;
}

int64 V_StringToInt64(const char*, int64, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
	return 0;
}

void V_StringToQAngle(const char*, QAngle&, bool*, char**, uint, IParsingErrorListener*) {
	// STUB
}

uint16 V_StringToUint16(const char*, uint16, bool*, char**, uint, IParsingErrorListener*) {
	// STUB
	return 0;
}

uint32 V_StringToUint32(const char*, uint32, bool*, char**, uint, IParsingErrorListener*) {
	// STUB
	return 0;
}

uint64 V_StringToUint64(const char*, uint64, bool*, char**, uint, IParsingErrorListener*) {
	// STUB
	return 0;
}

void V_StringToVector(const char*, Vector&, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
}

void V_StringToVector2D(const char*, Vector2D&, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
}

void V_StringToVector4D(const char*, Vector4D&, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
}

void V_StringToVectorWS(const char*, VectorWS&, bool*, char**, uint, IParsingErrorListener*)
{
	// STUB
}

int V_tier0_strlen(const char*) {
	// STUB
	return 0;
}

#endif
