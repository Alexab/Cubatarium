# Отладка Cubatarium на Android

## Android Studio (рекомендуется)

### Первый запуск

```powershell
.\platforms\android\setup-studio.ps1
```

Скрипт создаёт `local.properties`, настраивает SDK и открывает Android Studio с каталогом `platforms/android`.

**В Studio:** File → Open → `platforms/android` (если открываете вручную).

### Отладка Java + Native

1. Дождитесь **Gradle Sync** (CMake подхватывает корневой `CMakeLists.txt`).
2. Выберите конфигурацию **app** и эмулятор/устройство.
   - На Windows-хосте для эмулятора нужен **x86_64 AVD** (ARM AVD на x86 не поддерживается).
3. Нажмите **Debug** (Shift+F9), не Run — в конфигурации включён **Dual** (Java + Native).
4. Breakpoints:
   - Java: `MainActivity.java`, `AssetExtractor.java`
   - C++: `platforms/android/native/android_main.cpp`, `src/App/Platform/AppRunner.cpp`, `egl_context.cpp`

Logcat откроется автоматически. Нативные логи идут с тегами **`App`**, **`EGL`**, **`Android`**, **`Asset`**, **`Text`** (не `Cubatarium`).

### Если Studio не видит C++ исходники

- Build → Refresh Linked C++ Projects
- Убедитесь, что собирается **debug** (`jniDebuggable true`, `debugSymbolLevel FULL` в `app/build.gradle`)

## Сборка debug APK (CLI)

```powershell
.\build-android.ps1
# или
cd platforms\android
.\gradlew.bat assembleDebug
```

APK: `platforms/android/app/build/outputs/apk/debug/cubatarium-<version>.apk`

## Release AAB (Google Play / AppGallery / stores)

```powershell
.\build-android-release.ps1
```

Подпись: `scripts/android/setup-release-keystore.ps1` → `platforms/android/keystore.properties` (см. `keystore.properties.example`).

AAB: `platforms/android/app/build/outputs/bundle/release/cubatarium-<version>.aab`

Инструкции: [`packaging/README.md`](../../packaging/README.md), [`docs/DISTRIBUTION.md`](../../docs/DISTRIBUTION.md).

## Установка и запуск (adb)

```powershell
adb install -r platforms\android\app\build\outputs\apk\debug\cubatarium-*.apk
adb logcat -c
adb shell am start -n com.cubatarium/.MainActivity
adb logcat -s App:E EGL:E Android:E Asset:E Text:E Text:I AndroidRuntime:E libc:F DEBUG:F
```

## Отладка из Cursor (lldb, запасной вариант)

На Windows NDK lldb может падать при attach. Альтернатива:

```powershell
.\platforms\android\debug-native.ps1
```

Конфигурация **Android Native Attach** в `.vscode/launch.json` — для x86_64 эмулятора.

## Типичные проблемы

| Симптом | Проверка |
|---------|----------|
| Gradle Sync failed | `local.properties` → `sdk.dir`; JDK 17+ (Studio JBR) |
| Чёрный экран | Logcat: `App`, `EGL`, `Text`; частая причина — нет `files/game/fonts/Roboto-Regular.ttf` |
| Нет текстур | `files/game/` на устройстве после `AssetExtractor` |
| Нет шрифта / UI не стартует | `adb shell run-as com.cubatarium ls files/game/fonts/`; должен быть `Roboto-Regular.ttf` |
| Текст не виден (меню пустое) | Logcat `Text:I` — ожидается `Loaded N glyphs (M textured)`; `Text:E` — `glTexImage2D failed` или `No textured glyphs` |
| Native breakpoints серые | Debug, не Run; rebuild debug; Refresh Linked C++ Projects |
| «Приложение не установлено» | ABI: `armeabi-v7a`, `arm64-v8a`, `x86_64` в APK |
| Первый запуск долго | Копирование ассетов в `files/game/` (сотни MB) |
| ARM AVD на Windows | Не поддерживается; используйте x86_64 AVD |

## Ассеты

Gradle task `syncAssets` копирует ресурсы в `app/src/main/assets/` перед сборкой.
При первом запуске Java-код копирует их в internal storage (`files/game/`).
