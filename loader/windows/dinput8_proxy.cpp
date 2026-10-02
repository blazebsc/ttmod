// Clean-room dinput8 proxy for MCSM1 (x86). Windows-only; NOT built on Linux.
// Drops in beside MinecraftStoryMode.exe as dinput8.dll (game imports DINPUT8.dll — verified).
// Forwards DirectInput8Create to the real system DLL, then loads ttmod_framework.dll.
// Differences from telltale_hook reference: graceful failure (returns FALSE, never
// ExitProcess), no MessageBox, framework name is ours. No copied code.
#ifdef _WIN32
#include <windows.h>

static HMODULE g_real = nullptr;
static HMODULE g_framework = nullptr;

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t g_orig = nullptr;

static DWORD WINAPI InitThread(LPVOID) {
    // Load framework after loader lock is released.
    g_framework = LoadLibraryA("ttmod_framework.dll");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        char sys[MAX_PATH];
        UINT n = GetSystemDirectoryA(sys, MAX_PATH - 14);
        if (n == 0 || n > MAX_PATH - 14) return FALSE;
        sys[n] = '\0';
        lstrcatA(sys, "\\dinput8.dll");
        g_real = LoadLibraryA(sys);
        if (!g_real) return FALSE; // graceful: game loader falls back, no kill
        g_orig = (DirectInput8Create_t)GetProcAddress(g_real, "DirectInput8Create");
        if (!g_orig) return FALSE;
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_framework) FreeLibrary(g_framework);
        if (g_real) FreeLibrary(g_real);
        g_framework = nullptr;
        g_real = nullptr;
        g_orig = nullptr;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE a1, DWORD a2, REFIID a3, LPVOID* a4, LPUNKNOWN a5) {
    if (!g_orig) return E_FAIL;
    return g_orig(a1, a2, a3, a4, a5);
}
#endif
