// Windows-only entry points for the VST3 DLL. Windows hosts call InitDll/ExitDll after loading the
// module. The SDK's own dllmain.cpp is not used here, because the cross-build also pulls in
// linuxmain.cpp (the SDK picks it from the Linux host), and the two define the same symbol.
#if defined(_WIN32)
#define NAGINATA_EXPORT extern "C" __declspec(dllexport)
#else
#define NAGINATA_EXPORT extern "C" __attribute__((visibility("default")))
#endif

NAGINATA_EXPORT bool InitDll() { return true; }
NAGINATA_EXPORT bool ExitDll() { return true; }
