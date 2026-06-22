# scripts/

Build and environment helpers for Cubatarium.

## build/

| Script | Purpose |
|--------|---------|
| `android-debug.ps1` | `assembleDebug` → debug APK |
| `android-release.ps1` | `bundleRelease` → signed AAB for stores |
| `windows-configure.ps1` | CMake configure → `build/desktop-msvc/` (runtime in `bin/`) |
| `windows-installer.ps1` | Stage + Actual Installer setup EXE |
| `linux-configure.sh` | CMake configure → `build/desktop-linux/` |

Root shims: `build-android.ps1`, `build-android-release.ps1`, `configure.ps1`.

## branding/

| Script | Purpose |
|--------|---------|
| `generate-app-icons.ps1` | Icons for desktop, Android, store listings, Linux hicolor |

## env/

| Script | Purpose |
|--------|---------|
| `setup-android-env.ps1` | `ANDROID_HOME`, `JAVA_HOME`, PATH for Android SDK |
| `fix-java-precedence.ps1` | Remove Oracle Java 8 from SYSTEM Path (admin) |

## android/

| Script | Purpose |
|--------|---------|
| `generate-launcher-icons.ps1` | Shim → `branding/generate-app-icons.ps1` |
| `setup-release-keystore.ps1` | Generate upload keystore in `platforms/android/` |

Signing config (`keystore.properties`, `*.jks`) stays in `platforms/android/` next to Gradle.
