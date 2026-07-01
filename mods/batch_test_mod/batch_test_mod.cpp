// =============================================================================
// Description  : Batch CDEF tester — reads a list of definition IDs from
//                batch_cdefs.txt (next to this DLL) and tests each one against
//                AddItemToInventory within a single game session.
//
//                Unlike add_item_mod (one hardcoded ID, F1-triggered), this
//                mod needs no keypress and no recompile per ID:
//                  1. Waits until the hero inventory resolves (= save loaded).
//                  2. Every kTestIntervalMs, takes the next untested CDEF,
//                     writes a "TESTING" line to batch_results.csv BEFORE the
//                     attempt (so a crash still identifies the culprit), then
//                     spawns + adds it and writes the outcome.
//                  3. On next launch, already-tested CDEFs are skipped by
//                     replaying batch_results.csv — a crash resumes cleanly
//                     from the next untested ID.
//
//                Results: batch_results.csv (CDEF,Outcome) where Outcome is
//                  ADDED_OK (returned 1), REJECTED (returned 0),
//                  CREATE_FAILED (CThing spawn failed), or TESTING (crashed
//                  mid-test if still the last line after a relaunch).
//
//                Item-add plumbing mirrors add_item_mod.cpp by Yaranorgoth.
// =============================================================================

#include "../shared/fable_addresses.h"
#include "../shared/fable_types.h"
#include "../shared/mod_log.h"
#include "../shared/safe_memory.h"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

