// UWP_SplitScreenMod_Complete.cpp
// Versión completa mejorada con scanner de offsets integrado
// Para Halo MCC Microsoft Store v1.3385.0.0

#include "pch.h"
#include "UWP_Detection.h"
#include "UWP_MemoryPatterns.h"
#include "MinHook.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <xinput.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
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
#include <unordered_map>
#include <queue>

using namespace DirectX;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 2
#endif

// ============================================================================
// ESTRUCTURAS Y CLASSES AUXILIARES
// ============================================================================

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

// Scanner de offsets mejorado
class HaloMCCOffsetScanner {
public:
    struct ScanPatterns {
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
        
        LogToFile("=== Starting offset scan for Halo MCC 1.3385.0.0 ===");
        
        HMODULE gameModule = GetGameModule();
        if (!gameModule) {
            LogToFile("ERROR: No se pudo encontrar el módulo del juego");
            return offsets;
        }
        
        MODULEINFO modInfo;
        GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo));
        
        uintptr_t baseAddress = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
        size_t moduleSize = modInfo.SizeOfImage;
        
        LogToFile("Módulo base: 0x" + ToHexString(baseAddress));
        LogToFile("Tamaño módulo: 0x" + ToHexString(moduleSize));
        
        // Escanear patrones
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

private:
    static HMODULE GetGameModule() {
        const char* moduleNames[] = {
            "MCCWinStore-Win64-Shipping.exe",  // Microsoft Store
            "MCC-Win64-Shipping.exe",          // Steam
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
    
    static uintptr_t ScanSplitScreenCheck(uintptr_t baseAddress, size_t moduleSize) {
        LogToFile("Buscando patrón de verificación split-screen...");
        
        auto pattern = ScanPatterns::GetSplitScreenCheckPattern();
        auto mask = ScanPatterns::GetSplitScreenCheckMask();
        
        for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
            if (PatternMatch(baseAddress + i, pattern, mask)) {
                uint32_t relativeAddr = *reinterpret_cast<uint32_t*>(baseAddress + i + 2);
                uintptr_t targetAddr = baseAddress + i + 7 + relativeAddr;
                
                LogToFile("✓ Split-screen check encontrado en: 0x" + ToHexString(baseAddress + i));
                LogToFile("  -> Apunta a: 0x" + ToHexString(targetAddr));
                
                return targetAddr;
            }
        }
        
        LogToFile("✗ Patrón split-screen no encontrado");
        return 0;
    }
    
    static uintptr_t ScanPlayerCount(uintptr_t baseAddress, size_t moduleSize) {
        LogToFile("Buscando patrón de conteo de jugadores...");
        
        auto pattern = ScanPatterns::GetPlayerCountPattern();
        auto mask = ScanPatterns::GetPlayerCountMask();
        
        for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
            if (PatternMatch(baseAddress + i, pattern, mask)) {
                uint32_t relativeAddr = *reinterpret_cast<uint32_t*>(baseAddress + i + 2);
                uintptr_t targetAddr = baseAddress + i + 6 + relativeAddr;
                
                LogToFile("✓ Player count encontrado en: 0x" + ToHexString(baseAddress + i));
                LogToFile("  -> Apunta a: 0x" + ToHexString(targetAddr));
                
                return targetAddr;
            }
        }
        
        LogToFile("✗ Patrón player count no encontrado");
        return 0;
    }
    
    static uintptr_t ScanCameraBase(uintptr_t baseAddress, size_t moduleSize) {
        LogToFile("Buscando patrón de matriz de cámara...");
        
        auto pattern = ScanPatterns::GetCameraMatrixPattern();
        auto mask = ScanPatterns::GetCameraMatrixMask();
        
        for (size_t i = 0; i <= moduleSize - pattern.size(); i++) {
            if (PatternMatch(baseAddress + i, pattern, mask)) {
                uint32_t relativeAddr = *reinterpret_cast<uint32_t*>(baseAddress + i + 3);
                uintptr_t targetAddr = baseAddress + i + 7 + relativeAddr;
                
                LogToFile("✓ Camera matrix encontrada en: 0x" + ToHexString(baseAddress + i));
                LogToFile("  -> Base cámara: 0x" + ToHexString(targetAddr));
                
                return targetAddr;
            }
        }
        
        LogToFile("✗ Patrón camera matrix no encontrado");
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
    
    static std::string ToHexString(uintptr_t value) {
        std::stringstream ss;
        ss << std::hex << std::uppercase << value;
        return ss.str();
    }
    
    static void LogToFile(const std::string& message) {
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
        
        OutputDebugStringA(("[OFFSET_SCANNER] " + message + "\n").c_str());
    }
};

// ============================================================================
// ESTRUCTURAS DE CÁMARA Y RENDERIZADO
// ============================================================================

struct CameraState {
    XMFLOAT3 position;
    XMFLOAT3 rotation;
    XMFLOAT3 forward;
    XMFLOAT3 up;
    XMFLOAT3 right;
    XMMATRIX viewMatrix;
    XMMATRIX projMatrix;
    float fov;
    float aspectRatio;
    float nearPlane;
    float farPlane;
    bool isDirty;
    
    CameraState() : 
        position(0, 0, 0),
        rotation(0, 0, 0),
        forward(0, 0, 1),
        up(0, 1, 0),
        right(1, 0, 0),
        fov(60.0f),
        aspectRatio(16.0f/9.0f),
        nearPlane(0.1f),
        farPlane(1000.0f),
        isDirty(true) {}
    
    void UpdateMatrices() {
        if (!isDirty) return;
        
        XMVECTOR pos = XMLoadFloat3(&position);
        XMVECTOR fwd = XMLoadFloat3(&forward);
        XMVECTOR upVec = XMLoadFloat3(&up);
        
        viewMatrix = XMMatrixLookToLH(pos, fwd, upVec);
        projMatrix = XMMatrixPerspectiveFovLH(
            XMConvertToRadians(fov),
            aspectRatio,
            nearPlane,
            farPlane
        );
        
        isDirty = false;
    }
};

struct PlayerState {
    int playerSlot;
    int controllerIndex;
    CameraState camera;
    bool active;
    XINPUT_STATE lastInput;
    float movementSpeed;
    float rotationSpeed;
    
    ID3D11RenderTargetView* renderTarget;
    ID3D11DepthStencilView* depthStencil;
    ID3D11Texture2D* renderTexture;
    ID3D11ShaderResourceView* shaderResourceView;
    
    PlayerState() : 
        playerSlot(-1),
        controllerIndex(-1),
        active(false),
        movementSpeed(5.0f),
        rotationSpeed(2.0f),
        renderTarget(nullptr),
        depthStencil(nullptr),
        renderTexture(nullptr),
        shaderResourceView(nullptr) {
        ZeroMemory(&lastInput, sizeof(XINPUT_STATE));
    }
    
    ~PlayerState() {
        ReleaseRenderTargets();
    }
    
    void ReleaseRenderTargets() {
        SAFE_RELEASE(shaderResourceView);
        SAFE_RELEASE(renderTarget);
        SAFE_RELEASE(depthStencil);
        SAFE_RELEASE(renderTexture);
    }
};

struct RenderPipeline {
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain* swapChain;
    
    ID3D11RenderTargetView* originalRenderTarget;
    ID3D11DepthStencilView* originalDepthStencil;
    
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;
    ID3D11InputLayout* inputLayout;
    ID3D11Buffer* vertexBuffer;
    ID3D11SamplerState* samplerState;
    
    D3D11_VIEWPORT fullscreenViewport;
    std::array<D3D11_VIEWPORT, MAX_PLAYERS> playerViewports;
    
    bool initialized;
    
