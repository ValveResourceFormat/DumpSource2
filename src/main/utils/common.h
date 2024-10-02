#ifdef _WIN32
#define ROOTBIN "bin/win64/"
#define GAMEBIN "csgo/bin/win64/"
#else
#define ROOTBIN "bin/linuxsteamrt64/"
#define GAMEBIN "csgo/bin/linuxsteamrt64/"
#endif

void ExitError(const char* pMsg, ...);