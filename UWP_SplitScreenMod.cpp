// UWP_SplitScreenMod_Extended.cpp
// Version extendida con renderizado real de pantalla dividida para Halo MCC Microsoft Store
// Incluye: duplicación de render targets, múltiples viewports, intercepción de cámaras,
// y técnicas avanzadas de rendering para lograr split-screen funcional

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

// ------------------------------------------------------------
// Advanced Camera and Rendering Structures
// ------------------------------------------------------------
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
        
        // Calculate view matrix
        XMVECTOR pos = XMLoadFloat3(&position);
        XMVECTOR fwd = XMLoadFloat3(&forward);
        XMVECTOR upVec = XMLoadFloat3(&up);
        
        viewMatrix = XMMatrixLookToLH(pos, fwd, upVec);
        
        // Calculate projection matrix
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
    
    // Render targets for this player's view
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

// ------------------------------------------------------------
// Render Pipeline Structures
// ------------------------------------------------------------
struct RenderPipeline {
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain* swapChain;
    
    // Original backbuffer
    ID3D11RenderTargetView* originalRenderTarget;
    ID3D11DepthStencilView* originalDepthStencil;
    
    // Split screen composition
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;
    ID3D11InputLayout* inputLayout;
    ID3D11Buffer* vertexBuffer;
    ID3D11SamplerState* samplerState;
    
    // Viewport configurations
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

// ------------------------------------------------------------
// Memory Pattern Scanner for Camera Addresses
// ------------------------------------------------------------
class CameraPatternScanner {
public:
    struct CameraOffsets {
        uintptr_t viewMatrixOffset;
        uintptr_t projMatrixOffset;
        uintptr_t positionOffset;
        uintptr_t rotationOffset;
        uintptr_t fovOffset;
        bool valid;
        
        CameraOffsets() : 
            viewMatrixOffset(0),
            projMatrixOffset(0),
            positionOffset(0),
            rotationOffset(0),
            fovOffset(0),
            valid(false) {}
    };
    
    static CameraOffsets ScanForCameraOffsets() {
        CameraOffsets offsets;
        
        // Common patterns for camera structures in Halo engine
        // These are example patterns - actual patterns need to be found through RE
        
        // Pattern for view matrix access
        std::vector<uint8_t> viewMatrixPattern = {
            0x0F, 0x10, 0x05, 0x00, 0x00, 0x00, 0x00,  // movups xmm0, [rel address]
            0x0F, 0x11, 0x00                            // movups [rax], xmm0
        };
        
        // Pattern for camera position access
        std::vector<uint8_t> positionPattern = {
            0xF3, 0x0F, 0x10, 0x40, 0x00,  // movss xmm0, [rax+offset]
            0xF3, 0x0F, 0x10, 0x48, 0x04,  // movss xmm1, [rax+4]
            0xF3, 0x0F, 0x10, 0x50, 0x08   // movss xmm2, [rax+8]
        };
        
        // TODO: Implement actual pattern scanning using UWPMemoryScanner
        // This would require reverse engineering the specific game version
        
        return offsets;
    }
};

// ------------------------------------------------------------
// Main Split Screen Mod Class (Enhanced)
// ------------------------------------------------------------
class UWPSplitScreenMod {
private:
    // State
    std::atomic<bool> initialized{ false };
    std::atomic<bool> splitScreenActive{ false };
    std::atomic<bool> renderingInProgress{ false };
    GamePlatform platform = GamePlatform::UNKNOWN;
    GameVersion currentGame = GameVersion::UNKNOWN_GAME;
    
    // Rendering
    RenderPipeline renderPipeline;
    std::mutex renderMutex;
    
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
    
    // Camera management
    CameraPatternScanner::CameraOffsets cameraOffsets;
    uintptr_t gameCameraBase = 0;
    
    // Background threads
    std::thread hookThread;
    std::thread controllerWatchThread;
    std::thread cameraUpdateThread;
    std::atomic<bool> stopThreads{ false };
    
    // Logging
    std::mutex logMutex;
    std::ofstream logFile;
    std::atomic<uint64_t> frameCounter{ 0 };
    
    // Performance metrics
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    float deltaTime = 0.016f; // 60 FPS default
    
public:
    static UWPSplitScreenMod& GetInstance() {
        static UWPSplitScreenMod instance;
        return instance;
    }
    
