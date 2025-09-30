#include "pch.h"
#include "HaloMCC_OffsetScanner.h"
#include "SEHHelpers.h"
#include <psapi.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

#pragma comment(lib, "psapi.lib")

// Forward declarations de funciones auxiliares
static std::string ToHexString(uintptr_t value);
static void LogToFile(const std::string& message);

// ============================================================================
// Implementaciones de ScanPatterns
// ============================================================================

std::vector<uint8_t> HaloMCCOffsetScanner::ScanPatterns::GetSplitScreenCheckPattern() {
    return {
        0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x0F, 0x84, 0x00, 0x00, 0x00, 0x00
    };
}

std::vector<bool> HaloMCCOffsetScanner::ScanPatterns::GetSplitScreenCheckMask() {
    return {
        true, true, false, false, false, false, true,
        true, true, false, false, false, false
    };
}

std::vector<uint8_t> HaloMCCOffsetScanner::ScanPatterns::GetPlayerCountPattern() {
    return {
        0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x83, 0xF8, 0x01,
        0x7E, 0x00
    };
}

std::vector<bool> HaloMCCOffsetScanner::ScanPatterns::GetPlayerCountMask() {
    return {
        true, true, false, false, false, false,
        true, true, true,
        true, false
    };
}

std::vector<uint8_t> HaloMCCOffsetScanner::ScanPatterns::GetCameraMatrixPattern() {
    return {
        0x0F, 0x10, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x0F, 0x11, 0x01,
        0x0F, 0x10, 0x0D, 0x00, 0x00, 0x00, 0x00
    };
}

std::vector<bool> HaloMCCOffsetScanner::ScanPatterns::GetCameraMatrixMask() {
    return {
        true, true, true, false, false, false, false,
        true, true, true,
        true, true, true, false, false, false, false
    };
}

// ============================================================================
// Métodos privados
// ============================================================================

HMODULE HaloMCCOffsetScanner::GetGameModule() {
    const char* moduleNames[] = {
        "MCCWinStore-Win64-Shipping.exe",
        "MCC-Win64-Shipping.exe",
        nullptr
    };

    for (int i = 0; moduleNames[i]; i++) {
        HMODULE mod = GetModuleHandleA(moduleNames[i]);
        if (mod) {
            LogToFile("Módulo encontrado: " + std::string(moduleNames[i]));
            return mod;
        }
    }

    return GetModuleHandleA(nullptr);
}

bool HaloMCCOffsetScanner::PatternMatch(uintptr_t address, const std::vector<uint8_t>& pattern, const std::vector<bool>& mask) {
    if (pattern.empty() || mask.empty() || pattern.size() != mask.size()) return false;
    
    std::vector<unsigned char> maskBuf(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) {
        maskBuf[i] = mask[i] ? 1 : 0;
    }
    
    return SEH_PatternMatchRaw(address, pattern.data(), maskBuf.data(), pattern.size());
}

uintptr_t HaloMCCOffsetScanner::ScanSplitScreenCheck(uintptr_t baseAddress, size_t moduleSize) {
    LogToFile("Buscando patrón de verificación split-screen...");

    auto pattern = ScanPatterns::GetSplitScreenCheckPattern();
    auto mask = ScanPatterns::GetSplitScreenCheckMask();

    for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
        if (PatternMatch(baseAddress + i, pattern, mask)) {
            uint32_t relativeAddr = 0;
            if (SEH_MemReadRaw(baseAddress + i + 2, &relativeAddr, sizeof(uint32_t))) {
                uintptr_t targetAddr = baseAddress + i + 7 + relativeAddr;

                LogToFile("✓ Split-screen check encontrado en: 0x" + ToHexString(baseAddress + i));
                LogToFile("  -> Apunta a: 0x" + ToHexString(targetAddr));

                return targetAddr;
            }
        }
    }

    LogToFile("✗ Patrón split-screen no encontrado");
    return 0;
}