namespace {

// Resolved at runtime from add_item_mod.dll so cancel_vanilla_add_item_mod
// does not cancel our additions. Falls back to a local dummy if unavailable.
bool g_LocalFlagFallback = false;
bool *g_pAddingItemFromMod = &g_LocalFlagFallback;

void ResolveModFlag() {
  HMODULE h = GetModuleHandleA("add_item_mod.dll");
  if (!h)
    return;
  auto p = reinterpret_cast<bool *>(GetProcAddress(h, "g_AddingItemFromMod"));
  if (p)
    g_pAddingItemFromMod = p;
}

constexpr DWORD kTestIntervalMs = 1500;   // delay between item tests
constexpr DWORD kSettleAfterLoadMs = 5000; // wait after inventory first resolves

char g_CdefListPath[MAX_PATH] = {};
char g_ResultsPath[MAX_PATH] = {};

// ---------------------------------------------------------------------------
// Inventory resolution (same walk as add_item_mod)
// ---------------------------------------------------------------------------
void *GetHeroInventory() {
  void *pMainGame = nullptr;
  if (!SafeRead(reinterpret_cast<LPCVOID>(kMainGameComponentPtr), pMainGame) || !pMainGame)
    return nullptr;

  void *pPlayerManager = nullptr;
  if (!SafeRead(reinterpret_cast<LPCVOID>(static_cast<char *>(pMainGame) + kPlayerManagerOffset), pPlayerManager) || !pPlayerManager)
    return nullptr;

  auto getMainPlayer = reinterpret_cast<void *(__thiscall *)(void *)>(kGetMainPlayerAddr);
  void *pPlayer = getMainPlayer(pPlayerManager);
  if (!pPlayer)
    return nullptr;

  auto getCharThing = reinterpret_cast<void *(__thiscall *)(void *)>(kGetCharacterThingAddr);
  void *pHero = getCharThing(pPlayer);
  if (!pHero)
    return nullptr;

  constexpr int kTCInventory = 17;
  auto hasTC = reinterpret_cast<bool(__thiscall *)(void *, int)>(kHasTCAddr);
  if (!hasTC(pHero, kTCInventory))
    return nullptr;

  auto getTCNode = reinterpret_cast<int(__thiscall *)(int, int *)>(kGetTCNodeAddr);
  int v29 = kTCInventory;
  int pHeroBase = reinterpret_cast<int>(pHero);
  int v5 = getTCNode(pHeroBase + 68, &v29);

  int pHeroOffset72 = 0;
  SafeRead(reinterpret_cast<LPCVOID>(pHeroBase + 72), pHeroOffset72);

  if (v5 == pHeroOffset72 ||
      [&]() { int v5Val = 0; SafeRead(reinterpret_cast<LPCVOID>(v5), v5Val); return v5Val > kTCInventory; }()) {
    v5 = pHeroOffset72;
  }

  void *pInventory = nullptr;
  SafeRead(reinterpret_cast<LPCVOID>(v5 + 4), pInventory);
  return pInventory;
}

// ---------------------------------------------------------------------------
// CreateFableItem (same as add_item_mod)
// ---------------------------------------------------------------------------
void *CreateFableItem(DWORD definition_id) {
  if (definition_id == 0)
    return nullptr;

  CCharString sysName;
  auto initStr = reinterpret_cast<void(__thiscall *)(CCharString *, const char *, int)>(
      kCCharStringCtorAddr);
  initStr(&sysName, "Item", -1);

  CVector origin = {0.0f, 0.0f, 0.0f};

  auto createThing = reinterpret_cast<void *(__fastcall *)(
      int, CVector *, int, int, int, CCharString *)>(kCreateThingAddr);
  return createThing(static_cast<int>(definition_id), &origin, 0, 0, 0, &sysName);
}

// ---------------------------------------------------------------------------
// Results file helpers
// ---------------------------------------------------------------------------
void AppendResult(DWORD cdef, const char *outcome) {
  FILE *fp = nullptr;
  fopen_s(&fp, g_ResultsPath, "a");
  if (!fp)
    return;
  fprintf(fp, "%lu,%s\n", cdef, outcome);
  fclose(fp);
}

// Reads batch_results.csv and returns the set of CDEFs already attempted.
// A trailing "TESTING" line (no outcome after it) means that CDEF crashed the
// game last session — rewrite it as CRASHED so it is skipped from now on.
std::unordered_set<DWORD> LoadTested() {
  std::unordered_set<DWORD> tested;
  std::vector<std::string> lines;

  FILE *fp = nullptr;
  fopen_s(&fp, g_ResultsPath, "r");
  if (fp) {
    char buf[256];
    while (fgets(buf, sizeof(buf), fp))
      lines.emplace_back(buf);
    fclose(fp);
  }

  // First pass: find the index of the LAST line for each CDEF. A TESTING
  // line is only a crash marker if no later line resolved that CDEF.
  std::unordered_map<DWORD, size_t> lastLineFor;
  for (size_t i = 0; i < lines.size(); ++i) {
    DWORD cdef = 0;
    if (sscanf_s(lines[i].c_str(), "%lu,", &cdef) == 1) {
      tested.insert(cdef);
      lastLineFor[cdef] = i;
    }
  }

  bool rewriteNeeded = false;
  for (auto &[cdef, idx] : lastLineFor) {
    char outcome[64] = {};
    if (sscanf_s(lines[idx].c_str(), "%*lu,%63s", outcome, (unsigned)_countof(outcome)) == 1 &&
        strcmp(outcome, "TESTING") == 0) {
      char fixed[128];
      sprintf_s(fixed, "%lu,CRASHED\n", cdef);
      lines[idx] = fixed;
      rewriteNeeded = true;
    }
  }

  if (rewriteNeeded) {
    fopen_s(&fp, g_ResultsPath, "w");
    if (fp) {
      for (const auto &l : lines)
        fputs(l.c_str(), fp);
      fclose(fp);
    }
  }

  return tested;
}

std::vector<DWORD> LoadCdefList() {
  std::vector<DWORD> cdefs;
  FILE *fp = nullptr;
  fopen_s(&fp, g_CdefListPath, "r");
  if (!fp) {
    Log("[BatchTest] Could not open CDEF list: %s", g_CdefListPath);
    return cdefs;
  }
  char buf[256];
  while (fgets(buf, sizeof(buf), fp)) {
    DWORD cdef = 0;
    if (sscanf_s(buf, "%lu", &cdef) == 1 && cdef != 0)
      cdefs.push_back(cdef);
  }
  fclose(fp);
  return cdefs;
}

// ---------------------------------------------------------------------------
// Main-thread execution via window subclass (engine TLS requirement)
// ---------------------------------------------------------------------------
#define WM_TEST_CDEF (WM_USER + 102)
#define WM_CHECK_INVENTORY (WM_USER + 103)

HWND g_GameHwnd = nullptr;
WNDPROC g_OriginalWndProc = nullptr;
volatile LONG g_TestDone = 0; // signaled after each WM_TEST_CDEF is handled

void DoTestCdef(DWORD cdef) {
  DWORD dummy = 0;
  void *inventory = GetHeroInventory();
  if (!inventory || !SafeRead(inventory, dummy)) {
    AppendResult(cdef, "NO_INVENTORY");
    return;
  }

  void *newItem = CreateFableItem(cdef);
  if (!newItem || !SafeRead(newItem, dummy)) {
    AppendResult(cdef, "CREATE_FAILED");
    return;
  }

  auto fn = reinterpret_cast<AddItemToInventory_t>(kAddItemToInventoryAddr);
  *g_pAddingItemFromMod = true;
  char result = fn(inventory, newItem, false, true, 0, true /*silent*/);
  *g_pAddingItemFromMod = false;

  AppendResult(cdef, result ? "ADDED_OK" : "REJECTED");
  Log("[BatchTest] CDEF %lu -> %s", cdef, result ? "ADDED_OK" : "REJECTED");
}

LRESULT CALLBACK GameWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_TEST_CDEF) {
    DoTestCdef(static_cast<DWORD>(wParam));
    InterlockedExchange(&g_TestDone, 1);
    return 0;
  }
  if (uMsg == WM_CHECK_INVENTORY) {
    // Runs on the main thread, so the engine object walk is safe even
    // while a save is loading.
    DWORD dummy = 0;
    void *inv = GetHeroInventory();
    return (inv && SafeRead(inv, dummy)) ? 1 : 0;
  }
  return CallWindowProc(g_OriginalWndProc, hwnd, uMsg, wParam, lParam);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM) {
  DWORD wndPid = 0;
  GetWindowThreadProcessId(hwnd, &wndPid);
  if (wndPid == GetCurrentProcessId() &&
      GetWindow(hwnd, GW_OWNER) == nullptr &&
      IsWindowVisible(hwnd)) {
    g_GameHwnd = hwnd;
    return FALSE;
  }
  return TRUE;
}

