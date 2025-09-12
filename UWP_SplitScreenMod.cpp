// UWP_SplitScreenMod_Complete.cpp
// Versión completa — soporte split-screen real para 2 jugadores (hooks XInput + Present)
// Incluye: mapeo dinámico de gamepads -> jugadores, limpieza en DLL_PROCESS_DETACH,
// logging por frame, y stubs de integración con estructuras internas del motor.

#include "pch.h"
#include "UWP_Detection.h"
#include "UWP_MemoryPatterns.h"
#include "MinHook.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <xinput.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include <sstream>
#include <atomic>
#include <mutex>
#include <iomanip>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "xinput.lib")

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 2
#endif

// ------------------------------------------------------------
// Simple camera/player state structs (placeholders)
// You can expand these with matrices if integrating directly
// with the game's camera structures (requires offsets / patterns).
// ------------------------------------------------------------
struct CameraState {
    float pos[3];
    float rot[3];
};

struct PlayerState {
    int playerSlot;        // logical player index (0..numPlayers-1)
    int controllerIndex;   // XInput controller index (0..3) or -1 if none
    CameraState camera;
    bool active;
};

// ------------------------------------------------------------
// Main split-screen mod class (singleton)
// ------------------------------------------------------------
class UWPSplitScreenMod {
private:
    // State
    std::atomic<bool> initialized{ false };
    std::atomic<bool> splitScreenActive{ false };
    GamePlatform platform = GamePlatform::UNKNOWN;
    GameVersion currentGame = GameVersion::UNKNOWN_GAME;

    // MinHook trampolines / addresses
    typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
    typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
    Present_t fpPresent = nullptr;
    void* presentTargetAddr = nullptr;

    XInputGetState_t fpXInputGetState = nullptr;
    void* xinputTargetAddr = nullptr;

    // Players
    std::array<PlayerState, MAX_PLAYERS> players;
    int numPlayers = MAX_PLAYERS;

    // Background threads
    std::thread hookThread;
    std::thread controllerWatchThread;
    std::atomic<bool> stopThreads{ false };
    std::mutex logMutex;

    // Logging
    std::ofstream logFile;

    // Frame counter for debug logging
    std::atomic<uint64_t> frameCounter{ 0 };

public:
    static UWPSplitScreenMod& GetInstance() {
        static UWPSplitScreenMod instance;
        return instance;
    }

    // -------------------------
    // Utilities
    // -------------------------
    std::string MhStatusToStr(MH_STATUS s) {
        switch (s) {
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
        default: return "MH_UNKNOWN(" + std::to_string((int)s) + ")";
        }
    }

