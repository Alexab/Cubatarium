# OBJ-1: инварианты идентификаторов вокселей объектов

Дата: 2026-11-04.

## Исправленные дефекты

`ResolveObjectVoxelPlacementId` при непустом `Type` теперь возвращает только результат разрешения имени в активном registry. Неизвестное имя даёт `BLOCK_AIR`; stale numeric ID больше не используется как fallback. Для пустого `Type` сохранён legacy numeric режим, но ID принимается только при наличии имени в активном registry.

`UObjectLibrary::RebindBlockIds` при неуспешном разрешении обязательно записывает `BLOCK_AIR`, поэтому ID от предыдущего pack не переживает смену registry. Последующий успешный rebind восстанавливает актуальный ID.

Диагностика unknown type при загрузке и rebind содержит object, type и pack и дедуплицируется через существующий `LoggedUnknownTypes`.

## Регрессионные проверки

В `object_placement_mode_test` добавлены случаи:

- stale numeric + валидный `Type` — выбирается ID по имени;
- unknown `Type` + stale numeric — AIR;
- legacy numeric валидный/невалидный — валидный сохраняется, невалидный даёт AIR;
- failed pack rebind — AIR, повторный успешный rebind — актуальный ID.

Release-результаты:

- `object_placement_mode_test`: PASS;
- `worldgen_populate_parity_probe`: PASS, все 9 baseline cases;
- `worldgen_vegetation_placement_integration_test`: PASS;
- полный target `Cubatarium`: PASS, проверка статических зависимостей Windows PASS.

Массовая регенерация assets не выполнялась: для инвариантов загрузчика она не требуется, новых расхождений generated ID этап не выявил. Существующее предупреждение о missing prefab `cubatarium_worldgen_cc0::asset_mossy_ruin_piece` остаётся отдельным дефектом данных.
