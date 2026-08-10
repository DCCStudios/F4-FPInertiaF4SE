#pragma once

// Runtime editor-ID cache for weapon (and ammo) forms.
//
// The vanilla engine only retains editor IDs for a handful of form types
// (keywords, actions, races, globals, ...). For everything else — WEAP
// included — TESForm::SetFormEditorID (vfunc 0x3B) discards the string
// during plugin parsing, so GetFormEditorID (vfunc 0x3A) returns "" and
// every EditorID-keyed feature (per-weapon inertia presets, WBFOV,
// Fire on Empty JSONs) silently dies unless the user happens to run a
// third-party EDID loader (Baka Framework's LoadEditorIDs patch). These
// hooks capture the editor ID as each record is parsed and serve it back
// from a private cache, removing that hidden dependency.
namespace EditorIDCache
{
	// Installs the Get/SetFormEditorID vfunc hooks on the WEAP and AMMO
	// vtables. Must run inside F4SEPlugin_Load: game data loading (where
	// SetFormEditorID is invoked per record) begins after all F4SE
	// plugins have loaded, and any form parsed before the hook is in
	// place would be missing from the cache.
	void Install();

	// One-line health check for user logs: probes a vanilla weapon and
	// reports whether weapon editor-ID resolution works on this setup.
	// Call at kGameDataReady, after all plugins have been parsed.
	void LogDiagnostics();
}
