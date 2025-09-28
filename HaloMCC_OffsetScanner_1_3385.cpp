// HaloMCC_OffsetScanner_1_3385.cpp
// Scanner específico para encontrar offsets de split-screen en Halo MCC v1.3385.0.0
#include "pch.h"
#include "UWP_Detection.h"
#include "UWP_MemoryPatterns.h"

class HaloMCCOffsetScanner {
public:
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

    // Patrones específicos para la versión 1.3385.0.0
    struct ScanPatterns {
        // Patrón para encontrar la función que verifica split-screen
        // Busca: cmp [reg+offset], 1; je skip_splitscreen
        static std::vector<uint8_t> GetSplitScreenCheckPattern() {
            return {
                0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x01,  // cmp dword ptr [rel address], 1
                0x0F, 0x84, 0x00, 0x00, 0x00, 0x00          // je offset
            };
        }
        
        static std::vector<bool> GetSplitScreenCheckMask() {
            return {
                true, true, false, false, false, false, true,
                true, true, false, false, false, false
            };
        }
        
        // Patrón para player count
        static std::vector<uint8_t> GetPlayerCountPattern() {
            return {
                0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,        // mov eax, [rel address]
                0x83, 0xF8, 0x01,                           // cmp eax, 1
                0x7E, 0x00                                  // jle offset
            };
        }
        
        static std::vector<bool> GetPlayerCountMask() {
            return {
                true, true, false, false, false, false,
                true, true, true,
                true, false
            };
        }
        
        // Patrón para cámara principal
        static std::vector<uint8_t> GetCameraMatrixPattern() {
            return {
                0x0F, 0x10, 0x05, 0x00, 0x00, 0x00, 0x00,  // movups xmm0, [rel camera_matrix]
                0x0F, 0x11, 0x01,                           // movups [rcx], xmm0
                0x0F, 0x10, 0x0D, 0x00, 0x00, 0x00, 0x00   // movups xmm1, [rel camera_matrix+16]
            };
        }
        
        static std::vector<bool> GetCameraMatrixMask() {
            return {
                true, true, true, false, false, false, false,
                true, true, true,
                true, true, true, false, false, false, false
            };
        }
    };

    static GameOffsets ScanForOffsets() {
        GameOffsets offsets;
        
        Log("=== Starting offset scan for Halo MCC 1.3385.0.0 ===");
        
        // Obtener información del módulo principal
        HMODULE gameModule = GetGameModule();
        if (!gameModule) {
            Log("ERROR: No se pudo encontrar el módulo del juego");
            return offsets;
        }
        
        MODULEINFO modInfo;
        GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo));
        
        uintptr_t baseAddress = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
        size_t moduleSize = modInfo.SizeOfImage;
        
        Log("Módulo base: 0x" + ToHexString(baseAddress));
        Log("Tamaño módulo: 0x" + ToHexString(moduleSize));
        
        // Escanear patrones
        offsets.splitScreenEnabledOffset = ScanSplitScreenCheck(baseAddress, moduleSize);
        offsets.playerCountOffset = ScanPlayerCount(baseAddress, moduleSize);
        offsets.cameraBaseOffset = ScanCameraBase(baseAddress, moduleSize);
        
        // Validar que encontramos los offsets críticos
        if (offsets.splitScreenEnabledOffset && offsets.playerCountOffset) {
            offsets.valid = true;
            Log("✓ Offsets encontrados exitosamente");
            LogOffsets(offsets);
        } else {
            Log("✗ No se pudieron encontrar todos los offsets necesarios");
        }
        
        return offsets;
    }

