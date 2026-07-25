#include "Inertia.h"
#include "Settings.h"
#include "InertiaPresets.h"
#include "Menu.h"
#include "ChamberExclusion.h"
#include "WeaponFOV.h"
#include "FireOnEmpty.h"
#include "ContextualLean.h"

// ============================================================
// Plugin Info
// ============================================================
namespace Plugin
{
	static constexpr auto NAME    = "FPGunplayOverhaul"sv;
	static constexpr auto VERSION = REL::Version{ 1, 1, 0 };
}

// ============================================================
// Global enabled flag — set to false if dependencies are missing
// ============================================================
namespace
{
	std::atomic<bool> g_pluginEnabled{ true };

	void DisableWithError(std::string_view title, std::string_view body)
	{
		logger::critical("[FPGunplayOverhaul] {}: {}", title, body);
		g_pluginEnabled.store(false);
		::MessageBoxA(nullptr, body.data(), title.data(), 0 /* MB_OK */);
	}

	void LogOptionalDependencies()
	{
		const auto lighthouseInfo = F4SE::GetPluginInfo("LighthousePapyrusExtender");
		if (lighthouseInfo) {
			logger::info("[FPGunplayOverhaul] LighthousePapyrusExtender v{} detected.",
				lighthouseInfo->version);
		}
	}

	// ============================================================
	// F4SE messaging callback
	// ============================================================
	// FOV Slider F4SE handshake (matched in FOV Slider F4SE/src/FOVManager.cpp):
	//   FSRF  - settings changed, refresh WBFOV defaults
	//   FSLK  - external override lock state. 1-byte payload: 0=unlock, 1=lock.
	//           While locked, FPGunplayOverhaul's WBFOV stops applying the viewmodel
	//           FOV so FOV Slider F4SE can own the Pip-Boy / Terminal /
	//           Aiming contexts without a tug-of-war.
	static constexpr std::uint32_t kFOVSliderRefreshMsg = 0x46535246;
	static constexpr std::uint32_t kFOVSliderLockMsg    = 0x46534C4B;

	void MessageCallback(F4SE::MessagingInterface::Message* msg)
	{
		if (!msg) return;

		if (msg->type == kFOVSliderRefreshMsg) {
			if (g_pluginEnabled) {
				logger::trace("[FPGunplayOverhaul] FOV Slider F4SE refresh received - reloading WBFOV defaults");
				WeaponFOV::Manager::GetSingleton()->RefreshDefaults();
			}
			return;
		}
		if (msg->type == kFOVSliderLockMsg) {
			if (g_pluginEnabled) {
				const bool locked = msg->dataLen >= 1 &&
					msg->data && *static_cast<const std::uint8_t*>(msg->data) != 0;
				WeaponFOV::Manager::GetSingleton()->SetExternalOverride(locked);
			}
			return;
		}

		switch (msg->type) {
		case F4SE::MessagingInterface::kGameDataReady:
		{
			logger::info("[FPGunplayOverhaul] kGameDataReady - initializing");
			LogOptionalDependencies();
			Settings::GetSingleton()->Load();
			InertiaPresets::GetSingleton()->Init();
			ChamberExclusion::Manager::GetSingleton()->Init();
			WeaponFOV::Manager::GetSingleton()->Init();
			FireOnEmpty::Manager::GetSingleton()->Init();
			ContextualLean::Manager::GetSingleton()->Init();
			Inertia::InertiaManager::GetSingleton()->InitSuperSprint();
			Inertia::Install();
			Menu::Register();
			break;
		}

		case F4SE::MessagingInterface::kPostLoadGame:
			if (!g_pluginEnabled) return;
			logger::info("[FPGunplayOverhaul] kPostLoadGame - reloading config");
			Settings::GetSingleton()->Load();
			Inertia::InertiaManager::GetSingleton()->OnGameLoaded();
			ChamberExclusion::Manager::GetSingleton()->ReapplyAllKeywords(
				RE::PlayerCharacter::GetSingleton());
			WeaponFOV::Manager::GetSingleton()->RefreshDefaults();
			WeaponFOV::Manager::GetSingleton()->ScheduleLoadRetry();
			ContextualLean::Manager::GetSingleton()->OnGameLoaded();
			break;

		case F4SE::MessagingInterface::kNewGame:
			if (!g_pluginEnabled) return;
			logger::info("[FPGunplayOverhaul] kNewGame - reloading config");
			Settings::GetSingleton()->Load();
			Inertia::InertiaManager::GetSingleton()->OnGameLoaded();
			ChamberExclusion::Manager::GetSingleton()->ReapplyAllKeywords(
				RE::PlayerCharacter::GetSingleton());
			WeaponFOV::Manager::GetSingleton()->RefreshDefaults();
			WeaponFOV::Manager::GetSingleton()->ScheduleLoadRetry();
			ContextualLean::Manager::GetSingleton()->OnGameLoaded();
			break;

		default:
			break;
		}
	}
} // namespace

// ============================================================
// F4SE Plugin Query
// ============================================================
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* F4SE, F4SE::PluginInfo* info)
{
	info->infoVersion = F4SE::PluginInfo::kVersion;
	info->name        = Plugin::NAME.data();
	info->version     = 1;

	if (F4SE->IsEditor()) {
		logger::critical("[FPGunplayOverhaul] Loaded in editor, skipping");
		return false;
	}

	const auto ver = F4SE->RuntimeVersion();
	if (ver < F4SE::RUNTIME_1_10_162) {
		logger::critical("[FPGunplayOverhaul] Unsupported runtime version {}", ver.string());
		return false;
	}

	return true;
}

// ============================================================
// F4SE Plugin Load
// ============================================================
extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* F4SE)
{
	F4SE::Init(F4SE, {
		.log = true,
		.logName = Plugin::NAME.data(),
		.trampoline = true,
		.trampolineSize = 128,
	});

	logger::info("{} v{}.{}.{} loading", Plugin::NAME,
		Plugin::VERSION[0], Plugin::VERSION[1], Plugin::VERSION[2]);
	logger::info(
		"Runtime {} selected ({})",
		F4SE->RuntimeVersion().string(),
		REX::FModule::IsRuntimeOG() ? "OG" :
			REX::FModule::IsRuntimeNG() ? "NG" : "AE");

	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageCallback)) {
		logger::critical("[FPGunplayOverhaul] Failed to register messaging listener");
		return false;
	}
	// Second registration for cross-plugin messages from FOV Slider F4SE.
	// F4SE's RegisterListener filters by SENDER plugin name; the default
	// listener above only sees messages whose sender is "F4SE" (the
	// kGameDataReady / kPostLoadGame stream). To receive our 'FSRF' refresh
	// from FOV Slider F4SE we register a second listener targeting that
	// plugin's name as the sender filter.
	if (!messaging->RegisterListener(MessageCallback, "FOVSliderF4SE")) {
		logger::warn("[FPGunplayOverhaul] Failed to register FOVSliderF4SE message listener (cross-plugin refresh disabled)");
	}

	logger::info("[FPGunplayOverhaul] Plugin loaded, waiting for kGameDataReady");
	return true;
}