// ---------------------------------------------------------------------------
// Batch thread
// ---------------------------------------------------------------------------
DWORD WINAPI BatchThread(LPVOID) {
  ResolveModFlag();
  std::vector<DWORD> cdefs = LoadCdefList();
  if (cdefs.empty()) {
    Log("[BatchTest] No CDEFs to test (missing/empty batch_cdefs.txt). Idle.");
    return 0;
  }

  std::unordered_set<DWORD> tested = LoadTested();
  Log("[BatchTest] Loaded %zu CDEFs, %zu already tested.", cdefs.size(), tested.size());

  // Find the game window.
  while (!g_GameHwnd) {
    EnumWindows(EnumWindowsProc, 0);
    Sleep(100);
  }
  g_OriginalWndProc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtr(g_GameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GameWndProc)));

  // Wait for a save to be loaded (inventory resolvable), then let it settle.
  // The check is marshalled to the main thread via SendMessageTimeout —
  // walking engine objects from this worker thread races save-load teardown
  // and can crash the game.
  Log("[BatchTest] Waiting for a save to be loaded...");
  while (true) {
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(g_GameHwnd, WM_CHECK_INVENTORY, 0, 0,
                            SMTO_ABORTIFHUNG, 2000, &result) && result == 1)
      break;
    Sleep(500);
  }
  Log("[BatchTest] Save detected. Settling %lu ms before testing begins.", kSettleAfterLoadMs);
  Sleep(kSettleAfterLoadMs);

  size_t done = 0, total = 0;
  for (DWORD cdef : cdefs) {
    if (tested.count(cdef))
      continue;
    ++total;

    // Record intent BEFORE testing so a hard crash still identifies the ID.
    AppendResult(cdef, "TESTING");

    // Overwrite the TESTING line with the real outcome by appending — the
    // loader treats the LAST line for a CDEF as authoritative, and rewrites
    // trailing TESTING lines as CRASHED on next boot. Simplest approach:
    // append outcome; both lines list the same CDEF so it stays "tested".
    InterlockedExchange(&g_TestDone, 0);
    PostMessage(g_GameHwnd, WM_TEST_CDEF, static_cast<WPARAM>(cdef), 0);

    // Wait for the main thread to finish this test.
    DWORD waited = 0;
    while (!InterlockedCompareExchange(&g_TestDone, 0, 0) && waited < 30000) {
      Sleep(100);
      waited += 100;
    }
    ++done;

    Sleep(kTestIntervalMs);
  }

  Log("[BatchTest] Batch complete: %zu newly tested this session.", done);
  AppendResult(0, "SESSION_COMPLETE");
  return 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(hModule);
    InitModLog(hModule, "batch_test_mod.log");

    // Resolve sibling file paths (next to this DLL).
    char dir[MAX_PATH];
    GetModuleFileNameA(hModule, dir, MAX_PATH);
    char *lastSlash = strrchr(dir, '\\');
    if (lastSlash)
      *(lastSlash + 1) = '\0';
    sprintf_s(g_CdefListPath, "%sbatch_cdefs.txt", dir);
    sprintf_s(g_ResultsPath, "%sbatch_results.csv", dir);

    Log("[BatchTest] Loaded. List=%s Results=%s", g_CdefListPath, g_ResultsPath);
    CreateThread(nullptr, 0, BatchThread, nullptr, 0, nullptr);
  }
  return TRUE;
}
