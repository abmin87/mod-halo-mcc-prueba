// UWP_SplitScreenMod.cpp - Versión corregida con fixes en hooks
#include "pch.h"
#include "UWP_Detection.h"
#include "UWP_MemoryPatterns.h"
#include "MinHook.h"
#include <d3d11.h>
#include <dxgi.h>
#include <xinput.h>
#include <dxgi1_2.h>
#include <tlhelp32.h>
#include <psapi.h>

class UWPSplitScreenMod {
private:
    // Estado del mod
    bool initialized = false;
    bool splitScreenActive = false;
    GamePlatform platform = GamePlatform::UNKNOWN;
    GameVersion currentGame = GameVersion::UNKNOWN_GAME;

    // Hooks
    typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
    typedef HRESULT(STDMETHODCALLTYPE* Present1_t)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
    typedef HRESULT(WINAPI* D3D11CreateDeviceAndSwapChain_t)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

    Present_t fpPresent = nullptr;                 // Trampoline (original) de Present
    void* presentTargetAddr = nullptr;             // Dirección real de Present
    Present1_t fpPresent1 = nullptr;
    XInputGetState_t fpXInputGetState = nullptr;   // Trampoline (original) de XInputGetState
    void* xinputTargetAddr = nullptr;              // Dirección real de XInputGetState
    D3D11CreateDeviceAndSwapChain_t fpD3D11CreateDeviceAndSwapChain = nullptr;

    // Logging
    std::ofstream logFile;

public:
    static UWPSplitScreenMod& GetInstance() {
        static UWPSplitScreenMod instance;
        return instance;
    }

    // Función para convertir códigos de error de MinHook a texto
    std::string MinHookStatusToString(MH_STATUS status) {
        switch (status) {
        case MH_OK: return "MH_OK";
        case MH_ERROR_NOT_INITIALIZED: return "MH_ERROR_NOT_INITIALIZED";
        case MH_ERROR_ALREADY_INITIALIZED: return "MH_ERROR_ALREADY_INITIALIZED";
        case MH_ERROR_NOT_CREATED: return "MH_ERROR_NOT_CREATED";
        case MH_ERROR_ALREADY_CREATED: return "MH_ERROR_ALREADY_CREATED";
        case MH_ERROR_ENABLED: return "MH_ERROR_ENABLED";
        case MH_ERROR_DISABLED: return "MH_ERROR_DISABLED";
        case MH_ERROR_NOT_EXECUTABLE: return "MH_ERROR_NOT_EXECUTABLE";
        case MH_ERROR_UNSUPPORTED_FUNCTION: return "MH_ERROR_UNSUPPORTED_FUNCTION";
        case MH_ERROR_MEMORY_ALLOC: return "MH_ERROR_MEMORY_ALLOC";
        case MH_ERROR_MEMORY_PROTECT: return "MH_ERROR_MEMORY_PROTECT";
        case MH_ERROR_MODULE_NOT_FOUND: return "MH_ERROR_MODULE_NOT_FOUND";
        case MH_ERROR_FUNCTION_NOT_FOUND: return "MH_ERROR_FUNCTION_NOT_FOUND";
        default: return "UNKNOWN_ERROR(" + std::to_string(status) + ")";
        }
    }

