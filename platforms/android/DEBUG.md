# Отладка Cubatarium на Android

## Сборка debug APK

```powershell
.\build-android.ps1
# или
cd platforms\android
.\gradlew.bat assembleDebug
```

APK: `platforms/android/app/build/outputs/apk/debug/app-debug.apk`

## Установка и запуск

```powershell
adb install -r platforms\android\app\build\outputs\apk\debug\app-debug.apk
adb logcat -c
adb shell am start -n com.cubatarium/.MainActivity
adb logcat -s Cubatarium:V libc:V DEBUG:V
```

## Нативные логи

Тег `Cubatarium` — `CubatariumLogInfo` / `CubatariumLogError` в `src/App/Platform/Log.cpp`.

Полезные сообщения: EGL init, ошибки шейдеров, исключения в `RunCubatarium`.

## lldb (jniDebuggable)

1. Соберите `assembleDebug` (`jniDebuggable true` в `app/build.gradle`).
2. Установите APK на устройство/эмулятор.
3. В VS Code / Cursor: конфигурация **Android Native Debug** (`.vscode/launch.json`) — attach к процессу `com.cubatarium`.

```powershell
adb forward tcp:5039 tcp:5039
```

## Типичные проблемы

| Симптом | Проверка |
|---------|----------|
| Чёрный экран | `adb logcat -s Cubatarium`; EGL / surface / шейдеры |
| Нет текстур | `AssetExtractor` — каталог `files/game/` на устройстве |
| Старый Java | `JAVA_HOME` → Android Studio JBR 21 |
| CMake / game-activity | `buildFeatures { prefab true }`, `games-activity:3.0.5` |
| «Приложение не установлено» / мгновенный вылет при старте | ABI: APK содержит `armeabi-v7a` и `arm64-v8a`. Проверка: `adb shell getprop ro.product.cpu.abilist` |
| Первый запуск долго «висит» | Нормально: копирование ассетов в `files/game/` (сотни MB) |

Проверить ABI в APK:

```powershell
# внутри APK должны быть lib/armeabi-v7a/*.so и lib/arm64-v8a/*.so
```

## Ассеты

Gradle task `syncAssets` копирует ресурсы в `app/src/main/assets/` перед сборкой.
При первом запуске Java-код копирует их в internal storage (`files/game/`).
