// UWP_Detection.h
#pragma once
#include <windows.h>
#include <string>

enum class GamePlatform {
    STEAM,
    MICROSOFT_STORE,
    UNKNOWN
};

enum class GameVersion {
    HALO_CE,
    HALO_2,
    HALO_2A, 
    HALO_3,
    HALO_REACH,
    HALO_4,
    UNKNOWN_GAME
};

class UWPGameDetector {
public:
    static GamePlatform DetectPlatform();
    static GameVersion DetectCurrentGame();
    static std::string GetExecutableName();
    static bool IsUWPApplication();
    static std::string GetGameVersionString();
    
private:
    static bool ProcessExists(const std::string& processName);
    static DWORD GetProcessIdByName(const std::string& processName);
};

// UWP_Detection.cpp
#include "UWP_Detection.h"
#include <tlhelp32.h>
#include <psapi.h>

GamePlatform UWPGameDetector::DetectPlatform() {
    // Buscar proceso específico de Microsoft Store
    if (ProcessExists("MCCWinStore-Win64-Shipping.exe")) {
        return GamePlatform::MICROSOFT_STORE;
    }
    
    // Buscar proceso de Steam
    if (ProcessExists("MCC-Win64-Shipping.exe")) {
        return GamePlatform::STEAM;
    }
    
    return GamePlatform::UNKNOWN;
}

bool UWPGameDetector::ProcessExists(const std::string& processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    bool found = false;
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (processName == pe32.szExeFile) {
                found = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
    return found;
}

DWORD UWPGameDetector::GetProcessIdByName(const std::string& processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    DWORD pid = 0;
    
    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (processName == pe32.szExeFile) {
                pid = pe32.th32ProcessID;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }
    
    CloseHandle(hSnapshot);
    return pid;
}

bool UWPGameDetector::IsUWPApplication() {
    // Las aplicaciones UWP tienen características específicas
    HANDLE hProcess = GetCurrentProcess();
    
    // Verificar si estamos en un contenedor UWP
    BOOL isInAppContainer = FALSE;
    HANDLE hToken;
    
    if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
        DWORD tokenInfo;
        DWORD returnLength;
        
        if (GetTokenInformation(hToken, TokenIsAppContainer, &tokenInfo, 
                              sizeof(tokenInfo), &returnLength)) {
            isInAppContainer = tokenInfo;
        }
        CloseHandle(hToken);
    }
    
    return isInAppContainer == TRUE;
}

std::string UWPGameDetector::GetExecutableName() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    
    std::string fullPath(buffer);
    size_t lastSlash = fullPath.find_last_of("\\/");
    
    if (lastSlash != std::string::npos) {
        return fullPath.substr(lastSlash + 1);
    }
    
    return fullPath;
}

GameVersion UWPGameDetector::DetectCurrentGame() {
    // Analizar la ventana actual o memoria para detectar qué juego está corriendo
    HWND hWnd = GetForegroundWindow();
    char windowTitle[256];
    GetWindowTextA(hWnd, windowTitle, sizeof(windowTitle));
    
    std::string title(windowTitle);
    
    if (title.find("Halo: Combat Evolved") != std::string::npos) {
        return GameVersion::HALO_CE;
    } else if (title.find("Halo 2") != std::string::npos) {
        return GameVersion::HALO_2;
    } else if (title.find("Halo: Reach") != std::string::npos) {
        return GameVersion::HALO_REACH;
    } else if (title.find("Halo 3") != std::string::npos) {
        return GameVersion::HALO_3;
    } else if (title.find("Halo 4") != std::string::npos) {
        return GameVersion::HALO_4;
    }
    
    return GameVersion::UNKNOWN_GAME;
}