    void Log(const std::string& message) {
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char timeStr[26];
            ctime_s(timeStr, sizeof(timeStr), &now);
            timeStr[24] = '\0';
            logFile << "[" << timeStr << "] " << message << std::endl;
            logFile.flush();
        }
    }

    void RenderSplitScreen(IDXGISwapChain* pSwapChain) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            Log("RenderSplitScreen called for split screen rendering!");
            loggedOnce = true;
        }

        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;

        HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice);
        if (FAILED(hr) || !pDevice) return;

        pDevice->GetImmediateContext(&pContext);
        if (!pContext) {
            pDevice->Release();
            return;
        }

        // Configurar viewports básicos para split screen vertical
        D3D11_VIEWPORT viewports[2] = {};

        // Viewport para jugador 1 (lado izquierdo)
        viewports[0].TopLeftX = 0;
        viewports[0].TopLeftY = 0;
        viewports[0].Width = 960;  // Mitad de 1920
        viewports[0].Height = 1080;
        viewports[0].MinDepth = 0.0f;
        viewports[0].MaxDepth = 1.0f;

        // Viewport para jugador 2 (lado derecho)
        viewports[1].TopLeftX = 960;
        viewports[1].TopLeftY = 0;
        viewports[1].Width = 960;
        viewports[1].Height = 1080;
        viewports[1].MinDepth = 0.0f;
        viewports[1].MaxDepth = 1.0f;

        // Aplicar viewports para split screen
        pContext->RSSetViewports(2, viewports);

        pContext->Release();
        pDevice->Release();
    }

    bool Initialize() {
        logFile.open("UWPSplitScreen.log", std::ios::out | std::ios::app);
        Log("=== UWP Split Screen Mod Initialize ===");

        platform = UWPGameDetector::DetectPlatform();
        if (platform != GamePlatform::MICROSOFT_STORE) {
            Log("ERROR: Not running on Microsoft Store version!");
            return false;
        }

        Log("Microsoft Store platform detected");

        if (!UWPGameDetector::IsUWPApplication()) {
            Log("INFO: Not in UWP container, continuing anyway");
        }

        MH_STATUS mhStatus = MH_Initialize();
        if (mhStatus != MH_OK) {
            Log("ERROR: MinHook initialization failed: " + MinHookStatusToString(mhStatus));
            return false;
        }
        Log("MinHook initialized successfully");

        Log("Starting delayed hook installation thread...");
        CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
            UWPSplitScreenMod* mod = static_cast<UWPSplitScreenMod*>(param);
            return mod->DelayedHookInstallation();
            }, this, 0, NULL);

        initialized = true;
        Log("UWP Split Screen Mod initialized successfully (hooks pending)");
        return true;
    }

