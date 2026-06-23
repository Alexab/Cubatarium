# Distribution channels

Cubatarium ships on multiple platforms. Build scripts live in [`scripts/`](../scripts/); per-channel guides and store assets live here.

| Channel | Platform | Guide | Build output |
|---------|----------|-------|--------------|
| Windows installer | Desktop | [windows/README.md](windows/README.md) | `packaging/windows/installer/Cubatarium-<version>.exe` |
| Linux desktop | Desktop | [linux/README.md](linux/README.md) | run from `bin/Cubatarium` |
| Google Play | Android | [android/google-play/README.md](android/google-play/README.md) | `platforms/android/app/build/outputs/bundle/release/cubatarium-*.aab` |
| Huawei AppGallery | Android | [android/huawei/README.md](android/huawei/README.md) | same AAB as Google Play |
| F-Droid (draft) | Android | [android/fdroid/README.md](android/fdroid/README.md) | built by F-Droid from source |

## Shared Android store assets

- Icons and screenshots: [`android/store-assets/`](android/store-assets/)
- Regenerate icons: `scripts/branding/generate-app-icons.ps1`

## Quick commands (from repo root)

```powershell
```powershell
.\configure.ps1 -Config Release
cmake --build build\desktop-msvc --config Release
.\scripts\build\windows-installer.ps1

.\build-android.ps1
.\build-android-release.ps1
```

Root-level `configure.ps1`, `build-android.ps1`, and `build-android-release.ps1` are shims to `scripts/build/`.
