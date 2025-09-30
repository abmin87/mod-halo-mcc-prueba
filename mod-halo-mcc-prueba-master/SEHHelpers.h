#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

// SEH helpers as inline functions so they can be included in multiple TUs without duplicate symbols.
static inline bool SEH_MemReadRaw(uintptr_t address, void* out, size_t size) {
    __try {
        memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static inline bool SEH_MemWriteRaw(uintptr_t address, const void* buffer, size_t size) {
    __try {
        memcpy(reinterpret_cast<void*>(address), buffer, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static inline bool SEH_PatternMatchRaw(uintptr_t address, const uint8_t* pattern, const unsigned char* maskBuf, size_t len) {
    __try {
        for (size_t i = 0; i < len; ++i) {
            if (maskBuf[i] && *reinterpret_cast<const uint8_t*>(address + i) != pattern[i]) {
                return false;
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