    RenderPipeline() : 
        device(nullptr),
        context(nullptr),
        swapChain(nullptr),
        originalRenderTarget(nullptr),
        originalDepthStencil(nullptr),
        vertexShader(nullptr),
        pixelShader(nullptr),
        inputLayout(nullptr),
        vertexBuffer(nullptr),
        samplerState(nullptr),
        initialized(false) {}
    
    ~RenderPipeline() {
        Cleanup();
    }
    
    void Cleanup() {
        SAFE_RELEASE(samplerState);
        SAFE_RELEASE(vertexBuffer);
        SAFE_RELEASE(inputLayout);
        SAFE_RELEASE(pixelShader);
        SAFE_RELEASE(vertexShader);
        SAFE_RELEASE(originalDepthStencil);
        SAFE_RELEASE(originalRenderTarget);
        initialized = false;
    }
};

// ============================================================================
// CLASE PRINCIPAL MEJORADA
// ============================================================================

class UWPSplitScreenMod {
private:
    // Estado
    std::atomic<bool> initialized{ false };
    std::atomic<bool> splitScreenActive{ false };
    std::atomic<bool> renderingInProgress{ false };
    GamePlatform platform = GamePlatform::UNKNOWN;
    GameVersion currentGame = GameVersion::UNKNOWN_GAME;
    
    // Renderizado
    RenderPipeline renderPipeline;
    std::mutex renderMutex;
    
    // ========================================
    // NUEVAS VARIABLES PARA OFFSETS
    // ========================================
    GameOffsets gameOffsets;
    bool offsetsScanned = false;
    std::mutex offsetMutex;
    int lastKnownPlayerCount = 1;
    bool lastKnownSplitScreenState = false;
    std::chrono::steady_clock::time_point lastOffsetScanTime;
    
    // Hook management
    typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
    typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    typedef void(STDMETHODCALLTYPE* DrawIndexed_t)(ID3D11DeviceContext*, UINT, UINT, INT);
    typedef void(STDMETHODCALLTYPE* Draw_t)(ID3D11DeviceContext*, UINT, UINT);
    typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
    
    Present_t fpPresent = nullptr;
    ResizeBuffers_t fpResizeBuffers = nullptr;
    DrawIndexed_t fpDrawIndexed = nullptr;
    Draw_t fpDraw = nullptr;
    XInputGetState_t fpXInputGetState = nullptr;
    
    void* presentTargetAddr = nullptr;
    void* resizeBuffersTargetAddr = nullptr;
    void* drawIndexedTargetAddr = nullptr;
    void* drawTargetAddr = nullptr;
    void* xinputTargetAddr = nullptr;
    
    // Players
    std::array<PlayerState, MAX_PLAYERS> players;
    int numPlayers = MAX_PLAYERS;
    int currentRenderingPlayer = -1;
    
    // Background threads
    std::thread hookThread;
    std::thread controllerWatchThread;
    std::thread cameraUpdateThread;
    std::thread hotkeyThread;
    std::atomic<bool> stopThreads{ false };
    
    // Logging
    std::mutex logMutex;
    std::ofstream logFile;
    std::atomic<uint64_t> frameCounter{ 0 };
    
    // Performance metrics
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    float deltaTime = 0.016f;

    // ========================================
    // MÉTODOS PARA MANEJO DE OFFSETS
    // ========================================

    bool ScanGameOffsetsOnce() {
        std::lock_guard<std::mutex> lock(offsetMutex);
        
        if (offsetsScanned) {
            return gameOffsets.valid;
        }
        
        Log("=== Iniciando escaneo de offsets ===");
        Log("Versión del juego: " + GameVersionToString(currentGame));
        Log("Plataforma: " + PlatformToString(platform));
        
        lastOffsetScanTime = std::chrono::steady_clock::now();
        
        // Usar el scanner automático
        gameOffsets = HaloMCCOffsetScanner::ScanForOffsets();
        offsetsScanned = true;
        
        if (gameOffsets.valid) {
            Log("✓ Offsets escaneados exitosamente");
            LogFoundOffsets();
            TestOffsetsInitial();
            return true;
        } else {
            Log("✗ Escaneo automático falló - intentando offsets conocidos");
            return TryFallbackOffsets();
        }
    }
    
    void LogFoundOffsets() {
        Log("=== OFFSETS ENCONTRADOS ===");
        
        if (gameOffsets.playerCountOffset) {
            Log("Player Count: 0x" + ToHexString(gameOffsets.playerCountOffset));
        }
        
        if (gameOffsets.splitScreenEnabledOffset) {
            Log("Split Screen Flag: 0x" + ToHexString(gameOffsets.splitScreenEnabledOffset));
        }
        
        if (gameOffsets.cameraBaseOffset) {
            Log("Camera Base: 0x" + ToHexString(gameOffsets.cameraBaseOffset));
        }
        
        Log("============================");
    }
    
    void TestOffsetsInitial() {
        Log("=== TESTING INICIAL DE OFFSETS ===");
        
        try {
            if (gameOffsets.playerCountOffset) {
                int currentPlayers = ReadPlayerCount();
                Log("Jugadores actuales: " + std::to_string(currentPlayers));
            }
            
            if (gameOffsets.splitScreenEnabledOffset) {
                bool splitEnabled = ReadSplitScreenEnabled();
                Log("Split-screen: " + std::string(splitEnabled ? "HABILITADO" : "DESHABILITADO"));
            }
            
        } catch (const std::exception& e) {
            Log("Error en test inicial: " + std::string(e.what()));
        }
        
        Log("=== FIN TEST INICIAL ===");
    }
    
    bool TryFallbackOffsets() {
        Log("=== Intentando offsets conocidos ===");
        
        gameOffsets = GameOffsets{};
        
        // AQUÍ PONES LOS OFFSETS QUE ENCUENTRES CON CHEAT ENGINE
        if (platform == GamePlatform::MICROSOFT_STORE) {
            
            if (currentGame == GameVersion::HALO_CE) {
                Log("Cargando offsets para Halo: Combat Evolved");
                
                // REEMPLAZAR ESTOS VALORES CON LOS OFFSETS REALES:
                // gameOffsets.playerCountOffset = 0x????????;        // TU OFFSET AQUÍ
                // gameOffsets.splitScreenEnabledOffset = 0x????????; // TU OFFSET AQUÍ
                // gameOffsets.cameraBaseOffset = 0x????????;         // TU OFFSET AQUÍ
                
                // Ejemplo de como quedaría cuando encuentres los offsets:
                // gameOffsets.playerCountOffset = 0x1A2B3C40;
                // gameOffsets.splitScreenEnabledOffset = 0x1A2B3C44;
                // gameOffsets.cameraBaseOffset = 0x1A2B4000;
                
                // Offsets relativos de cámara (típicamente son fijos)
                gameOffsets.viewMatrixOffset = 0x40;  
                gameOffsets.projMatrixOffset = 0x80;  
                gameOffsets.positionOffset = 0x10;    
                gameOffsets.rotationOffset = 0x1C;    
            }
            else if (currentGame == GameVersion::HALO_REACH) {
                Log("Cargando offsets para Halo: Reach");
                
                // REEMPLAZAR CON TUS OFFSETS:
                // gameOffsets.playerCountOffset = 0x????????;        
                // gameOffsets.splitScreenEnabledOffset = 0x????????; 
                // gameOffsets.cameraBaseOffset = 0x????????;         
            }
            else if (currentGame == GameVersion::HALO_2) {
                Log("Cargando offsets para Halo 2");
                
                // REEMPLAZAR CON TUS OFFSETS:
                // gameOffsets.playerCountOffset = 0x????????;        
                // gameOffsets.splitScreenEnabledOffset = 0x????????; 
                // gameOffsets.cameraBaseOffset = 0x????????;         
            }
            else if (currentGame == GameVersion::HALO_3) {
                Log("Cargando offsets para Halo 3");
                
                // REEMPLAZAR CON TUS OFFSETS:
                // gameOffsets.playerCountOffset = 0x????????;        
                // gameOffsets.splitScreenEnabledOffset = 0x????????; 
                // gameOffsets.cameraBaseOffset = 0x????????;         
            }
        }
        else if (platform == GamePlatform::STEAM) {
            Log("Plataforma Steam - offsets diferentes necesarios");
            // Steam tendrá offsets completamente diferentes
        }
        
        // Validar offsets
        gameOffsets.valid = (gameOffsets.playerCountOffset != 0) && 
                           (gameOffsets.splitScreenEnabledOffset != 0);
        
        Log(gameOffsets.valid ? "✓ Fallback offsets cargados" : "✗ No hay offsets disponibles");
        
        if (gameOffsets.valid) {
            TestOffsetsInitial();
        }
        
        return gameOffsets.valid;
    }

