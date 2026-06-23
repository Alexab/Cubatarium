# Google Play — Closed testing (альфа)

Пошаговая инструкция для выкладки Cubatarium на закрытый тестовый трек Google Play.

## 1. Подготовка release-сборки

### Keystore (upload key)

```powershell
.\scripts\android\setup-release-keystore.ps1
cd platforms\android
copy keystore.properties.example keystore.properties
# Отредактировать keystore.properties: пароли и путь к .jks
```

Сохраните `cubatarium-upload.jks` и пароли в надёжном месте (бэкап). При первой загрузке в Play включите **Play App Signing**.

### Сборка AAB

```powershell
.\build-android-release.ps1
```

Артефакт: `platforms/android/app/build/outputs/bundle/release/cubatarium-<version>.aab`

`versionCode` берётся из `git rev-list --count HEAD` (или из `platforms/android/version.properties`).

### Store assets

- **Иконка 512×512:** [`../store-assets/icon-512.png`](../store-assets/icon-512.png) (перегенерация: `scripts/branding/generate-app-icons.ps1`)
- Минимум 2 скриншота (landscape, с эмулятора или устройства) → `packaging/android/store-assets/screenshots/`
- Опционально: feature graphic 1024×500

---

## 2. Аккаунт Google Play Console ($25)

1. Откройте [play.google.com/console](https://play.google.com/console)
2. Создайте **Personal** аккаунт разработчика
3. Оплатите **$25** (разово)
4. Пройдите **верификацию личности** (документ)
5. Установите приложение **Play Console** на Android и пройдите **верификацию устройства**

---

## 3. Создание приложения

1. **Create app** → Cubatarium
2. Тип: **Game**, **Free**
3. Declarations: политики Google Play, export compliance (обычно «No custom encryption» для офлайн-игры)

---

## 4. Обязательные формы (Dashboard)

| Раздел | Значение для Cubatarium |
|--------|-------------------------|
| **Data safety** | No data collected (офлайн, нет INTERNET permission) |
| **Content rating** | IARC-анкета → скорее Everyone / 3+ |
| **Target audience** | Указать возраст; не отмечать «привлекает детей», если не целитесь на детей |
| **Store listing** | Название, описания, иконка 512×512, скриншоты |
| **Privacy policy** | При отсутствии сбора данных часто достаточно Data safety; при желании — простая страница на GitHub Pages |

---

## 5. Closed testing (альфа)

1. **Release → Testing → Closed testing** → создать трек (например «Alpha»)
2. **Create new release** → загрузить `cubatarium-*.aab`
3. Release notes → **Review release** → **Start rollout**
4. **Testers** → добавить email-список или Google Group
5. Раздать тестерам opt-in ссылку: `https://play.google.com/apps/testing/com.cubatarium`

Тестеры открывают ссылку на устройстве, принимают приглашение и устанавливают через Play Store.

Первая проверка Google: от нескольких часов до 1–2 дней.

---

## 6. Обновления альфы

1. Убедиться, что `versionCode` вырос (новый коммит в git или bump в `version.properties`)
2. `.\build-android-release.ps1`
3. Closed testing → New release → загрузить AAB → rollout

---

## 7. Production (не сейчас)

Для **personal** аккаунта после ноября 2023 перед публичным релизом нужен closed test: **12 тестеров × 14 дней подряд**. Для альфы на Closed testing это **не требуется**.

---

## Чеклист перед первой загрузкой

- [ ] `keystore.properties` и бэкап `.jks`
- [ ] `bundleRelease` успешен, AAB подписан upload-ключом
- [ ] `versionCode` > 0 и уникален для Play
- [ ] Иконка и скриншоты в Console
- [ ] Data safety, Content rating, Target audience заполнены
- [ ] Тестеры добавлены, opt-in ссылка разослана
