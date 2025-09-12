// WTSAPI32_Proxy.cpp
#include <windows.h>
#include <string>
#include <fstream>
#include <chrono>

// Si mi InitializeMod está exportada como extern "C" __declspec(dllexport)
// en otro archivo dentro del mismo proyecto, declarar aquí como extern "C".
extern "C" void InitializeMod(); // función implementada en UWP_SplitScreenMod_*.cpp

// Variables globales
static HMODULE hOriginalDLL = nullptr;
static bool bInitialized = false;
static bool bModInitialized = false;

// Forward: helper para obtener funciones originales
template <typename T>
T GetOriginalFunction(const char* name) {
    if (!hOriginalDLL) return nullptr;
    return reinterpret_cast<T>(GetProcAddress(hOriginalDLL, name));
}

// Función para inicializar el mod solo una vez (lanza InitializeMod en un thread)
void TryInitializeMod() {
    if (bModInitialized) return;
    bModInitialized = true;

    std::ofstream modInitLog("WTSProxy_ModInit.log", std::ios::out | std::ios::app);
    if (modInitLog.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &now);
        timeStr[24] = '\0';
        modInitLog << "[" << timeStr << "] TryInitializeMod() called" << std::endl;
        modInitLog.close();
    }

    // Ejecutar InitializeMod en un hilo separado para no bloquear las llamadas WTS
    CreateThread(NULL, 0, [](LPVOID) -> DWORD {
        Sleep(500); // Pequeña pausa para estabilizar
        try {
            InitializeMod();
        }
        catch (...) {
            std::ofstream errorLog("WTSProxy_ModError.log", std::ios::out | std::ios::app);
            if (errorLog.is_open()) {
                errorLog << "Exception during mod initialization!" << std::endl;
                errorLog.close();
            }
        }
        return 0;
    }, NULL, 0, NULL);
}

// Implementaciones de funciones WTSAPI32 (proxy)
// Cada función intenta usar la función original de la DLL del sistema; antes llama a TryInitializeMod()