    // ========================================
    // MÉTODOS PARA LEER/ESCRIBIR MEMORIA
    // ========================================
    
    int ReadPlayerCount() {
        if (!gameOffsets.valid || !gameOffsets.playerCountOffset) {
            return 1;
        }
        
        return ReadMemoryValue<int>(gameOffsets.playerCountOffset, 1);
    }
    
    bool ReadSplitScreenEnabled() {
        if (!gameOffsets.valid || !gameOffsets.splitScreenEnabledOffset) {
            return false;
        }
        
        int value = ReadMemoryValue<int>(gameOffsets.splitScreenEnabledOffset, 1);
        return value > 1;
    }
    
    bool WritePlayerCount(int count) {
        if (!gameOffsets.valid || !gameOffsets.playerCountOffset) {
            return false;
        }
        
        return WriteMemoryValue<int>(gameOffsets.playerCountOffset, count);
    }
    
    bool WriteSplitScreenEnabled(int playerCount) {
        if (!gameOffsets.valid || !gameOffsets.splitScreenEnabledOffset) {
            return false;
        }
        
        return WriteMemoryValue<int>(gameOffsets.splitScreenEnabledOffset, playerCount);
    }
    
    template<typename T>
    T ReadMemoryValue(uintptr_t address, T defaultValue) {
        __try {
            return *reinterpret_cast<T*>(address);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("Error leyendo memoria en: 0x" + ToHexString(address));
            return defaultValue;
        }
    }
    
    template<typename T>
    bool WriteMemoryValue(uintptr_t address, T value) {
        return WriteProtectedMemory(address, &value, sizeof(T));
    }
    
    bool WriteProtectedMemory(uintptr_t address, const void* buffer, size_t size) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, 
                           PAGE_EXECUTE_READWRITE, &oldProtect)) {
            Log("VirtualProtect falló para: 0x" + ToHexString(address));
            return false;
        }
        
        __try {
            memcpy(reinterpret_cast<void*>(address), buffer, size);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("Excepción escribiendo memoria en: 0x" + ToHexString(address));
            
            DWORD temp;
            VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &temp);
            return false;
        }
        
        DWORD temp;
        VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &temp);
        return true;
    }
    
    std::string ToHexString(uintptr_t value) {
        std::stringstream ss;
        ss << "0x" << std::hex << std::uppercase << value;
        return ss.str();
    }

