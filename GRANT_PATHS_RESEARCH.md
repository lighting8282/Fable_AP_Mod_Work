# Grant-path research — unblocking gold / weapons / clothing / XP

The one grant path we use, `AddItemToInventory` (0x005BF654), only handles **carried
items**. Gold, weapons, clothing, augmentations, spells and emotes crash it (see
README "crash classes"). Each needs its own grant path. Web research (July 2026)
turned up concrete leads for how the game itself grants these — each maps to a native
engine function or a field in the hero's creature structure that a DLL mod can target
the same way the creator already targets `AddItemToInventory`.

## Gold — STRONG lead

Fable TLC's script system exposes a native command:

```
Money.Add(GetLocalHero(), 100000, 0)
```

- `GetLocalHero()` returns the hero creature (we already locate the hero/inventory).
- `Money.Add(hero, amount, flags)` credits gold — this is the real give-gold path.
- Confirmed the plain `OBJECT_GOLD_N` item route is a dead end: prime-test proved
  even adding `OBJECT_HERO_MONEY_BAG` (4306) first does NOT stop the crash. Gold is a
  stat, not an item.

**Action:** find the native function behind `Money.Add` (string/xref search in the
binary for "Money.Add" or the coins field on the hero), or the hero-gold offset, and
call/write it from the mod. This single unlock covers all 58 sheet gold amounts (which
are AP reward *values*, not distinct items — the game only has ~17 denominations).

## Experience — lead

- `Debug.SpawnExperienceOrbs()` — script command that grants XP via orbs.
- Same approach: find the native function or the hero XP field.

## Weapons — lead (hero creature structure)

Community weapon-give tutorials add a weapon by writing its CDEF into the
**`Weapon 1` (…N) field of the `Creature_Hero` def (Creature #1470)**, NOT via
inventory-add. So the runtime grant path is: write the weapon CDEF into the hero
creature's weapon slot(s) rather than calling `AddItemToInventory`.

**Action:** locate the hero creature's weapon-slot fields at runtime (CHero/CCreature
structure) and write the CDEF there. Covers Melee (71) + Ranged (14).

## Clothing — lead (hero creature structure)

Clothing is added to the hero's **`CCreatureDef` worn/clothing list** (worn-appearance
parts), which is why `OBJECT_HERO_*` crash `AddItemToInventory`. Runtime grant path:
add the clothing CDEF to the hero's clothing list.

**Action:** locate the hero clothing list in the CHero/CCreature structure and append
the CDEF. Covers Clothing (195).

## Augmentations — related

`OBJECT_*_AUGMENTATION` (2877–2886) apply to a weapon's augment sockets, so they need
the weapon path plus a socket-apply call — revisit after weapons are solved.

## Best next step

The creator (Yaranorgoth) already hooks engine functions and knows the CThing/CHero
layout — armed with these **named leads** (`Money.Add`, `Debug.SpawnExperienceOrbs`,
`Creature_Hero.Weapon 1`, `CCreatureDef` clothing list), they are well-positioned to
point us at the addresses/offsets, or confirm them quickly. This is the highest-value
question to ask them.

## Sources

- FableTLCMod wiki / forums (script "Book of Scripts"): http://www.fabletlcmod.com/wiki/doku.php
- `Money.Add(GetLocalHero(), 100000, 0)` and `Debug.SpawnExperienceOrbs()` surfaced via
  fabletlcmod script discussion.
- Weapon-give (Creature_Hero "Weapon 1" field): http://fablemodding.blogspot.com/2012/11/how-to-mod-adding-weapon-to-your.html
- Weapon/clothing via CCreatureDef: fabletlcmod tutorials + fablehero.com forums.
- Cheat-table communities (gold pointer paths, alternative to a function):
  FearlessRevolution thread t=9065, XPG cheat table.
