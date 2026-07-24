# Windows distribution

Desktop installer for Cubatarium using [Actual Installer](https://www.actualinstaller.com/).

## Prerequisites

- Visual Studio 2022, CMake, vcpkg — see [INSTALL_DEPENDENCIES.md](../../INSTALL_DEPENDENCIES.md)
- vcpkg triplet **`x64-windows-static`** (declared in [`vcpkg.json`](../../vcpkg.json); CRT `/MT`)
- Actual Installer at `C:\Program Files (x86)\Actual Installer\`

## System requirements (installed app)

- Windows 10/11 x64
- GPU with **OpenGL 3.3 Core** and an up-to-date driver (integrated or discrete)
- No VC++ Redistributable required (static build)

If the app closes silently, check `<InstallDir>\bin\cubatarium.log` or run from Command Prompt:

```powershell
cd C:\Cubatarium\bin
.\Cubatarium.exe --console
.\Cubatarium.exe --smoke-packs
```

Support script from repo root:

```powershell
.\scripts\doctor-windows.ps1 -BinDir C:\Cubatarium\bin
```

## Release build flow

```powershell
.\configure.ps1 -Config Release -Fresh   # use -Fresh after triplet/CRT changes
cmake --build build\desktop-msvc --config Release --parallel 8
.\scripts\build\windows-installer.ps1
```

`prepare-installer.ps1` runs verify, stages `InstallSources/`, and runs `doctor-windows.ps1`.

**Сборка setup.exe:** на Actual Installer **Free** командная строка `actinst /S` часто не создаёт дистрибутив (exit 0, файла нет). Надёжный способ — GUI:

```powershell
.\packaging\windows\installer\prepare-installer.ps1 -Gui
```

Откроется проект `Cubatarium.aip` — нажмите зелёную кнопку **Build**. Результат: `packaging/windows/installer/Cubatarium-<version>.exe` (сейчас обычно `Cubatarium-0.0.2.0.exe`).

Только staging (без Actual Installer):

```powershell
.\packaging\windows\installer\prepare-installer.ps1 -StageOnly
```

## Output

- Staging (gitignored): `packaging/windows/installer/InstallSources/`
- Setup: `packaging/windows/installer/Cubatarium-<version>.exe`

## Staged content

Copied from `bin/` (CMake POST_BUILD) with fallback to repo root:

- `Cubatarium.exe`, `config.json`
- `shaders/`, `prefabs/`, `models/`, `content/`, `fonts/`, `objects/`
- `resource_packs/` (excludes `minecraft_legacy_16`)

Default install path: `C:\Cubatarium`, executable at `<InstallDir>\bin\Cubatarium.exe`.

## Project files

- [`installer/Cubatarium.aip`](installer/Cubatarium.aip) — Actual Installer project
- [`installer/prepare-installer.ps1`](installer/prepare-installer.ps1) — staging script
