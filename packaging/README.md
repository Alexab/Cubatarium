# Distribution channels

Cubatarium ships on multiple platforms. Build scripts live in [`scripts/`](../scripts/); per-channel guides and store assets live here.

| Channel | Platform | Guide | Build output |
|---------|----------|-------|--------------|
| Windows installer | Desktop | [windows/README.md](windows/README.md) | `packaging/windows/installer/Cubatarium-<version>.exe` |
| Linux desktop | Desktop | [linux/README.md](linux/README.md) | run from `bin/Cubatarium` |
| Google Play | Android | [android/google-play/README.md](android/google-play/README.md) | `platforms/android/app/build/outputs/bundle/release/cubatarium-*.aab` |
| Huawei AppGallery | Android | [android/huawei/README.md](android/huawei/README.md) | same AAB as Google Play |
| Xiaomi GetApps | Android | [android/xiaomi/README.md](android/xiaomi/README.md) | `platforms/android/app/build/outputs/apk/release/cubatarium-*.apk` |
| RuStore | Android | [android/rustore/README.md](android/rustore/README.md) | APK or same AAB as Google Play |
| F-Droid (draft) | Android | [android/fdroid/README.md](android/fdroid/README.md) | built by F-Droid from source |

## Shared Android store assets

- Icons and screenshots: [`android/store-assets/`](android/store-assets/)
- Regenerate icons: `scripts/branding/generate-app-icons.ps1`
- Privacy policy template: [`android/store-assets/PRIVACY_POLICY.md`](android/store-assets/PRIVACY_POLICY.md)
- Third-party notices (for attribution/store links): [`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)

## Licensing notes

- Cubatarium source code is MIT-licensed (`../LICENSE`).
- Release bundles also contain third-party libraries/assets under additional licenses (for example Apache-2.0, CC0, CC BY, CC BY-SA).
- Use `THIRD_PARTY_NOTICES.md` as the canonical attribution file for Windows/Linux/Android distributions.

## Quick commands (from repo root)

```powershell
.\configure.ps1 -Config Release
cmake --build build\desktop-msvc --config Release
.\scripts\build\windows-installer.ps1

.\build-android.ps1
.\build-android-release.ps1
.\build-android-release-apk.ps1 -Verify
```

Root-level `configure.ps1`, `build-android.ps1`, `build-android-release.ps1`, and `build-android-release-apk.ps1` are shims to `scripts/build/`.