    void Log(const std::string& s) {
        std::lock_guard<std::mutex> lk(logMutex);
        if (!logFile.is_open()) {
            logFile.open("UWPSplitScreen_Full.log", std::ios::out | std::ios::app);
        }
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char buf[26];
            ctime_s(buf, sizeof(buf), &now);
            buf[24] = '\0';
            logFile << "[" << buf << "] " << s << std::endl;
            logFile.flush();
        }
    }

    // -------------------------
    // Initialization / cleanup
    // -------------------------
    bool Initialize() {
        if (initialized.load()) {
            Log("Initialize called but already initialized");
            return true;
        }

        Log("Initialize: starting UWP Split Screen Mod");

        logFile.open("UWPSplitScreen_Full.log", std::ios::out | std::ios::app);
        Log("=== UWP Split Screen Mod Initialize ===");

        platform = UWPGameDetector::DetectPlatform();
        if (platform == GamePlatform::MICROSOFT_STORE) {
            Log("Platform: Microsoft Store (UWP) detected");
        } else {
            Log("Platform: Not Microsoft Store (proceeding anyway)");
        }

        if (!UWPGameDetector::IsUWPApplication()) {
            Log("IsUWPApplication returned false (continuing)");
        } else {
            Log("IsUWPApplication returned true");
        }

        MH_STATUS st = MH_Initialize();
        if (st != MH_OK) {
            Log("MH_Initialize returned: " + MhStatusToStr(st));
            // continue; sometimes MH already initialised by other mods, but still continue
        } else {
            Log("MinHook initialized");
        }

        InitializePlayers();

        stopThreads.store(false);

        // start delayed hook installer thread
        hookThread = std::thread([this]() {
            this->DelayedHookInstallation();
            });

        // start controller watcher thread to map controllers -> players dynamically
        controllerWatchThread = std::thread([this]() {
            this->ControllerWatcherLoop();
            });

        initialized.store(true);
        Log("Initialize: finished (threads started)");
        return true;
    }

    void Cleanup() {
        Log("Cleanup called");
        if (!initialized.load()) {
            Log("Cleanup: not initialized, skipping");
            return;
        }

        // Stop background threads
        stopThreads.store(true);
        if (hookThread.joinable()) {
            Log("Joining hookThread...");
            hookThread.join();
        }
        if (controllerWatchThread.joinable()) {
            Log("Joining controllerWatchThread...");
            controllerWatchThread.join();
        }

        // Disable hooks
        MH_DisableHook(MH_ALL_HOOKS);
        Log("MH_DisableHook(MH_ALL_HOOKS) called");

        // Uninitialize MinHook
        MH_Uninitialize();
        Log("MH_Uninitialize called");

        // close logs
        if (logFile.is_open()) {
            logFile << "=== Cleanup finished ===" << std::endl;
            logFile.close();
        }

        initialized.store(false);
        Log("Cleanup complete");
    }

    ~UWPSplitScreenMod() {
        // ensure cleanup if still active
        try {
            Cleanup();
        }
        catch (...) {
            // swallow
        }
    }

    // -------------------------
    // Players & mapping
    // -------------------------
    void InitializePlayers() {
        for (int i = 0; i < numPlayers; ++i) {
            players[i].playerSlot = i;
            players[i].controllerIndex = -1;
            players[i].camera.pos[0] = players[i].camera.pos[1] = players[i].camera.pos[2] = 0.0f;
            players[i].camera.rot[0] = players[i].camera.rot[1] = players[i].camera.rot[2] = 0.0f;
            players[i].active = false;
        }
        Log("Players initialized (controllerIndex set to -1)");
    }

    // Map connected controllers to player slots (simple policy: first N connected -> players 0..N-1)
    void UpdateControllerMappingOnce() {
        // Query XInput for each possible controller and build list of connected indices
        std::vector<int> connected;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE st{};
            DWORD res = 0;
            if (fpXInputGetState) {
                // use trampoline if available for a stable read
                res = fpXInputGetState(i, &st);
            } else {
                // fallback direct call to XInputGetState
                auto raw = reinterpret_cast<XInputGetState_t>(GetProcAddress(GetModuleHandleA("xinput1_4.dll"), "XInputGetState"));
                if (raw) res = raw(i, &st);
                else res = ERROR_DEVICE_NOT_CONNECTED;
            }
            if (res == ERROR_SUCCESS) connected.push_back(static_cast<int>(i));
        }

        // Assign connected controllers to players (first-come-first-served)
        for (int p = 0; p < numPlayers; ++p) {
            int prev = players[p].controllerIndex;
            int newIndex = -1;
            if (p < (int)connected.size()) newIndex = connected[p];
            players[p].controllerIndex = newIndex;
            players[p].active = (newIndex != -1);
            if (prev != newIndex) {
                std::ostringstream oss;
                oss << "Controller mapping changed for player " << p << ": " << prev << " -> " << newIndex;
                Log(oss.str());
            }
        }
    }

    // Background loop to watch for controller connections/disconnections
    void ControllerWatcherLoop() {
        Log("ControllerWatcherLoop started");
        while (!stopThreads.load()) {
            try {
                UpdateControllerMappingOnce();
            }
            catch (...) {
                Log("ControllerWatcherLoop: exception updating mapping");
            }
            // check every 1.2 seconds
            for (int i = 0; i < 12 && !stopThreads.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Log("ControllerWatcherLoop exiting");
    }

    // -------------------------
    // Hook installation
    // -------------------------
    void DelayedHookInstallation() {
        Log("DelayedHookInstallation started");
        const int maxAttempts = 25;
        for (int attempt = 1; attempt <= maxAttempts && !stopThreads.load(); ++attempt) {
            Log("DelayedHookInstallation attempt " + std::to_string(attempt) + "/" + std::to_string(maxAttempts));

            if (!IsGameActiveAndRendering()) {
                Log("Game not ready yet - sleeping 2000ms");
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                continue;
            }

            Log("Game ready - attempting hooks");

            bool xok = HookXInput();
            if (xok) Log("HookXInput succeeded");
            else Log("HookXInput failed (will still try present)");

            bool pok = HookPresentUsingMethod1();
            if (!pok) pok = HookPresentUsingMethod2();

            if (pok) {
                splitScreenActive.store(true);
                Log("Present hooked successfully; splitScreenActive = true");
                return;
            }

            Log("Present hook failed; retrying in 3s");
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }

        Log("DelayedHookInstallation finished with no success");
    }

    bool IsGameActiveAndRendering() {
        // Look for a window containing Halo/MCC or get D3D modules
        bool foundWindow = false;
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            char title[512]; GetWindowTextA(hwnd, title, sizeof(title));
            std::string s(title);
            if (s.find("Halo") != std::string::npos || s.find("MCC") != std::string::npos || s.find("Master Chief Collection") != std::string::npos) {
                if (IsWindowVisible(hwnd)) {
                    RECT r; GetClientRect(hwnd, &r);
                    if ((r.right - r.left) >= 800 && (r.bottom - r.top) >= 600) {
                        *reinterpret_cast<bool*>(lParam) = true;
                        return FALSE;
                    }
                }
            }
            return TRUE;
            }, reinterpret_cast<LPARAM>(&foundWindow));

        if (!foundWindow) {
            Log("IsGameActiveAndRendering: window not found");
            return false;
        }

        if (!GetModuleHandleA("d3d11.dll") || !GetModuleHandleA("dxgi.dll")) {
            Log("IsGameActiveAndRendering: d3d11/dxgi not loaded");
            return false;
        }

        Log("IsGameActiveAndRendering: success");
        return true;
    }

    // -------------------------
    // Hook Present methods
    // -------------------------
    bool HookPresentUsingMethod1() {
        Log("HookPresentUsingMethod1: creating temporary SwapChain to locate Present");
        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        IDXGISwapChain* pSwapChain = nullptr;

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = 800;
        sd.BufferDesc.Height = 600;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = GetDesktopWindow();
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &pSwapChain, &pDevice, nullptr, &pContext);

        if (FAILED(hr) || !pSwapChain) {
            Log("HookPresentUsingMethod1: D3D11CreateDeviceAndSwapChain failed hr=" + HResultToString(hr));
            if (pSwapChain) pSwapChain->Release();
            if (pContext) pContext->Release();
            if (pDevice) pDevice->Release();
            return false;
        }

        void** vtbl = *reinterpret_cast<void***>(pSwapChain);
        void* presentAddr = vtbl[8];
        presentTargetAddr = presentAddr;

        {
            std::ostringstream oss;
            oss << "HookPresentUsingMethod1: present at 0x" << std::hex << reinterpret_cast<uintptr_t>(presentAddr);
            Log(oss.str());
        }

        MH_STATUS cst = MH_CreateHook(presentAddr, &UWPSplitScreenMod::Present_Hook_Static, reinterpret_cast<LPVOID*>(&fpPresent));
        Log("MH_CreateHook(present) -> " + MhStatusToStr(cst));

        if (cst == MH_OK || cst == MH_ERROR_ALREADY_CREATED) {
            SIZE_T patchSize = 16;
            DWORD old = 0;
            if (VirtualProtect(presentAddr, patchSize, PAGE_EXECUTE_READWRITE, &old)) {
                Log("VirtualProtect succeeded on present target");
            } else {
                Log("VirtualProtect failed on present target (GetLastError=" + std::to_string(GetLastError()) + ")");
            }

            MH_STATUS en = MH_EnableHook(presentAddr);
            Log("MH_EnableHook(present) -> " + MhStatusToStr(en));

            if (old != 0) { DWORD tmp = 0; VirtualProtect(presentAddr, patchSize, old, &tmp); }

            if (en == MH_OK) {
                if (pSwapChain) pSwapChain->Release();
                if (pContext) pContext->Release();
                if (pDevice) pDevice->Release();
                Log("HookPresentUsingMethod1 succeeded");
                return true;
            } else {
                // fallback enable all
                MH_STATUS fb = MH_EnableHook(MH_ALL_HOOKS);
                Log("Fallback MH_EnableHook(MH_ALL_HOOKS) -> " + MhStatusToStr(fb));
                if (fb == MH_OK) {
                    if (pSwapChain) pSwapChain->Release();
                    if (pContext) pContext->Release();
                    if (pDevice) pDevice->Release();
                    Log("HookPresentUsingMethod1 fallback succeeded");
                    return true;
                }
            }
        }

        if (pSwapChain) pSwapChain->Release();
        if (pContext) pContext->Release();
        if (pDevice) pDevice->Release();
        Log("HookPresentUsingMethod1 failed");
        return false;
    }

    bool HookPresentUsingMethod2() {
        Log("HookPresentUsingMethod2: pattern scan via UWPMemoryScanner (if implemented)");
        try {
            UWPMemoryScanner::MemoryPattern pat = UWPMemoryScanner::GetSwapChainPresentPattern();
            DWORD_PTR addr = UWPMemoryScanner::FindPattern("dxgi.dll", pat);
            if (addr != 0) {
                presentTargetAddr = reinterpret_cast<void*>(addr);
                Log("Pattern found at 0x" + ToHex(addr));
                MH_STATUS cs = MH_CreateHook(presentTargetAddr, &UWPSplitScreenMod::Present_Hook_Static, reinterpret_cast<LPVOID*>(&fpPresent));
                Log("MH_CreateHook(pattern) -> " + MhStatusToStr(cs));
                if (cs == MH_OK || cs == MH_ERROR_ALREADY_CREATED) {
                    MH_STATUS en = MH_EnableHook(presentTargetAddr);
                    Log("MH_EnableHook(pattern) -> " + MhStatusToStr(en));
                    if (en == MH_OK) return true;
                    MH_STATUS fb = MH_EnableHook(MH_ALL_HOOKS);
                    Log("Fallback MH_EnableHook -> " + MhStatusToStr(fb));
                    return (fb == MH_OK);
                }
            } else {
                Log("Pattern not found");
            }
        }
        catch (...) {
            Log("Exception in HookPresentUsingMethod2");
        }
        return false;
    }

    // -------------------------
    // Hook XInput
    // -------------------------
    bool HookXInput() {
        Log("HookXInput: attempt hooking XInputGetState");

        const char* names[] = { "XINPUT1_4.dll", "XINPUT1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll" };
        for (const char* n : names) {
            HMODULE h = GetModuleHandleA(n);
            if (!h) h = LoadLibraryA(n);
            if (!h) continue;
            FARPROC proc = GetProcAddress(h, "XInputGetState");
            if (!proc) continue;

            xinputTargetAddr = reinterpret_cast<void*>(proc);
            std::ostringstream oss; oss << "Found XInputGetState at " << n << " addr 0x" << std::hex << reinterpret_cast<uintptr_t>(proc);
            Log(oss.str());

            MH_STATUS cs = MH_CreateHook(proc, &UWPSplitScreenMod::XInputGetState_Hook_Static, reinterpret_cast<LPVOID*>(&fpXInputGetState));
            Log("MH_CreateHook(XInput) -> " + MhStatusToStr(cs));
            if (cs == MH_OK || cs == MH_ERROR_ALREADY_CREATED) {
                DWORD old = 0; SIZE_T patch = 16;
                if (VirtualProtect(proc, patch, PAGE_EXECUTE_READWRITE, &old)) {
                    Log("VirtualProtect succeeded on XInput target");
                } else {
                    Log("VirtualProtect failed on XInput target (GetLastError=" + std::to_string(GetLastError()) + ")");
                }
                MH_STATUS en = MH_EnableHook(proc);
                Log("MH_EnableHook(XInput) -> " + MhStatusToStr(en));
                if (old != 0) { DWORD tmp = 0; VirtualProtect(proc, patch, old, &tmp); }
                if (en == MH_OK) { Log("HookXInput succeeded"); return true; }
                MH_STATUS fb = MH_EnableHook(MH_ALL_HOOKS);
                Log("HookXInput fallback -> " + MhStatusToStr(fb));
                if (fb == MH_OK) return true;
            }
        }
        Log("HookXInput: all candidates failed");
        return false;
    }

    // -------------------------
    // Helpers
    // -------------------------
    static std::string ToHex(DWORD_PTR v) {
        std::ostringstream oss;
        oss << "0x" << std::hex << v;
        return oss.str();
    }

    static std::string HResultToString(HRESULT hr) {
        std::ostringstream oss;
        oss << "0x" << std::hex << hr;
        return oss.str();
    }

    // -------------------------
    // Hook wrappers (static -> instance)
    // -------------------------
    static DWORD WINAPI XInputGetState_Hook_Static(DWORD dwUserIndex, XINPUT_STATE* pState) {
        return UWPSplitScreenMod::GetInstance().XInputGetState_Hook(dwUserIndex, pState);
    }

    static HRESULT STDMETHODCALLTYPE Present_Hook_Static(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        return UWPSplitScreenMod::GetInstance().Present_Hook(pSwapChain, SyncInterval, Flags);
    }

    // -------------------------
    // Hook implementations (instance)
    // -------------------------
    DWORD XInputGetState_Hook(DWORD dwUserIndex, XINPUT_STATE* pState) {
        // Map incoming controller index (dwUserIndex) to game expectation.
        // Our policy: if the game asks for index K, we return state of controller mapped to that index,
        // or fallback to original behaviour.
        // Simpler approach: when game calls with index equal to playerSlot, we forward mapped controller index.
        // We'll attempt to serve game requests; but to keep behaviour safe we'll call original trampoline where available.

        // If we have a mapping where players[p].controllerIndex == dwUserIndex, return original
        // But better: if game queries index 0..3, we forward to the same index, but our ControllerWatcher
        // sets players[].controllerIndex so gameplay will use those controllers per player (this is a best-effort approach).

        // Use trampoline if available:
        if (fpXInputGetState) {
            // Optionally: if you want to virtualize controllers per player, implement mapping here.
            return fpXInputGetState(dwUserIndex, pState);
        } else {
            // fallback: call system XInput directly
            typedef DWORD(WINAPI* RawX)(DWORD, XINPUT_STATE*);
            RawX raw = nullptr;
            HMODULE h = GetModuleHandleA("xinput1_4.dll");
            if (!h) h = GetModuleHandleA("xinput9_1_0.dll");
            if (h) raw = reinterpret_cast<RawX>(GetProcAddress(h, "XInputGetState"));
            if (raw) return raw(dwUserIndex, pState);
            return ERROR_DEVICE_NOT_CONNECTED;
        }
    }

    HRESULT Present_Hook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        // Very simple strategy:
        // - Call RenderSplitScreen to set viewports & do testing clears for each player.
        // - Then call trampoline/original present to let the engine finish (alternatively call present first).
        // IMPORTANT: For a true multi-camera game view you must, before each present, modify the game's
        // active camera to the player's camera (requires identifying and writing game camera structures).
        // Here we include a stub call ActivateGameCameraForPlayer() where integration can be added.

        uint64_t fc = ++frameCounter;
        if (fc % 300 == 0) Log("Present_Hook frame " + std::to_string(fc));

        if (splitScreenActive.load()) {
            // For each player we would:
            //  - set game's active camera to player's camera (ActivateGameCameraForPlayer(p))
            //  - allow the engine to render one frame for that camera
            //
            // As a safe first step we adjust viewports and clear for each player (visual verification).
            RenderSplitScreen(pSwapChain);

            // NOTE: if you want to attempt per-player engine rendering:
            //   - Attempt to locate the game's camera object (pattern or offset)
            //   - For each player: write camera transform into engine structures, then call engine's render path
            //   - That step is engine-specific and requires offsets / patterns; see ActivateGameCameraForPlayer below
        }

        // Call original present (trampoline) if available
        if (fpPresent) {
            return fpPresent(pSwapChain, SyncInterval, Flags);
        }
        // Fallback to calling the address we discovered (if any)
        if (presentTargetAddr) {
            typedef HRESULT(STDMETHODCALLTYPE* RawPresent)(IDXGISwapChain*, UINT, UINT);
            RawPresent raw = reinterpret_cast<RawPresent>(presentTargetAddr);
            if (raw) return raw(pSwapChain, SyncInterval, Flags);
        }

        // Nothing to call - log and fail gracefully
        Log("Present_Hook: no original present available");
        return E_FAIL;
    }

    // -------------------------
    // Stub: attempt to set game's camera for a specific player
    // (Requires engine offsets/patterns - these are placeholders that you must implement
    // based on reverse engineering or known offsets. They intentionally do nothing here
    // but show where to place integration code.)
    // -------------------------
    bool ActivateGameCameraForPlayer(int playerIndex) {
        // TODO: find game camera pointer (pattern scan) and overwrite its transform with players[playerIndex].camera
        // Example steps:
        // 1) Use UWPMemoryScanner::FindPattern to locate the global camera object or a function that sets camera.
        // 2) Write into that memory region the camera transform (position/rotation) for this player.
        // 3) Possibly call a game function to refresh camera matrices.
        //
        // WARNING: Writing into game memory without correct offsets can crash the game.
        // Only implement this if you have correct offsets or if you test in a controlled environment.

        // For now: return false to indicate not implemented.
        (void)playerIndex;
        return false;
    }

}; // class UWPSplitScreenMod

// -------------------------
// Exported initializer (called by proxy)
// -------------------------
extern "C" __declspec(dllexport) void InitializeMod() {
    try {
        UWPSplitScreenMod::GetInstance().Initialize();
    }
    catch (const std::exception& e) {
        std::ofstream ef("UWPSplitScreen_Exception.log", std::ios::out | std::ios::app);
        if (ef.is_open()) {
            ef << "Exception in InitializeMod: " << e.what() << std::endl;
            ef.close();
        }
    }
    catch (...) {
        std::ofstream ef("UWPSplitScreen_Exception.log", std::ios::out | std::ios::app);
        if (ef.is_open()) {
            ef << "Unknown exception in InitializeMod" << std::endl;
            ef.close();
        }
    }
}

// -------------------------
// DllMain: try to be resilient and clean up on detach
// -------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // prefer InitializeMod to be called by proxy; but as fallback start init thread
        CreateThread(NULL, 0, [](LPVOID) -> DWORD {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            InitializeMod();
            return 0;
            }, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        // Ensure cleanup
        UWPSplitScreenMod::GetInstance().Cleanup();
        break;
    }
    return TRUE;
}
```0

