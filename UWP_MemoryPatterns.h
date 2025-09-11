// UWP_MemoryPatterns.h
#pragma once
#include <windows.h>
#include <vector>
#include <string>

class UWPMemoryScanner {
public:
    struct MemoryPattern {
        std::vector<uint8_t> pattern;
        std::vector<bool> mask;
        size_t offset;
        std::string description;
    };
    
    static DWORD_PTR FindPattern(const std::string& moduleName, const MemoryPattern& pattern);
    static DWORD_PTR FindPatternInRange(DWORD_PTR start, size_t size, const MemoryPattern& pattern);
    static bool IsUWPMemoryProtected(DWORD_PTR address);
    static std::vector<MEMORY_BASIC_INFORMATION> GetMemoryRegions();
    
    // Patrones específicos para Microsoft Store version
    static MemoryPattern GetD3D11DevicePattern();
    static MemoryPattern GetSwapChainPresentPattern();
    static MemoryPattern GetXInputPattern();
    static MemoryPattern GetGameStatePattern();
    
private:
    static bool PatternMatch(const uint8_t* data, const MemoryPattern& pattern);
    static MODULEINFO GetModuleInfo(const std::string& moduleName);
};

// UWP_MemoryPatterns.cpp
#include "UWP_MemoryPatterns.h"
#include <psapi.h>
#include <iostream>

DWORD_PTR UWPMemoryScanner::FindPattern(const std::string& moduleName, const MemoryPattern& pattern) {
    MODULEINFO modInfo = GetModuleInfo(moduleName);
    if (modInfo.lpBaseOfDll == nullptr) return 0;
    
    return FindPatternInRange(
        reinterpret_cast<DWORD_PTR>(modInfo.lpBaseOfDll),
        modInfo.SizeOfImage,
        pattern
    );
}

DWORD_PTR UWPMemoryScanner::FindPatternInRange(DWORD_PTR start, size_t size, const MemoryPattern& pattern) {
    for (size_t i = 0; i <= size - pattern.pattern.size(); ++i) {
        const uint8_t* current = reinterpret_cast<const uint8_t*>(start + i);
        
        // Verificar si podemos leer esta memoria
        if (!IsUWPMemoryProtected(start + i)) {
            if (PatternMatch(current, pattern)) {
                return start + i + pattern.offset;
            }
        }
    }
    return 0;
}

bool UWPMemoryScanner::PatternMatch(const uint8_t* data, const MemoryPattern& pattern) {
    for (size_t i = 0; i < pattern.pattern.size(); ++i) {
        if (pattern.mask[i] && data[i] != pattern.pattern[i]) {
            return false;
        }
    }
    return true;
}

bool UWPMemoryScanner::IsUWPMemoryProtected(DWORD_PTR address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) {
        // Las aplicaciones UWP tienen protecciones especiales
        return (mbi.Protect & PAGE_GUARD) || 
               (mbi.Protect & PAGE_NOACCESS) || 
               (mbi.State != MEM_COMMIT);
    }
    return true;
}

MODULEINFO UWPMemoryScanner::GetModuleInfo(const std::string& moduleName) {
    MODULEINFO modInfo = {};
    HMODULE hModule = GetModuleHandleA(moduleName.c_str());
    
    if (hModule) {
        GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo));
    }
    
    return modInfo;
}

std::vector<MEMORY_BASIC_INFORMATION> UWPMemoryScanner::GetMemoryRegions() {
    std::vector<MEMORY_BASIC_INFORMATION> regions;
    MEMORY_BASIC_INFORMATION mbi;
    DWORD_PTR address = 0;
    
    while (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) {
        regions.push_back(mbi);
        address = reinterpret_cast<DWORD_PTR>(mbi.BaseAddress) + mbi.RegionSize;
        
        // Límite de seguridad para evitar loops infinitos
        if (address >= 0x7FFFFFFF) break;
    }
    
    return regions;
}

// Patrones específicos para encontrar funciones en Microsoft Store version
UWPMemoryScanner::MemoryPattern UWPMemoryScanner::GetD3D11DevicePattern() {
    // Patrón para encontrar D3D11Device en UWP
    return {
        {0x48, 0x89, 0x5C, 0x24, 0x00, 0x57, 0x48, 0x83, 0xEC, 0x00, 0x48, 0x8B, 0xFA},
        {true, true, true, true, false, true, true, true, true, false, true, true, true},
        0,
        "D3D11Device Creation Pattern"
    };
}

UWPMemoryScanner::MemoryPattern UWPMemoryScanner::GetSwapChainPresentPattern() {
    // Patrón más específico para Present en UWP
    return {
        {0x48, 0x89, 0x74, 0x24, 0x00, 0x48, 0x89, 0x7C, 0x24, 0x00, 0x41, 0x56},
        {true, true, true, true, false, true, true, true, true, false, true, true},
        0,
        "SwapChain Present Pattern"
    };
}

UWPMemoryScanner::MemoryPattern UWPMemoryScanner::GetXInputPattern() {
    // Patrón para XInputGetState en UWP context
    return {
        {0x85, 0xC9, 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x83, 0xF9, 0x04},
        {true, true, true, true, false, false, false, false, true, true, true},
        0,
        "XInputGetState Pattern"
    };
}

UWPMemoryScanner::MemoryPattern UWPMemoryScanner::GetGameStatePattern() {
    // Patrón para detectar el estado actual del juego
    return {
        {0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74},
        {true, true, true, false, false, false, false, true, true, true, true},
        0,
        "Game State Pattern"
    };
}