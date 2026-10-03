# Experimental flight and analysis scripts

Обновлено: 2026-10-03. Цель каталога — сохранить и объяснить поддерживаемые flight tools и одноразовые анализаторы, которые накопились в рабочем дереве во время streaming/rendering расследований.

## Поддерживаемый поток

- [`tools/flight_sim_run.py`](../../tools/flight_sim_run.py) — единственный общий запускатель. Он настраивает flight-sim, сохраняет perf JSONL/manifest/report, восстанавливает временно изменённые `config.json` и пользовательские данные и вызывает анализатор.
- [`tools/flight_sim_analyze.py`](../../tools/flight_sim_analyze.py) — расчёт агрегатов и ворот из JSONL.
- [`tools/flight_sim_suite.py`](../../tools/flight_sim_suite.py) — запуск групп сценариев; [`tools/flight_sim_iterate.py`](../../tools/flight_sim_iterate.py) — последовательные итерации фиксов.
- Диагностические поддерживаемые модули: `flight_sim_baseline.py`, `flight_sim_checkpoint.py`, `flight_sim_diag.py`, `flight_sim_eval.py`, `flight_sim_parity.py`, `flight_sim_phase_gate.py`, `flight_sim_timeline_analyze.py`, `perf_capture.py`, `compare_idle_autofly_manual.py`, `CompareFlightF5.py`, `AnalyzeEnterLit.py`, `AnalyzePhase54Scorecard.py`–`AnalyzePhase57Scorecard.py`, `analyze_stop_hang_dive.py`.
- Основной повторяемый маршрут: `python tools/flight_sim_run.py --scenario product-174657-far --world World_164 --visible --report bin/suite_reports/engine_refactor/<run>.json`. Default fly — 1800 секунд при обычной скорости (scale 1), чтобы иметь запас до checkpoint 8192 блока; сценарий не использует teleport. Короткий 300-секундный probe не является дальним acceptance. Перед запуском требуются Release EXE и зафиксированные start/config hashes.
- Для сопоставления disk reload и procedural creation задать `CUBA_WORLD_COLUMN_SOURCE_TRACE=1` в окружении процесса; runner сохраняет трассу `WorldColumnSource` вместе с обычными flight-артефактами. Сопоставлять по координатам с ray/pixel и mesh lifecycle; один source event не объясняет цвет.
- Для unload/reload diagnostic можно задать `--fly-phase-sec 300 --reverse-course-after-sec 150`: autopilot разворачивается после 150 секунд того же no-teleport полёта, чтобы вернуться по зоне, которую уже прошёл и мог выгрузить. В `flight_sim_report.json` сохраняются целевой yaw, количество отклонений heading и факт разворота.
- Для исследования cold saved-world entry: `--scenario fz-cold-enter --world World_164 --visible`. После появления phase timing использовать отчёт только как startup/load контроль, а не как far-distance acceptance.

## Сохранённые одноразовые материалы

Следующие 79 локальных flight/replay/analyzer скриптов ранее не отслеживались Git и теперь сохранены без переименования. Имена и хардкодированные параметры намеренно оставлены неизменными для сопоставления с историческими M-run артефактами. Это исследовательский архив: многие файлы указывают на конкретные старые JSONL/report пути в `bin/`, а значит, не являются самодостаточными переносимыми тестами. Их прошлые результаты и ограничения описаны в [аудите](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md).

### Индекс сохранённых одноразовых сценариев и анализаторов