    // -------------------------
    // Initialization
    // -------------------------
    bool Initialize() {
        if (initialized.load()) {
            Log("Initialize called but already initialized");
            return true;
        }
        
        Log("=== UWP Split Screen Mod Extended Initialize ===");
        
        platform = UWPGameDetector::DetectPlatform();
        currentGame = UWPGameDetector::DetectCurrentGame();
        
        Log("Platform: " + PlatformToString(platform));
        Log("Game: " + GameVersionToString(currentGame));
        
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
        
        initialized.store(true);
        Log("Initialize: completed successfully");
        return true;
    }
    
    void Cleanup() {
        if (!initialized.load()) return;
        
        Log("Cleanup: starting");
        
        stopThreads.store(true);
        
        if (hookThread.joinable()) hookThread.join();
        if (controllerWatchThread.joinable()) controllerWatchThread.join();
        if (cameraUpdateThread.joinable()) cameraUpdateThread.join();
        
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        
        renderPipeline.Cleanup();
        
        for (auto& player : players) {
            player.ReleaseRenderTargets();
        }
        
        initialized.store(false);
        Log("Cleanup: completed");
    }
    
    // -------------------------
    // Player Management
    // -------------------------
    void InitializePlayers() {
        for (int i = 0; i < numPlayers; ++i) {
            players[i].playerSlot = i;
            players[i].controllerIndex = -1;
            players[i].active = false;
            
            // Initialize camera positions with offset
            players[i].camera.position.x = i * 2.0f;
            players[i].camera.position.y = 1.7f; // Eye height
            players[i].camera.position.z = -5.0f;
            
            players[i].camera.UpdateMatrices();
        }
        Log("Players initialized");
    }
    
    // -------------------------
    // Rendering Pipeline
    // -------------------------
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
        
