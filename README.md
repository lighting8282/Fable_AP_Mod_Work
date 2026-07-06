# Fable AP Mod Work

Working record of tooling and findings for cataloging **Fable: The Lost Chapters / Fable Anniversary item CDEFs** (definition IDs), in support of an Archipelago (AP) randomizer world. This builds on top of two upstream projects by **Yaranorgoth (lgbarrere)**:

- [`FableAnniversary-Random`](https://github.com/lgbarrere/FableAnniversary-Random) — dinput8 proxy mod loader + `add_item_mod` (spawns an item into the hero's inventory on F1)
- `FableModdingTools` (FA-SYMLINKER) — symlinks FTLC ↔ Fable Anniversary so FTLC tools (Fable Explorer ShadowNet, ChocolateBox) work on Anniversary

## The problem

Building an AP world needs a full table of item IDs (CDEFs) mapped to in-game display names. FESN's `OBJECT` list contains ~2,850 definitions, but:

1. Most are world objects (walls, doors, scenery), not inventory items.
2. Internal symbol names often don't match display names (`OBJECT_SUPER_HEALTH_POTION` = "Elixir of Life"; `OBJECT_BOOK_STORY_04` = "A Hero's Journey II").

The original manual workflow was: look up a CDEF in FESN → paste into `add_item_mod.cpp` line 163 → rebuild → launch → load save → press F1 → check inventory / observe crash → record → repeat. Minutes per single ID.

## What was built

### 1. CDEF extraction (`tools/parse_fesn_screenshot_dump.py`, `data/cdefs.txt`)

FESN has no bulk export and its tree doesn't support copy. The full `OBJECT` tree was captured via screenshots, transcribed, and parsed with a regex (`ID - SYMBOL_NAME`) into `data/cdefs.txt` — **2,852 unique CDEFs** with symbol names. Also exported as `data/OBJECT_tree_full_list.tsv`.

Note: FESN's tree only populated after **File → Open** on `data/CompiledDefs/game.bin` inside the FTLC install; the `OBJECT_FAMILLY` section is groupings of OBJECT CDEFs, not additional item definitions, and was skipped.

### 2. Name auto-matcher (`tools/match_cdefs_to_items.py`, `data/matched_items.tsv`)

Normalizes symbol names (strip `OBJECT_` prefix, `_01` variant suffixes, punctuation) and matches them against the AP item sheet (`data/Fable lists for AP - Items.tsv`). **46 items auto-matched** on the first pass (weapons, potions, food, augmentations, some quest items). Books/clothing/tattoos/styles don't match by name and need in-game testing.

### 3. Outer-loop batch tester (`tools/batch_test_cdefs.py`) — superseded

Automates the original loop: patches `add_item_mod.cpp`, runs `deploy.bat`, waits for the user to load a save and press F1, then reads `mods/add_item_mod/add_item_mod.log` for `CreateFableItem`/`AddItemToInventory` results and detects crashes via process polling. Results append to CSV, crash-safe.

Still ~1 build+boot cycle per ID (~1–2 min each). Kept for reference; superseded by the in-game batch mod.

Caveat discovered: synthetic `keybd_event` F1 never reaches the game because it runs elevated (UIPI blocks lower-privilege synthetic input), so the script asks the user to press F1 themselves.

### 4. In-game batch tester (`mods/batch_test_mod/`) — the current workhorse

New mod DLL loaded by the dinput8 proxy. Per session:

1. Reads `batch_cdefs.txt` (one CDEF per line, placed next to the DLL in `<FTLC>/mods/batch_test_mod/`).
2. Waits until the hero inventory pointer resolves (= save loaded), settles 5 s.
3. Tests each untested CDEF ~1.5 s apart **on the game's main thread** (marshalled via window-message subclassing — engine TLS requirement), writing to `batch_results.csv`:
   - `ADDED_OK` — AddItemToInventory returned 1 (item is in inventory)
   - `REJECTED` — returned 0
   - `CREATE_FAILED` — CThing spawn failed
   - `CRASHED` — a `TESTING` marker written before each attempt is rewritten to this on next boot if it was never resolved
4. Skips already-tested CDEFs on relaunch — **a crash costs one relaunch + save load, then testing resumes automatically.**

No recompiles, no keypresses. 28 IDs tested in ~1 minute of game time in the first session.

**Crash fix (important):** the first version polled `GetHeroInventory()` from a worker thread, which races save-load teardown and crashes the game intermittently. The wait loop now marshals the check to the main thread via `SendMessageTimeout` (`WM_CHECK_INVENTORY`).

Items are added with `silent=true`. This matters: books tested via the old F1 path (`silent=false`) returned **REJECTED** and sometimes crashed; the same books via the silent path are **ADDED_OK** and appear in inventory. The pickup sound/UI path appears to be what fails, not the item add.

### 5. Quality-of-life changes to the upstream mod loader (`modified-files/`)

These are copies of the modified upstream files (originals belong to lgbarrere's repo):

- **`dinput8.sln`** — hand-written classic `.sln`; the repo ships `dinput8.slnx`, which requires VS 17.10+ (MSB4068 on older MSBuild). VS 2026 also builds the `.slnx` fine.
- **`deploy.bat`** — solution path switched to `.sln`; `DEFAULT_DIR_FTLC_STEAM` pointed at the actual install (`A:\SteamLibrary\...`).
- **`windowed_hook.cpp`** — added `MakeBorderless()`: strips title bar/border (WS_POPUP) after CreateDevice and re-applies on device Reset.
- **`dllmain.cpp`** — hooks `IDirectInput8::CreateDevice` → `SetCooperativeLevel` so the **mouse** is exclusive+foreground (single accurate cursor in-game, auto-released on alt-tab) while the **keyboard** is non-exclusive+foreground (Print Screen / Win keys work). Device instances created with `GUID_SysMouse` are tracked because all DirectInput device instances share one vtable.
  - First attempt (non-exclusive mouse) caused a double-cursor desync: game cursor driven by relative deltas + free OS cursor receiving the actual clicks.
  - Even so, the game minimizes on focus loss, which breaks screenshot tools — workaround: a delayed PowerShell `CopyFromScreen` capture fired from another terminal while the game keeps focus.

## Findings so far

| Category | Result | Notes |
|---|---|---|
| Potions (4292–4299) | 4294 confirmed ADDED_OK | "Elixir of Life"; the guide's known-good test ID |
| Gold (4644–4660) | **CRASHES** (10/10 tested) | Gold is a player counter, not an inventory CThing. `AddItemToInventory` is the wrong mechanism — an AP implementation needs a direct gold-stat function |
| Trophies (4495–4510) | 16/16 ADDED_OK | Display names confirmed by screenshot (below) |
| Books (4539–4550) | 12/12 ADDED_OK (silent add) | REJECTED/crashy via non-silent F1 path |
| Food (4274–4289) | tested via old script | see session CSVs |

### Confirmed CDEF → display name mappings

Books (`BOOK_STORY_01–12`, confirmed by inventory add order):

| CDEF | In-game name | On AP sheet? |
|---|---|---|
| 4539 | Sutter Family History | added |
| 4540 | The Repentant Alchemist | ✓ |
| 4541 | The Rotten Apple | ✓ |
| 4542 | A Hero's Journey II | ✓ |
| 4543 | Avo Nice Day | added |
| 4544 | A Hero's Journey III | ✓ |
| 4545 | The Oakvale Raid | ✓ |
| 4546 | The Trials Of Aarkan | ✓ |
| 4547 | The Bargate Poems | added |
| 4548 | A Hero's Journey I | ✓ |
| 4549 | The story of 'x' | added |
| 4550 | The Guild Of Zeroes | ✓ |

Trophies (confirmed by screenshots):

| CDEF | In-game name | On AP sheet? |
|---|---|---|
| 4495 | Bandit Seal | ✓ |
| 4496 | Wasp Queen's Head | ✓ |
| 4497 | Evil Wasp Queen Head | dev test item ("not for the final game!") |
| 4498 | King Scorpion Sting | ✓ ("King Scorpion Stinger") |
| 4499 | Balverine's Head | added — distinct row from the sheet's existing "White Balverine Head" (still untested, likely a different CDEF e.g. `TROPHY_BALVERINE_FM_HEAD` 4512) |
| 4500 | Claw | added |
| 4501 | Hero's Axe | added |
| 4502 | Hero's Belt | added |
| 4503 | Lobster's Claw | added |
| 4504 | Hobbe Tooth Trophy | ✓ ("Hobbe Tooth") |
| 4505 | Trader's Head | ✓ |
| 4506 | Scrumpy Crate | added |
| 4507 | Fist Fighters Trophy | ✓ ("Fist Fighter's Trophy") |
| 4508 | Thunder's Helmet | ✓ |
| 4509 | Kraken Tooth | ✓ |
| 4510 | Archaeologist's Map | added — distinct row from the sheet's existing "Map To Lost Bay" (that is 4525, confirmed session 2) |

### Session 2 (batch of 44: trophies 4511–4527, books 4551–4577)

All 44 tested `ADDED_OK`, zero crashes. Trophy display names confirmed by screenshot:

| CDEF | In-game name | Notes |
|---|---|---|
| 4511 | Dragon Scale | Trophies tab (found via isolated test — hidden by duplicate-name collapsing in the full inventory) |
| 4512 | White Balverine Head | resolves the row-656 flag |
| 4513 | Golden Fish | |
| 4514 | Hobbe Head | |
| 4515 | Maze's Clasp | |
| 4516 | Minion's Helmet | |
| 4517 | Fist Fighters Trophy | **duplicate display name** — 4507 also shows as this; needs disambiguation |
| 4518 | Scorpion Sting | new row; distinct from "King Scorpion Stinger" (4498) |
| 4519 | Silver Arrow | |
| 4520 | Trader's Feather | |
| 4521 | Undead Hand | |
| 4522 | Whisper's Brooch | |
| 4523 | Jack's Mask | symbol `TROPHY_JOB_MASK`; appears in the **"Items: Other"** tab, not Trophies |
| 4524 | Dragon Gate | `ADDED_OK` per log but **not visible in any inventory tab** — internal/non-holdable token; recorded `Inventory?=No` |
| 4525 | Map to Lost Bay | resolves the row-646 flag |
| 4526 | Summoner's Gauntlet | |
| 4527 | Fire Heart Band | |

Books 4551–4575: 4551=A Hero's Journey IV (new), and the `BOOK_GUILD_*` range (4555–4573) plus 4562/4564/4574/4575 map onto existing sheet book titles (The Arena, A Love Story, The Dragons, Book of Spells, Creatures of Albion I–III/North, The Guild of Zeroes, The Hierarchy of Weapons, The Northern Wastes, The Old Kingdom, The Other Land, The Pale Balverine, The Tale of Maxley, The Tale of Twinblade, Three Haikus, Theresa's Letter, The Bloodline) + new rows Seers and Prophets (4569). Sheet's "Three Haikus by Milo the Bard" shows in-game as "Miko" — spelling variant flagged.

4576 (`MAZE_JOURNAL`) and 4577 (`HAUNTED_DIARY_01`) don't appear in Books despite the symbol names — resolved in session 3 (see below): they're Quest-category items, filling existing blank-CDEF sheet rows "Arban's Thaumaturgica" and "Dusty Notebook" respectively.

### Session 3: one-by-one (F1/F2) manual mode + a critical methodology finding

Built an F1/F2 mode into `batch_test_mod` (`onebyone_list.txt` / `onebyone_index.txt` next to the DLL — presence of the list file enables this mode over the automatic batch mode). **F1** spawns the current CDEF into the loaded save; **F2** advances the cursor and relaunches `Fable.exe` automatically (spawns a detached `cmd.exe /c timeout /t 2 & start` helper so the mod's own process can exit cleanly first). The index persists across restarts, so a crash or manual relaunch resumes at the right CDEF.

Used this to isolate the `BOOK_STORY_13-16` (4551-4554) cluster on **fresh game profiles** (one CDEF per profile — full disambiguation, no reliance on position or name). Result: **2 of 4 were wrong in the original session-2 batch reading**:

| CDEF | Session-2 batch read (position-based) | **Session-3 clean isolated test (truth)** |
|---|---|---|
| 4551 | The Balverine Slayer | The Balverine Slayer ✓ (coincidentally right) |
| 4552 | The Tailor's Tragedy | The Tailor's Tragedy ✓ (coincidentally right) |
| 4553 | A Hero's Journey IV | **The Trigamist** ✗ |
| 4554 | (assumed duplicate of 4551) | **A Hero's Journey IV** ✗ — there is no duplicate |

**Why this happened:** the Books inventory UI does **not** display items in add-order — it uses fixed internal display slots (proven earlier: "The Dragons" sits above "Three Haikus" despite being added later). The `BOOK_STORY_01-12` block from session 1 only *happened* to read correctly because, by chance, add-order matched slot-order for that particular sub-range on that particular save. It is not a general rule.

**Given that finding, all 12 `BOOK_STORY_01-12` books (4539-4550) were re-verified with the same clean single-CDEF-per-fresh-profile method** to make sure the session-1 batch reading wasn't similarly compromised. Result: **all 12 came back identical to the original session-1 reading** — that block was genuinely fine. Final confirmed book-symbol range 4539-4554, all clean-verified:

| CDEF | Title | | CDEF | Title |
|---|---|---|---|---|
| 4539 | Sutter Family History | | 4547 | The Bargate Poems |
| 4540 | The Repentant Alchemist | | 4548 | A Hero's Journey I |
| 4541 | The Rotten Apple | | 4549 | The story of 'x' |
| 4542 | A Hero's Journey II | | 4550 | The Guild Of Zeroes |
| 4543 | Avo Nice Day | | 4551 | The Balverine Slayer |
| 4544 | A Hero's Journey III | | 4552 | The Tailor's Tragedy |
| 4545 | The Oakvale Raid | | 4553 | The Trigamist |
| 4546 | The Trials Of Aarkan | | 4554 | A Hero's Journey IV |

**Rule going forward: any CDEF-to-name mapping derived from *position* in a multi-item batch (not from a descriptive symbol name) must be treated as unverified until confirmed by an isolated single-CDEF test on a fresh profile.** Descriptive-symbol mappings (`GUILD_ARENA` → The Arena, `TROPHY_KRAKEN_TOOTH` → Kraken Tooth, etc.) don't have this problem since they don't depend on position at all.

Also resolved via one-by-one: 4576 (`OBJECT_MAZE_JOURNAL`) = **Arban's Thaumaturgica** and 4577 (`OBJECT_HAUNTED_DIARY_01`) = **Dusty Notebook** — both Quest-category items despite book-like symbol names, filling existing blank-CDEF sheet rows. This closes out every CDEF from sessions 2 and 3 (4495-4577) with no remaining unknowns.

**Auto-name-logging investigation (abandoned for now):** attempted a cheap win — a memory probe (`ProbeStrings` in `batch_test_mod.cpp`, since removed) that walked a spawned CThing's memory 2 pointer-levels deep logging every readable string, run against a known CDEF (4553). Result: the display name is **not** stored on the object or reachable within that walk — it dumped plenty of internal engine/TC-class strings but no book titles. This means the display name is resolved on-demand at render time (likely `def_id → definition → text-ID → text.big` lookup), which isn't a quick win — would need the actual name-resolution function found via proper disassembly, not a memory walk. Parked; one-by-one (F1/F2) manual verification is the current best method for ambiguous items.

## Sheet sync

We work in a **personal copy** of the shared Google Sheet ("Copy of Fable lists for AP"), not the shared one directly, to avoid live-editing something other testers depend on. `data/Fable lists for AP - Items.tsv` mirrors that copy's "Items" tab exactly.

Sync process (no reliable automated export was found — Google blocks unauthenticated fetches on private sheets, and the in-browser download didn't land in an accessible folder in this environment):

1. In the sheet: **File → Download → Tab Separated Values (.tsv)**.
2. Move/rename the downloaded file into this repo's `data/` folder as `Fable lists for AP - Items.tsv` (overwriting the old copy).
3. Commit and push.

Do this after any batch of edits to the sheet (ours or manual). The reverse direction — new CDEFs confirmed via `batch_test_mod` — get entered into the sheet directly (via browser automation or by hand) before running this export, so the TSV always reflects the sheet's current state.

Note: the sheet's own category assignments don't always match assumptions made earlier in this file — e.g. "Bandit Seal" exists as **two separate rows**, one under Quest (no CDEF) and one under Trophy (CDEF 4495, confirmed). Trust the TSV/sheet over category guesses in the tables above.

## Current workflow (per session)

1. Put target CDEFs (one per line) in `<FTLC>/mods/batch_test_mod/batch_cdefs.txt`. To re-test IDs, archive/delete `batch_results.csv` first.
2. `deploy.bat` (only needed if mod source changed — otherwise just launch `Fable.exe`).
3. Load a save. The mod does the rest; watch `batch_results.csv`.
4. If the game crashes: relaunch `Fable.exe`, reload the save — resumes automatically past the crasher.
5. Screenshot the inventory (items appear in ID order) to map display names; on this machine the game minimizes on focus loss, so use a delayed background capture.
6. Feed confirmed mappings back into the AP items sheet.

## Environment notes / gotchas

- FTLC at `A:\SteamLibrary\steamapps\common\Fable The Lost Chapters`; FESN config (`FableModdingTools/FA-SYMLINKER/tools/FableExplorer/config.xml`) must point `InstallDirectory` there or its tree stays empty.
- FESN shows data only after File → Open on `data/CompiledDefs/game.bin`.
- The symlinker's option 4 junctions only `Levels/FinalAlbion` (for running the modded game) — it is unrelated to FESN's data loading.
- `deploy.bat` runs elevated; synthetic keyboard input from non-elevated processes won't reach the game (UIPI).
- Windows Store Python 3.13 was used; heredoc/inline `python -c` with multi-line code is unreliable from Git Bash — script files are more dependable.
- **`ADDED_OK` ≠ player-visible.** Some CDEFs the engine accepts (`AddItemToInventory` returns 1) never appear in any inventory tab — e.g. Dragon Gate (4524). These are internal/non-holdable tokens; record `Inventory?=No` despite the OK.
- **Inventory has multiple tabs beyond the obvious ones** (Trophies, Books, **Items: Other**, Quest, Photo Journal). An item can land in an unexpected tab (Jack's Mask, a `TROPHY_*` symbol, shows under "Items: Other"). When an item tests OK but isn't in the expected tab, check the others.
- **Duplicate display names collapse in the full inventory list.** With a cluttered save, two CDEFs sharing a name (4507/4517 both "Fist Fighters Trophy") or items further down can be hidden. Testing a **single CDEF against a fresh game profile** disambiguates — that's how 4511/4523/4524 were pinned down.
- Screenshot capture note: the delayed PowerShell `CopyFromScreen` grabs whatever is frontmost when it fires. The game minimizes on focus loss, so click the Fable **taskbar icon** (not just anywhere) during the countdown, or you capture the browser instead.

## Upstream update (2026-07-04): fable1_mods.zip

Yaranorgoth published a prebuilt `fable1_mods.zip` (pinned in the Archipelago Discord `#fable-anniversary` channel) plus `GuideToFindItemCDEFs.txt` recruiting manual testers. Changes on his side:

- `add_item_mod` now reads the CDEF from `mods/add_item_mod/add_item_id.txt` at F1-press — no recompile per ID (solves the same problem as our superseded outer-loop script).
- The windowed hook moved into a separate `mods/windowed_mod/windowed_mod.dll` so it can be deleted independently.
- Results are to be recorded in the shared Google Sheet ("Item" tab). We instead work in a personal copy of that sheet (see "Sheet sync" below) and haven't yet merged findings back into the shared one.

His intended workflow is still one ID at a time by hand (edit txt → F1 → eyeball inventory → update sheet). `batch_test_mod` remains a generation ahead: whole lists per session, no keypresses, silent adds, crash-resume, machine-readable CSV.

**Do not install his zip over a source-built setup** — his `dinput8.dll` replaces ours and drops the borderless + mouse/keyboard cooperative-level fixes (his `windowed_mod` is windowed-only). Building from our modified source provides everything the zip does and more. Untested: whether our `batch_test_mod` finds `g_AddingItemFromMod` in his rebuilt `add_item_mod.dll` (we resolve it at runtime to keep `cancel_vanilla_add_item_mod` from cancelling our adds); irrelevant while building everything from source.

## Next steps

- [x] Test trophies 4511–4527 — done session 2 (resolved White Balverine Head=4512, Map To Lost Bay=4525)
- [x] Test books 4551–4577 — done session 2 (`BOOK_GUILD_*` map onto existing sheet titles as predicted)
- [x] Identify 4551-4554 (`BOOK_STORY_13-16`) via isolated one-by-one tests — done session 3, corrected 2 wrong batch-reads (see Session 3 writeup)
- [x] Identify 4576 (`MAZE_JOURNAL`) = Arban's Thaumaturgica, 4577 (`HAUNTED_DIARY_01`) = Dusty Notebook — done session 3
- [ ] Re-verify any other position-derived (non-descriptive-symbol) mappings if found — descriptive-symbol mappings are considered reliable. The entire 4495-4577 range is now fully clean-verified with no gaps.
- [ ] Disambiguate the 4507 vs 4517 "Fist Fighters Trophy" duplicate
- [ ] Clothing (`OBJECT_HERO_*` 3404–3519) — large range, sheet names won't match symbols; screenshot mapping needed
- [ ] Tattoo/haircut/beard cards (4326–4427)
- [ ] Quest items, gifts, weapons not yet auto-matched
- [ ] Gold: find the direct "give gold" function (AddItemToInventory crashes)
- [ ] Verify TLC CDEFs carry over to Fable Anniversary via the symlink workflow
- [ ] Share findings/tooling with lgbarrere (batch_test_mod restructures his add-item flow; built as a separate mod so his originals are untouched)
- [ ] Decide whether/how to merge our personal sheet copy's confirmed CDEFs back into the shared Google Sheet ("Item" tab)