extern "C" __declspec(dllexport)
BOOL WINAPI WTSQuerySessionInformationW(
    HANDLE hServer,
    DWORD SessionId,
    DWORD WTSInfoClass,
    LPWSTR* ppBuffer,
    DWORD* pBytesReturned)
{
    TryInitializeMod();

    typedef BOOL(WINAPI* OriginalFunc)(HANDLE, DWORD, DWORD, LPWSTR*, DWORD*);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSQuerySessionInformationW");

    if (pOriginal) {
        return pOriginal(hServer, SessionId, WTSInfoClass, ppBuffer, pBytesReturned);
    }

    if (ppBuffer) *ppBuffer = nullptr;
    if (pBytesReturned) *pBytesReturned = 0;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSQuerySessionInformationA(
    HANDLE hServer,
    DWORD SessionId,
    DWORD WTSInfoClass,
    LPSTR* ppBuffer,
    DWORD* pBytesReturned)
{
    TryInitializeMod();

    typedef BOOL(WINAPI* OriginalFunc)(HANDLE, DWORD, DWORD, LPSTR*, DWORD*);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSQuerySessionInformationA");

    if (pOriginal) {
        return pOriginal(hServer, SessionId, WTSInfoClass, ppBuffer, pBytesReturned);
    }

    if (ppBuffer) *ppBuffer = nullptr;
    if (pBytesReturned) *pBytesReturned = 0;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

extern "C" __declspec(dllexport)
VOID WINAPI WTSFreeMemory(LPVOID pMemory)
{
    TryInitializeMod();

    typedef VOID(WINAPI* OriginalFunc)(LPVOID);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSFreeMemory");

    if (pOriginal) {
        pOriginal(pMemory);
        return;
    }

    if (pMemory) {
        LocalFree(pMemory);
    }
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSEnumerateSessionsW(
    HANDLE hServer,
    DWORD Reserved,
    DWORD Version,
    LPVOID* ppSessionInfo,
    DWORD* pCount)
{
    TryInitializeMod();

    typedef BOOL(WINAPI* OriginalFunc)(HANDLE, DWORD, DWORD, LPVOID*, DWORD*);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSEnumerateSessionsW");

    if (pOriginal) {
        return pOriginal(hServer, Reserved, Version, ppSessionInfo, pCount);
    }

    if (ppSessionInfo) *ppSessionInfo = nullptr;
    if (pCount) *pCount = 0;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSEnumerateSessionsA(
    HANDLE hServer,
    DWORD Reserved,
    DWORD Version,
    LPVOID* ppSessionInfo,
    DWORD* pCount)
{
    TryInitializeMod();

    typedef BOOL(WINAPI* OriginalFunc)(HANDLE, DWORD, DWORD, LPVOID*, DWORD*);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSEnumerateSessionsA");

    if (pOriginal) {
        return pOriginal(hServer, Reserved, Version, ppSessionInfo, pCount);
    }

    if (ppSessionInfo) *ppSessionInfo = nullptr;
    if (pCount) *pCount = 0;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

extern "C" __declspec(dllexport)
DWORD WINAPI WTSGetActiveConsoleSessionId(void)
{
    TryInitializeMod();

    typedef DWORD(WINAPI* OriginalFunc)(void);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSGetActiveConsoleSessionId");

    if (pOriginal) {
        return pOriginal();
    }

    return 1; // Fallback session ID
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSQueryUserToken(ULONG SessionId, PHANDLE phToken)
{
    TryInitializeMod();

    typedef BOOL(WINAPI* OriginalFunc)(ULONG, PHANDLE);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSQueryUserToken");

    if (pOriginal) {
        return pOriginal(SessionId, phToken);
    }

    if (phToken) *phToken = nullptr;
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSRegisterSessionNotification(HWND hWnd, DWORD dwFlags)
{
    TryInitializeMod();

    typedef BOOL(WINAPI* OriginalFunc)(HWND, DWORD);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSRegisterSessionNotification");

    if (pOriginal) {
        return pOriginal(hWnd, dwFlags);
    }

    return TRUE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSUnRegisterSessionNotification(HWND hWnd)
{
    TryInitializeMod();

    typedef BOOL(WINAPI* OriginalFunc)(HWND);
    static OriginalFunc pOriginal = GetOriginalFunction<OriginalFunc>("WTSUnRegisterSessionNotification");

    if (pOriginal) {
        return pOriginal(hWnd);
    }

    return TRUE;
}

// Función para inicializar el proxy (carga la DLL original y escribe logs)
BOOL InitializeWTSProxy() {
    if (bInitialized) return TRUE;

    std::ofstream proxyLog("WTSProxy_Init.log", std::ios::out | std::ios::app);
    if (proxyLog.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &now);
        timeStr[24] = '\0';
        proxyLog << "[" << timeStr << "] InitializeWTSProxy() called" << std::endl;
    }

    char systemPath[MAX_PATH];
    if (GetSystemDirectoryA(systemPath, sizeof(systemPath))) {
        std::string originalPath = std::string(systemPath) + "\\wtsapi32.dll";
        hOriginalDLL = LoadLibraryA(originalPath.c_str());

        if (proxyLog.is_open()) {
            if (hOriginalDLL) {
                proxyLog << "Original wtsapi32.dll loaded successfully from: " << originalPath << std::endl;
            }
            else {
                proxyLog << "Failed to load original wtsapi32.dll from: " << originalPath << std::endl;
                proxyLog << "Using fallback implementations" << std::endl;
            }
            proxyLog.close();
        }
    }

    bInitialized = TRUE;
    return TRUE;
}

void CleanupWTSProxy() {
    if (hOriginalDLL) {
        FreeLibrary(hOriginalDLL);
        hOriginalDLL = nullptr;
    }
    bInitialized = FALSE;
}

// DllMain: inicializa el proxy al adjuntarse la DLL
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        InitializeWTSProxy();

        std::ofstream attachLog("WTSProxy_Attach.log", std::ios::out | std::ios::app);
        if (attachLog.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char timeStr[26];
            ctime_s(timeStr, sizeof(timeStr), &now);
            timeStr[24] = '\0';

            attachLog << "[" << timeStr << "] DLL_PROCESS_ATTACH - WTSAPI32 Proxy loaded" << std::endl;

            // Log del proceso que nos carga
            char processName[MAX_PATH];
            GetModuleFileNameA(NULL, processName, MAX_PATH);
            attachLog << "Process: " << processName << std::endl;

            // Log del PID
            attachLog << "Process ID: " << GetCurrentProcessId() << std::endl;

            attachLog.close();
        }
    }
    break;

    case DLL_PROCESS_DETACH:
        CleanupWTSProxy();

        {
            std::ofstream detachLog("WTSProxy_Attach.log", std::ios::out | std::ios::app);
            if (detachLog.is_open()) {
                auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                char timeStr[26];
                ctime_s(timeStr, sizeof(timeStr), &now);
                timeStr[24] = '\0';

                detachLog << "[" << timeStr << "] DLL_PROCESS_DETACH - WTSAPI32 Proxy unloaded" << std::endl;
                detachLog.close();
            }
        }
        break;
    }
    return TRUE;
}