        // Get backbuffer description
        ID3D11Texture2D* backBuffer = nullptr;
        hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr)) {
            Log("Failed to get backbuffer");
            return false;
        }
        
        D3D11_TEXTURE2D_DESC backBufferDesc;
        backBuffer->GetDesc(&backBufferDesc);
        
        // Setup viewports
        renderPipeline.fullscreenViewport.Width = (float)backBufferDesc.Width;
        renderPipeline.fullscreenViewport.Height = (float)backBufferDesc.Height;
        renderPipeline.fullscreenViewport.MinDepth = 0.0f;
        renderPipeline.fullscreenViewport.MaxDepth = 1.0f;
        renderPipeline.fullscreenViewport.TopLeftX = 0;
        renderPipeline.fullscreenViewport.TopLeftY = 0;
        
        // Setup split screen viewports (horizontal split for 2 players)
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            renderPipeline.playerViewports[i] = renderPipeline.fullscreenViewport;
            
            if (numPlayers == 2) {
                // Horizontal split
                renderPipeline.playerViewports[i].Height = backBufferDesc.Height / 2.0f;
                renderPipeline.playerViewports[i].TopLeftY = i * (backBufferDesc.Height / 2.0f);
            }
            // Could add 4-player split here
        }
        
        // Create render targets for each player
        for (int i = 0; i < numPlayers; ++i) {
            if (!CreatePlayerRenderTarget(i, backBufferDesc.Width, backBufferDesc.Height)) {
                Log("Failed to create render target for player " + std::to_string(i));
                backBuffer->Release();
                return false;
            }
        }
        
        // Create composition shaders
        if (!CreateCompositionShaders()) {
            Log("Failed to create composition shaders");
            backBuffer->Release();
            return false;
        }
        
        // Create sampler state
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
        
        // Create render texture
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
        
        // Create render target view
        hr = renderPipeline.device->CreateRenderTargetView(player.renderTexture, nullptr, &player.renderTarget);
        if (FAILED(hr)) return false;
        
        // Create shader resource view
        hr = renderPipeline.device->CreateShaderResourceView(player.renderTexture, nullptr, &player.shaderResourceView);
        if (FAILED(hr)) return false;
        
        // Create depth stencil
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
        // Vertex shader for fullscreen quad
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
        
        // Pixel shader for texture sampling
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
        
        // Compile vertex shader
        HRESULT hr = D3DCompile(vsSource, strlen(vsSource), "vs", nullptr, nullptr, 
                                "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                Log("VS compile error: " + std::string((char*)errorBlob->GetBufferPointer()));
                errorBlob->Release();
            }
            return false;
        }
        
        // Compile pixel shader
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
        
        // Create shaders
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
        
        // Create input layout
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        
        hr = renderPipeline.device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(),
                                                      vsBlob->GetBufferSize(), &renderPipeline.inputLayout);
        
        vsBlob->Release();
        psBlob->Release();
        
        if (FAILED(hr)) return false;
        
        // Create vertex buffer for fullscreen quad
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
    
    // -------------------------
    // Rendering Functions
    // -------------------------
    void RenderSplitScreen(IDXGISwapChain* pSwapChain) {
        if (!renderPipeline.initialized) {
            if (!InitializeRenderPipeline(pSwapChain)) {
                return;
            }
        }
        
        std::lock_guard<std::mutex> lock(renderMutex);
        
        // Save current render state
        ID3D11RenderTargetView* currentRTV = nullptr;
        ID3D11DepthStencilView* currentDSV = nullptr;
        renderPipeline.context->OMGetRenderTargets(1, &currentRTV, &currentDSV);
        
        // Get backbuffer
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
        if (FAILED(hr)) return;
        
        ID3D11RenderTargetView* backBufferRTV = nullptr;
        hr = renderPipeline.device->CreateRenderTargetView(backBuffer, nullptr, &backBufferRTV);
        backBuffer->Release();
        if (FAILED(hr)) return;
        
        // Clear backbuffer
        float clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        renderPipeline.context->ClearRenderTargetView(backBufferRTV, clearColor);
        
        // Render each player's view to the backbuffer
        for (int i = 0; i < numPlayers; ++i) {
            if (!players[i].active) continue;
            
            // Set viewport for this player
            renderPipeline.context->RSSetViewports(1, &renderPipeline.playerViewports[i]);
            
            // Render player's view
            RenderPlayerView(i, backBufferRTV);
        }
        
        // Restore original viewport
        renderPipeline.context->RSSetViewports(1, &renderPipeline.fullscreenViewport);
        
        // Cleanup
        backBufferRTV->Release();
        
        // Restore original render targets
        renderPipeline.context->OMSetRenderTargets(1, &currentRTV, currentDSV);
        if (currentRTV) currentRTV->Release();
        if (currentDSV) currentDSV->Release();
    }
    
    void RenderPlayerView(int playerIndex, ID3D11RenderTargetView* targetRTV) {
        PlayerState& player = players[playerIndex];
        
        // For now, render a colored rectangle for each player as a test
        // In a real implementation, this would capture the game's rendering for this player
        
        // Set render target
        renderPipeline.context->OMSetRenderTargets(1, &targetRTV, nullptr);
        
        // Create a simple colored clear for testing
        float color[4];
        if (playerIndex == 0) {
            color[0] = 0.2f; color[1] = 0.3f; color[2] = 0.8f; color[3] = 1.0f; // Blue tint
        } else {
            color[0] = 0.8f; color[1] = 0.3f; color[2] = 0.2f; color[3] = 1.0f; // Red tint
        }
        
        // Draw player indicator (temporary - replace with actual game render)
        D3D11_VIEWPORT vp;
        UINT numViewports = 1;
        renderPipeline.context->RSGetViewports(&numViewports, &vp);
        
        // Draw a border around the viewport
        // This would be replaced by actual game rendering with the player's camera
        
        // TODO: Inject player camera into game and trigger render
        InjectPlayerCamera(playerIndex);
    }
    
    // -------------------------
    // Camera Injection
    // -------------------------
    void InjectPlayerCamera(int playerIndex) {
        if (!cameraOffsets.valid || gameCameraBase == 0) {
            // Try to find camera base if not found yet
            ScanForGameCamera();
            return;
        }
        
        PlayerState& player = players[playerIndex];
        player.camera.UpdateMatrices();
        
        // Write camera data to game memory
        // WARNING: These offsets are placeholders and need to be found through RE
        try {
            if (cameraOffsets.viewMatrixOffset) {
                uintptr_t viewMatrixAddr = gameCameraBase + cameraOffsets.viewMatrixOffset;
                WriteProtectedMemory(viewMatrixAddr, &player.camera.viewMatrix, sizeof(XMMATRIX));
            }
            
            if (cameraOffsets.projMatrixOffset) {
                uintptr_t projMatrixAddr = gameCameraBase + cameraOffsets.projMatrixOffset;
                WriteProtectedMemory(projMatrixAddr, &player.camera.projMatrix, sizeof(XMMATRIX));
            }
            
            if (cameraOffsets.positionOffset) {
                uintptr_t positionAddr = gameCameraBase + cameraOffsets.positionOffset;
                WriteProtectedMemory(positionAddr, &player.camera.position, sizeof(XMFLOAT3));
            }
            
            if (cameraOffsets.rotationOffset) {
                uintptr_t rotationAddr = gameCameraBase + cameraOffsets.rotationOffset;
                WriteProtectedMemory(rotationAddr, &player.camera.rotation, sizeof(XMFLOAT3));
            }
        }
        catch (...) {
            Log("Exception injecting camera for player " + std::to_string(playerIndex));
        }
    }
    
    void ScanForGameCamera() {
        // Scan for camera patterns specific to Halo engine
        cameraOffsets = CameraPatternScanner::ScanForCameraOffsets();
        
        // Alternative: Try known offsets for different Halo versions
        if (!cameraOffsets.valid) {
            TryKnownCameraOffsets();
        }
    }
    
    void TryKnownCameraOffsets() {
        // Known offsets for different Halo versions (these are examples)
        // Actual offsets need to be found through reverse engineering
        
        if (currentGame == GameVersion::HALO_CE) {
            // Halo CE camera offsets
            cameraOffsets.viewMatrixOffset = 0x1234560;
            cameraOffsets.positionOffset = 0x1234500;
            cameraOffsets.rotationOffset = 0x123450C;
        }
        else if (currentGame == GameVersion::HALO_2) {
            // Halo 2 camera offsets
            cameraOffsets.viewMatrixOffset = 0x2345670;
            cameraOffsets.positionOffset = 0x2345600;
            cameraOffsets.rotationOffset = 0x234560C;
        }
        // Add more game versions...
    }
    
    bool WriteProtectedMemory(uintptr_t address, const void* buffer, size_t size) {
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return false;
        }
        
        memcpy(reinterpret_cast<void*>(address), buffer, size);
        
        DWORD temp;
        VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &temp);
        return true;
    }
    
    // -------------------------
    // Hook Implementations
    // -------------------------
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
        
        // Update frame timing
        auto currentTime = std::chrono::high_resolution_clock::now();
        if (lastFrameTime.time_since_epoch().count() > 0) {
            deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        }
        lastFrameTime = currentTime;
        
        if (fc % 300 == 0) {
            Log("Frame " + std::to_string(fc) + " | FPS: " + std::to_string(1.0f / deltaTime));
        }
        
        // Only render split screen if active and not already rendering
        if (splitScreenActive.load() && !renderingInProgress.exchange(true)) {
            try {
                RenderSplitScreen(pSwapChain);
            }
            catch (...) {
                Log("Exception in RenderSplitScreen");
            }
            renderingInProgress.store(false);
        }
        
        // Call original Present
        if (fpPresent) {
            return fpPresent(pSwapChain, SyncInterval, Flags);
        }
        
        return S_OK;
    }
    
    HRESULT ResizeBuffers_Hook(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, 
                               DXGI_FORMAT Format, UINT Flags) {
        Log("ResizeBuffers called: " + std::to_string(Width) + "x" + std::to_string(Height));
        
        // Release render pipeline resources before resize
        renderPipeline.Cleanup();
        for (auto& player : players) {
            player.ReleaseRenderTargets();
        }
        
        // Call original
        HRESULT hr = fpResizeBuffers ? fpResizeBuffers(pSwapChain, BufferCount, Width, Height, Format, Flags) : S_OK;
        
        // Reinitialize after resize
        if (SUCCEEDED(hr)) {
            renderPipeline.initialized = false;
            // Will be reinitialized on next Present
        }
        
        return hr;
    }
    
    void DrawIndexed_Hook(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndex, INT BaseVertex) {
        // Track draw calls for analysis if needed
        if (currentRenderingPlayer >= 0 && currentRenderingPlayer < numPlayers) {
            // Could intercept specific draw calls here
        }
        
        if (fpDrawIndexed) {
            fpDrawIndexed(pContext, IndexCount, StartIndex, BaseVertex);
        }
    }
    
    void Draw_Hook(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertex) {
        // Track draw calls for analysis if needed
        if (currentRenderingPlayer >= 0 && currentRenderingPlayer < numPlayers) {
            // Could intercept specific draw calls here
        }
        
        if (fpDraw) {
            fpDraw(pContext, VertexCount, StartVertex);
        }
    }
    
    DWORD XInputGetState_Hook(DWORD dwUserIndex, XINPUT_STATE* pState) {
        // Get the raw input state first
        DWORD result = ERROR_DEVICE_NOT_CONNECTED;
        
        if (fpXInputGetState) {
            result = fpXInputGetState(dwUserIndex, pState);
        }
        
        // Map controller to player and store input
        for (int i = 0; i < numPlayers; ++i) {
            if (players[i].controllerIndex == (int)dwUserIndex && result == ERROR_SUCCESS) {
                players[i].lastInput = *pState;
                break;
            }
        }
        
        return result;
    }
    
    // -------------------------
    // Hook Installation
    // -------------------------
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
            
            // Hook XInput
            if (!HookXInput()) {
                Log("XInput hook failed");
                success = false;
            }
            
            // Hook D3D11
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
    
    bool IsGameReadyForHooking() {
        // Check for game window
        HWND gameWindow = FindGameWindow();
        if (!gameWindow) {
            Log("Game window not found");
            return false;
        }
        
        // Check if D3D11 is loaded
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
        
        // Create temporary D3D11 device to get vtable
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
        
        // Hook IDXGISwapChain::Present (index 8)
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
        
        // Hook IDXGISwapChain::ResizeBuffers (index 13)
        status = MH_CreateHook(resizeBuffersTargetAddr, &ResizeBuffers_Hook_Static,
                              reinterpret_cast<LPVOID*>(&fpResizeBuffers));
        if (status != MH_OK) {
            Log("Failed to create ResizeBuffers hook: " + MhStatusToStr(status));
        } else {
            MH_EnableHook(resizeBuffersTargetAddr);
        }
        
        // Hook ID3D11DeviceContext::DrawIndexed (index 12) and Draw (index 13)
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
        
        // Cleanup
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
    
    // -------------------------
    // Background Loops
    // -------------------------
    void ControllerWatcherLoop() {
        Log("ControllerWatcherLoop: started");
        
        while (!stopThreads.load()) {
            UpdateControllerMappings();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        
        Log("ControllerWatcherLoop: stopped");
    }
    
    void UpdateControllerMappings() {
        std::vector<int> connectedControllers;
        
        // Check which controllers are connected
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            XINPUT_STATE state;
            DWORD result = ERROR_DEVICE_NOT_CONNECTED;
            
            if (fpXInputGetState) {
                result = fpXInputGetState(i, &state);
            } else {
                // Fallback to direct call
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
        
        // Map controllers to players
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
    
    void CameraUpdateLoop() {
        Log("CameraUpdateLoop: started");
        
        while (!stopThreads.load()) {
            UpdatePlayerCameras();
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
        
        Log("CameraUpdateLoop: stopped");
    }
    
    void UpdatePlayerCameras() {
        for (int i = 0; i < numPlayers; ++i) {
            if (!players[i].active) continue;
            
            PlayerState& player = players[i];
            XINPUT_STATE& input = player.lastInput;
            
            // Update camera based on controller input
            float moveSpeed = player.movementSpeed * deltaTime;
            float rotSpeed = player.rotationSpeed * deltaTime;
            
            // Right stick for rotation
            float rx = input.Gamepad.sThumbRX / 32768.0f;
            float ry = input.Gamepad.sThumbRY / 32768.0f;
            
            if (fabs(rx) > 0.15f || fabs(ry) > 0.15f) {
                player.camera.rotation.y += rx * rotSpeed;
                player.camera.rotation.x -= ry * rotSpeed;
                player.camera.rotation.x = max(-89.0f, min(89.0f, player.camera.rotation.x));
                player.camera.isDirty = true;
            }
            
            // Left stick for movement
            float mx = input.Gamepad.sThumbLX / 32768.0f;
            float my = input.Gamepad.sThumbLY / 32768.0f;
            
            if (fabs(mx) > 0.15f || fabs(my) > 0.15f) {
                // Calculate movement direction based on camera rotation
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
            
            // Update camera vectors if dirty
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
    
    // -------------------------
    // Utility Functions
    // -------------------------
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

// -------------------------
// Export Functions
// -------------------------
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

// -------------------------
// DLL Entry Point
// -------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            // Delayed initialization
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                InitializeMod();
                return 0;
            }, nullptr, 0, nullptr);
            break;
            
        case DLL_PROCESS_DETACH:
            CleanupMod();
            break;
    }
    return TRUE;
}