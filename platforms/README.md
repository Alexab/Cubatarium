# platforms/

Platform-specific build and tooling (lowercase paths, consistent with repo root).

| Directory | Purpose |
|-----------|---------|
| `android/` | Gradle project, APK build (created during Android port) |
| `windows/` | Windows helpers, including Android env setup |

## Android development environment (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File platforms/windows/setup-android-env.ps1
```

Sets `ANDROID_HOME`, `JAVA_HOME` (Android Studio JBR), and PATH entries for `adb`, `sdkmanager`, CMake.
