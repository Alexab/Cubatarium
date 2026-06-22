# Desktop (Windows / Linux / macOS) — CMake from repo root, runtime in bin/.

| Path | Role |
|------|------|
| [`CMakePresets.json`](../../CMakePresets.json) | `windows-msvc`, `linux-ninja` presets |
| `build/desktop-msvc/` | Windows CMake/MSBuild tree (gitignored) |
| `build/desktop-linux/` | Linux CMake/Ninja tree (gitignored) |
| [`bin/`](../../bin/) | **Runtime:** `Cubatarium` exe + game data copies |
| [`resources/branding/`](../../resources/branding/) | Shared app icon source |

Android uses [`../android/`](../android/) with its own Gradle build tree.

## Windows

```powershell
.\configure.ps1 -Config Release
cmake --build build\desktop-msvc --config Release
# or: cmake --preset windows-msvc && cmake --build --preset windows-msvc-release
```

Run from `bin/` (working directory for debug/launch).

Requires `VCPKG_ROOT` (or edit preset / `scripts/build/windows-configure.ps1`).

## Linux

```bash
./scripts/build/linux-configure.sh Release
cmake --build build/desktop-linux
./bin/Cubatarium
```

Desktop launcher: [`packaging/linux/`](../../packaging/linux/README.md).

## Icons

```powershell
.\scripts\branding\generate-app-icons.ps1
```

Regenerates `resources/branding/`, Android mipmaps, store assets, and Linux hicolor icon.
