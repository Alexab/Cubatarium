# Проверки текущего аудита

Отчет завершен 22 сентября 2026. База: `dcc02e27201542c1ef95ad87217af9843bd69368`.
Среда: Windows, MSVC, Release; существующие зависимости `build/desktop-msvc/vcpkg_installed/x64-windows-static`.
Production-код не изменялся. Новая проверка собирается изолированно и не добавлена в основной CMake.

## 1. Зарегистрированные CTest

До итогового прогона актуализированы target-бинарники текущего исходника. Первоначальный запуск старых бинарников давал другой результат (publication_audit PASS, sticky_overlay executable отсутствовал); он не используется как итог.

Сборка:

```powershell
cmake --build build/desktop-msvc --config Release --parallel 3 --target publication_audit job_admission_lifetime_test chunk_input_stamp_test inputs_still_valid_visual_test visible_black_attribution_test draw_oracle_gate_test block_catalog_queries_test mesh_inputs_catalog_test normal_shutdown_test frame_deadline_test streamer_deadline_policy_test streamer_amortize_policy_test sticky_overlay_remesh_policy_test greedy_vertex_pool_production_test column_flow_scheduler_test frustum_clip_test column_scheduler_audit_repro streaming_render_ready_invariants_test miss_first_mesh_class_test mesh_publish_contract_test mesh_gpu_store_mdi_test greedy_vertex_pool_lifetime_test capture_token_validation_test cruise_sot_phase_test decor_mesh_integration_test relight_result_install_test mesh_neighbor_policy_test mesh_light_stale_policy_test column_emerge_bump_test frame_streaming_budget_test idle_recovery_policy_test
ctest --test-dir build/desktop-msvc -C Release --output-on-failure -j 3
```

Сборка успешна. Итог повторного CTest: **31 PASS / 1 FAIL из 32**. Driver test использует production test executable и проходит, отдельного одноименного build target не требуется.

FAIL: `publication_audit`:

```text
retained_batch_aliases_new_allocation=0 retained_bytes_overwritten=0
repeated_omission_two_live_allocations_overlap=0
empty_replacement_retains_ghost_and_dirty=0 empty_via_replace_kind=1
live_append_helper_ok=1
reordered_table_keeps_publication_epoch=1
representation_switch_clears_mdi=1
remove_coord_representation_switch_clears_mdi=1
explicit_remove_clears_mdi=1
generation_ledger_rejects_stale_free=1
correctness_violations=1
```

Интерпретация: тест ожидает изменение publication epoch при reorder. Production намеренно сохраняет geometry publication version при membership-only reorder и отдельно инвалидирует compact/обновляет table revision. Это конфликт контракта и regression test; не доказательство повторения исправленных alias/double-free. Нужно определить epochs и проверить всех потребителей, а не молча обновить expected значение.

Остальные зарегистрированные тесты, включая `greedy_vertex_pool_driver_test`, `sticky_overlay_remesh_policy_test`, `mesh_gpu_store_mdi_test`, `capture_token_validation_test`, `relight_result_install_test`, прошли. Прохождение helper tests не доказывает эквивалентность production callers.

## 2. Дополнительные существующие тесты

```powershell
cmake --build build/desktop-msvc --config Release --parallel 2 --target relight_install_planner_test mark_relit_characterization_test fluid_mesh_faces_test fluid_surface_pack_reuse_test
```

Сборка всех четырех targets успешна. Каждый executable запущен отдельно:

| Executable в `build/desktop-msvc/Release` | Exit | Результат |
|---|---:|---|
| `relight_install_planner_test.exe` | 1 | `FAIL: P7: skip remesh when FullyDark light rev matches` |
| `mark_relit_characterization_test.exe` | 0 | PASS, без stdout |
| `fluid_mesh_faces_test.exe` | 0 | `fluid_mesh_faces_test: OK` |
| `fluid_surface_pack_reuse_test.exe` | 0 | `fluid_surface_pack_reuse_test: ok` |

FAIL relight требует решения, должен ли нулевой валидный свет инициировать rebuild. Падение не следует исправлять удалением проверки без ADR. PASS fluid tests не проверяет автоматически world/material identity обнаруженного cache key.

## 3. Новые изолированные контрпримеры

Исходник: [CurrentContractRepro.cpp](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_21/CurrentContractRepro.cpp).

```powershell
cmake -S docs/streaming/audit_2026_09_21 -B build/audit-20260921 -G "Visual Studio 17 2022" -A x64
cmake --build build/audit-20260921 --config Release --parallel 2
& build/audit-20260921/Release/current_contract_repro.exe
```

Первое configure внутри sandbox не определило compiler. Повторный configure с разрешением выполнился успешно; build успешен. Проверка не запускает игру, не загружает пользовательский мир, не создает GL context.

Итог, воспроизведен повторно:

```text
same_world_full_vs_incremental_state_mismatch=1 full=1 incremental=2 stamp_still_valid=1
resident_snapshot_uncredited_after_refresh=1 before=65536 after=0
pending_no_progress_age100_only_notes_stall=1
matching_mixed_material_chunk_rejected_by_pass_hash=1
deadline_allows_reuse_despite_changed_camera_key=1
stable_geometry_skips_sort_even_if_camera_sort_revision_changed=1
violations=6
```

Exit 1 здесь означает воспроизведенные нарушения; exit 2 зарезервирован для ошибки harness.

| Проверка | Уровень доказательства | Еще необходимо |
|---|---|---|
| Full vs incremental | Реальные UBlockWorld, MeshCaptureStore и snapshot; одинаковые данные, разные states при valid stamp | Дифференциальный тест faces/light полного mesher |
| Snapshot credit | Реальный store/admission: retained snapshot остается доступен при освобожденном кредите | Многопоточный lifetime/cancel/eviction coverage |
| Pending age 100 | Реальный production planner вызывает только note stall | End-to-end job lifecycle и no-progress сценарий |
| Material hash | Production hash/helper для full-chunk и per-pass доменов | GL/production writer mixed-pass integration |
| Deadline cull | Полный key не разрешает reuse, deadline predicate разрешает | Command/pixel test при повороте и исчерпанном бюджете |
| Transparent sort | Production skip не получает camera revision | Production reorder и визуальный тест стабильной сцены с движущейся камерой |

Тесты последних двух пунктов доказывают небезопасность разрешающего условия при соответствующих входах call site, а не содержат GPU-рендер кадра. Ни один из шести результатов сам по себе не связывает конкретный пиксель ручного пролета с единственной причиной.

## 4. Метрики и ограничения

```powershell
python -X utf8 docs/streaming/audit_2026_09_21/analyze_current.py --output docs/streaming/audit_2026_09_21/metrics.json
```

Анализатор прочитал 17 perf JSONL от 21 сентября; сохраняет SHA256 входных файлов, counts, duplicate/conflicting keys, фильтры all/moving/corridor, sampled maxima и spike rows. Средние periods не выдаются за индивидуальные кадры. Выводы и ограничения описаны в [основном аудите](E:/Work/Home/Cubatarium/docs/streaming/CURRENT_STATE_AUDIT_2026-09-22.md).

Новые игровые пролеты, запись видео и GPU frame capture не проводились. Автоматического подтверждения отсутствия дыр/подмены текстур нет. Тестирование Android не проводилось. Production-код, существующие планы и пользовательские миры не менялись; коммиты и откаты не выполнялись.