uintptr_t HaloMCCOffsetScanner::ScanPlayerCount(uintptr_t baseAddress, size_t moduleSize) {
    LogToFile("Buscando patrón de conteo de jugadores...");

    auto pattern = ScanPatterns::GetPlayerCountPattern();
    auto mask = ScanPatterns::GetPlayerCountMask();

    for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
        if (PatternMatch(baseAddress + i, pattern, mask)) {
            uint32_t relativeAddr = 0;
            if (SEH_MemReadRaw(baseAddress + i + 2, &relativeAddr, sizeof(uint32_t))) {
                uintptr_t targetAddr = baseAddress + i + 6 + relativeAddr;

                LogToFile("✓ Player count encontrado en: 0x" + ToHexString(baseAddress + i));
                LogToFile("  -> Apunta a: 0x" + ToHexString(targetAddr));

                return targetAddr;
            }
        }
    }

    LogToFile("✗ Patrón player count no encontrado");
    return 0;
}

uintptr_t HaloMCCOffsetScanner::ScanCameraBase(uintptr_t baseAddress, size_t moduleSize) {
    LogToFile("Buscando patrón de matriz de cámara...");

    auto pattern = ScanPatterns::GetCameraMatrixPattern();
    auto mask = ScanPatterns::GetCameraMatrixMask();

    for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
        if (PatternMatch(baseAddress + i, pattern, mask)) {
            uint32_t relativeAddr = 0;
            if (SEH_MemReadRaw(baseAddress + i + 3, &relativeAddr, sizeof(uint32_t))) {
                uintptr_t targetAddr = baseAddress + i + 7 + relativeAddr;

                LogToFile("✓ Camera matrix encontrada en: 0x" + ToHexString(baseAddress + i));
                LogToFile("  -> Base cámara: 0x" + ToHexString(targetAddr));

                return targetAddr;
            }
        }
    }

    LogToFile("✗ Patrón camera matrix no encontrado");
    return 0;
}

// ============================================================================
// Método público principal
// ============================================================================

GameOffsets HaloMCCOffsetScanner::ScanForOffsets() {
    GameOffsets offsets;

    LogToFile("=== Starting offset scan for Halo MCC 1.3385.0.0 ===");

    HMODULE gameModule = GetGameModule();
    if (!gameModule) {
        LogToFile("ERROR: No se pudo encontrar el módulo del juego");
        return offsets;
    }

    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo))) {
        LogToFile("ERROR: No se pudo obtener información del módulo");
        return offsets;
    }

    uintptr_t baseAddress = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
    size_t moduleSize = modInfo.SizeOfImage;

    LogToFile("Módulo base: 0x" + ToHexString(baseAddress));
    LogToFile("Tamaño módulo: 0x" + ToHexString(moduleSize));

    offsets.splitScreenEnabledOffset = ScanSplitScreenCheck(baseAddress, moduleSize);
    offsets.playerCountOffset = ScanPlayerCount(baseAddress, moduleSize);
    offsets.cameraBaseOffset = ScanCameraBase(baseAddress, moduleSize);

    if (offsets.splitScreenEnabledOffset && offsets.playerCountOffset) {
        offsets.valid = true;
        LogToFile("✓ Offsets encontrados exitosamente");
    } else {
        LogToFile("✗ No se pudieron encontrar todos los offsets necesarios");
    }

    return offsets;
}

// ============================================================================
// Funciones auxiliares
// ============================================================================

static std::string ToHexString(uintptr_t value) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << value;
    return ss.str();
}

static void LogToFile(const std::string& message) {
    std::ofstream logFile("HaloMCC_OffsetScanner.log", std::ios::out | std::ios::app);
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        char timeStr[100];
        struct tm timeInfo;
        localtime_s(&timeInfo, &time_t);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);

        logFile << "[" << timeStr << "] " << message << std::endl;
        logFile.close();
    }
}