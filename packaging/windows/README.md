# Windows distribution

Desktop installer for Cubatarium using [Actual Installer](https://www.actualinstaller.com/).

## Prerequisites

- Visual Studio 2022, CMake, vcpkg (`x64-windows-static`) — see [INSTALL_DEPENDENCIES.md](../../INSTALL_DEPENDENCIES.md)
- Actual Installer at `C:\Program Files (x86)\Actual Installer\`

## Build flow

```powershell
.\configure.ps1 -Config Release
cmake --build build\desktop-msvc --config Release --parallel 8
.\scripts\build\windows-installer.ps1
```

Staging only (no Actual Installer):

```powershell
.\packaging\windows\installer\prepare-installer.ps1 -StageOnly
```

## Output

- Staging (gitignored): `packaging/windows/installer/InstallSourcesQt/`
- Setup: `packaging/windows/installer/Cubatarium-<version>.exe`

## Staged content

Copied from `bin/` (CMake POST_BUILD) with fallback to repo root:

- `Cubatarium.exe`, `config.json`
- `shaders/`, `prefabs/`, `models/`, `textures/`, `content/`, `fonts/`
- `resource_packs/` (excludes `minecraft_legacy_16`)

Default install path: `C:\Cubatarium`, executable at `<InstallDir>\bin\Cubatarium.exe`.

## Project files

- [`installer/CubatariumQt.aip`](installer/CubatariumQt.aip) — Actual Installer project
- [`installer/prepare-installer.ps1`](installer/prepare-installer.ps1) — staging script
