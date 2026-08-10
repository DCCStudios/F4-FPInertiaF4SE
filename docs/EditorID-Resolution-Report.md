# Report: Weapon EditorID resolution failure (v1.3.0, "No EDID" log)

**Date:** 2026-08-06
**Input:** Cursor-model handoff analysis of `FPGunplayOverhaul (1) No EDID.log` (AE 1.11.221, v1.3.0)
**Verdict:** The handoff's code-level findings are all accurate, but it missed the actual root
cause, and its recommended fix (reverse lookup in `AllFormsByEditorID`) **will not work** for the
affected users.

---

## 1. Verification of the handoff's claims

| Claim | Verdict | Evidence |
|---|---|---|
| `FetchEquippedWeapon` leaves `editorID` empty when `GetFormEditorID()` is empty | ✅ Confirmed | `Inertia.cpp:2365-2366` |
| `GetEquippedWeaponEditorID` (member) returns `""` on empty | ✅ Confirmed | `Inertia.cpp:2405-2411` |
| `GetEquippedWeaponEditorIDStatic` falls back to `"0x{:08X}"` | ✅ Confirmed | `Inertia.cpp:2214-2221` |
| WBFOV menu refuses entry creation on empty EDID | ✅ Confirmed | `Menu.cpp:2950-2953` |
| Fire-on-Empty menu refuses entry creation on empty EDID | ✅ Confirmed | `Menu.cpp:3113-3116` |
| Fire-on-Empty runtime skips when EDID empty | ✅ Confirmed | `Inertia.cpp:4207-4209` ("Skipped: equipped weapon has no EditorID") |
| Chamber Exclusion keys by FormID with display fallback (unaffected) | ✅ Confirmed | `Menu.cpp:4631-4651`, `ChamberExclusion` API takes FormID |
| Menu can create a `0x…`-keyed inertia preset the runtime never matches | ✅ Confirmed | Create path: `Menu.cpp:21-24` → `GetEquippedWeaponEditorIDStatic` (`0x…` fallback) → `Menu.cpp:2339/2369` keys the preset. Runtime lookup: `Inertia.cpp:2600` uses `weap.editorID`, which stays `""` → looks up nothing. |
| Lighthouse is detect-and-log only; no runtime dependency | ✅ Confirmed | `main.cpp:36-43` |
| WBFOV `Transition (no apply): weapon='<none>'` line format | ✅ Confirmed | `WeaponFOV.cpp:813-818`; `'<none>'` with `present=yes` means an equipped WEAP whose `GetFormEditorID()` returned empty |
| "Weapon-type keywords resolve, so AllFormsByEditorID is fine" | ⚠️ **Faulty inference** | See §2. Keywords resolving proves the map works *for keywords* — it says nothing about weapons, and on this user's setup weapons are almost certainly **not in that map at all**. |

## 2. Root cause (missed by the handoff)

**The vanilla Fallout 4 engine discards editor IDs for `TESObjectWEAP` (and most other form
types) at load time.** `TESForm::GetFormEditorID()` is virtual (vfunc `0x3A`) and the base
implementation returns `""` (`lib/commonlibf4/include/RE/T/TESForm.h:96`); the base
`SetFormEditorID` (vfunc `0x3B`) discards the string during plugin parsing. Only a small set of
form classes override these and retain their EDIDs — `BGSKeyword`, `BGSAction`,
`BGSLocationRefType`, `TESGlobal`, `TESRace`, quests, etc. That is exactly why FPGO's keyword and
`ActionMelee` lookups succeed in the same session where every WEAP returns an empty EDID. This is
not a bug in the user's setup, and no vanilla runtime version returns WEAP editor IDs.

