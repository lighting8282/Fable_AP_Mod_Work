# Fable AP Mod Work

Cataloging **Fable: The Lost Chapters / Fable Anniversary** item CDEFs (definition IDs) → in-game display names, for an Archipelago (AP) randomizer world. Builds on **Yaranorgoth's [`FableAnniversary-Random`](https://github.com/lgbarrere/FableAnniversary-Random)** mod loader.

## Current method: one-by-one verification

Every CDEF is tested **individually, on a fresh game profile**, via an F1 hotkey. This is the only fully reliable method — batch/positional reading of a cluttered inventory has produced wrong mappings before (see "Why one-by-one" below).

**Per CDEF:**
1. Set the target CDEF in `<FTLC>\mods\batch_test_mod\onebyone_list.txt` (single line) and reset `onebyone_index.txt` to `0`.
2. Launch `Fable.exe`, start a **brand-new game profile**.
3. Get in-game, press **F1** — spawns/applies the item.
4. Check inventory (or the relevant menu — some things aren't inventory items, see below) and report the single name seen.
5. Record the CDEF → name mapping in the sheet; move to the next CDEF.

`mods/batch_test_mod/batch_test_mod.cpp` is the mod source (built via the `FableAnniversary-Random` repo's `deploy.bat`, with `add_item_mod`/`cancel_vanilla_add_item_mod` removed from the deployed mods folder so nothing else grabs F1 or cancels the add). F2 exists for auto-advance+relaunch batch use but isn't used in the current one-by-one-only workflow.

## Why one-by-one (not batch/screenshot)

Earlier sessions tried adding many CDEFs at once and reading names off a screenshot of the inventory list. This **produced wrong mappings** at least twice, because:
- The inventory does not reliably show items in add-order — some menus (Books) use fixed internal display slots, not add-order.
- Duplicate display names across different CDEFs collapse into one visible row in a cluttered inventory.
- An item can land in an unexpected tab (Trophies vs. Books vs. "Items: Other" vs. Quest vs. Photo Journal).

One CDEF + one fresh profile removes all of that ambiguity — whatever single item/name appears is unambiguously that CDEF.

## Things to check for, per item

- **Not everything is a normal inventory item.** Gold, haircuts, beards, tattoos etc. apply an effect directly (hairstyle changes, stat changes) rather than becoming a held item — `AddItemToInventory` may return "rejected" for these even though the effect clearly happened. Check Hero Status / appearance screens, not just Inventory, when nothing shows up there.
- **Check all tabs**, not just the one you expect: Inventory, Trophies, Books, Items: Other, Quest, Photo Journal.
- Some CDEFs are accepted by the engine but never appear anywhere (non-holdable internal tokens) — that's a valid, useful result too.

## Sheet sync

We work in a personal copy of the shared Google Sheet, not the shared one directly. `data/Fable lists for AP - Items.tsv` mirrors its "Items" tab.

1. In the sheet: **File → Download → Tab Separated Values (.tsv)**.
2. Replace `data/Fable lists for AP - Items.tsv` in this repo with the download.
3. Commit and push.

## Repo layout

- `mods/batch_test_mod/` — the mod source (one-by-one F1 mode + legacy batch mode).
- `data/cdefs.txt` — full CDEF/symbol list scraped from FESN's OBJECT tree (2,852 entries).
- `data/Fable lists for AP - Items.tsv` — the current confirmed CDEF → name mapping (source of truth mirrors the Google Sheet).
- `tools/` — helper scripts (FESN dump parsing, symbol-name auto-matching); mostly superseded by one-by-one verification now.
- `modified-files/` — our changes to the upstream `FableAnniversary-Random` repo (`dllmain.cpp`, `windowed_hook.cpp`, `deploy.bat`, `dinput8.sln`) — copy these over the upstream repo, see `SETUP.md`.

## Next up

Continuing one-by-one through: haircut/beard/tash/tattoo cards (4326–4427), then clothing (`OBJECT_HERO_*` 3404–3519), then remaining quest items/gifts/weapons not yet mapped. Also still open: find the direct gold-stat function (gold CDEFs aren't real inventory items), and decide whether to merge into the shared Google Sheet.
