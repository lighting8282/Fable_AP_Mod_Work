// =============================================================================
// Author       : Yaranorgoth
// Description  : Entry point for the dinput8 proxy DLL.
//                Forwards DirectInput8Create to the real system DLL, loads
//                any *.dll files from the "mods" folder, and triggers the
//                function-prototype dump for Fable.exe on startup.
// =============================================================================

#include "pch.h"

#include "../../mods/shared/mod_log.h"
#include "windowed_hook.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <unknwn.h>
#include <windows.h>

// ----------------------
// Proxy DirectInput8
// ----------------------
HMODULE realDinput8 = nullptr;

typedef HRESULT(WINAPI *DirectInput8Create_t)(HINSTANCE, DWORD, REFIID,
                                              LPVOID *, LPUNKNOWN);

DirectInput8Create_t real_DirectInput8Create = nullptr;

// ----------------------
// Force non-exclusive DirectInput devices
// ----------------------
// Fable acquires its mouse device in exclusive mode, which is what actually
// confines/hides the cursor (independent of window border or ClipCursor).
// We patch IDirectInput8::CreateDevice so every returned device's
// SetCooperativeLevel is forced to exclusive + foreground (instead of
// exclusive + background). Foreground-exclusive devices auto-release when
// the window loses focus (alt-tab, clicking another app) and re-acquire on
// refocus, so the cursor is free outside the window without the desync/
// double-cursor artifact that non-exclusive mode causes (the game keeps its
// own relative-delta-driven cursor even while the real OS cursor moves
// independently).
namespace {

constexpr int kIDI8_CreateDevice = 3;      // IDirectInput8 vtable slot
constexpr int kDevice_SetCoopLevel = 13;   // IDirectInputDevice8 vtable slot
// Mouse: exclusive+foreground — single accurate cursor in-game, auto-released
// on alt-tab/focus loss. Keyboard: non-exclusive+foreground — system keys
// (Print Screen, Win, etc.) keep working while the game is focused.
constexpr DWORD kMouseCoopFlags = 0x1 /*DISCL_EXCLUSIVE*/ | 0x4 /*DISCL_FOREGROUND*/;
constexpr DWORD kKeyboardCoopFlags = 0x2 /*DISCL_NONEXCLUSIVE*/ | 0x4 /*DISCL_FOREGROUND*/;

// {6F1D2B60-D5A0-11CF-BFC7-444553540000}
constexpr GUID kGuidSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

using CreateDevice_IDI8_t = HRESULT(STDMETHODCALLTYPE *)(void *, const GUID &, void **, LPUNKNOWN);
using SetCoopLevel_t = HRESULT(STDMETHODCALLTYPE *)(void *, HWND, DWORD);

CreateDevice_IDI8_t g_origIDI8CreateDevice = nullptr;
SetCoopLevel_t g_origSetCoopLevel = nullptr;

// Device instances created with GUID_SysMouse (vtable is shared across all
// device instances, so the SetCooperativeLevel hook must look up which
// device it was invoked on).
constexpr int kMaxMouseDevices = 8;
void *g_MouseDevices[kMaxMouseDevices] = {};

bool IsMouseDevice(void *pDevice) {
  for (void *p : g_MouseDevices)
    if (p == pDevice)
      return true;
  return false;
}

void RememberMouseDevice(void *pDevice) {
  for (void *&p : g_MouseDevices) {
    if (p == pDevice)
      return;
    if (!p) {
      p = pDevice;
      return;
    }
  }
}

void *PatchVTableSlot(void **vtable, int slot, void *newFn) {
  void **target = &vtable[slot];
  DWORD oldProt = 0;
  if (!VirtualProtect(target, sizeof(void *), PAGE_EXECUTE_READWRITE, &oldProt))
    return nullptr;
  void *old = *target;
  *target = newFn;
  VirtualProtect(target, sizeof(void *), oldProt, &oldProt);
  return old;
}

HRESULT STDMETHODCALLTYPE HookSetCooperativeLevel(void *pThis, HWND hwnd, DWORD) {
  DWORD flags = IsMouseDevice(pThis) ? kMouseCoopFlags : kKeyboardCoopFlags;
  return g_origSetCoopLevel(pThis, hwnd, flags);
}

HRESULT STDMETHODCALLTYPE HookIDI8CreateDevice(void *pThis, const GUID &rguid,
                                               void **lplpDirectInputDevice,
                                               LPUNKNOWN pUnkOuter) {
  HRESULT hr = g_origIDI8CreateDevice(pThis, rguid, lplpDirectInputDevice, pUnkOuter);
  if (SUCCEEDED(hr) && lplpDirectInputDevice && *lplpDirectInputDevice) {
    if (IsEqualGUID(rguid, kGuidSysMouse))
      RememberMouseDevice(*lplpDirectInputDevice);
    if (!g_origSetCoopLevel) {
      void **deviceVtable = *reinterpret_cast<void ***>(*lplpDirectInputDevice);
      void *old = PatchVTableSlot(deviceVtable, kDevice_SetCoopLevel,
                                  reinterpret_cast<void *>(&HookSetCooperativeLevel));
      if (old)
        g_origSetCoopLevel = reinterpret_cast<SetCoopLevel_t>(old);
    }
  }
  return hr;
}

void InstallNonExclusiveInputHook(void *pDirectInput8) {
  if (!pDirectInput8 || g_origIDI8CreateDevice)
    return;
  void **vtable = *reinterpret_cast<void ***>(pDirectInput8);
  void *old = PatchVTableSlot(vtable, kIDI8_CreateDevice,
                              reinterpret_cast<void *>(&HookIDI8CreateDevice));
  if (old)
    g_origIDI8CreateDevice = reinterpret_cast<CreateDevice_IDI8_t>(old);
}

} // namespace

