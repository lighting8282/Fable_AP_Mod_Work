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

// One-by-one (F1/F2) manual mode paths.
char g_OneByOneListPath[MAX_PATH] = {};   // onebyone_list.txt (presence = enable)
char g_OneByOneIndexPath[MAX_PATH] = {};  // onebyone_index.txt (0-based cursor)
char g_OneByOnePrimePath[MAX_PATH] = {};  // onebyone_prime.txt (optional CDEF added BEFORE target on F1)
char g_DumpValuePath[MAX_PATH] = {};      // dump_value.txt (F3 scans hero struct for this DWORD)
char g_GameDir[MAX_PATH] = {};            // <FTLC> root
char g_FableExePath[MAX_PATH] = {};       // <FTLC>\Fable.exe

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
// Hero CThing resolution (subset of GetHeroInventory, stops at the hero Thing).
// Optionally also returns the CPlayer object. Calls engine functions, so it
// must run on the game's main thread (same rule as GetHeroInventory).
// ---------------------------------------------------------------------------
void *GetHeroThing(void **outPlayer = nullptr) {
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
  if (outPlayer)
    *outPlayer = pPlayer;
  auto getCharThing = reinterpret_cast<void *(__thiscall *)(void *)>(kGetCharacterThingAddr);
  return getCharThing(pPlayer);
}

// A plausible in-process heap/data pointer (aligned, in the usable user range).
static bool LooksLikePtr(DWORD v) {
  return (v & 3u) == 0u && v >= 0x00010000u && v < 0x7FFF0000u;
}