public:
    static UWPSplitScreenMod& GetInstance() {
        static UWPSplitScreenMod instance;
        return instance;
    }
    
    // ========================================
    // INICIALIZACIÓN MEJORADA
    // ========================================
    
    bool Initialize() {
        if (initialized.load()) {
            Log("Initialize llamado pero ya está inicializado");
            return true;
        }
        
        Log("=== UWP Split Screen Mod Extended Initialize ===");
        
        platform = UWPGameDetector::DetectPlatform();
        currentGame = UWPGameDetector::DetectCurrentGame();
        
        Log("Platform: " + PlatformToString(platform));
        Log("Game: " + GameVersionToString(currentGame));
        
        // Escanear offsets antes de continuar
        if (!ScanGameOffsetsOnce()) {
            Log("ADVERTENCIA: No se pudieron encontrar offsets válidos");
            Log("El mod continuará pero la funcionalidad será limitada");
        }
        
        MH_STATUS st = MH_Initialize();
        if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
            Log("MH_Initialize failed: " + MhStatusToStr(st));
            return false;
        }
        
        InitializePlayers();
        
        stopThreads.store(false);
        
        // Start background threads
        hookThread = std::thread([this]() { this->DelayedHookInstallation(); });
        controllerWatchThread = std::thread([this]() { this->ControllerWatcherLoop(); });
        cameraUpdateThread = std::thread([this]() { this->CameraUpdateLoop(); });
        hotkeyThread = std::thread([this]() { this->HotkeyLoop(); });
        
        initialized.store(true);
        Log("Initialize: completado exitosamente");
        return true;
    }
    
    void Cleanup() {
        if (!initialized.load()) return;
        
        Log("Cleanup: starting");
        
        stopThreads.store(true);
        
        if (hookThread.joinable()) hookThread.join();
        if (controllerWatchThread.joinable()) controllerWatchThread.join();
        if (cameraUpdateThread.joinable()) cameraUpdateThread.join();
        if (hotkeyThread.joinable()) hotkeyThread.join();
        
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        
        renderPipeline.Cleanup();
        
        for (auto& player : players) {
            player.ReleaseRenderTargets();
        }
        
        initialized.store(false);
        Log("Cleanup: completado");
    }
    
    // ========================================
    // MÉTODOS PÚBLICOS NUEVOS
    // ========================================
    
    void ToggleSplitScreen() {
        if (!gameOffsets.valid) {
            Log("No se puede cambiar split-screen - no hay offsets válidos");
            return;
        }
        
        bool currentlyEnabled = ReadSplitScreenEnabled();
        int currentPlayerCount = ReadPlayerCount();
        
        Log("Estado actual - Jugadores: " + std::to_string(currentPlayerCount) + 
            ", Split-screen: " + (currentlyEnabled ? "ON" : "OFF"));
        
        if (currentlyEnabled || currentPlayerCount > 1) {
            Log("Deshabilitando split-screen");
            WriteSplitScreenEnabled(1);
            WritePlayerCount(1);
            splitScreenActive.store(false);
        } else {
            Log("Habilitando split-screen");
            WriteSplitScreenEnabled(2);
            WritePlayerCount(2);
            splitScreenActive.store(true);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        bool newState = ReadSplitScreenEnabled();
        int newCount = ReadPlayerCount();
        
        Log("Nuevo estado - Jugadores: " + std::to_string(newCount) + 
            ", Split-screen: " + (newState ? "ON" : "OFF"));
    }
    
    bool IsSplitScreenCurrentlyEnabled() {
        return ReadSplitScreenEnabled();
    }
    
    int GetCurrentPlayerCount() {
        return ReadPlayerCount();
    }
    
    void TestOffsets() {
        if (!gameOffsets.valid) {
            Log("=== TEST DE OFFSETS - SIN OFFSETS VÁLIDOS ===");
            return;
        }
        
        Log("=== TEST MANUAL DE OFFSETS ===");
        
        int currentPlayers = GetCurrentPlayerCount();
        bool splitEnabled = IsSplitScreenCurrentlyEnabled();
        
        Log("Estado inicial:");
        Log("  Jugadores: " + std::to_string(currentPlayers));
        Log("  Split-screen: " + std::string(splitEnabled ? "HABILITADO" : "DESHABILITADO"));
        
        Log("Probando toggle...");
        ToggleSplitScreen();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        int newPlayers = GetCurrentPlayerCount();
        bool newSplitEnabled = IsSplitScreenCurrentlyEnabled();
        
        Log("Estado después del toggle:");
        Log("  Jugadores: " + std::to_string(newPlayers));
        Log("  Split-screen: " + std::string(newSplitEnabled ? "HABILITADO" : "DESHABILITADO"));
        
        if (currentPlayers != newPlayers || splitEnabled != newSplitEnabled) {
            Log("✓ TEST EXITOSO - Los valores cambiaron");
        } else {
            Log("✗ TEST FALLÓ - No se detectaron cambios");
            Log("  Posibles causas:");
            Log("  - Offsets incorrectos");
            Log("  - Memoria protegida");
            Log("  - El juego revierte los cambios");
        }
        
        Log("=== FIN TEST MANUAL ===");
    }
    
    void ExportOffsets() {
        if (!gameOffsets.valid) {
            Log("No hay offsets válidos para exportar");
            return;
        }
        
        std::string filename = "HaloMCC_Offsets_v1.3385.0.0_" + 
                              GameVersionToString(currentGame) + ".txt";
        
        std::replace(filename.begin(), filename.end(), ' ', '_');
        std::replace(filename.begin(), filename.end(), ':', '');
        
        std::ofstream exportFile(filename);
        if (!exportFile.is_open()) {
            Log("Error creando archivo de exportación");
            return;
        }
        
        exportFile << "// Halo MCC v1.3385.0.0 Offsets\n";
        exportFile << "// Juego: " << GameVersionToString(currentGame) << "\n";
        exportFile << "// Plataforma: " << PlatformToString(platform) << "\n";
        exportFile << "// Generado: " << GetCurrentTimeString() << "\n\n";
        
        exportFile << "// Offsets absolutos (para código C++):\n";
        exportFile << std::hex << std::uppercase;
        exportFile << "playerCountOffset = 0x" << gameOffsets.playerCountOffset << ";\n";
        exportFile << "splitScreenEnabledOffset = 0x" << gameOffsets.splitScreenEnabledOffset << ";\n";
        exportFile << "cameraBaseOffset = 0x" << gameOffsets.cameraBaseOffset << ";\n\n";
        
        uintptr_t moduleBase = GetModuleBaseAddress();
        if (moduleBase) {
            exportFile << "// Offsets relativos (para Cheat Engine):\n";
            exportFile << "MCCWinStore-Win64-Shipping.exe+" << std::hex 
                      << (gameOffsets.playerCountOffset - moduleBase) 
                      << " = Player Count\n";
            exportFile << "MCCWinStore-Win64-Shipping.exe+" << std::hex 
                      << (gameOffsets.splitScreenEnabledOffset - moduleBase) 
                      << " = Split Screen Flag\n\n";
        }
        
        exportFile << "// Script para Cheat Engine:\n";
        exportFile << "[ENABLE]\n";
        if (moduleBase) {
            exportFile << "writeInteger(\"MCCWinStore-Win64-Shipping.exe+" 
                      << std::hex << (gameOffsets.splitScreenEnabledOffset - moduleBase)
                      << "\", 2)\n";
        }
        exportFile << "\n[DISABLE]\n";
        if (moduleBase) {
            exportFile << "writeInteger(\"MCCWinStore-Win64-Shipping.exe+" 
                      << std::hex << (gameOffsets.splitScreenEnabledOffset - moduleBase)
                      << "\", 1)\n";
        }
        
        exportFile.close();
        Log("Offsets exportados a: " + filename);
    }

private:
    // ========================================
    // INICIALIZACIÓN DE JUGADORES
    // ========================================
    
    void InitializePlayers() {
        for (int i = 0; i < numPlayers; ++i) {
            players[i].playerSlot = i;
            players[i].controllerIndex = -1;
            players[i].active = false;
            
            players[i].camera.position.x = i * 2.0f;
            players[i].camera.position.y = 1.7f;
            players[i].camera.position.z = -5.0f;
            
            players[i].camera.UpdateMatrices();
        }
        Log("Players initialized");
    }

    // ========================================
    // PIPELINE DE RENDERIZADO
    // ========================================
    
    bool InitializeRenderPipeline(IDXGISwapChain* pSwapChain) {
        if (renderPipeline.initialized) return true;
        
        Log("InitializeRenderPipeline: starting");
        
        HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&renderPipeline.device);
        if (FAILED(hr)) {
            Log("Failed to get D3D11 device from swap chain");
            return false;
        }
        
        renderPipeline.device->GetImmediateContext(&renderPipeline.context);
        renderPipeline.swapChain = pSwapChain;
        
        ID3D11Texture2D* backBuffer = nullptr;
        hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr)) {
            Log("Failed to get backbuffer");
            return false;
        }
        
        D3D11_TEXTURE2D_DESC backBufferDesc;
        backBuffer->GetDesc(&backBufferDesc);
        
        renderPipeline.fullscreenViewport.Width = (float)backBufferDesc.Width;
        renderPipeline.fullscreenViewport.Height = (float)backBufferDesc.Height;
        renderPipeline.fullscreenViewport.MinDepth = 0.0f;
        renderPipeline.fullscreenViewport.MaxDepth = 1.0f;
        renderPipeline.fullscreenViewport.TopLeftX = 0;
        renderPipeline.fullscreenViewport.TopLeftY = 0;
        
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            renderPipeline.playerViewports[i] = renderPipeline.fullscreenViewport;
            
            if (numPlayers == 2) {
                renderPipeline.playerViewports[i].Height = backBufferDesc.Height / 2.0f;
                renderPipeline.playerViewports[i].TopLeftY = i * (backBufferDesc.Height / 2.0f);
            }
        }
        
        for (int i = 0; i < numPlayers; ++i) {
            if (!CreatePlayerRenderTarget(i, backBufferDesc.Width, backBufferDesc.Height)) {
                Log("Failed to create render target for player " + std::to_string(i));
                backBuffer->Release();
                return false;
            }
        }
        
        if (!CreateCompositionShaders()) {
            Log("Failed to create composition shaders");
            backBuffer->Release();
            return false;
        }
        
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        
        hr = renderPipeline.device->CreateSamplerState(&samplerDesc, &renderPipeline.samplerState);
        if (FAILED(hr)) {
            Log("Failed to create sampler state");
            backBuffer->Release();
            return false;
        }
        
        backBuffer->Release();
        
        renderPipeline.initialized = true;
        Log("InitializeRenderPipeline: completed successfully");
        return true;
    }
    
    bool CreatePlayerRenderTarget(int playerIndex, UINT width, UINT height) {
        if (playerIndex >= numPlayers) return false;
        
        PlayerState& player = players[playerIndex];
        player.ReleaseRenderTargets();
        
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        
        HRESULT hr = renderPipeline.device->CreateTexture2D(&textureDesc, nullptr, &player.renderTexture);
        if (FAILED(hr)) return false;
        
        hr = renderPipeline.device->CreateRenderTargetView(player.renderTexture, nullptr, &player.renderTarget);
        if (FAILED(hr)) return false;
        
        hr = renderPipeline.device->CreateShaderResourceView(player.renderTexture, nullptr, &player.shaderResourceView);
        if (FAILED(hr)) return false;
        
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        
        ID3D11Texture2D* depthTexture = nullptr;
        hr = renderPipeline.device->CreateTexture2D(&depthDesc, nullptr, &depthTexture);
        if (FAILED(hr)) return false;
        
        hr = renderPipeline.device->CreateDepthStencilView(depthTexture, nullptr, &player.depthStencil);
        depthTexture->Release();
        
        return SUCCEEDED(hr);
    }
    
    bool CreateCompositionShaders() {
        const char* vsSource = R"(
            struct VS_INPUT {
                float2 pos : POSITION;
                float2 tex : TEXCOORD0;
            };
            
            struct PS_INPUT {
                float4 pos : SV_POSITION;
                float2 tex : TEXCOORD0;
            };
            
            PS_INPUT main(VS_INPUT input) {
                PS_INPUT output;
                output.pos = float4(input.pos.x, input.pos.y, 0.0f, 1.0f);
                output.tex = input.tex;
                return output;
            }
        )";
        
        const char* psSource = R"(
            Texture2D tex : register(t0);
            SamplerState samp : register(s0);
            
            struct PS_INPUT {
                float4 pos : SV_POSITION;
                float2 tex : TEXCOORD0;
            };
            
            float4 main(PS_INPUT input) : SV_TARGET {
                return tex.Sample(samp, input.tex);
            }
        )";
        
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        
        HRESULT hr = D3DCompile(vsSource, strlen(vsSource), "vs", nullptr, nullptr, 
                                "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                Log("VS compile error: " + std::string((char*)errorBlob->GetBufferPointer()));
                errorBlob->Release();
            }
            return false;
        }
        
        hr = D3DCompile(psSource, strlen(psSource), "ps", nullptr, nullptr,
                       "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                Log("PS compile error: " + std::string((char*)errorBlob->GetBufferPointer()));
                errorBlob->Release();
            }
            vsBlob->Release();
            return false;
        }
        
        hr = renderPipeline.device->CreateVertexShader(vsBlob->GetBufferPointer(), 
                                                       vsBlob->GetBufferSize(), 
                                                       nullptr, &renderPipeline.vertexShader);
        if (FAILED(hr)) {
            vsBlob->Release();
            psBlob->Release();
            return false;
        }
        
        hr = renderPipeline.device->CreatePixelShader(psBlob->GetBufferPointer(),
                                                      psBlob->GetBufferSize(),
                                                      nullptr, &renderPipeline.pixelShader);
        if (FAILED(hr)) {
            vsBlob->Release();
            psBlob->Release();
            return false;
        }
        
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        
        hr = renderPipeline.device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(),
                                                      vsBlob->GetBufferSize(), &renderPipeline.inputLayout);
        
        vsBlob->Release();
        psBlob->Release();
        
        if (FAILED(hr)) return false;
        
        struct Vertex {
            float x, y, u, v;
        };
        
        Vertex vertices[] = {
            { -1.0f,  1.0f, 0.0f, 0.0f },
            {  1.0f,  1.0f, 1.0f, 0.0f },
            { -1.0f, -1.0f, 0.0f, 1.0f },
            {  1.0f, -1.0f, 1.0f, 1.0f }
        };
        
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.ByteWidth = sizeof(vertices);
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertices;
        
        hr = renderPipeline.device->CreateBuffer(&bufferDesc, &initData, &renderPipeline.vertexBuffer);
        
        return SUCCEEDED(hr);
    }
    
    void RenderSplitScreen(IDXGISwapChain* pSwapChain) {
        if (!renderPipeline.initialized) {
            if (!InitializeRenderPipeline(pSwapChain)) {
                return;
            }
        }
        
        std::lock_guard<std::mutex> lock(renderMutex);
        
        ID3D11RenderTargetView* currentRTV = nullptr;
        ID3D11DepthStencilView* currentDSV = nullptr;
        renderPipeline.context->OMGetRenderTargets(1, &currentRTV, &currentDSV);
        
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr)) return;
        
        ID3D11RenderTargetView* backBufferRTV = nullptr;
        hr = renderPipeline.device->CreateRenderTargetView(backBuffer, nullptr, &backBufferRTV);
        backBuffer->Release();
        if (FAILED(hr)) return;
        
        float clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        renderPipeline.context->ClearRenderTargetView(backBufferRTV, clearColor);
        
        for (int i = 0; i < numPlayers; ++i) {
            if (!players[i].active) continue;
            
            renderPipeline.context->RSSetViewports(1, &renderPipeline.playerViewports[i]);
            RenderPlayerView(i, backBufferRTV);
        }
        
        renderPipeline.context->RSSetViewports(1, &renderPipeline.fullscreenViewport);
        
        backBufferRTV->Release();
        
        renderPipeline.context->OMSetRenderTargets(1, &currentRTV, currentDSV);
        if (currentRTV) currentRTV->Release();
        if (currentDSV) currentDSV->Release();
    }
    
    void RenderPlayerView(int playerIndex, ID3D11RenderTargetView* targetRTV) {
        PlayerState& player = players[playerIndex];
        
        renderPipeline.context->OMSetRenderTargets(1, &targetRTV, nullptr);
        
        float color[4];
        if (playerIndex == 0) {
            color[0] = 0.2f; color[1] = 0.3f; color[2] = 0.8f; color[3] = 1.0f; // Blue tint
        } else {
            color[0] = 0.8f; color[1] = 0.3f; color[2] = 0.2f; color[3] = 1.0f; // Red tint
        }
        
        D3D11_VIEWPORT vp;
        UINT numViewports = 1;
        renderPipeline.context->RSGetViewports(&numViewports, &vp);
        
        InjectPlayerCamera(playerIndex);
    }

    // ========================================
    // INYECCIÓN DE CÁMARA MEJORADA
    // ========================================
    
    void InjectPlayerCamera(int playerIndex) {
        if (!gameOffsets.valid || !gameOffsets.cameraBaseOffset) {
            return;
        }
        
        PlayerState& player = players[playerIndex];
        player.camera.UpdateMatrices();
        
        uintptr_t cameraBase = gameOffsets.cameraBaseOffset;
        
        try {
            if (gameOffsets.viewMatrixOffset) {
                uintptr_t viewMatrixAddr = cameraBase + gameOffsets.viewMatrixOffset;
                WriteProtectedMemory(viewMatrixAddr, &player.camera.viewMatrix, sizeof(XMMATRIX));
            }
            
            if (gameOffsets.projMatrixOffset) {
                uintptr_t projMatrixAddr = cameraBase + gameOffsets.projMatrixOffset;
                WriteProtectedMemory(projMatrixAddr, &player.camera.projMatrix, sizeof(XMMATRIX));
            }
            
            if (gameOffsets.positionOffset) {
                uintptr_t positionAddr = cameraBase + gameOffsets.positionOffset;
                WriteProtectedMemory(positionAddr, &player.camera.position, sizeof(XMFLOAT3));
            }
            
            if (gameOffsets.rotationOffset) {
                uintptr_t rotationAddr = cameraBase + gameOffsets.rotationOffset;
                WriteProtectedMemory(rotationAddr, &player.camera.rotation, sizeof(XMFLOAT3));
            }
            
        }
        catch (...) {
            Log("Exception injecting camera for player " + std::to_string(playerIndex));
        }
    }

    // ========================================
    // HOOKS MEJORADOS
    // ========================================
    
    static HRESULT STDMETHODCALLTYPE Present_Hook_Static(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        return GetInstance().Present_Hook(pSwapChain, SyncInterval, Flags);
    }
    
    static HRESULT STDMETHODCALLTYPE ResizeBuffers_Hook_Static(IDXGISwapChain* pSwapChain, UINT BufferCount, 
                                                               UINT Width, UINT Height, DXGI_FORMAT Format, UINT Flags) {
        return GetInstance().ResizeBuffers_Hook(pSwapChain, BufferCount, Width, Height, Format, Flags);
    }
    
    static void STDMETHODCALLTYPE DrawIndexed_Hook_Static(ID3D11DeviceContext* pContext, UINT IndexCount, 
                                                          UINT StartIndex, INT BaseVertex) {
        GetInstance().DrawIndexed_Hook(pContext, IndexCount, StartIndex, BaseVertex);
    }
    
    static void STDMETHODCALLTYPE Draw_Hook_Static(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertex) {
        GetInstance().Draw_Hook(pContext, VertexCount, StartVertex);
    }
    
    static DWORD WINAPI XInputGetState_Hook_Static(DWORD dwUserIndex, XINPUT_STATE* pState) {
        return GetInstance().XInputGetState_Hook(dwUserIndex, pState);
    }
    
    HRESULT Present_Hook(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        uint64_t fc = ++frameCounter;
        
        auto currentTime = std::chrono::high_resolution_clock::now();
        if (lastFrameTime.time_since_epoch().count() > 0) {
            deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        }
        lastFrameTime = currentTime;
        
        if (fc % 300 == 0) {
            Log("Frame " + std::to_string(fc) + " | FPS: " + std::to_string(1.0f / deltaTime));
            
            if (gameOffsets.valid) {
                int players = GetCurrentPlayerCount();
                bool splitEnabled = IsSplitScreenCurrentlyEnabled();
                
                Log("Estado juego - Jugadores: " + std::to_string(players) + 
                    " | Split: " + (splitEnabled ? "ON" : "OFF"));
                
                lastKnownPlayerCount = players;
                lastKnownSplitScreenState = splitEnabled;
            }
        }
        
        bool shouldRenderSplitScreen = false;
        
        if (gameOffsets.valid && !renderingInProgress.exchange(true)) {
            int currentPlayers = GetCurrentPlayerCount();
            bool gameHasSplitScreen = IsSplitScreenCurrentlyEnabled();
            
            shouldRenderSplitScreen = gameHasSplitScreen && currentPlayers > 1;
            splitScreenActive.store(shouldRenderSplitScreen);
            
        } else if (!gameOffsets.valid && !renderingInProgress.exchange(true)) {
            shouldRenderSplitScreen = splitScreenActive.load();
        }
        
        if (shouldRenderSplitScreen) {
            try {
                RenderSplitScreen(pSwapChain);
            }
            catch (...) {
                Log("Excepción en RenderSplitScreen");
            }
            renderingInProgress.store(false);
        }
        
        if (fpPresent) {
            return fpPresent(pSwapChain, SyncInterval, Flags);
        }
        
        return S_OK;
    }
    
    HRESULT ResizeBuffers_Hook(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, 
                               DXGI_FORMAT Format, UINT Flags) {
        Log("ResizeBuffers called: " + std::to_string(Width) + "x" + std::to_string(Height));
        
        renderPipeline.Cleanup();
        for (auto& player : players) {
            player.ReleaseRenderTargets();
        }
        
        HRESULT hr = fpResizeBuffers ? fpResizeBuffers(pSwapChain, BufferCount, Width, Height, Format, Flags) : S_OK;
        
        if (SUCCEEDED(hr)) {
            renderPipeline.initialized = false;
        }
        
        return hr;
    }
    
    void DrawIndexed_Hook(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndex, INT BaseVertex) {
        if (currentRenderingPlayer >= 0 && currentRenderingPlayer < numPlayers) {
            // Could intercept specific draw calls here
        }
        
        if (fpDrawIndexed) {
            fpDrawIndexed(pContext, IndexCount, StartIndex, BaseVertex);
        }
    }
    
    void Draw_Hook(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertex) {
        if (currentRenderingPlayer >= 0 && currentRenderingPlayer < numPlayers) {
            // Could intercept specific draw calls here
        }
        
        if (fpDraw) {
            fpDraw(pContext, VertexCount, StartVertex);
        }
    }
    
    DWORD XInputGetState_Hook(DWORD dwUserIndex, XINPUT_STATE* pState) {
        DWORD result = ERROR_DEVICE_NOT_CONNECTED;
        
        if (fpXInputGetState) {
            result = fpXInputGetState(dwUserIndex, pState);
        }
        
        for (int i = 0; i < numPlayers; ++i) {
            if (players[i].controllerIndex == (int)dwUserIndex && result == ERROR_SUCCESS) {
                players[i].lastInput = *pState;
                break;
            }
        }
        
        return result;
    }

    // ========================================
    // LOOPS DE BACKGROUND
    // ========================================
    
    void DelayedHookInstallation() {
        Log("DelayedHookInstallation: starting");
        
        const int maxAttempts = 30;
        for (int attempt = 1; attempt <= maxAttempts && !stopThreads.load(); ++attempt) {
            Log("Hook installation attempt " + std::to_string(attempt));
            
            if (!IsGameReadyForHooking()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                continue;
            }
            
            bool success = true;
            
            if (!HookXInput()) {
                Log("XInput hook failed");
                success = false;
            }
            
            if (!HookD3D11()) {
                Log("D3D11 hooks failed");
                success = false;
            }
            
            if (success) {
                splitScreenActive.store(true);
                Log("All hooks installed successfully");
                return;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }
        
        Log("DelayedHookInstallation: max attempts reached");
    }
    
    void ControllerWatcherLoop() {
        Log("ControllerWatcherLoop: started");
        
        while (!stopThreads.load()) {
            UpdateControllerMappings();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        Log("ControllerWatcherLoop: stopped");
    }
    
    void CameraUpdateLoop() {
        Log("CameraUpdateLoop: started");
        
        while (!stopThreads.load()) {
            UpdatePlayerCameras();
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
        
        Log("CameraUpdateLoop: stopped");
    }
    
    // ========================================
    // LOOP DE HOTKEYS
    // ========================================
    
    void HotkeyLoop() {
        Log("HotkeyLoop: started");
        Log("Hotkeys disponibles:");
        Log("  F9 - Toggle Split-Screen");
        Log("  F10 - Test Offsets");
        Log("  F11 - Export Offsets");
        Log("  F12 - Rescan Offsets");
        
        bool f9Pressed = false;
        bool f10Pressed = false;
        bool f11Pressed = false;
        bool f12Pressed = false;
        
        while (!stopThreads.load()) {
            // F9 para toggle split-screen
            if (GetAsyncKeyState(VK_F9) & 0x8000) {
                if (!f9Pressed) {
                    f9Pressed = true;
                    Log("F9 pressed - toggling split-screen");
                    ToggleSplitScreen();
                }
            } else {
                f9Pressed = false;
            }
            
            // F10 para test de offsets
            if (GetAsyncKeyState(VK_F10) & 0x8000) {
                if (!f10Pressed) {
                    f10Pressed = true;
                    Log("F10 pressed - testing offsets");
                    TestOffsets();
                }
            } else {
                f10Pressed = false;
            }
            
            // F11 para exportar offsets
            if (GetAsyncKeyState(VK_F11) & 0x8000) {
                if (!f11Pressed) {
                    f11Pressed = true;
                    Log("F11 pressed - exporting offsets");
                    ExportOffsets();
                }
            } else {
                f11Pressed = false;
            }
            
            // F12 para re-escanear offsets
            if (GetAsyncKeyState(VK_F12) & 0x8000) {
                if (!f12Pressed) {
                    f12Pressed = true;
                    Log("F12 pressed - rescanning offsets");
                    RescanOffsets();
                }
            } else {
                f12Pressed = false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        Log("HotkeyLoop: stopped");
    }
    
    void RescanOffsets() {
        Log("=== RESCAN DE OFFSETS ===");
        
        offsetsScanned = false;
        gameOffsets = GameOffsets{};
        
        if (ScanGameOffsetsOnce()) {
            Log("✓ Rescan exitoso");
        } else {
            Log("✗ Rescan falló");
        }
    }

    // ========================================
    // UTILIDADES DE JUEGO
    // ========================================
    
    bool IsGameReadyForHooking() {
        HWND gameWindow = FindGameWindow();
        if (!gameWindow) {
            Log("Game window not found");
            return false;
        }
        
        if (!GetModuleHandleA("d3d11.dll") || !GetModuleHandleA("dxgi.dll")) {
            Log("D3D11/DXGI not loaded");
            return false;
        }
        
        return true;
    }
    
    HWND FindGameWindow() {
        HWND result = nullptr;
        
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            char title[512];
            GetWindowTextA(hwnd, title, sizeof(title));
            std::string titleStr(title);
            
            if (titleStr.find("Halo") != std::string::npos || 
                titleStr.find("MCC") != std::string::npos ||
                titleStr.find("Master Chief Collection") != std::string::npos) {
                
                if (IsWindowVisible(hwnd)) {
                    *reinterpret_cast<HWND*>(lParam) = hwnd;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&result));
        
        return result;
    }
    
    bool HookD3D11() {
        Log("HookD3D11: creating temporary device for vtable access");
        
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
        
        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &pSwapChain, &pDevice, &featureLevel, &pContext);
        
        if (FAILED(hr)) {
            Log("Failed to create D3D11 device");
            return false;
        }
        
        bool success = true;
        
        void** swapChainVTable = *reinterpret_cast<void***>(pSwapChain);
        presentTargetAddr = swapChainVTable[8];
        resizeBuffersTargetAddr = swapChainVTable[13];
        
        MH_STATUS status = MH_CreateHook(presentTargetAddr, &Present_Hook_Static, 
                                         reinterpret_cast<LPVOID*>(&fpPresent));
        if (status != MH_OK) {
            Log("Failed to create Present hook: " + MhStatusToStr(status));
            success = false;
        } else {
            status = MH_EnableHook(presentTargetAddr);
            if (status != MH_OK) {
                Log("Failed to enable Present hook: " + MhStatusToStr(status));
                success = false;
            }
        }
        
        status = MH_CreateHook(resizeBuffersTargetAddr, &ResizeBuffers_Hook_Static,
                              reinterpret_cast<LPVOID*>(&fpResizeBuffers));
        if (status != MH_OK) {
            Log("Failed to create ResizeBuffers hook: " + MhStatusToStr(status));
        } else {
            MH_EnableHook(resizeBuffersTargetAddr);
        }
        
        void** contextVTable = *reinterpret_cast<void***>(pContext);
        drawIndexedTargetAddr = contextVTable[12];
        drawTargetAddr = contextVTable[13];
        
        status = MH_CreateHook(drawIndexedTargetAddr, &DrawIndexed_Hook_Static,
                              reinterpret_cast<LPVOID*>(&fpDrawIndexed));
        if (status == MH_OK) {
            MH_EnableHook(drawIndexedTargetAddr);
        }
        
        status = MH_CreateHook(drawTargetAddr, &Draw_Hook_Static,
                              reinterpret_cast<LPVOID*>(&fpDraw));
        if (status == MH_OK) {
            MH_EnableHook(drawTargetAddr);
        }
        
        pSwapChain->Release();
        pContext->Release();
        pDevice->Release();
        
        return success;
    }
    
    bool HookXInput() {
        Log("HookXInput: attempting to hook XInputGetState");
        
        const char* modules[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        
        for (const char* moduleName : modules) {
            HMODULE hModule = GetModuleHandleA(moduleName);
            if (!hModule) {
                hModule = LoadLibraryA(moduleName);
            }
            
            if (!hModule) continue;
            
            void* proc = GetProcAddress(hModule, "XInputGetState");
            if (!proc) continue;
            
            xinputTargetAddr = proc;
            Log("Found XInputGetState in " + std::string(moduleName));
            
            MH_STATUS status = MH_CreateHook(proc, &XInputGetState_Hook_Static,
                                            reinterpret_cast<LPVOID*>(&fpXInputGetState));
            if (status != MH_OK) {
                Log("Failed to create XInput hook: " + MhStatusToStr(status));
                continue;
            }
            
            status = MH_EnableHook(proc);
            if (status != MH_OK) {
                Log("Failed to enable XInput hook: " + MhStatusToStr(status));
                continue;
            }
            
            Log("XInput hook installed successfully");
            return true;
        }
        
        return false;
    }
    
    void UpdateControllerMappings() {
        std::vector<int> connectedControllers;
        
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE state;
            DWORD result = ERROR_DEVICE_NOT_CONNECTED;
            
            if (fpXInputGetState) {
                result = fpXInputGetState(i, &state);
            } else {
                typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
                HMODULE hModule = GetModuleHandleA("xinput1_4.dll");
                if (hModule) {
                    auto func = reinterpret_cast<XInputGetState_t>(GetProcAddress(hModule, "XInputGetState"));
                    if (func) result = func(i, &state);
                }
            }
            
            if (result == ERROR_SUCCESS) {
                connectedControllers.push_back(i);
            }
        }
        
        for (int i = 0; i < numPlayers; ++i) {
            int oldController = players[i].controllerIndex;
            
            if (i < (int)connectedControllers.size()) {
                players[i].controllerIndex = connectedControllers[i];
                players[i].active = true;
                
                if (oldController != connectedControllers[i]) {
                    Log("Player " + std::to_string(i) + " mapped to controller " + 
                        std::to_string(connectedControllers[i]));
                }
            } else {
                players[i].controllerIndex = -1;
                players[i].active = false;
                
                if (oldController != -1) {
                    Log("Player " + std::to_string(i) + " controller disconnected");
                }
            }
        }
    }
    
    void UpdatePlayerCameras() {
        for (int i = 0; i < numPlayers; ++i) {
            if (!players[i].active) continue;
            
            PlayerState& player = players[i];
            XINPUT_STATE& input = player.lastInput;
            
            float moveSpeed = player.movementSpeed * deltaTime;
            float rotSpeed = player.rotationSpeed * deltaTime;
            
            float rx = input.Gamepad.sThumbRX / 32768.0f;
            float ry = input.Gamepad.sThumbRY / 32768.0f;
            
            if (fabs(rx) > 0.15f || fabs(ry) > 0.15f) {
                player.camera.rotation.y += rx * rotSpeed;
                player.camera.rotation.x -= ry * rotSpeed;
                player.camera.rotation.x = max(-89.0f, min(89.0f, player.camera.rotation.x));
                player.camera.isDirty = true;
            }
            
            float mx = input.Gamepad.sThumbLX / 32768.0f;
            float my = input.Gamepad.sThumbLY / 32768.0f;
            
            if (fabs(mx) > 0.15f || fabs(my) > 0.15f) {
                float yaw = XMConvertToRadians(player.camera.rotation.y);
                float pitch = XMConvertToRadians(player.camera.rotation.x);
                
                XMVECTOR forward = XMVectorSet(
                    sinf(yaw) * cosf(pitch),
                    sinf(pitch),
                    cosf(yaw) * cosf(pitch),
                    0.0f
                );
                
                XMVECTOR right = XMVectorSet(
                    cosf(yaw),
                    0.0f,
                    -sinf(yaw),
                    0.0f
                );
                
                XMVECTOR movement = XMVectorScale(forward, my * moveSpeed);
                movement = XMVectorAdd(movement, XMVectorScale(right, mx * moveSpeed));
                
                XMFLOAT3 delta;
                XMStoreFloat3(&delta, movement);
                
                player.camera.position.x += delta.x;
                player.camera.position.y += delta.y;
                player.camera.position.z += delta.z;
                player.camera.isDirty = true;
            }
            
            if (player.camera.isDirty) {
                UpdateCameraVectors(player.camera);
            }
        }
    }
    
    void UpdateCameraVectors(CameraState& camera) {
        float yaw = XMConvertToRadians(camera.rotation.y);
        float pitch = XMConvertToRadians(camera.rotation.x);
        
        camera.forward.x = sinf(yaw) * cosf(pitch);
        camera.forward.y = sinf(pitch);
        camera.forward.z = cosf(yaw) * cosf(pitch);
        
        XMVECTOR fwd = XMLoadFloat3(&camera.forward);
        fwd = XMVector3Normalize(fwd);
        XMStoreFloat3(&camera.forward, fwd);
        
        XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        XMVECTOR right = XMVector3Cross(worldUp, fwd);
        right = XMVector3Normalize(right);
        XMStoreFloat3(&camera.right, right);
        
        XMVECTOR up = XMVector3Cross(fwd, right);
        up = XMVector3Normalize(up);
        XMStoreFloat3(&camera.up, up);
        
        camera.UpdateMatrices();
    }

    // ========================================
    // UTILIDADES Y HELPERS
    // ========================================
    
    uintptr_t GetModuleBaseAddress() {
        HMODULE gameModule = GetModuleHandleA("MCCWinStore-Win64-Shipping.exe");
        if (!gameModule) {
            gameModule = GetModuleHandleA("MCC-Win64-Shipping.exe");
        }
        if (!gameModule) {
            gameModule = GetModuleHandleA(nullptr);
        }
        return reinterpret_cast<uintptr_t>(gameModule);
    }
    
    std::string GetCurrentTimeString() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    void Log(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        
        if (!logFile.is_open()) {
            logFile.open("UWPSplitScreen_Extended.log", std::ios::out | std::ios::app);
        }
        
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
        
        OutputDebugStringA(("[UWP_SPLITSCREEN] " + message + "\n").c_str());
    }
    
    std::string MhStatusToStr(MH_STATUS status) {
        switch (status) {
            case MH_OK: return "MH_OK";
            case MH_ERROR_ALREADY_INITIALIZED: return "MH_ERROR_ALREADY_INITIALIZED";
            case MH_ERROR_NOT_INITIALIZED: return "MH_ERROR_NOT_INITIALIZED";
            case MH_ERROR_ALREADY_CREATED: return "MH_ERROR_ALREADY_CREATED";
            case MH_ERROR_NOT_CREATED: return "MH_ERROR_NOT_CREATED";
            case MH_ERROR_ENABLED: return "MH_ERROR_ENABLED";
            case MH_ERROR_DISABLED: return "MH_ERROR_DISABLED";
            case MH_ERROR_NOT_EXECUTABLE: return "MH_ERROR_NOT_EXECUTABLE";
            case MH_ERROR_UNSUPPORTED_FUNCTION: return "MH_ERROR_UNSUPPORTED_FUNCTION";
            case MH_ERROR_MEMORY_ALLOC: return "MH_ERROR_MEMORY_ALLOC";
            case MH_ERROR_MEMORY_PROTECT: return "MH_ERROR_MEMORY_PROTECT";
            default: return "MH_UNKNOWN(" + std::to_string(status) + ")";
        }
    }
    
    std::string PlatformToString(GamePlatform platform) {
        switch (platform) {
            case GamePlatform::STEAM: return "Steam";
            case GamePlatform::MICROSOFT_STORE: return "Microsoft Store";
            default: return "Unknown";
        }
    }
    
    std::string GameVersionToString(GameVersion version) {
        switch (version) {
            case GameVersion::HALO_CE: return "Halo: Combat Evolved";
            case GameVersion::HALO_2: return "Halo 2";
            case GameVersion::HALO_2A: return "Halo 2 Anniversary";
            case GameVersion::HALO_3: return "Halo 3";
            case GameVersion::HALO_REACH: return "Halo: Reach";
            case GameVersion::HALO_4: return "Halo 4";
            default: return "Unknown Game";
        }
    }
};

// ============================================================================
// FUNCIONES DE EXPORTACIÓN
// ============================================================================

extern "C" __declspec(dllexport) void InitializeMod() {
    try {
        UWPSplitScreenMod::GetInstance().Initialize();
    }
    catch (const std::exception& e) {
        std::ofstream errorLog("UWPSplitScreen_Error.log", std::ios::out | std::ios::app);
        if (errorLog.is_open()) {
            errorLog << "Exception in InitializeMod: " << e.what() << std::endl;
            errorLog.close();
        }
    }
}

extern "C" __declspec(dllexport) void CleanupMod() {
    try {
        UWPSplitScreenMod::GetInstance().Cleanup();
    }
    catch (...) {
        // Silent cleanup
    }
}

// Funciones adicionales para control externo
extern "C" __declspec(dllexport) void ToggleSplitScreen() {
    try {
        UWPSplitScreenMod::GetInstance().ToggleSplitScreen();
    }
    catch (...) {}
}

extern "C" __declspec(dllexport) void TestOffsets() {
    try {
        UWPSplitScreenMod::GetInstance().TestOffsets();
    }
    catch (...) {}
}

extern "C" __declspec(dllexport) void ExportOffsets() {
    try {
        UWPSplitScreenMod::GetInstance().ExportOffsets();
    }
    catch (...) {}
}

extern "C" __declspec(dllexport) bool IsSplitScreenEnabled() {
    try {
        return UWPSplitScreenMod::GetInstance().IsSplitScreenCurrentlyEnabled();
    }
    catch (...) {
        return false;
    }
}

extern "C" __declspec(dllexport) int GetPlayerCount() {
    try {
        return UWPSplitScreenMod::GetInstance().GetCurrentPlayerCount();
    }
    catch (...) {
        return 1;
    }
}

// ============================================================================
// DLL ENTRY POINT
// ============================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            
            // Log del proceso que nos carga
            std::ofstream attachLog("UWPSplitScreen_Attach.log", std::ios::out | std::ios::app);
            if (attachLog.is_open()) {
                char processName[MAX_PATH];
                GetModuleFileNameA(NULL, processName, MAX_PATH);
                
                auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                char timeStr[26];
                ctime_s(timeStr, sizeof(timeStr), &now);
                timeStr[24] = '\0';
                
                attachLog << "[" << timeStr << "] DLL attached to: " << processName << std::endl;
                attachLog << "Process ID: " << GetCurrentProcessId() << std::endl;
                attachLog.close();
            }
            
            // Inicialización retardada
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                InitializeMod();
                return 0;
            }, nullptr, 0, nullptr);
            break;
            
        case DLL_PROCESS_DETACH:
            CleanupMod();
            
            std::ofstream detachLog("UWPSplitScreen_Attach.log", std::ios::out | std::ios::app);
            if (detachLog.is_open()) {
                auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                char timeStr[26];
                ctime_s(timeStr, sizeof(timeStr), &now);
                timeStr[24] = '\0';
                
                detachLog << "[" << timeStr << "] DLL detached" << std::endl;
                detachLog.close();
            }
            break;
    }
    return TRUE;
}

