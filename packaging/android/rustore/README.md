# RuStore

Выкладка Cubatarium в [RuStore Console](https://console.rustore.ru). Поддерживаются **APK** и **AAB**; для единообразия с Xiaomi можно использовать тот же signed APK.

## Артефакты

| Формат | Команда | Путь |
|--------|---------|------|
| APK | `.\build-android-release-apk.ps1 -Verify` | `platforms/android/app/build/outputs/apk/release/cubatarium-*.apk` |
| AAB | `.\build-android-release.ps1` | `platforms/android/app/build/outputs/bundle/release/cubatarium-*.aab` |

Package name: `com.cubatarium`. Подпись: upload keystore из `platforms/android/keystore.properties`.

## 1. Подготовка сборки

```powershell
.\scripts\android\setup-release-keystore.ps1
# platforms\android\keystore.properties

.\build-android-release-apk.ps1 -Verify   # или build-android-release.ps1 для AAB
```

См. также [google-play/README.md](../google-play/README.md) (keystore, versionCode).

## 2. Регистрация

1. [RuStore Console](https://console.rustore.ru) → регистрация разработчика
2. Верификация (физ. или юр. лицо — по требованиям RuStore)
3. Создать приложение → Android → `com.cubatarium`

## 3. Первая публикация (production)

RuStore требует заполненную карточку приложения перед модерацией.

1. **Загрузить версию** → выбрать signed APK или AAB
2. Заполнить описание, иконку, скриншоты ([`../store-assets/`](../store-assets/))
3. Указать контакты разработчика и privacy policy
4. Отправить на модерацию
5. После одобрения — опубликовать (ручная или автоматическая публикация по настройкам)

> **Важно для альфы:** закрытое альфа-тестирование в RuStore может потребовать **уже опубликованную production-версию**. Рекомендуемый порядок: сначала минимальный production-релиз, затем альфа/бета для тестеров.

## 4. Альфа-тестирование (закрытое)

1. RuStore Console → **Тестирование** → **Альфа-версии**
2. **Загрузить альфа-версию** → signed APK или AAB (новый `versionCode`)
3. Дождаться модерации
4. **Тестировщики** → добавить по **VK ID** (тестер должен быть авторизован в RuStore на телефоне)
5. Тестеры устанавливают/обновляют приложение **только через приложение RuStore** на устройстве (веб-каталог альфу не показывает)

Ограничения:

- Одновременно активна только одна альфа-версия
- Альфа доступна в мобильном каталоге RuStore, не в веб-версии

## 5. Бета-тестирование

Аналогично альфе: раздел **Бета-версии** в консоли. Подходит для расширенного круга тестеров после альфы.

## 6. Обновления

1. Увеличить `versionCode`
2. Пересобрать APK/AAB
3. Загрузить в нужный трек (production / alpha / beta)
4. Пройти модерацию

При обновлении production: подпись и package name должны совпадать с предыдущими версиями.

## Чеклист

- [ ] Signed APK или AAB собран
- [ ] `versionCode` уникален и больше предыдущего
- [ ] Иконка, скриншоты, описание заполнены
- [ ] Privacy policy URL указан
- [ ] Для альфы: production опубликован (если требует консоль)
- [ ] Тестеры добавлены по VK ID, установка через RuStore app

## См. также

- [Xiaomi GetApps](../xiaomi/README.md) — только APK
- [Google Play](../google-play/README.md) — AAB, closed testing
- [store-assets](../store-assets/) — иконка и скриншоты
