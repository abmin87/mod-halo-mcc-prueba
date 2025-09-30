// UWP_Detection.h
#pragma once

// IMPORTANTE: Remover pch.h si no tienes precompiled headers configurados
// #include "pch.h"  // <- Comentar o remover esta línea

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

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
    static std::vector<std::string> GetRunningProcesses();
};

// ============================================================================
// Implementación usando comandos del sistema (más compatible)
// ============================================================================

std::vector<std::string> UWPGameDetector::GetRunningProcesses() {
    std::vector<std::string> processes;

    // Usar WMI/tasklist en lugar de ToolHelp32
    FILE* pipe = _popen("tasklist /fo csv /nh", "r");
    if (!pipe) return processes;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);

        // Extraer nombre del proceso (primera columna CSV)
        size_t firstQuote = line.find('"');
        size_t secondQuote = line.find('"', firstQuote + 1);

        if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
            std::string processName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
            processes.push_back(processName);
        }
    }

    _pclose(pipe);
    return processes;
}

bool UWPGameDetector::ProcessExists(const std::string& processName) {
    auto processes = GetRunningProcesses();
    for (const auto& process : processes) {
        if (process == processName) {
            return true;
        }
    }
    return false;
}

DWORD UWPGameDetector::GetProcessIdByName(const std::string& processName) {
    FILE* pipe = _popen(("tasklist /fi \"imagename eq " + processName + "\" /fo csv /nh").c_str(), "r");
    if (!pipe) return 0;

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);

        // Buscar el PID (segunda columna)
        size_t firstComma = line.find(',');
        size_t secondComma = line.find(',', firstComma + 1);

        if (firstComma != std::string::npos && secondComma != std::string::npos) {
            std::string pidStr = line.substr(firstComma + 2, secondComma - firstComma - 3); // Quitar comillas
            _pclose(pipe);
            return static_cast<DWORD>(std::stoul(pidStr));
        }
    }

    _pclose(pipe);
    return 0;
}

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

bool UWPGameDetector::IsUWPApplication() {
    HANDLE hProcess = GetCurrentProcess();
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
    HWND hWnd = GetForegroundWindow();
    char windowTitle[256];
    GetWindowTextA(hWnd, windowTitle, sizeof(windowTitle));

    std::string title(windowTitle);

    if (title.find("Halo: Combat Evolved") != std::string::npos) {
        return GameVersion::HALO_CE;
    }
    else if (title.find("Halo 2 Anniversary") != std::string::npos) {
        return GameVersion::HALO_2A;
    }
    else if (title.find("Halo 2") != std::string::npos) {
        return GameVersion::HALO_2;
    }
    else if (title.find("Halo: Reach") != std::string::npos) {
        return GameVersion::HALO_REACH;
    }
    else if (title.find("Halo 3") != std::string::npos) {
        return GameVersion::HALO_3;
    }
    else if (title.find("Halo 4") != std::string::npos) {
        return GameVersion::HALO_4;
    }

    return GameVersion::UNKNOWN_GAME;
}

std::string UWPGameDetector::GetGameVersionString() {
    switch (DetectCurrentGame()) {
    case GameVersion::HALO_CE: return "Halo: Combat Evolved";
    case GameVersion::HALO_2: return "Halo 2";
    case GameVersion::HALO_2A: return "Halo 2 Anniversary";
    case GameVersion::HALO_3: return "Halo 3";
    case GameVersion::HALO_REACH: return "Halo: Reach";
    case GameVersion::HALO_4: return "Halo 4";
    default: return "Unknown Game";
    }
}