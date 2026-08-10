#include "EditorIDCache.h"

namespace
{
	// FormID -> editor ID. std::unordered_map is node-based, so the
	// c_str() handed out by the getter hook stays valid across rehashes
	// for the lifetime of the process (entries are never erased).
	// Guarded because records are parsed on the loader thread while the
	// menu and runtime read from the game/render threads.
	std::shared_mutex g_edidLock;
	std::unordered_map<std::uint32_t, std::string> g_edids;

	// Per-vtable Get/SetFormEditorID (0x3A/0x3B) hook. Both thunks chain
	// to the previous vfunc, so coexistence with Baka Framework's
	// LoadEditorIDs patch is order-independent — whichever installed
	// last answers first and defers to the other on a miss. We
	// deliberately do NOT insert into the game's AllFormsByEditorID map:
	// FPGO only ever needs form -> EditorID for weapons, and staying out
	// of the shared map avoids EDID-collision interactions with other
	// plugins' patches.
	template <class T>
	class FormEditorIDHook
	{
	public:
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtable{ T::VTABLE[0] };
			GetOriginal = vtable.write_vfunc(0x3A, GetThunk);
			SetOriginal = vtable.write_vfunc(0x3B, SetThunk);
		}

	private:
		static const char* GetThunk(const RE::TESForm* a_this)
		{
			{
				std::shared_lock lock(g_edidLock);
				auto it = g_edids.find(a_this->formID);
				if (it != g_edids.end()) {
					return it->second.c_str();
				}
			}
			return GetOriginal(a_this);
		}

		static bool SetThunk(RE::TESForm* a_this, const char* a_edid)
		{
			// Skip runtime-created forms (0xFFxxxxxx): their FormIDs are
			// transient and remapped across save loads.
			if (a_edid && a_edid[0] && a_this->formID < 0xFF000000) {
				std::unique_lock lock(g_edidLock);
				// try_emplace: a later override of the same FormID keeps
				// the first-seen EDID (overrides carry the same one).
				g_edids.try_emplace(a_this->formID, a_edid);
			}
			return SetOriginal(a_this, a_edid);
		}

		inline static REL::Relocation<decltype(GetThunk)> GetOriginal;
		inline static REL::Relocation<decltype(SetThunk)> SetOriginal;
	};
}

void EditorIDCache::Install()
{
	FormEditorIDHook<RE::TESObjectWEAP>::Install();
	FormEditorIDHook<RE::TESAmmo>::Install();
	logger::info("[EditorIDCache] Get/SetFormEditorID vfunc hooks installed (WEAP, AMMO)");
}

void EditorIDCache::LogDiagnostics()
{
	std::size_t cached = 0;
	{
		std::shared_lock lock(g_edidLock);
		cached = g_edids.size();
	}

	// Probe a vanilla weapon so a user log answers "do weapon editor IDs
	// resolve on this setup?" at a glance. 0x00004822 is the Fallout4.esm
	// 10mm pistol; fall back to scanning in case a setup repurposes it.
	auto* probe = RE::TESForm::GetFormByID(0x00004822);
	if (!probe || probe->formType != RE::ENUM_FORM_ID::kWEAP) {
		probe = nullptr;
		const auto& [map, lock] = RE::TESForm::GetAllForms();
		RE::BSAutoReadLock l{ lock };
		if (map) {
			for (const auto& [id, form] : *map) {
				// Restrict to Fallout4.esm (load index 00) so the probe
				// can't be satisfied by a mod-added weapon.
				if (form && form->formType == RE::ENUM_FORM_ID::kWEAP && (id >> 24) == 0x00) {
					probe = form;
					break;
				}
			}
		}
	}

	const char* eid = probe ? probe->GetFormEditorID() : nullptr;
	if (eid && eid[0]) {
		logger::info("[EditorIDCache] Weapon editor-ID resolution OK ({} forms cached; probe 0x{:08X}='{}')",
			cached, probe->GetFormID(), eid);
	} else {
		logger::warn(
			"[EditorIDCache] Weapon editor-ID resolution UNAVAILABLE ({} forms cached) — "
			"EditorID-keyed per-weapon presets, WBFOV and Fire on Empty entries will not match; "
			"FormID (0x...) fallback keys apply where supported",
			cached);
	}
}