// Loads the real DLL on demand (lazy loading).
void EnsureLoaded() {
  if (real_DirectInput8Create)
    return; // Already resolved — nothing to do.

  if (!realDinput8) {
    char systemPath[MAX_PATH];
    GetSystemDirectoryA(systemPath, MAX_PATH);
    strcat_s(systemPath, "\\dinput8.dll");
    realDinput8 = LoadLibraryA(systemPath);
  }

  if (realDinput8) {
    real_DirectInput8Create =
        (DirectInput8Create_t)GetProcAddress(realDinput8, "DirectInput8Create");
  }
}

// Export for the game
extern "C" __declspec(dllexport) HRESULT WINAPI
DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf,
                   LPVOID *ppvOut, LPUNKNOWN punkOuter) {
  EnsureLoaded();

  if (!real_DirectInput8Create)
    return E_FAIL;

  HRESULT hr = real_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
  if (SUCCEEDED(hr) && ppvOut && *ppvOut)
    InstallNonExclusiveInputHook(*ppvOut);
  return hr;
}

// ----------------------

// ----------------------
// Mod loading
// ----------------------
void LoadMods() {
  std::string modPath = ".\\mods\\";

  if (!std::filesystem::exists(modPath)) {
    Log("Mod folder does not exist: %s", modPath.c_str());
    return;
  }

  for (const auto &entry : std::filesystem::recursive_directory_iterator(modPath)) {
    if (entry.is_regular_file() && entry.path().extension() == ".dll") {
      HMODULE mod = LoadLibraryA(entry.path().string().c_str());
      if (mod)
        Log("Loaded mod: %s", entry.path().filename().string().c_str());
      else
        Log("Failed to load mod: %s", entry.path().filename().string().c_str());
    }
  }
}

// ----------------------
// Initialization thread
// ----------------------
DWORD WINAPI InitThread(LPVOID) {

  Log("Fable Mod Loader Initialized");

  // Hook IDirect3D9::CreateDevice before the game's render loop starts,
  // so the game always launches in windowed mode.
  InstallWindowedHook();
  Log("Windowed hook installed.");

  LoadMods();

  Log("Startup complete. Mod loader running.");

  return 0;
}

// ----------------------
// DllMain
// ----------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    InitModLog(hModule, "FableModLoader.log");

    // Create a thread for anything that may take time
    CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
  }

  return TRUE;
}
