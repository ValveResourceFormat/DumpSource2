#ifdef _WIN32
#define PLATFORM_FOLDER "win64"
#else
#define PLATFORM_FOLDER "linuxsteamrt64"
#endif

void ExitError(const char* pMsg, ...);