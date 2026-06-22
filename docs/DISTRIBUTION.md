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
| F-Droid (draft) | [packaging/android/fdroid/README.md](../packaging/android/fdroid/README.md) |
| Android debugging | [platforms/android/DEBUG.md](../platforms/android/DEBUG.md) |
| Third-party licenses | [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) |

## Android release AAB (all stores)

```powershell
.\build-android-release.ps1
```

Artifact: `platforms/android/app/build/outputs/bundle/release/cubatarium-<version>.aab`

Store graphics: `packaging/android/store-assets/`
