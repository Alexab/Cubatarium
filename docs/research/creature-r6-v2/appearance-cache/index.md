# Appearance/cache: текущая проверка

Последний контекст: [mapping_final_catalog](context_mapping_final_catalog.json),
Release EXE4e76f9220ab4fea5cb40e5fd101aff6ac607f3377b990291187487a44e821968.

| Evidence | Результат | Границы |
|---|---|---|
| [Material audit](texture_mapping_audit.json) | native297, GPU RGBA891 | Synthetic fixtures, одна Walk pose; не art/FP/save/reload |
| [Skin ownership](skin_ownership_audit.json) | 560/560 | Пять bundled skins, raw/resolved-only selection |
| [Live cache](resource_cache_audit.json) | 161/161 | Mesh locator/invalidation и один R pack entry |
| [Pixel parity](mapping_body_parity.json) | 5290/5290 | Все61 native previews; прежние sampled art observations наследуются |
| [Ledger](ledger.json) | 61/61 | Source/evidence/payload integrity; не финальная art приёмка |

Фактические runtime routes17B/35G/3R/1sprite. Declared R4 включает
fire_spirit; его billboard сохраняет исходный тип изображения.

Прежние original/current листы моделей и motion observations:
[bone-motion index](../bone-motion/index.md). Текущий capture дал те же RGBA,
поэтому новые дубли composite sheets не создавались. Экипировка:
[equipment fit](../equipment-fit/index.md).

Прямо изученный кадр этого прохода — world wolf после диагностических
texture loads/readbacks/cleanup. Он не проверяет весь шаг, склон или маршрут.

![World wolf](runtime_mapping_final_catalog/frame_002.png)

[Visual review с SHA](visual_review.json), [полные правила и ограничения](README.md).
Полная R6 остаётся pending: resource recovery/texture-root-overlay reload,
полный user/FP/save flow, contacts/transitions/routes/habitats/culling,
clean package и matched performance.


Актуальное продолжение loader/root/pack recovery и process restart: [resource-reload](../resource-reload/index.md).
Исторические contexts выше сохранены; текущий EXE и remaining gates — в новом README.