// Scans the hero CThing (and the CPlayer object, and one pointer-hop deep from
// the hero) for any 4-byte field equal to `target`, logging offsets. Run twice
// with two known gold values; the offset that appears both times is the gold
// field. Reads dump_value.txt for the target. MAIN-THREAD ONLY.
void ScanHeroForValue() {
  DWORD target = 0;
  {
    FILE *fp = nullptr;
    fopen_s(&fp, g_DumpValuePath, "r");
    if (fp) { if (fscanf_s(fp, "%lu", &target) != 1) target = 0; fclose(fp); }
  }
  if (target == 0) {
    Log("[Scan] F3: no target — put your current gold amount in dump_value.txt first.");
    return;
  }
  void *pPlayer = nullptr;
  char *pHero = static_cast<char *>(GetHeroThing(&pPlayer));
  if (!pHero) {
    Log("[Scan] F3: no hero yet — load a save first.");
    return;
  }
  Log("[Scan] F3: target=%lu (0x%lX)  hero=0x%p  player=0x%p", target, target, pHero, pPlayer);

  constexpr size_t kDirectBytes = 0x600;  // scan window on each object
  constexpr size_t kDeepBytes   = 0x300;  // window inside each pointed-to block
  int hits = 0;

  auto scanRegion = [&](const char *label, char *base, size_t bytes) {
    if (!base) return;
    for (size_t off = 0; off + 4 <= bytes; off += 4) {
      DWORD v = 0;
      if (SafeRead(base + off, v) && v == target) {
        Log("[Scan]   DIRECT %s+0x%zX  (abs 0x%p)", label, off, base + off);
        ++hits;
      }
    }
  };

  scanRegion("hero", pHero, kDirectBytes);
  scanRegion("player", static_cast<char *>(pPlayer), kDirectBytes);

  // One hop deep: for each pointer-looking slot in the hero object, scan the
  // block it points at (this catches gold living in a stats/attribute sub-struct).
  for (size_t off = 0; off + 4 <= kDirectBytes; off += 4) {
    DWORD p = 0;
    if (!SafeRead(pHero + off, p) || !LooksLikePtr(p))
      continue;
    for (size_t j = 0; j + 4 <= kDeepBytes; j += 4) {
      DWORD v = 0;
      if (SafeRead(reinterpret_cast<char *>(p) + j, v) && v == target) {
        Log("[Scan]   DEEP hero+0x%zX -> +0x%zX  (ptr 0x%lX abs 0x%lX)", off, j, p, p + (DWORD)j);
        ++hits;
      }
    }
  }
  Log("[Scan] F3: done — %d match(es). Run again at a DIFFERENT gold value; the "
      "offset in BOTH runs is gold.", hits);
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

std::vector<DWORD> LoadCdefListFrom(const char *path) {
  std::vector<DWORD> cdefs;
  FILE *fp = nullptr;
  fopen_s(&fp, path, "r");
  if (!fp) {
    Log("[BatchTest] Could not open CDEF list: %s", path);
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

std::vector<DWORD> LoadCdefList() { return LoadCdefListFrom(g_CdefListPath); }

// ---------------------------------------------------------------------------
// Main-thread execution via window subclass (engine TLS requirement)
// ---------------------------------------------------------------------------
#define WM_TEST_CDEF (WM_USER + 102)
#define WM_CHECK_INVENTORY (WM_USER + 103)
#define WM_ONEBYONE_SPAWN (WM_USER + 104)
#define WM_DUMP_SCAN (WM_USER + 105)

HWND g_GameHwnd = nullptr;
WNDPROC g_OriginalWndProc = nullptr;
volatile LONG g_TestDone = 0; // signaled after each WM_TEST_CDEF is handled

std::vector<DWORD> g_OneByOneList;
size_t g_OneByOneIndex = 0;

// Adds one item to the hero inventory (silent), for the F1 manual spawn.
// Logs to the mod log only (keeps batch_results.csv clean).
void SpawnCurrentOneByOne() {
  if (g_OneByOneIndex >= g_OneByOneList.size()) {
    Log("[OneByOne] F1: list exhausted (index %zu).", g_OneByOneIndex);
    return;
  }
  DWORD cdef = g_OneByOneList[g_OneByOneIndex];

  DWORD dummy = 0;
  void *inv = GetHeroInventory();
  if (!inv || !SafeRead(inv, dummy)) {
    Log("[OneByOne] F1: no inventory yet — load a save first (CDEF %lu not spawned).", cdef);
    return;
  }

  // Optional PRIME step: if onebyone_prime.txt holds a CDEF, add it FIRST (same
  // session, same profile) before the target. Used to test whether granting a
  // "wallet" item (e.g. 4306 OBJECT_HERO_MONEY_BAG) first lets gold defs add
  // without crashing. Leave the file absent/empty for normal one-by-one.
  {
    FILE *pf = nullptr;
    fopen_s(&pf, g_OneByOnePrimePath, "r");
    if (pf) {
      unsigned long prime = 0;
      int got = fscanf_s(pf, "%lu", &prime);
      fclose(pf);
      if (got == 1 && prime != 0) {
        Log("[OneByOne] F1: PRIME creating CDEF %lu first ...", prime);
        void *pitem = CreateFableItem(prime);
        if (pitem && SafeRead(pitem, dummy)) {
          Log("[OneByOne] F1: PRIME add CDEF %lu ...", prime);
          auto pfn = reinterpret_cast<AddItemToInventory_t>(kAddItemToInventoryAddr);
          *g_pAddingItemFromMod = true;
          char pr = pfn(inv, pitem, false, false, 0, true);
          *g_pAddingItemFromMod = false;
          Log("[OneByOne] F1: PRIME CDEF %lu -> add result %d. Now adding target ...",
              prime, (int)(unsigned char)pr);
        } else {
          Log("[OneByOne] F1: PRIME CreateFableItem failed for CDEF %lu.", prime);
        }
      }
    }
  }

  Log("[OneByOne] F1: creating CDEF %lu ...", cdef);
  void *item = CreateFableItem(cdef);
  if (!item || !SafeRead(item, dummy)) {
    Log("[OneByOne] F1: CreateFableItem failed for CDEF %lu.", cdef);
    return;
  }
  Log("[OneByOne] F1: created item 0x%p, calling AddItemToInventory "
      "(quick_access=false) ...", item);
  auto fn = reinterpret_cast<AddItemToInventory_t>(kAddItemToInventoryAddr);
  *g_pAddingItemFromMod = true;
  char r = fn(inv, item, false, false /*add_quick_access*/, 0, true /*silent*/);
  *g_pAddingItemFromMod = false;
  Log("[OneByOne] F1: spawned CDEF %lu (index %zu) -> add result %d.",
      cdef, g_OneByOneIndex, (int)(unsigned char)r);
}

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
  if (uMsg == WM_ONEBYONE_SPAWN) {
    SpawnCurrentOneByOne();
    return 0;
  }
  if (uMsg == WM_DUMP_SCAN) {
    ScanHeroForValue();
    return 0;
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
// One-by-one (F1/F2) manual mode
// ---------------------------------------------------------------------------
size_t ReadOneByOneIndex() {
  FILE *fp = nullptr;
  fopen_s(&fp, g_OneByOneIndexPath, "r");
  if (!fp)
    return 0;
  unsigned long idx = 0;
  if (fscanf_s(fp, "%lu", &idx) != 1)
    idx = 0;
  fclose(fp);
  return static_cast<size_t>(idx);
}

void WriteOneByOneIndex(size_t idx) {
  FILE *fp = nullptr;
  fopen_s(&fp, g_OneByOneIndexPath, "w");
  if (!fp)
    return;
  fprintf(fp, "%zu\n", idx);
  fclose(fp);
}

// Spawns a detached helper that waits ~2s for this process to die, then
// relaunches Fable.exe from the game directory. Used by F2.
void RelaunchGame() {
  char cmd[1200];
  sprintf_s(cmd,
            "cmd.exe /c timeout /t 2 /nobreak >nul & start \"\" /D \"%s\" \"%s\"",
            g_GameDir, g_FableExePath);
  STARTUPINFOA si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, g_GameDir,
                     &si, &pi)) {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  }
}

// Polls F1 (spawn current) and F2 (advance + restart game).
DWORD WINAPI OneByOneHotkeyThread(LPVOID) {
  bool f1was = false, f2was = false, f3was = false;
  while (true) {
    bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    bool f2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;

    if (f1 && !f1was)
      PostMessage(g_GameHwnd, WM_ONEBYONE_SPAWN, 0, 0);

    if (f3 && !f3was)
      PostMessage(g_GameHwnd, WM_DUMP_SCAN, 0, 0);

    if (f2 && !f2was) {
      g_OneByOneIndex += 1;
      WriteOneByOneIndex(g_OneByOneIndex);
      if (g_OneByOneIndex < g_OneByOneList.size()) {
        Log("[OneByOne] F2: advancing to index %zu (CDEF %lu) — restarting game.",
            g_OneByOneIndex, g_OneByOneList[g_OneByOneIndex]);
        RelaunchGame();
      } else {
        Log("[OneByOne] F2: end of list reached — closing without relaunch.");
      }
      Sleep(250);
      ExitProcess(0);
    }

    f1was = f1;
    f2was = f2;
    f3was = f3;
    Sleep(30);
  }
  return 0;
}

DWORD WINAPI OneByOneThread(LPVOID) {
  ResolveModFlag();
  g_OneByOneList = LoadCdefListFrom(g_OneByOneListPath);
  if (g_OneByOneList.empty()) {
    Log("[OneByOne] onebyone_list.txt empty — nothing to do.");
    return 0;
  }
  g_OneByOneIndex = ReadOneByOneIndex();

  while (!g_GameHwnd) {
    EnumWindows(EnumWindowsProc, 0);
    Sleep(100);
  }
  g_OriginalWndProc = reinterpret_cast<WNDPROC>(
      SetWindowLongPtr(g_GameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GameWndProc)));

  DWORD cur = g_OneByOneIndex < g_OneByOneList.size()
                  ? g_OneByOneList[g_OneByOneIndex]
                  : 0;
  Log("[OneByOne] Ready. %zu CDEFs, cursor at index %zu (CDEF %lu). "
      "Load a save, then F1 = spawn current, F2 = next + restart.",
      g_OneByOneList.size(), g_OneByOneIndex, cur);

  CreateThread(nullptr, 0, OneByOneHotkeyThread, nullptr, 0, nullptr);
  return 0;
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
    sprintf_s(g_OneByOneListPath, "%sonebyone_list.txt", dir);
    sprintf_s(g_OneByOneIndexPath, "%sonebyone_index.txt", dir);
    sprintf_s(g_OneByOnePrimePath, "%sonebyone_prime.txt", dir);
    sprintf_s(g_DumpValuePath, "%sdump_value.txt", dir);

    // Derive the game root: dir is <FTLC>\mods\batch_test_mod\ — go up two levels.
    strcpy_s(g_GameDir, dir);
    for (int i = 0; i < 2; ++i) {
      size_t n = strlen(g_GameDir);
      if (n && g_GameDir[n - 1] == '\\')
        g_GameDir[n - 1] = '\0'; // strip trailing backslash
      char *s = strrchr(g_GameDir, '\\');
      if (s)
        *s = '\0'; // strip last component
    }
    sprintf_s(g_FableExePath, "%s\\Fable.exe", g_GameDir);

    // One-by-one manual mode takes precedence when onebyone_list.txt exists.
    FILE *probe = nullptr;
    fopen_s(&probe, g_OneByOneListPath, "r");
    if (probe) {
      fclose(probe);
      Log("[OneByOne] Enabled (found %s). GameDir=%s", g_OneByOneListPath, g_GameDir);
      CreateThread(nullptr, 0, OneByOneThread, nullptr, 0, nullptr);
    } else {
      Log("[BatchTest] Loaded. List=%s Results=%s", g_CdefListPath, g_ResultsPath);
      CreateThread(nullptr, 0, BatchThread, nullptr, 0, nullptr);
    }
  }
  return TRUE;
}