private:
    static HMODULE GetGameModule() {
        // Intentar diferentes nombres según la plataforma
        const char* moduleNames[] = {
            "MCCWinStore-Win64-Shipping.exe",  // Microsoft Store
            "MCC-Win64-Shipping.exe",          // Steam
            nullptr
        };
        
        for (int i = 0; moduleNames[i]; i++) {
            HMODULE mod = GetModuleHandleA(moduleNames[i]);
            if (mod) {
                Log("Módulo encontrado: " + std::string(moduleNames[i]));
                return mod;
            }
        }
        
        // Fallback: módulo principal
        return GetModuleHandleA(nullptr);
    }
    
    static uintptr_t ScanSplitScreenCheck(uintptr_t baseAddress, size_t moduleSize) {
        Log("Buscando patrón de verificación split-screen...");
        
        auto pattern = ScanPatterns::GetSplitScreenCheckPattern();
        auto mask = ScanPatterns::GetSplitScreenCheckMask();
        
        for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
            if (PatternMatch(baseAddress + i, pattern, mask)) {
                // Extraer dirección relativa
                uint32_t relativeAddr = *reinterpret_cast<uint32_t*>(baseAddress + i + 2);
                uintptr_t targetAddr = baseAddress + i + 7 + relativeAddr;
                
                Log("✓ Split-screen check encontrado en: 0x" + ToHexString(baseAddress + i));
                Log("  -> Apunta a: 0x" + ToHexString(targetAddr));
                
                return targetAddr;
            }
        }
        
        Log("✗ Patrón split-screen no encontrado");
        return 0;
    }
    
    static uintptr_t ScanPlayerCount(uintptr_t baseAddress, size_t moduleSize) {
        Log("Buscando patrón de conteo de jugadores...");
        
        auto pattern = ScanPatterns::GetPlayerCountPattern();
        auto mask = ScanPatterns::GetPlayerCountMask();
        
        for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
            if (PatternMatch(baseAddress + i, pattern, mask)) {
                // Extraer dirección relativa
                uint32_t relativeAddr = *reinterpret_cast<uint32_t*>(baseAddress + i + 2);
                uintptr_t targetAddr = baseAddress + i + 6 + relativeAddr;
                
                Log("✓ Player count encontrado en: 0x" + ToHexString(baseAddress + i));
                Log("  -> Apunta a: 0x" + ToHexString(targetAddr));
                
                return targetAddr;
            }
        }
        
        Log("✗ Patrón player count no encontrado");
        return 0;
    }
    
    static uintptr_t ScanCameraBase(uintptr_t baseAddress, size_t moduleSize) {
        Log("Buscando patrón de matriz de cámara...");
        
        auto pattern = ScanPatterns::GetCameraMatrixPattern();
        auto mask = ScanPatterns::GetCameraMatrixMask();
        
        for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
            if (PatternMatch(baseAddress + i, pattern, mask)) {
                // Extraer primera dirección relativa (matriz view)
                uint32_t relativeAddr = *reinterpret_cast<uint32_t*>(baseAddress + i + 3);
                uintptr_t targetAddr = baseAddress + i + 7 + relativeAddr;
                
                Log("✓ Camera matrix encontrada en: 0x" + ToHexString(baseAddress + i));
                Log("  -> Base cámara: 0x" + ToHexString(targetAddr));
                
                return targetAddr;
            }
        }
        
        Log("✗ Patrón camera matrix no encontrado");
        return 0;
    }
    
    static bool PatternMatch(uintptr_t address, const std::vector<uint8_t>& pattern, const std::vector<bool>& mask) {
        __try {
            for (size_t i = 0; i < pattern.size(); i++) {
                if (mask[i] && *reinterpret_cast<uint8_t*>(address + i) != pattern[i]) {
                    return false;
                }
            }
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    static void LogOffsets(const GameOffsets& offsets) {
        Log("=== OFFSETS ENCONTRADOS ===");
        Log("Split Screen Enabled: 0x" + ToHexString(offsets.splitScreenEnabledOffset));
        Log("Player Count: 0x" + ToHexString(offsets.playerCountOffset));
        Log("Camera Base: 0x" + ToHexString(offsets.cameraBaseOffset));
        Log("========================");
    }
    
    static std::string ToHexString(uintptr_t value) {
        std::stringstream ss;
        ss << std::hex << std::uppercase << value;
        return ss.str();
    }
    
    static void Log(const std::string& message) {
        std::ofstream logFile("HaloMCC_OffsetScan.log", std::ios::out | std::ios::app);
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            
            char timeStr[100];
            struct tm timeInfo;
            localtime_s(&timeInfo, &time_t);
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);
            
            logFile << "[" << timeStr << "] " << message << std::endl;
            logFile.flush();
        }
        
        // También output a debug console
        OutputDebugStringA(("[OFFSET_SCANNER] " + message + "\n").c_str());
    }
};

