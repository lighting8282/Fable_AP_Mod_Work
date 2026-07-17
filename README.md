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
- **Some CDEFs crash `AddItemToInventory` (0x005BF654).** The item *creates* fine (`CreateThing` succeeds) but the inventory-add itself faults. Known crashing classes — all things that need special handling the generic add doesn't do:
  - **Gold** (`OBJECT_GOLD_*`) — a stat counter, not a carried item.
  - **Weapons** (`OBJECT_IRON_*`/`STEEL_*`/`EBONY_*`/`CRYSTAL_*`/`LEGENDARY_*` swords/axes/bows, ~5474+) — equipped items. (5474 `OBJECT_IRON_LONGSWORD` is the reference case.)
  - **Clothing** (`OBJECT_HERO_*` 3404–3519) — worn-appearance parts.
  - **Augmentations** (`OBJECT_*_AUGMENTATION` 2877–2886) — applied to weapon slots, not carried.
  - **Quest cards** (`OBJECT_QUEST_CARD_*`, e.g. the punch-club cards 3782–3785) — crash like weapons. NOTE the *real* Fist-Fight items are the `PUNCH_CLUB_MEMBERSHIP_L1/L2/L3` defs (4528–4530), which add cleanly — do not confuse them with the crashing quest cards.
  - **Throwing orbs** (`OBJECT_THROWING_ORB_*` 4300–4304) — throwable-weapon class.
  - Loose world props (e.g. `OBJECT_SPOON_01` 5431, `OBJECT_FIREBALL` 5613) and `OBJECT_TATTOO_CARD_01` (4617).
  Verified thorough for weapons: crashes on both a fresh **kid** profile and an **adult** save, with `quick_access` true *and* false, `silent` true *and* false, and with the **creator's own prebuilt `add_item_mod`** (not just ours). So it is not our method — it's the function. These categories each need a dedicated grant path (give-gold, equip-weapon, wear-clothing), which we have not located.

## Deceptive symbols — trust the in-game name, not the symbol

The FESN symbol is frequently **not** the display name. Re-verifying already-"mapped" items one-by-one has repeatedly caught mislabels and recovered items previously thought absent. Confirmed examples:
- `OBJECT_TROPHY_DRAGON_GATE_01` (4524) displays as **"Archon's Circle"** — not "Dragon Gate". This recovered an item we'd marked absent, and exposed an old unverified "Dragon Gate = 4524" guess as wrong. No distinct "Dragon Gate" trophy exists.
- The six "self-help" books (The Sock Method, Making Friends, Eyes of a Killer, The Ugly Guide, Windbreaker Rule Book, You Are Not a Bad Person) live under `OBJECT_BOOK_RAISE/REDUCE_SEXINESS/AGREEABILITY/SCARINESS` (4320–4325) — nowhere near the `BOOK_STORY`/`BOOK_GUILD` range.
- `OBJECT_TROPHY_JOB_MASK_01` (4523) = "Jack's Mask"; `OBJECT_TROPHY_MAZE_HEAD_01` (4515) = "Maze's Clasp"; `OBJECT_WILL_POTION` (4296) = "Ages of the Will" while the plain "Will Potion" is `OBJECT_MANA_POTION` (4293).

**Lesson:** before declaring a sheet item "absent," gap-analyze the CDEF band around related mapped items and test candidates — the display name is authoritative.

## Known blocker: clothing

The sheet lists ~190 clothing items, but the OBJECT tree only contains:
- `OBJECT_HERO_*` (3404–3519) — **crashes on add**; confirmed with both the base template (3404 `OBJECT_HERO_BOOTS`) and a named variant (3422 `OBJECT_HERO_BOOTS_CHAINMAIL`). These appear to be worn-appearance model parts, not carryable items.
- `OBJECT_CLOTHING_*` (3348–3352) — only 5 generic templates (footwear/trousers/shirt/gloves/hat).

Neither gives us per-item clothing CDEFs. The real ones may live in a FESN def section that was never scraped (only `OBJECT` was). **Investigate that before attempting clothing again** — don't grind the `HERO_*` range, it just crashes.

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

## Status

**~247 sheet items verified-working** (single-CDEF isolated tests), plus **10 bonus items** the sheet omits.

**Categories complete (all sheet items add cleanly and are re-verified):**
- **Book 43/43** — includes the six `BOOK_RAISE/REDUCE` "self-help" books (4320–4325). All re-verified individually.
- **Trophy** — all sheet trophies re-verified; Archon's Circle recovered (4524), Fist Fighter's Trophy mapped (4517), phantom "Dragon Gate" corrected.
- **Potion 8/8**, **Food 12/12**, **Gift 13/13**, **Style 25/25**, **Tool 6/6**.

**Partial:** Tattoo (72/77 — a few custom/empty-slot + 4617-crash rows), Misc (dolls/sacks done; the 10 augmentations are known-CDEF-but-crash; 5 "artifact" names — Demon Relic, Rock of Necrilia, Skorm's Tear, Slab of Jeroen, Tablet of Torment — have no findable symbol), Quest (Fist Fight L1–3 = memberships 4528–4530; Bandit clothing + Nostro weapons are known-CDEF-but-crash).

**Blocked (crash `AddItemToInventory`, need dedicated grant paths):** weapons/Melee/Ranged (~5474–5636), clothing (`OBJECT_HERO_*` 3404–3519), gold, augmentations, quest cards, throwing orbs. See the crash-classes note above.

**Not inventory items:** Emote (expressions) and Spell (`OBJECT_ABILITY_*_SPELL_DUMMY`) add nothing — they need ability-grant paths, not inventory-add.

**Bonus items found (valid, not on the original sheet):** Adrenaline Potion (4299), Pocket Watch (4308), Jewel Box (4479), and the 7-item creature trade-drop cluster (`BALVERINE_CLAW`→Balverine Pelt, Nymph Wings, Troll Lump, Bandit Ears, Wasp Sting, Skeleton Bones, Hobbes Teeth, 4451–4457).

**Also open:** find the equip-weapon / wear-clothing / give-gold / apply-augmentation functions to unblock those categories (deeper RE, or ask the creator); locate the 5 mystery Misc artifacts (likely a non-`OBJECT` FESN section, or not real TLC items); populate the personal Google Sheet from this TSV (currently only the repo TSV is up to date).