**Why it works on some machines (including, presumably, the dev machine):**
[Baka Framework](https://www.nexusmods.com/fallout4/mods/43627) ships a `LoadEditorIDs` patch
(`bEnableLoadingEditorIDs`, **default true**) that vfunc-hooks `Get/SetFormEditorID` (0x3A/0x3B)
per form-class vtable — `TESObjectWEAP` is explicitly in its install list — caching
FormID→EDID privately and inserting each form into the game's `allFormsByEditorID` map
([source](https://github.com/shad0wshayd3-FO4/BakaFramework/blob/master/src/Misc/Patches/LoadEditorIDs.h)).
In Baka's source, `BGSKeyword`/`BGSAction` are the *commented-out* entries — because the engine
already retains those — which independently confirms the engine's native behavior. Lighthouse's
own documentation likewise states its `GetFormEditorID` papyrus function returns an empty string
unless Baka Framework is installed.

So: users with Baka Framework (very common in mod lists) see every per-weapon feature work; users
without it get empty EDIDs for **all** weapons, vanilla included — exactly this log.

## 3. Why the handoff's recommended fix fails

Its `ResolveWeaponEditorID()` step 2 — "reverse lookup in AllFormsByEditorID" — cannot recover a
weapon EDID on the affected setups, because WEAP forms are only *inserted* into that map by the
same third-party hook that already makes `GetFormEditorID()` work. The two data sources fail
together. On a vanilla engine the resolver degrades to step 3 (`"0x{:08X}"`), which:

- still cannot match author-shipped `Weapons/{EditorID}.json`, `WBFOV/{EditorID}.json`,
  `FireOnEmpty/{EditorID}.json` files (the entire distribution contract in `ModDescription.md`);
- produces load-order-fragile keys.

(Also a mechanical note: the map is `BSTHashMap<BSFixedString(EDID) → TESForm*>`, so a "reverse
lookup" is a full O(n) scan needing its own cache — cost with zero payoff here.)

## 4. Recommended fix

> **Implementation status (2026-08-06):** 4a and 4d are implemented (`src/EditorIDCache.cpp`,
> installed in `F4SEPlugin_Load`, diagnostics at `kGameDataReady`), plus the 4c lookup-side fix:
> `FetchEquippedWeapon` now falls back to the same `0x{:08X}` key the menu uses, so
> fallback-keyed presets round-trip. Full resolver unification (4c) and 4b remain open.

### 4a. Primary — own the hook (recommended)

Install the same per-vtable vfunc hooks Baka uses, but **only for `TESObjectWEAP`** (add
`TESAmmo` if `Inertia.cpp:2515`'s ammo-EDID logging should also work), at plugin load
(before `kGameDataReady`):

- Hook vfunc `0x3B` (`SetFormEditorID`) on `TESObjectWEAP::VTABLE[0]`: record
  `formID → std::string` in a private map (skip runtime forms `>= 0xFF000000`), then chain to
  the original.
- Hook vfunc `0x3A` (`GetFormEditorID`): return the cached string if present, else chain.

Properties:
- ~50 lines; CommonLibF4 `VTABLE` relocations already carry OG/NG/AE addresses, matching the
  project's existing multi-runtime support.
- **Coexists with Baka Framework**: both hooks chain to the previous vfunc, so install order
  doesn't matter; results are identical either way.
- Do **not** insert into the game's `allFormsByEditorID` map — FPGO only ever needs
  form→EDID for weapons, and staying out of the shared map avoids EDID-collision side effects
  and any behavioral overlap with Baka's conflict check.
- Memory cost is trivial (one string per WEAP record).

This restores the documented EditorID-keyed JSON behavior for *all* users with no new dependency.

### 4b. Alternative — declare the dependency

Detect Baka Framework at load (like the existing Lighthouse probe, `main.cpp:38`) and, when
absent, log a prominent warning and surface it in the menu ("Per-weapon presets require Baka
Framework"). Zero engine-touching code, but pushes the burden onto every user and mod-pack author.
Only worth it if hooking is off the table.

### 4c. In the same patch, regardless of 4a/4b — unify resolution

The handoff's *consistency* recommendation stands. One shared `ResolveWeaponEditorID(TESForm*)`:

1. `GetFormEditorID()` (now backed by 4a's cache) if non-empty;
2. fallback `"0x{:08X}"` — used identically by **create and lookup** paths, killing the current
   mismatch where the inertia menu keys a preset `0x…` (`Menu.cpp:2369`) that the runtime looks
   up as `""` (`Inertia.cpp:2600`);
3. log once per FormID when falling back.

Optional hardening: make runtime lookups try the resolved EDID first, then the `0x{:08X}` key, so
presets created during a no-EDID session still match after the user's EDID source changes (and
vice versa). Note `0x…` keys are load-order-sensitive; the one-time fallback log should say so.

### 4d. Diagnostics

Add a one-time post-`kGameDataReady` probe: fetch any vanilla WEAP (e.g. via `GetFormByID` on a
known Fallout4.esm weapon) and log whether `GetFormEditorID()` yields a value. A single
`"weapon EditorID resolution: OK/unavailable"` line would have made this report a one-glance
diagnosis. Also split the debug/menu message currently shown for empty EDIDs so "no weapon
equipped" and "weapon has no EditorID" are distinct (WBFOV/FOE menus already do this;
the inertia debug snapshot does not — `Inertia.cpp:2249`).

## 5. Sources

- [Baka Framework — Nexus](https://www.nexusmods.com/fallout4/mods/43627) and
  [LoadEditorIDs.h source](https://github.com/shad0wshayd3-FO4/BakaFramework/blob/master/src/Misc/Patches/LoadEditorIDs.h)
  (hooked-type list incl. `TESObjectWEAP`; `bEnableLoadingEditorIDs` default true in `src/Config/Config.h`)
- [Lighthouse Papyrus Extender — Nexus](https://www.nexusmods.com/fallout4/mods/71420) /
  [GitHub](https://github.com/GELUXRUM/LighthousePapyrusExtender) — its `GetFormEditorID`
  papyrus function documents returning `""` without Baka Framework
- CommonLibF4 `TESForm.h` (vfuncs 0x3A/0x3B defaults; `GetAllFormsByEditorID`)
