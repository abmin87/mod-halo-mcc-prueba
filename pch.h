// pch.h - Precompiled header for UWP Split Screen Mod
#pragma once

// Windows Headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wtsapi32.h>
#include <tlhelp32.h>
#include <psapi.h>

// DirectX Headers  
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

// Input Headers
#include <xinput.h>

// Standard Library
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <memory>

// MinHook
#include "MinHook.h"

// Suppress common warnings
#pragma warning(disable: 4996) // 'function': was declared deprecated
#pragma warning(disable: 4244) // conversion from 'type1' to 'type2', possible loss of data

// Link required libraries
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib") 
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "wtsapi32.lib")

// Common macros
#define SAFE_RELEASE(p) if (p) { p->Release(); p = nullptr; }
#define LOG_ERROR(msg) OutputDebugStringA(("[ERROR] " + std::string(msg)).c_str())
#define LOG_INFO(msg) OutputDebugStringA(("[INFO] " + std::string(msg)).c_str())