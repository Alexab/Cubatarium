# Xiaomi GetApps (小米应用商店)

Выкладка Cubatarium в [Xiaomi HyperOS Developer Platform](https://dev.mi.com) (GetApps). В отличие от Google Play, Xiaomi принимает **только APK**, не AAB.

## Почему отдельный артефакт

| Аспект | Cubatarium |
|--------|------------|
| Формат загрузки | **Signed APK** (`cubatarium-<version>.apk`) |
| Package name | `com.cubatarium` |
| AAB | **Не поддерживается** для загрузки |
| Подпись | Тот же upload keystore (`platforms/android/keystore.properties`) |
| CPU | Universal APK: `armeabi-v7a` + `arm64-v8a` (режим «单包上传») |

## 1. Сборка APK

```powershell
.\scripts\android\setup-release-keystore.ps1
# platforms\android\keystore.properties — см. google-play/README.md

.\build-android-release-apk.ps1 -Verify
```

Артефакт: `platforms/android/app/build/outputs/apk/release/cubatarium-<version>.apk`

## 2. Регистрация разработчика

1. Откройте [dev.mi.com](https://dev.mi.com) → регистрация разработчика (小米开放平台)
2. Пройдите верификацию аккаунта (email, документы — по региону)
3. **应用管理** → создать приложение → package `com.cubatarium`

## 3. Загрузка APK

1. На странице создания/обновления приложения выберите **单包上传** (один APK)
2. Загрузите signed APK (`cubatarium-*.apk`)
   - Подходит universal-сборка с `armeabi-v7a` + `arm64-v8a`
   - Альтернатива позже: **双包上传** (отдельные 32-bit и 64-bit APK)
3. Дождитесь автопарсинга и проверьте поля:
   - `versionName`, `versionCode`, `targetSdk`
   - CPU-адаптация: 32/64 compatible или 64-bit
4. Заполните store listing: название, описание, категория (Game)
5. Иконка и скриншоты из [`../store-assets/`](../store-assets/)

## 4. Модерация и тестирование

- Отправьте на проверку; модерация обычно **1–3 рабочих дня**
- До публичного релиза можно ограничить доступ внутренним тестированием (если доступно в консоли)
- Для обновлений: `versionCode` должен быть **строго больше** предыдущего

## 5. Обновления

1. Увеличить `versionCode` (новый git commit или `platforms/android/version.properties`)
2. `.\build-android-release-apk.ps1 -Verify`
3. В консоли: **更新版本** → загрузить новый APK → модерация

## Чеклист перед первой загрузкой

- [ ] `keystore.properties` и бэкап `cubatarium-upload.jks`
- [ ] `assembleRelease` успешен, APK подписан upload-ключом
- [ ] `apksigner verify` проходит (`-Verify` в скрипте)
- [ ] `versionCode` > 0
- [ ] Иконка 512×512 и минимум 2–3 скриншота (landscape)
- [ ] Privacy policy URL (офлайн-игра, данные не собираются)

## См. также

- [Google Play](../google-play/README.md) — AAB, closed alpha
- [RuStore](../rustore/README.md) — APK или AAB
- [store-assets](../store-assets/) — общие графические материалы
