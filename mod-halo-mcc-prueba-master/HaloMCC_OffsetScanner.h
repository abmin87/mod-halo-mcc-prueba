// HaloMCC_OffsetScanner.h
#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <cstdint>

// Estructura para offsets del juego
struct GameOffsets {
    // Camera offsets
    uintptr_t cameraBaseOffset = 0;
    uintptr_t viewMatrixOffset = 0;
    uintptr_t projMatrixOffset = 0;
    uintptr_t positionOffset = 0;
    uintptr_t rotationOffset = 0;

    // Player count offsets
    uintptr_t playerCountOffset = 0;
    uintptr_t maxPlayersOffset = 0;
    uintptr_t localPlayersOffset = 0;

    // Split screen control
    uintptr_t splitScreenEnabledOffset = 0;
    uintptr_t coopModeOffset = 0;

    // UI/Menu offsets
    uintptr_t menuStateOffset = 0;
    uintptr_t gameStateOffset = 0;

    bool valid = false;
};

// Scanner de offsets
class HaloMCCOffsetScanner {
public:
    struct ScanPatterns {
        static std::vector<uint8_t> GetSplitScreenCheckPattern();
        static std::vector<bool> GetSplitScreenCheckMask();
        static std::vector<uint8_t> GetPlayerCountPattern();
        static std::vector<bool> GetPlayerCountMask();
        static std::vector<uint8_t> GetCameraMatrixPattern();
        static std::vector<bool> GetCameraMatrixMask();
    };

    static GameOffsets ScanForOffsets();

private:
    static HMODULE GetGameModule();
    static uintptr_t ScanSplitScreenCheck(uintptr_t baseAddress, size_t moduleSize);
    static uintptr_t ScanPlayerCount(uintptr_t baseAddress, size_t moduleSize);
    static uintptr_t ScanCameraBase(uintptr_t baseAddress, size_t moduleSize);
    static bool PatternMatch(uintptr_t address, const std::vector<uint8_t>& pattern, const std::vector<bool>& mask);
};

// Función auxiliar exportada
extern "C" __declspec(dllexport) GameOffsets ScanGameOffsets();