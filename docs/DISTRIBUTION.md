# Distribution

Index of release and store deployment documentation.

| Topic | Location |
|-------|----------|
| Overview (all channels) | [packaging/README.md](../packaging/README.md) |
| Build scripts | [scripts/README.md](../scripts/README.md) |
| Windows installer | [packaging/windows/README.md](../packaging/windows/README.md) |
| Linux desktop | [packaging/linux/README.md](../packaging/linux/README.md) |
| Google Play (closed alpha) | [packaging/android/google-play/README.md](../packaging/android/google-play/README.md) |
| Huawei AppGallery | [packaging/android/huawei/README.md](../packaging/android/huawei/README.md) |
| Xiaomi GetApps | [packaging/android/xiaomi/README.md](../packaging/android/xiaomi/README.md) |
| RuStore | [packaging/android/rustore/README.md](../packaging/android/rustore/README.md) |
| F-Droid (draft) | [packaging/android/fdroid/README.md](../packaging/android/fdroid/README.md) |
| Android debugging | [platforms/android/DEBUG.md](../platforms/android/DEBUG.md) |
| Third-party licenses | [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) |

## Licensing checklist for releases

- Code remains licensed under MIT (`LICENSE`).
- Distribution bundles include third-party assets/libraries with their own licenses (MIT/Apache/CC0/CC BY/CC BY-SA).
- Include and publish [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) for every release channel (installer, APK/AAB, store listing links).
- Do not relabel CC BY / CC BY-SA assets as MIT; preserve attribution and upstream license terms.

## Android release builds

```powershell
# AAB — Google Play, Huawei, RuStore
.\build-android-release.ps1

# APK — Xiaomi GetApps, RuStore, sideload
.\build-android-release-apk.ps1 -Verify
```

| Artifact | Path |
|----------|------|
| AAB | `platforms/android/app/build/outputs/bundle/release/cubatarium-<version>.aab` |
| APK | `platforms/android/app/build/outputs/apk/release/cubatarium-<version>.apk` |

Store graphics and privacy policy: `packaging/android/store-assets/`

## Recommended first deploy order

1. Build APK + AAB, smoke-test via `adb install`
2. Google Play closed alpha (AAB) + Huawei (same AAB)
3. Xiaomi GetApps (APK)
4. RuStore production, then alpha testers (VK ID)