| Скрипт | Назначение |
|---|---|
| [`bin/research_flight_20260829/scripts/chunk_swap_audit.py`](../../bin/research_flight_20260829/scripts/chunk_swap_audit.py) | Correlates mesh-emerge spikes with capture retarget, chunk-not-ready and churn. |
| [`bin/research_flight_20260829/scripts/fm_enqueue_drain_audit.py`](../../bin/research_flight_20260829/scripts/fm_enqueue_drain_audit.py) | Audits first-mesh enqueue/drain timing from flight traces. |
| [`bin/tmp_a4_analyze.py`](../../bin/tmp_a4_analyze.py) | One-off flight/perf analysis helper. |
| [`bin/tmp_analyze_124859.py`](../../bin/tmp_analyze_124859.py) | One-off analysis: 124859. |
| [`bin/tmp_analyze_154921.py`](../../bin/tmp_analyze_154921.py) | One-off analysis: 154921. |
| [`bin/tmp_analyze_210431.py`](../../bin/tmp_analyze_210431.py) | One-off analysis: 210431. |
| [`bin/tmp_analyze_cheap_cruise_detail.py`](../../bin/tmp_analyze_cheap_cruise_detail.py) | One-off analysis: cheap cruise detail. |
| [`bin/tmp_analyze_cheap_cruise.py`](../../bin/tmp_analyze_cheap_cruise.py) | One-off analysis: cheap cruise. |
| [`bin/tmp_analyze_manual_205340.py`](../../bin/tmp_analyze_manual_205340.py) | One-off analysis: manual 205340. |
| [`bin/tmp_analyze_manual.py`](../../bin/tmp_analyze_manual.py) | One-off analysis: manual. |
| [`bin/tmp_compare_flights.py`](../../bin/tmp_compare_flights.py) | One-off comparison: flights. |
| [`bin/tmp_compare_two_perf.py`](../../bin/tmp_compare_two_perf.py) | One-off comparison: two perf. |
| [`bin/tmp_flight_audit.py`](../../bin/tmp_flight_audit.py) | One-off flight/perf analysis helper. |
| [`bin/tmp_i18_flight_cmp.py`](../../bin/tmp_i18_flight_cmp.py) | One-off flight/perf analysis helper. |
| [`bin/tmp_list_latest_perf.py`](../../bin/tmp_list_latest_perf.py) | Lists candidate performance logs for analysis. |
| [`bin/tmp_list_perf_files.py`](../../bin/tmp_list_perf_files.py) | Lists candidate performance logs for analysis. |
| [`bin/tmp_run_latest_perf.py`](../../bin/tmp_run_latest_perf.py) | One-off launcher for the most recent performance profile. |
| [`bin/tmp_summarize_latest_perf.py`](../../bin/tmp_summarize_latest_perf.py) | Summarizes the newest performance run. |
| [`bin/tmp_underfeet_from_perf.py`](../../bin/tmp_underfeet_from_perf.py) | Extracts under-feet streaming evidence from performance logs. |
| [`tmp_analyze_174657.py`](../../tmp_analyze_174657.py) | One-off analysis: 174657. |
| [`tmp_analyze_v2.py`](../../tmp_analyze_v2.py) | One-off analysis: v2. |
| [`tools/tmp_analyze_m158_trace.py`](../../tools/tmp_analyze_m158_trace.py) | M158 trace/report analysis: trace. |
| [`tools/tmp_analyze_m159_trace.py`](../../tools/tmp_analyze_m159_trace.py) | M159 trace/report analysis: trace. |
| [`tools/tmp_analyze_m161_trace.py`](../../tools/tmp_analyze_m161_trace.py) | M161 trace/report analysis: trace. |
| [`tools/tmp_analyze_m162_audit.py`](../../tools/tmp_analyze_m162_audit.py) | M162 trace/report analysis: audit. |
| [`tools/tmp_analyze_m163_plan_audit.py`](../../tools/tmp_analyze_m163_plan_audit.py) | M163 trace/report analysis: plan audit. |
| [`tools/tmp_analyze_m166_deep.py`](../../tools/tmp_analyze_m166_deep.py) | M166 trace/report analysis: deep. |
| [`tools/tmp_analyze_m166.py`](../../tools/tmp_analyze_m166.py) | One-off analysis: m166. |
| [`tools/tmp_analyze_m167_deep.py`](../../tools/tmp_analyze_m167_deep.py) | M167 trace/report analysis: deep. |
| [`tools/tmp_analyze_m168_deep.py`](../../tools/tmp_analyze_m168_deep.py) | M168 trace/report analysis: deep. |
| [`tools/tmp_analyze_m170_deep.py`](../../tools/tmp_analyze_m170_deep.py) | M170 trace/report analysis: deep. |
| [`tools/tmp_run_m159_live_face_demand_flight.py`](../../tools/tmp_run_m159_live_face_demand_flight.py) | M159 flight/replay: live face demand flight. |
| [`tools/tmp_run_m160_centered_face_light.py`](../../tools/tmp_run_m160_centered_face_light.py) | M160 flight/replay: centered face light. |
| [`tools/tmp_run_m161_opaque_work_owner.py`](../../tools/tmp_run_m161_opaque_work_owner.py) | M161 flight/replay: opaque work owner. |
| [`tools/tmp_run_m162_relight_handoff_audit.py`](../../tools/tmp_run_m162_relight_handoff_audit.py) | M162 flight/replay: relight handoff audit. |
| [`tools/tmp_run_m163_relight_plan_audit.py`](../../tools/tmp_run_m163_relight_plan_audit.py) | M163 flight/replay: relight plan audit. |
| [`tools/tmp_run_m164_stale_remesh_replay.py`](../../tools/tmp_run_m164_stale_remesh_replay.py) | M164 flight/replay: stale remesh replay. |
| [`tools/tmp_run_m165_stale_debt_flight.py`](../../tools/tmp_run_m165_stale_debt_flight.py) | M165 flight/replay: stale debt flight. |
| [`tools/tmp_run_m166_stale_light_cancel_lease.py`](../../tools/tmp_run_m166_stale_light_cancel_lease.py) | M166 flight/replay: stale light cancel lease. |
| [`tools/tmp_run_m167_near_ring_debt.py`](../../tools/tmp_run_m167_near_ring_debt.py) | M167 flight/replay: near ring debt. |
| [`tools/tmp_run_m168_visual_ring_debt.py`](../../tools/tmp_run_m168_visual_ring_debt.py) | M168 flight/replay: visual ring debt. |
| [`tools/tmp_run_m169_outer_ring_fair_share.py`](../../tools/tmp_run_m169_outer_ring_fair_share.py) | M169 flight/replay: outer ring fair share. |
| [`tools/tmp_run_m170_light_repair_priority.py`](../../tools/tmp_run_m170_light_repair_priority.py) | M170 flight/replay: light repair priority. |
| [`tools/tmp_run_m171_light_repair_ownership.py`](../../tools/tmp_run_m171_light_repair_ownership.py) | M171 flight/replay: light repair ownership. |
| [`tools/tmp_run_m172_preview_settlement.py`](../../tools/tmp_run_m172_preview_settlement.py) | M172 flight/replay: preview settlement. |
| [`tools/tmp_run_m173_preview_repair_debt.py`](../../tools/tmp_run_m173_preview_repair_debt.py) | M173 flight/replay: preview repair debt. |
| [`tools/tmp_run_m174_budgeted_repair_debt.py`](../../tools/tmp_run_m174_budgeted_repair_debt.py) | M174 flight/replay: budgeted repair debt. |
| [`tools/tmp_run_m175_local_preview_debt.py`](../../tools/tmp_run_m175_local_preview_debt.py) | M175 flight/replay: local preview debt. |
| [`tools/tmp_run_m176_visible_light_service.py`](../../tools/tmp_run_m176_visible_light_service.py) | M176 flight/replay: visible light service. |
| [`tools/tmp_run_m177_visible_light_service.py`](../../tools/tmp_run_m177_visible_light_service.py) | M177 flight/replay: visible light service. |
| [`tools/tmp_run_m178_stale_revision_debt.py`](../../tools/tmp_run_m178_stale_revision_debt.py) | M178 flight/replay: stale revision debt. |
| [`tools/tmp_run_m179_stale_preview.py`](../../tools/tmp_run_m179_stale_preview.py) | M179 flight/replay: stale preview. |
| [`tools/tmp_run_m180_voxel_ray_witness.py`](../../tools/tmp_run_m180_voxel_ray_witness.py) | M180 flight/replay: voxel ray witness. |
| [`tools/tmp_run_m181_voxel_ray_witness.py`](../../tools/tmp_run_m181_voxel_ray_witness.py) | M181 flight/replay: voxel ray witness. |
| [`tools/tmp_run_m182_pixel_hit_chunk.py`](../../tools/tmp_run_m182_pixel_hit_chunk.py) | M182 flight/replay: pixel hit chunk. |
| [`tools/tmp_run_m183_partial_column_repair.py`](../../tools/tmp_run_m183_partial_column_repair.py) | M183 flight/replay: partial column repair. |
| [`tools/tmp_run_m184_pixel_queue_lifecycle.py`](../../tools/tmp_run_m184_pixel_queue_lifecycle.py) | M184 flight/replay: pixel queue lifecycle. |
| [`tools/tmp_run_m185_nearest_hole_preview.py`](../../tools/tmp_run_m185_nearest_hole_preview.py) | M185 flight/replay: nearest hole preview. |
| [`tools/tmp_run_m186_forward_miss_priority.py`](../../tools/tmp_run_m186_forward_miss_priority.py) | M186 flight/replay: forward miss priority. |
| [`tools/tmp_run_m187_front_admission.py`](../../tools/tmp_run_m187_front_admission.py) | M187 flight/replay: front admission. |
| [`tools/tmp_run_m188_relight_deferred_owner.py`](../../tools/tmp_run_m188_relight_deferred_owner.py) | M188 flight/replay: relight deferred owner. |
| [`tools/tmp_run_m189_exact_hit_owner.py`](../../tools/tmp_run_m189_exact_hit_owner.py) | M189 flight/replay: exact hit owner. |
| [`tools/tmp_run_m190_hit_lifecycle.py`](../../tools/tmp_run_m190_hit_lifecycle.py) | M190 flight/replay: hit lifecycle. |
| [`tools/tmp_run_m191_firstmesh_fair48.py`](../../tools/tmp_run_m191_firstmesh_fair48.py) | M191 flight/replay: firstmesh fair48. |
| [`tools/tmp_run_m192_outer_firstmesh_lane.py`](../../tools/tmp_run_m192_outer_firstmesh_lane.py) | M192 flight/replay: outer firstmesh lane. |
| [`tools/tmp_run_m193_firstmesh_reserve_h5.py`](../../tools/tmp_run_m193_firstmesh_reserve_h5.py) | M193 flight/replay: firstmesh reserve h5. |
| [`tools/tmp_run_m194_justrelit_mesh_boost.py`](../../tools/tmp_run_m194_justrelit_mesh_boost.py) | M194 flight/replay: justrelit mesh boost. |
| [`tools/tmp_run_m195_forward_firstmesh.py`](../../tools/tmp_run_m195_forward_firstmesh.py) | M195 flight/replay: forward firstmesh. |
| [`tools/tmp_run_m196_forward_relight_h7.py`](../../tools/tmp_run_m196_forward_relight_h7.py) | M196 flight/replay: forward relight h7. |
| [`tools/tmp_run_m197_forward_relight_h6_h7.py`](../../tools/tmp_run_m197_forward_relight_h6_h7.py) | M197 flight/replay: forward relight h6 h7. |
| [`tools/tmp_run_m198_forward_debt_candidates.py`](../../tools/tmp_run_m198_forward_debt_candidates.py) | M198 flight/replay: forward debt candidates. |
| [`tools/tmp_run_m199_forward_debt_candidates.py`](../../tools/tmp_run_m199_forward_debt_candidates.py) | M199 flight/replay: forward debt candidates. |
| [`tools/tmp_run_m200_unowned_geometry_retry.py`](../../tools/tmp_run_m200_unowned_geometry_retry.py) | M200 flight/replay: unowned geometry retry. |
| [`tools/tmp_run_m201_throttled_geometry_retry.py`](../../tools/tmp_run_m201_throttled_geometry_retry.py) | M201 flight/replay: throttled geometry retry. |
| [`tools/tmp_run_m202_valid_mdi_packed_fallback.py`](../../tools/tmp_run_m202_valid_mdi_packed_fallback.py) | M202 flight/replay: valid mdi packed fallback. |
| [`tools/tmp_run_m203_packed_draw_path_trace.py`](../../tools/tmp_run_m203_packed_draw_path_trace.py) | M203 flight/replay: packed draw path trace. |
| [`tools/tmp_run_m204_packed_texture_lookup.py`](../../tools/tmp_run_m204_packed_texture_lookup.py) | M204 flight/replay: packed texture lookup. |
| [`tools/tmp_run_m205_block_id_palette.py`](../../tools/tmp_run_m205_block_id_palette.py) | M205 flight/replay: block id palette. |
| [`tools/tmp_run_m258_bounded_forward_preview.py`](../../tools/tmp_run_m258_bounded_forward_preview.py) | M258 flight/replay: bounded forward preview. |
## Повторяемость и ограничения

- Одноразовые M-run wrappers могут устанавливать env flags, редактировать локальную конфигурацию, брать точный `World_164` save-position и писать в именные report paths. Перед ручным запуском прочитать сам файл и сверить сохранение/restoration данных.
- Анализаторы обычно читают уже созданные `bin/logs/perf_*.jsonl` и `bin/suite_reports/**`. Большие логи и отчёты в этот commit не включаются; каталог сохраняет код, но не подменяет отсутствующие входные данные.
- Изменения исходников проверять на контрольном repeatable world; wrappers из архива не считать acceptance gate без актуального manifest и полного отчёта.
- Новые seed/world запускать периодически по [плану рефакторинга](ENGINE_REMEDIATION_PLAN_2026-10-03.md), не заменяя ими повторяемую World_164 базу.