private:
    DWORD DelayedHookInstallation() {
        Log("Delayed hook installation thread started");

        Sleep(3000);

        for (int attempt = 1; attempt <= 20; attempt++) {
            Log("Hook installation attempt " + std::to_string(attempt) + "/20");

            if (!IsGameActiveAndRendering()) {
                Log("Game not ready yet, waiting 6 seconds...");
                Sleep(6000);
                continue;
            }

            Log("Game appears active, installing hooks...");

            if (HookXInput()) {
                Log("XInput hook successful");
            }

            bool presentHooked = false;

            if (HookPresentUsingMethod1()) {
                Log("Present hook successful via Method 1 (Temporary SwapChain)!");
                presentHooked = true;
            }
            else if (HookPresentUsingMethod2()) {
                Log("Present hook successful via Method 2 (Memory Scan)!");
                presentHooked = true;
            }
            else if (HookPresentUsingMethod3()) {
                Log("Present hook successful via Method 3 (Pattern Scan)!");
                presentHooked = true;
            }

            if (presentHooked) {
                splitScreenActive = true;
                Log("All hooks installed successfully! Split screen mod is now active.");
                return 0;
            }

            Log("Present hook failed, will retry in 6 seconds...");
            Sleep(6000);
        }

        Log("ERROR: All hook installation attempts failed after 20 tries");
        return 1;
    }

    bool IsGameActiveAndRendering() {
        bool gameWindowFound = false;

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            char title[256];
            GetWindowTextA(hwnd, title, sizeof(title));
            std::string titleStr(title);

            if (titleStr.find("Halo") != std::string::npos ||
                titleStr.find("Master Chief Collection") != std::string::npos ||
                titleStr.find("MCC") != std::string::npos) {

                if (IsWindowVisible(hwnd)) {
                    RECT rect;
                    GetClientRect(hwnd, &rect);
                    if ((rect.right - rect.left) > 800 && (rect.bottom - rect.top) > 600) {
                        *reinterpret_cast<bool*>(lParam) = true;
                        return FALSE;
                    }
                }
            }
            return TRUE;
            }, reinterpret_cast<LPARAM>(&gameWindowFound));

        if (!gameWindowFound) {
            Log("Game window not found or not ready");
            return false;
        }

        HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
        HMODULE hDXGI = GetModuleHandleA("dxgi.dll");

        if (!hD3D11 || !hDXGI) {
            Log("DirectX modules not fully loaded yet");
            return false;
        }

        Log("Game appears active and ready for hook installation");
        return true;
    }

    bool HookPresentUsingMethod1() {
        Log("Method 1: Hook via temporary SwapChain creation");

        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        IDXGISwapChain* pSwapChain = nullptr;

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 1;
        swapChainDesc.BufferDesc.Width = 800;
        swapChainDesc.BufferDesc.Height = 600;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = GetDesktopWindow();
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &swapChainDesc, &pSwapChain, &pDevice, nullptr, &pContext);

        if (SUCCEEDED(hr) && pSwapChain) {
            Log("Temporary SwapChain created successfully");

            void** vtbl = *reinterpret_cast<void***>(pSwapChain);
            void* realPresent = vtbl[8];
            presentTargetAddr = realPresent;
            Log("Real Present function found at: 0x" + std::to_string((uintptr_t)realPresent));

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(realPresent, &mbi, sizeof(mbi))) {
                Log("Present function memory protect: 0x" + std::to_string(mbi.Protect));
            }

            MH_STATUS createStatus = MH_CreateHook(presentTargetAddr, &HookedPresent, reinterpret_cast<LPVOID*>(&fpPresent));
            Log("MH_CreateHook result: " + MinHookStatusToString(createStatus));

            if (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) {
                DWORD oldProtect = 0;
                SIZE_T patchSize = 16;
                if (!VirtualProtect(presentTargetAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    Log("VirtualProtect failed (continuing) - GetLastError: " + std::to_string(GetLastError()));
                }
                else {
                    Log("VirtualProtect succeeded for present target");
                }

                MH_STATUS enableStatus = MH_EnableHook(presentTargetAddr);
                Log("MH_EnableHook result: " + MinHookStatusToString(enableStatus));

                if (oldProtect != 0) {
                    DWORD tmp = 0;
                    VirtualProtect(presentTargetAddr, patchSize, oldProtect, &tmp);
                }

                if (enableStatus == MH_OK) {
                    Log("Present hook enabled successfully!");
                    pSwapChain->Release();
                    if (pContext) pContext->Release();
                    if (pDevice) pDevice->Release();
                    return true;
                }
                else {
                    MH_STATUS fallback = MH_EnableHook(MH_ALL_HOOKS);
                    Log("Fallback MH_EnableHook(MH_ALL_HOOKS) result: " + MinHookStatusToString(fallback));
                    if (fallback == MH_OK) {
                        Log("Present hook enabled via fallback!");
                        pSwapChain->Release();
                        if (pContext) pContext->Release();
                        if (pDevice) pDevice->Release();
                        return true;
                    }
                }
            }

            pSwapChain->Release();
            if (pContext) pContext->Release();
            if (pDevice) pDevice->Release();
            return false;
        }
        else {
            Log("Failed to create temporary SwapChain: HRESULT = 0x" + std::to_string(hr));
            return false;
        }
    }

    bool HookPresentUsingMethod2() {
        Log("Method 2: Hook via memory scanning for active SwapChain");

        MEMORY_BASIC_INFORMATION mbi;
        BYTE* address = nullptr;
        int regionsScanned = 0;

        while (VirtualQuery(address, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_COMMIT &&
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ))) {

                regionsScanned++;
                if (regionsScanned % 100 == 0) {
                    Log("Scanned " + std::to_string(regionsScanned) + " memory regions...");
                }

                if (ScanMemoryRegionForSwapChain(reinterpret_cast<BYTE*>(mbi.BaseAddress), mbi.RegionSize)) {
                    return true;
                }
            }

            address = reinterpret_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
            if (reinterpret_cast<uintptr_t>(address) > 0x7FFFFFFF) break;
        }

        Log("Memory scanning method failed after scanning " + std::to_string(regionsScanned) + " regions");
        return false;
    }

    static bool ScanMemoryRegionSafe(BYTE* baseAddr, SIZE_T regionSize, void** outPresentFunc) {
        __try {
            for (SIZE_T i = 0; i < regionSize - sizeof(void*) * 24; i += sizeof(void*)) {
                void** potentialVTable = reinterpret_cast<void**>(baseAddr + i);
                void* potentialPresent = nullptr;

                // Protección para evitar lecturas inválidas
                __try {
                    potentialPresent = potentialVTable[8];
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    continue;
                }

                if (potentialPresent) {
                    MEMORY_BASIC_INFORMATION mbi;
                    if (VirtualQuery(potentialPresent, &mbi, sizeof(mbi))) {
                        if (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) {
                            *outPresentFunc = potentialPresent;
                            return true;
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    bool ScanMemoryRegionForSwapChain(BYTE* baseAddr, SIZE_T regionSize) {
        void* potentialPresent = nullptr;

        if (ScanMemoryRegionSafe(baseAddr, regionSize, &potentialPresent)) {
            Log("Found potential Present function at: 0x" + std::to_string((uintptr_t)potentialPresent));

            presentTargetAddr = potentialPresent;
            MH_STATUS createStatus = MH_CreateHook(presentTargetAddr, &HookedPresent, reinterpret_cast<LPVOID*>(&fpPresent));
            Log("Memory scan - MH_CreateHook result: " + MinHookStatusToString(createStatus));

            if (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) {
                DWORD oldProtect = 0;
                SIZE_T patchSize = 16;
                if (!VirtualProtect(presentTargetAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    Log("VirtualProtect failed (continuing) - GetLastError: " + std::to_string(GetLastError()));
                }
                else {
                    Log("VirtualProtect succeeded for memory-scanned present target");
                }

                MH_STATUS enableStatus = MH_EnableHook(presentTargetAddr);
                Log("Memory scan - MH_EnableHook result: " + MinHookStatusToString(enableStatus));

                if (oldProtect != 0) {
                    DWORD tmp = 0;
                    VirtualProtect(presentTargetAddr, patchSize, oldProtect, &tmp);
                }

                if (enableStatus == MH_OK) {
                    Log("Successfully hooked Present via memory scan!");
                    return true;
                }
                else {
                    MH_STATUS fallback = MH_EnableHook(MH_ALL_HOOKS);
                    Log("Fallback MH_EnableHook(MH_ALL_HOOKS) result: " + MinHookStatusToString(fallback));
                    if (fallback == MH_OK) return true;
                }
            }
        }
        return false;
    }

    bool HookPresentUsingMethod3() {
        Log("Method 3: Hook via UWP pattern scanning");
        UWPMemoryScanner::MemoryPattern presentPattern = UWPMemoryScanner::GetSwapChainPresentPattern();
        DWORD_PTR presentAddress = UWPMemoryScanner::FindPattern("dxgi.dll", presentPattern);
        if (presentAddress != 0) {
            Log("Pattern scan found Present at: 0x" + std::to_string(presentAddress));

            presentTargetAddr = reinterpret_cast<void*>(presentAddress);
            MH_STATUS createStatus = MH_CreateHook(presentTargetAddr, &HookedPresent, reinterpret_cast<LPVOID*>(&fpPresent));
            Log("Pattern scan - MH_CreateHook result: " + MinHookStatusToString(createStatus));

            if (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) {
                DWORD oldProtect = 0;
                SIZE_T patchSize = 16;
                if (!VirtualProtect(presentTargetAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    Log("VirtualProtect failed (continuing) - GetLastError: " + std::to_string(GetLastError()));
                }
                else {
                    Log("VirtualProtect succeeded for pattern-scanned present target");
                }

                MH_STATUS enableStatus = MH_EnableHook(presentTargetAddr);
                Log("Pattern scan - MH_EnableHook result: " + MinHookStatusToString(enableStatus));

                if (oldProtect != 0) {
                    DWORD tmp = 0;
                    VirtualProtect(presentTargetAddr, patchSize, oldProtect, &tmp);
                }

                if (enableStatus == MH_OK) {
                    Log("Successfully hooked Present via pattern scan!");
                    return true;
                }
                else {
                    MH_STATUS fallback = MH_EnableHook(MH_ALL_HOOKS);
                    Log("Fallback MH_EnableHook(MH_ALL_HOOKS) result: " + MinHookStatusToString(fallback));
                    if (fallback == MH_OK) return true;
                }
            }
        }

        Log("Pattern scanning method failed");
        return false;
    }

    bool HookXInput() {
        Log("Installing XInput hook...");

        const char* xinputDlls[] = {
            "XINPUT1_4.dll",
            "XINPUT1_3.dll",
            "XINPUT9_1_0.dll"
        };

        for (const char* dllName : xinputDlls) {
            HMODULE hXInput = GetModuleHandleA(dllName);
            if (!hXInput) {
                hXInput = LoadLibraryA(dllName);
            }

            if (hXInput) {
                void* target = reinterpret_cast<void*>(GetProcAddress(hXInput, "XInputGetState"));
                if (!target) continue;

                xinputTargetAddr = target;
                Log(std::string("Found XInputGetState at: 0x") + std::to_string((uintptr_t)target) + " in " + dllName);

                // Crear hook: almacenar trampoline en fpXInputGetState
                MH_STATUS createStatus = MH_CreateHook(xinputTargetAddr, &HookedXInputGetState, reinterpret_cast<LPVOID*>(&fpXInputGetState));
                Log("XInput - MH_CreateHook result: " + MinHookStatusToString(createStatus));

                if (createStatus == MH_OK || createStatus == MH_ERROR_ALREADY_CREATED) {
                    DWORD oldProtect = 0;
                    SIZE_T patchSize = 16;
                    if (!VirtualProtect(xinputTargetAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        Log("VirtualProtect failed for XInput target (continuing) - GetLastError: " + std::to_string(GetLastError()));
                    }
                    else {
                        Log("VirtualProtect succeeded for XInput target");
                    }

                    MH_STATUS enableStatus = MH_EnableHook(xinputTargetAddr);
                    Log("XInput - MH_EnableHook result: " + MinHookStatusToString(enableStatus));

                    if (oldProtect != 0) {
                        DWORD tmp = 0;
                        VirtualProtect(xinputTargetAddr, patchSize, oldProtect, &tmp);
                    }

                    if (enableStatus == MH_OK) {
                        Log(std::string("XInput hook successful with ") + dllName);
                        return true;
                    }
                    else {
                        MH_STATUS fallback = MH_EnableHook(MH_ALL_HOOKS);
                        Log("XInput fallback MH_EnableHook(MH_ALL_HOOKS) result: " + MinHookStatusToString(fallback));
                        if (fallback == MH_OK) {
                            Log("XInput hook enabled via fallback!");
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    // Hook Functions - TODAS DEBEN SER STATIC
    static DWORD WINAPI HookedXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
        UWPSplitScreenMod& mod = GetInstance();
        if (!mod.fpXInputGetState) {
            // Si el trampoline no está listo, llamar al original directamente por fallback
            using RawXInput_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
            RawXInput_t raw = reinterpret_cast<RawXInput_t>(mod.xinputTargetAddr);
            if (raw) return raw(dwUserIndex, pState);
            return ERROR_DEVICE_NOT_CONNECTED;
        }

        DWORD result = mod.fpXInputGetState(dwUserIndex, pState);

        if (dwUserIndex <= 1 && result == ERROR_SUCCESS) {
            static int logCounter = 0;
            if (++logCounter % 300 == 0) {
                mod.Log("XInput Controller " + std::to_string(dwUserIndex) +
                    " active (packet: " + std::to_string(pState->dwPacketNumber) + ")");
            }
        }
        return result;
    }

    static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        UWPSplitScreenMod& mod = GetInstance();

        static bool firstCall = true;
        if (firstCall) {
            mod.Log("*** PRESENT HOOK EXECUTED FOR FIRST TIME! SPLIT SCREEN MOD IS WORKING! ***");
            firstCall = false;
        }

        static int frameCount = 0;
        frameCount++;

        if (frameCount % 60 == 0) {
            mod.Log("Present hook active - Frame " + std::to_string(frameCount) + " rendered");
        }

        if (!pSwapChain) {
            mod.Log("ERROR: pSwapChain is NULL in Present hook");
            return E_INVALIDARG;
        }

        if (mod.splitScreenActive) {
            mod.RenderSplitScreen(pSwapChain);
        }

        if (!mod.fpPresent) {
            // Si el trampoline no está listo, intentar llamar directamente a la dirección objetivo como fallback
            if (mod.presentTargetAddr) {
                using RawPresent_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
                RawPresent_t raw = reinterpret_cast<RawPresent_t>(mod.presentTargetAddr);
                if (raw) {
                    return raw(pSwapChain, SyncInterval, Flags);
                }
            }
            mod.Log("ERROR: fpPresent (trampoline) is NULL!");
            return E_FAIL;
        }

        return mod.fpPresent(pSwapChain, SyncInterval, Flags);
    }

public:
    ~UWPSplitScreenMod() {
        if (initialized) {
            MH_Uninitialize();
            Log("=== UWP Split Screen Mod Shutdown ===");
        }
        if (logFile.is_open()) {
            logFile.close();
        }
    }
};

// Función de inicialización global
void InitializeMod() {
    std::ofstream initLog("UWPSplitScreen_Init.log", std::ios::out | std::ios::app);
    if (initLog.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char timeStr[26];
        ctime_s(timeStr, sizeof(timeStr), &now);
        timeStr[24] = '\0';

        initLog << "[" << timeStr << "] InitializeMod() called from proxy..." << std::endl;
        initLog.close();
    }

    try {
        UWPSplitScreenMod::GetInstance().Initialize();
    }
    catch (const std::exception& e) {
        std::ofstream errorLog("UWPSplitScreen_Exception.log", std::ios::out | std::ios::app);
        if (errorLog.is_open()) {
            errorLog << "Exception during mod initialization: " << e.what() << std::endl;
            errorLog.close();
        }
    }
    catch (...) {
        std::ofstream errorLog("UWPSplitScreen_Exception.log", std::ios::out | std::ios::app);
        if (errorLog.is_open()) {
            errorLog << "Unknown exception during mod initialization!" << std::endl;
            errorLog.close();
        }
    }
}
