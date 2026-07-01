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
| 4499 | Balverine's Head | added — sheet's "White Balverine Head" is likely 4512 `TROPHY_BALVERINE_FM_HEAD` (untested) |
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
| 4510 | Archaeologist's Map | added — sheet's "Map To Lost Bay" is likely 4525 `TROPHY_SHIP_MAP` (untested) |

`data/Fable lists for AP - Items UPDATED.tsv` is the AP sheet with all confirmed CDEFs filled in (16 rows updated, 12 new rows).

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

## Next steps

- [ ] Test remaining trophies 4511–4527 (resolves the White Balverine Head / Map To Lost Bay guesses)
- [ ] Test books 4551–4554 (`BOOK_STORY_13–16`) and 4555–4577 (`BOOK_GUILD_*` — symbols suggest they cover most remaining sheet books)
- [ ] Clothing (`OBJECT_HERO_*` 3404–3519) — large range, sheet names won't match symbols; screenshot mapping needed
- [ ] Tattoo/haircut/beard cards (4326–4427)
- [ ] Quest items, gifts, weapons not yet auto-matched
- [ ] Gold: find the direct "give gold" function (AddItemToInventory crashes)
- [ ] Verify TLC CDEFs carry over to Fable Anniversary via the symlink workflow
- [ ] Share findings/tooling with lgbarrere (batch_test_mod restructures his add-item flow; built as a separate mod so his originals are untouched)