// Función helper para usar desde tu mod principal
extern "C" __declspec(dllexport) HaloMCCOffsetScanner::GameOffsets ScanGameOffsets() {
    return HaloMCCOffsetScanner::ScanForOffsets();
}

// Test runner para verificar offsets
class OffsetTester {
public:
    static void TestOffsets(const HaloMCCOffsetScanner::GameOffsets& offsets) {
        if (!offsets.valid) {
            Log("Cannot test - offsets not valid");
            return;
        }
        
        Log("=== TESTING OFFSETS ===");
        
        // Test split screen flag
        if (offsets.splitScreenEnabledOffset) {
            __try {
                uint32_t value = *reinterpret_cast<uint32_t*>(offsets.splitScreenEnabledOffset);
                Log("Split screen enabled value: " + std::to_string(value));
            }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                Log("ERROR: Cannot read split screen offset");
            }
        }
        
        // Test player count
        if (offsets.playerCountOffset) {
            __try {
                uint32_t value = *reinterpret_cast<uint32_t*>(offsets.playerCountOffset);
                Log("Player count value: " + std::to_string(value));
            }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                Log("ERROR: Cannot read player count offset");
            }
        }
        
        Log("=== TEST COMPLETE ===");
    }

private:
    static void Log(const std::string& message) {
        std::ofstream logFile("HaloMCC_OffsetTest.log", std::ios::out | std::ios::app);
        if (logFile.is_open()) {
            logFile << message << std::endl;
        }
    }
};

// Función para parchear el juego con los offsets encontrados
class GamePatcher {
public:
    static bool EnableSplitScreen(const HaloMCCOffsetScanner::GameOffsets& offsets) {
        if (!offsets.valid) return false;
        
        bool success = true;
        
        // Habilitar split screen
        if (offsets.splitScreenEnabledOffset) {
            success &= WriteProtectedMemory(offsets.splitScreenEnabledOffset, 2); // 2 players
        }
        
        // Modificar conteo máximo de jugadores
        if (offsets.playerCountOffset) {
            success &= WriteProtectedMemory(offsets.playerCountOffset, 2);
        }
        
        Log(success ? "Split screen habilitado exitosamente" : "Error habilitando split screen");
        return success;
    }
    
    static bool DisableSplitScreen(const HaloMCCOffsetScanner::GameOffsets& offsets) {
        if (!offsets.valid) return false;
        
        bool success = true;
        
        // Deshabilitar split screen
        if (offsets.splitScreenEnabledOffset) {
            success &= WriteProtectedMemory(offsets.splitScreenEnabledOffset, 1); // 1 player
        }
        
        if (offsets.playerCountOffset) {
            success &= WriteProtectedMemory(offsets.playerCountOffset, 1);
        }
        
        Log(success ? "Split screen deshabilitado exitosamente" : "Error deshabilitando split screen");
        return success;
    }

private:
    static bool WriteProtectedMemory(uintptr_t address, uint32_t value) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(uint32_t), 
                           PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }
        
        *reinterpret_cast<uint32_t*>(address) = value;
        
        DWORD temp;
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(uint32_t), oldProtect, &temp);
        return true;
    }
    
    static void Log(const std::string& message) {
        std::ofstream logFile("HaloMCC_Patcher.log", std::ios::out | std::ios::app);
        if (logFile.is_open()) {
            logFile << message << std::endl;
        }
    }
};
