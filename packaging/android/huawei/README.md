# Huawei AppGallery

Выкладка Cubatarium в [AppGallery Connect](https://developer.huawei.com/consumer/en/service/josp/agc/index.html) на том же release AAB, что и для Google Play.

## Почему отдельный канал

Huawei-устройства без Google Mobile Services всё равно могут устанавливать приложения из AppGallery. Cubatarium **офлайн** и **не использует GMS/HMS** — отдельная сборка не нужна.

| Аспект | Cubatarium |
|--------|------------|
| Артефакт | `cubatarium-<version>.aab` (тот же signed AAB) |
| Package name | `com.cubatarium` |
| HMS Core | Не требуется |
| Подпись | Тот же upload keystore (`platforms/android/keystore.properties`) |

## 1. Сборка

```powershell
.\build-android-release.ps1
```

См. также [google-play/README.md](../google-play/README.md) (keystore, versionCode).

## 2. Регистрация разработчика

1. [AppGallery Connect](https://developer.huawei.com/consumer/en/service/josp/agc/index.html) → регистрация (бесплатно для разработчиков)
2. Верификация аккаунта (email, документы — по региону)
3. Создать приложение → **Android** → `com.cubatarium`

## 3. Загрузка

1. **App information** → загрузить AAB (или APK при необходимости)
2. Store listing: иконка и скриншоты из [`../store-assets/`](../store-assets/)
3. **Content rating** и **Privacy** — аналог Play: данные не собираются, офлайн-игра
4. **Release management** → внутреннее / закрытое тестирование перед публикацией

## 4. Тестирование

- Добавить тестеров по email в AppGallery Connect
- Тестеры устанавливают через AppGallery (не через adb)

Первая проверка: обычно от нескольких часов до нескольких рабочих дней.

## 5. Обновления

1. Увеличить `versionCode`
2. `.\build-android-release.ps1`
3. Загрузить новый AAB в AppGallery Connect

## Опционально позже

- **HMS SDK** — только если появятся push, карты, IAP через Huawei
- **Китай (материк)** — может потребоваться ICP filing и локальная compliance; для глобального AppGallery вне КНР обычно не нужно

## Чеклист

- [ ] Signed AAB собран
- [ ] AppGallery Connect: приложение создано
- [ ] Store listing, рейтинг, privacy заполнены
- [ ] Тестеры добавлены
