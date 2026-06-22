# platforms/

Platform-specific projects (lowercase paths, consistent with repo root).

| Directory | Purpose |
|-----------|---------|
| `android/` | Gradle/NDK project, APK and AAB builds |
| `desktop/` | Desktop CMake presets and build/run layout |

Build scripts: [`scripts/`](../scripts/README.md).  
Distribution guides: [`packaging/`](../packaging/README.md).

## Android development environment (Windows)

```powershell
powershell -ExecutionPolicy Bypass -File scripts/env/setup-android-env.ps1
```

Sets `ANDROID_HOME`, `JAVA_HOME` (Android Studio JBR), and PATH entries for `adb`, `sdkmanager`, CMake.
