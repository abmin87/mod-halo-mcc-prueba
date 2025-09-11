// WTSAPI32_Proxy.cpp
#include "pch.h"
#include <windows.h>

static HMODULE hOriginalDLL = nullptr;

// Helper template para cargar funciones
template <typename T>
T LoadOriginalFunction(const char* name) {
    if (!hOriginalDLL) return nullptr;
    return reinterpret_cast<T>(GetProcAddress(hOriginalDLL, name));
}

// Implementación de las funciones WTSAPI32 principales
extern "C" __declspec(dllexport)
BOOL WINAPI WTSQuerySessionInformationW(
    HANDLE hServer, 
    DWORD SessionId, 
    WTS_INFO_CLASS WTSInfoClass,
    LPWSTR* ppBuffer, 
    DWORD* pBytesReturned)
{
    typedef BOOL(WINAPI* Func_t)(HANDLE, DWORD, WTS_INFO_CLASS, LPWSTR*, DWORD*);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSQuerySessionInformationW");
    
    if (originalFunc) {
        return originalFunc(hServer, SessionId, WTSInfoClass, ppBuffer, pBytesReturned);
    }
    
    // Fallback para UWP - retornar información básica
    if (ppBuffer && pBytesReturned) {
        *ppBuffer = nullptr;
        *pBytesReturned = 0;
    }
    return FALSE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSQuerySessionInformationA(
    HANDLE hServer, 
    DWORD SessionId, 
    WTS_INFO_CLASS WTSInfoClass,
    LPSTR* ppBuffer, 
    DWORD* pBytesReturned)
{
    typedef BOOL(WINAPI* Func_t)(HANDLE, DWORD, WTS_INFO_CLASS, LPSTR*, DWORD*);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSQuerySessionInformationA");
    
    if (originalFunc) {
        return originalFunc(hServer, SessionId, WTSInfoClass, ppBuffer, pBytesReturned);
    }
    
    // Fallback para UWP
    if (ppBuffer && pBytesReturned) {
        *ppBuffer = nullptr;
        *pBytesReturned = 0;
    }
    return FALSE;
}

extern "C" __declspec(dllexport)
VOID WINAPI WTSFreeMemory(LPVOID pMemory)
{
    typedef VOID(WINAPI* Func_t)(LPVOID);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSFreeMemory");
    
    if (originalFunc) {
        originalFunc(pMemory);
    } else if (pMemory) {
        // Fallback básico
        LocalFree(pMemory);
    }
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSEnumerateSessionsW(
    HANDLE hServer,
    DWORD Reserved,
    DWORD Version,
    PWTS_SESSION_INFOW* ppSessionInfo,
    DWORD* pCount)
{
    typedef BOOL(WINAPI* Func_t)(HANDLE, DWORD, DWORD, PWTS_SESSION_INFOW*, DWORD*);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSEnumerateSessionsW");
    
    if (originalFunc) {
        return originalFunc(hServer, Reserved, Version, ppSessionInfo, pCount);
    }
    
    // Fallback para UWP - simular una sesión activa
    if (ppSessionInfo && pCount) {
        *ppSessionInfo = nullptr;
        *pCount = 0;
    }
    return FALSE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSEnumerateSessionsA(
    HANDLE hServer,
    DWORD Reserved,
    DWORD Version,
    PWTS_SESSION_INFOA* ppSessionInfo,
    DWORD* pCount)
{
    typedef BOOL(WINAPI* Func_t)(HANDLE, DWORD, DWORD, PWTS_SESSION_INFOA*, DWORD*);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSEnumerateSessionsA");
    
    if (originalFunc) {
        return originalFunc(hServer, Reserved, Version, ppSessionInfo, pCount);
    }
    
    // Fallback para UWP
    if (ppSessionInfo && pCount) {
        *ppSessionInfo = nullptr;
        *pCount = 0;
    }
    return FALSE;
}

extern "C" __declspec(dllexport)
DWORD WINAPI WTSGetActiveConsoleSessionId()
{
    typedef DWORD(WINAPI* Func_t)();
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSGetActiveConsoleSessionId");
    
    if (originalFunc) {
        return originalFunc();
    }
    
    // Fallback para UWP - retornar sesión activa por defecto
    return 1; // Session ID 1 como fallback
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSQueryUserToken(ULONG SessionId, PHANDLE phToken)
{
    typedef BOOL(WINAPI* Func_t)(ULONG, PHANDLE);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSQueryUserToken");
    
    if (originalFunc) {
        return originalFunc(SessionId, phToken);
    }
    
    // Fallback para UWP
    if (phToken) {
        *phToken = nullptr;
    }
    return FALSE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSRegisterSessionNotification(HWND hWnd, DWORD dwFlags)
{
    typedef BOOL(WINAPI* Func_t)(HWND, DWORD);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSRegisterSessionNotification");
    
    if (originalFunc) {
        return originalFunc(hWnd, dwFlags);
    }
    
    // Fallback para UWP - simular éxito
    return TRUE;
}

extern "C" __declspec(dllexport)
BOOL WINAPI WTSUnRegisterSessionNotification(HWND hWnd)
{
    typedef BOOL(WINAPI* Func_t)(HWND);
    static Func_t originalFunc = LoadOriginalFunction<Func_t>("WTSUnRegisterSessionNotification");
    
    if (originalFunc) {
        return originalFunc(hWnd);
    }
    
    // Fallback para UWP - simular éxito
    return TRUE;
}

// Función para inicializar el proxy
BOOL InitializeProxy() {
    // Intentar cargar la DLL original desde System32
    char systemPath[MAX_PATH];
    GetSystemDirectoryA(systemPath, MAX_PATH);
    
    std::string originalPath = std::string(systemPath) + "\\wtsapi32.dll";
    hOriginalDLL = LoadLibraryA(originalPath.c_str());
    
    if (!hOriginalDLL) {
        // Intentar con una copia renombrada si existe
        char modulePath[MAX_PATH];
        GetModuleFileNameA(GetModuleHandle(nullptr), modulePath, MAX_PATH);
        
        // Obtener directorio del ejecutable
        std::string moduleDir = std::string(modulePath);
        size_t lastSlash = moduleDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            moduleDir = moduleDir.substr(0, lastSlash + 1);
            std::string backupPath = moduleDir + "wtsapi32_original.dll";
            hOriginalDLL = LoadLibraryA(backupPath.c_str());
        }
    }
    
    // En UWP, es posible que no podamos cargar la DLL original
    // pero el mod puede funcionar con los fallbacks
    return TRUE; // No fallar si no podemos cargar el original
}