// ============================================================================
// TESTING MODE (opcional - para development)
// ============================================================================

#ifdef TESTING_MODE
int main() {
    try {
        std::cout << "=== HALO MCC SPLIT SCREEN MOD - TESTING MODE ===" << std::endl;
        
        // Log memory layout
        HMODULE gameModule = GetModuleHandleA("MCCWinStore-Win64-Shipping.exe");
        if (!gameModule) {
            gameModule = GetModuleHandleA("MCC-Win64-Shipping.exe");
        }
        
        if (gameModule) {
            MODULEINFO modInfo;
            GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo));
            
            std::cout << "Module Base: 0x" << std::hex << reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll) << std::endl;
            std::cout << "Module Size: 0x" << std::hex << modInfo.SizeOfImage << std::endl;
        }
        
        // Initialize mod
        if (UWPSplitScreenMod::GetInstance().Initialize()) {
            std::cout << "Mod initialized successfully" << std::endl;
            
            // Wait for user input to test functions
            std::cout << "\nPress keys to test:" << std::endl;
            std::cout << "1 - Toggle Split Screen" << std::endl;
            std::cout << "2 - Test Offsets" << std::endl;
            std::cout << "3 - Export Offsets" << std::endl;
            std::cout << "Q - Quit" << std::endl;
            
            char input;
            while (std::cin >> input && input != 'q' && input != 'Q') {
                switch (input) {
                    case '1':
                        UWPSplitScreenMod::GetInstance().ToggleSplitScreen();
                        break;
                    case '2':
                        UWPSplitScreenMod::GetInstance().TestOffsets();
                        break;
                    case '3':
                        UWPSplitScreenMod::GetInstance().ExportOffsets();
                        break;
                    default:
                        std::cout << "Invalid option" << std::endl;
                        break;
                }
                
                std::cout << "\nPress another key (Q to quit): ";
            }
            
            UWPSplitScreenMod::GetInstance().Cleanup();
        } else {
            std::cout << "Failed to initialize mod" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
    }
    
    return 0;
}
#endif
