# FP Inertia (F4SE)

First-person weapon inertia and related tweaks for **Fallout 4**, implemented as an **F4SE** plugin. In-game configuration uses the **F4SE Menu Framework** (ImGui). Optional features include weapon-based viewmodel FOV (WBFOV), chamber-exclusion keyword support, and integration hooks for other FOV plugins.

**Repository:** [github.com/DCCStudios/F4-FPInertiaF4SE](https://github.com/DCCStudios/F4-FPInertiaF4SE)

## Requirements (runtime)

| Requirement | Notes |
|-------------|--------|
| **Fallout 4** | Current supported F4SE branch for your game version. |
| **[F4SE](https://f4se.silverlock.org/)** | Script extender matching your game build. |
| **[F4SE Menu Framework](https://www.nexusmods.com/fallout4/mods/58459)** | Required for the in-game menu (`F4SEMenuFramework.dll`). Without it, the plugin still runs but the menu is unavailable. |

### Optional integrations

- **FOV Slider F4SE** — messaging handshake for WBFOV defaults and lock/unlock during Pip-Boy / terminals / aiming contexts.
- **LighthousePapyrusExtender** — detected at load; logged when present.
- **UneducatedReload** — extras UI for runtime `ChamberExclusion` keyword list (when that plugin is installed).

## User data paths

Relative to the game folder:

| Path | Purpose |
|------|---------|
| `Data\F4SE\Plugins\FPInertia.dll` | Plugin binary |
| `Data\F4SE\Plugins\FPInertia.ini` | Global settings |
| `Data\F4SE\Plugins\FPInertia\` | Per-weapon-type presets and weapon-specific JSON |
| `Data\F4SE\Plugins\FPInertia\WBFOV\` | Per-weapon viewmodel FOV JSON (`{EditorID}.json`) |

Logs: `Documents\My Games\Fallout4\F4SE\FPInertia.log`

## Building from source

### Prerequisites

- **Windows**, **Visual Studio 2022** (C++ desktop workload)
- **CMake** 3.21 or newer
- **[vcpkg](https://github.com/microsoft/vcpkg)** with `VCPKG_ROOT` set (manifest mode; see `vcpkg.json`)
- **[CommonLibF4](https://github.com/CharmedBaryon/CommonLibF4)** checked out so this project can add it as a subdirectory:

  Default layout expected by `CMakeLists.txt`:

  ```
  <parent>/
    FPInertia/          ← this repository (clone here)
    PluginTemplate/
      CommonLibF4/
        CommonLibF4/    ← add_subdirectory target
  ```

  If your tree differs, edit the `add_subdirectory(...CommonLibF4...)` path in `CMakeLists.txt`.

- Optional: set **`Fallout4Path`** to your game install and enable **`COPY_BUILD`** in CMake to copy the DLL, PDB, and `FPInertia.ini` after each build.

### Configure and compile

```powershell
cd FPInertia
cmake --preset <your-preset>   # or configure manually with -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build build/release --config Release
```

Output is written under `Compile\F4SE\Plugins\` (see `CMAKE_*_OUTPUT_DIRECTORY` in `CMakeLists.txt`). Copy `FPInertia.dll` and `FPInertia.ini` into `Data\F4SE\Plugins\` for testing.

### Toolchain notes

- **C++23**, **MSVC**, static vcpkg triplet `x64-windows-static-md` (see `CMakeLists.txt`).
- Dependencies pulled via vcpkg include **nlohmann-json**, **spdlog**, and CommonLibF4’s transitive packages (Boost pieces, **fmt**, etc.).

## License

The `vcpkg.json` manifest lists **MIT** for this package metadata. Add a root `LICENSE` file to the repository if you want SPDX-standard terms on GitHub; dependency and CommonLibF4 licenses remain theirs.

## Contributing

Issues and pull requests are welcome against [DCCStudios/F4-FPInertiaF4SE](https://github.com/DCCStudios/F4-FPInertiaF4SE). Please match existing code style and keep changes scoped to the problem being solved.
