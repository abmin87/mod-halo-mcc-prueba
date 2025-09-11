// UWP_SplitScreenMod.cpp
#include "pch.h"
#include "UWP_Detection.h"
#include "UWP_MemoryPatterns.h"
#include "MinHook.h"
#include <d3d11.h>
#include <dxgi.h>
#include <xinput.h>
#include <fstream>

class UWPSplitScreenMod {
private:
    // Estado del mod
    bool initialized = false;
    bool splitScreenActive = false;
    GamePlatform platform = GamePlatform::UNKNOWN;
    GameVersion currentGame = GameVersion::UNKNOWN_GAME;
    
    // Hooks
    typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
    Present_t fpPresent = nullptr;
    
    typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
    XInputGetState_t fpXInputGetState = nullptr;
    
    typedef HRESULT(WINAPI* D3D11CreateDeviceAndSwapChain_t)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    D3D11CreateDeviceAndSwapChain_t fpD3D11CreateDeviceAndSwapChain = nullptr;
    
    // Logging
    std::ofstream logFile;
    
public:
    static UWPSplitScreenMod& GetInstance() {
        static UWPSplitScreenMod instance;
        return instance;
    }
    
    bool Initialize() {
        // Abrir archivo de log
        logFile.open("UWPSplitScreen.log", std::ios::out | std::ios::app);
        Log("=== UWP Split Screen Mod Initialize ===");
        
        // Detectar plataforma
        platform = UWPGameDetector::DetectPlatform();
        if (platform != GamePlatform::MICROSOFT_STORE) {
            Log("ERROR: Not running on Microsoft Store version!");
            return false;
        }
        
        Log("Microsoft Store platform detected");
        
        // Verificar si estamos en UWP container
        if (!UWPGameDetector::IsUWPApplication()) {
            Log("WARNING: Not running in UWP container, may have limited functionality");
        }
        
        // Inicializar MinHook
        if (MH_Initialize() != MH_OK) {
            Log("ERROR: MinHook initialization failed");
            return false;
        }
        
        // Esperar a que el juego esté completamente cargado
        if (!WaitForGameInitialization()) {
            Log("ERROR: Game initialization timeout");
            return false;
        }
        
        // Instalar hooks
        if (!InstallHooks()) {
            Log("ERROR: Failed to install hooks");
            return false;
        }
        
        initialized = true;
        Log("UWP Split Screen Mod initialized successfully");
        return true;
    }
    
private:
    void Log(const std::string& message) {
        if (logFile.is_open()) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char timeStr[26];
            ctime_s(timeStr, sizeof(timeStr), &now);
            // Remover newline del tiempo
            timeStr[24] = '\0';
            logFile << "[" << timeStr << "] " << message << std::endl;
            logFile.flush();
        }
    }
    
    bool WaitForGameInitialization() {
        Log("Waiting for game initialization...");
        
        // Esperar hasta que D3D11.dll esté cargado
        int attempts = 0;
        const int maxAttempts = 30; // 15 segundos
        
        while (attempts < maxAttempts) {
            HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
            HMODULE hDXGI = GetModuleHandleA("dxgi.dll");
            
            if (hD3D11 && hDXGI) {
                // Verificar que el juego específico esté cargado
                currentGame = UWPGameDetector::DetectCurrentGame();
                if (currentGame != GameVersion::UNKNOWN_GAME) {
                    Log("Game detected and D3D11 loaded");
                    return true;
                }
            }
            
            Sleep(500);
            attempts++;
        }
        
        Log("Timeout waiting for game initialization");
        return false;
    }
    
    bool InstallHooks() {
        Log("Installing hooks...");
        
        // Hook XInputGetState primero (más fácil de encontrar)
        if (!HookXInput()) {
            Log("WARNING: XInput hook failed, continuing anyway");
        }
        
        // Hook D3D11CreateDeviceAndSwapChain
        if (!HookD3D11Creation()) {
            Log("ERROR: D3D11 creation hook failed");
            return false;
        }
        
        // Intentar encontrar SwapChain existente usando patrones de memoria
        if (!HookExistingSwapChain()) {
            Log("WARNING: Existing SwapChain hook failed, will try runtime hooking");
        }
        
        Log("Hooks installation completed");
        return true;
    }
    
    bool HookXInput() {
        Log("Hooking XInput...");
        
        // Intentar múltiples versiones de XInput (como hace el mod original)
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
                fpXInputGetState = reinterpret_cast<XInputGetState_t>(
                    GetProcAddress(hXInput, "XInputGetState"));
                
                if (fpXInputGetState) {
                    if (MH_CreateHook(fpXInputGetState, &HookedXInputGetState, 
                                    reinterpret_cast<LPVOID*>(&fpXInputGetState)) == MH_OK) {
                        MH_EnableHook(fpXInputGetState);
                        Log("XInput hook successful with " + std::string(dllName));
                        return true;
                    }
                }
            }
        }
        
        Log("XInput hook failed - all versions tried");
        return false;
    }
    
    bool HookD3D11Creation() {
        Log("Hooking D3D11CreateDeviceAndSwapChain...");
        
        HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
        if (!hD3D11) {
            Log("ERROR: d3d11.dll not found");
            return false;
        }
        
        fpD3D11CreateDeviceAndSwapChain = reinterpret_cast<D3D11CreateDeviceAndSwapChain_t>(
            GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain"));
        
        if (!fpD3D11CreateDeviceAndSwapChain) {
            Log("ERROR: D3D11CreateDeviceAndSwapChain not found");
            return false;
        }
        
        if (MH_CreateHook(fpD3D11CreateDeviceAndSwapChain, &HookedD3D11CreateDeviceAndSwapChain,
                         reinterpret_cast<LPVOID*>(&fpD3D11CreateDeviceAndSwapChain)) != MH_OK) {
            Log("ERROR: Failed to create D3D11CreateDeviceAndSwapChain hook");
            return false;
        }
        
        MH_EnableHook(fpD3D11CreateDeviceAndSwapChain);
        Log("D3D11CreateDeviceAndSwapChain hook successful");
        return true;
    }
    
    bool HookExistingSwapChain() {
        Log("Searching for existing SwapChain...");
        
        // Usar pattern scanning para encontrar SwapChain existente
        UWPMemoryScanner::MemoryPattern presentPattern = 
            UWPMemoryScanner::GetSwapChainPresentPattern();
        
        DWORD_PTR presentAddress = UWPMemoryScanner::FindPattern("dxgi.dll", presentPattern);
        
        if (presentAddress != 0) {
            Log("Found existing Present function at: 0x" + 
                std::to_string(presentAddress));
            
            fpPresent = reinterpret_cast<Present_t>(presentAddress);
            
            if (MH_CreateHook(fpPresent, &HookedPresent, 
                            reinterpret_cast<LPVOID*>(&fpPresent)) == MH_OK) {
                MH_EnableHook(fpPresent);
                Log("Existing SwapChain hook successful");
                return true;
            }
        }
        
        Log("Existing SwapChain not found, will hook during creation");
        return false;
    }
    
    // Hook Functions
    static DWORD WINAPI HookedXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
        UWPSplitScreenMod& mod = GetInstance();
        
        // Log solo para los primeros controladores para evitar spam
        if (dwUserIndex <= 1) {
            mod.Log("XInput Controller " + std::to_string(dwUserIndex) + " accessed");
        }
        
        DWORD result = mod.fpXInputGetState(dwUserIndex, pState);
        
        // Aquí podrías implementar lógica para mapear controles
        // de múltiples jugadores si es necesario
        
        return result;
    }
    
    static HRESULT WINAPI HookedD3D11CreateDeviceAndSwapChain(
        IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
        UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
        UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
        IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
        D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext) {
        
        UWPSplitScreenMod& mod = GetInstance();
        mod.Log("D3D11CreateDeviceAndSwapChain called");
        
        HRESULT hr = mod.fpD3D11CreateDeviceAndSwapChain(
            pAdapter, DriverType, Software, Flags,
            pFeatureLevels, FeatureLevels, SDKVersion,
            pSwapChainDesc, ppSwapChain,
            ppDevice, pFeatureLevel, ppImmediateContext);
        
        if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
            // Hookear el Present del SwapChain recién creado
            void** vtbl = *reinterpret_cast<void***>(*ppSwapChain);
            mod.fpPresent = reinterpret_cast<Present_t>(vtbl[8]); // Present is vtable[8]
            
            if (MH_CreateHook(mod.fpPresent, &HookedPresent, 
                            reinterpret_cast<LPVOID*>(&mod.fpPresent)) == MH_OK) {
                MH_EnableHook(mod.fpPresent);
                mod.Log("SwapChain Present hook successful");
                mod.splitScreenActive = true;
            } else {
                mod.Log("ERROR: Failed to hook SwapChain Present");
            }
        }
        
        return hr;
    }
    
    static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        UWPSplitScreenMod& mod = GetInstance();
        
        if (mod.splitScreenActive) {
            // Implementar split screen rendering
            mod.RenderSplitScreen(pSwapChain);
        }
        
        return mod.fpPresent(pSwapChain, SyncInterval, Flags);
    }
    
    void RenderSplitScreen(IDXGISwapChain* pSwapChain) {
        static int frameCount = 0;
        frameCount++;
        
        // Log cada 120 frames para verificar que está funcionando
        if (frameCount % 120 == 0) {
            Log("Split screen frame " + std::to_string(frameCount));
        }
        
        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        ID3D11RenderTargetView* pRTV = nullptr;
        ID3D11Texture2D* pBackBuffer = nullptr;
        
        HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice);
        if (FAILED(hr) || !pDevice) return;
        
        pDevice->GetImmediateContext(&pContext);
        if (!pContext) {
            pDevice->Release();
            return;
        }
        
        hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        if (FAILED(hr) || !pBackBuffer) {
            pContext->Release();
            pDevice->Release();
            return;
        }
        
        hr = pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRTV);
        if (FAILED(hr) || !pRTV) {
            pBackBuffer->Release();
            pContext->Release();
            pDevice->Release();
            return;
        }
        
        // Configurar viewports para split screen
        D3D11_TEXTURE2D_DESC bbDesc;
        pBackBuffer->GetDesc(&bbDesc);
        
        D3D11_VIEWPORT viewports[2] = {};
        
        // Jugador 1 (izquierda)
        viewports[0].TopLeftX = 0;
        viewports[0].TopLeftY = 0;
        viewports[0].Width = bbDesc.Width / 2.0f;
        viewports[0].Height = static_cast<FLOAT>(bbDesc.Height);
        viewports[0].MinDepth = 0.0f;
        viewports[0].MaxDepth = 1.0f;
        
        // Jugador 2 (derecha)
        viewports[1].TopLeftX = bbDesc.Width / 2.0f;
        viewports[1].TopLeftY = 0;
        viewports[1].Width = bbDesc.Width / 2.0f;
        viewports[1].Height = static_cast<FLOAT>(bbDesc.Height);
        viewports[1].MinDepth = 0.0f;
        viewports[1].MaxDepth = 1.0f;
        
        // Aplicar viewport para cada jugador
        for (int player = 0; player < 2; player++) {
            pContext->OMSetRenderTargets(1, &pRTV, nullptr);
            pContext->RSSetViewports(1, &viewports[player]);
            
            // Aquí iría la lógica de renderizado específica para cada jugador
            // Por ahora solo limpiamos con colores diferentes para testing
            FLOAT clearColor[4] = {
                player == 0 ? 0.1f : 0.3f,  // Rojo
                0.1f,                        // Verde  
                player == 0 ? 0.3f : 0.1f,  // Azul
                1.0f                         // Alpha
            };
            pContext->ClearRenderTargetView(pRTV, clearColor);
        }
        
        // Cleanup
        pRTV->Release();
        pBackBuffer->Release();
        pContext->Release();
        pDevice->Release();
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