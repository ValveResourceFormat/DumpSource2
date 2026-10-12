#pragma once
#ifdef _WIN32
#define PLATFORM_FOLDER "win64"
#else
#define PLATFORM_FOLDER "linuxsteamrt64"
#endif

void ExitError(const char* pMsg, ...);

class SimpleCUtlString
{
public:
	const char* Get();

private:
	const char* m_pString = nullptr;
};

// The layout of the SDK's CUtlVector, for the game to fill. Like SimpleCUtlString, it never frees what the game allocates.
template <typename T>
class SimpleCUtlVector
{
public:
	int Count() const { return m_Size; }
	const T& operator[](int i) const { return m_pMemory[i]; }

private:
	int m_Size = 0;
	T* m_pMemory = nullptr;
	int m_nAllocationCount = 0;
	int m_nGrowSize = 0;
};
