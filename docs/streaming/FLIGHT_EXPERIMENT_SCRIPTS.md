# Experimental flight and analysis scripts

Обновлено: 2026-10-04. Цель каталога — сохранить и объяснить поддерживаемые flight tools и одноразовые анализаторы, которые накопились в рабочем дереве во время streaming/rendering расследований.

## Поддерживаемый поток

- [`tools/flight_sim_run.py`](../../tools/flight_sim_run.py) — единственный общий запускатель. Он настраивает flight-sim, сохраняет perf JSONL/manifest/report, восстанавливает временно изменённые `config.json` и исходный `users.json`, повторно выставляет контрольную позицию перед каждым `--repeat` и вызывает анализатор.
- [`tools/flight_sim_fixed_day.py`](../../tools/flight_sim_fixed_day.py) — контролируемый дневной запуск поверх общего runner. Временно задаёт `time_of_day=0.25`, `time_frozen=true`, clear weather, нулевую облачность и выключенную авто-погоду; в `finally` восстанавливает исходный `world_data.json` побайтно. Аргументы маршрута передаются после `--`. Пример: `python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m379_world164_m335_fixed_day --report bin/suite_reports/engine_refactor/m379_world164_m335_fixed_day.json`. Не менять профиль камеры ради «более красивого» кадра.
- [`tools/flight_sim_analyze.py`](../../tools/flight_sim_analyze.py) — расчёт агрегатов и ворот из JSONL.
- [`tools/analyze_renderer_pixel_trace.py`](../../tools/analyze_renderer_pixel_trace.py) — сводит pixel/ray witnesses, draw-state, pool OOM и screen-ray debt из perf JSONL; запускается как `python tools/analyze_renderer_pixel_trace.py bin/logs/perf_<run>.jsonl`. M380 сохранил 32 768 pixel probes, 191 samples ниже luma 32, 8 192 screen-ray samples и pool/draw witnesses; summary: [M380 trace analysis](../../bin/suite_reports/engine_refactor/m380_renderer_pixel_trace_20261004.json).
- [`tools/flight_sim_suite.py`](../../tools/flight_sim_suite.py) — запуск групп сценариев; [`tools/flight_sim_iterate.py`](../../tools/flight_sim_iterate.py) — последовательные итерации фиксов.
- Диагностические поддерживаемые модули: `flight_sim_baseline.py`, `flight_sim_checkpoint.py`, `flight_sim_diag.py`, `flight_sim_eval.py`, `flight_sim_parity.py`, `flight_sim_phase_gate.py`, `flight_sim_timeline_analyze.py`, `perf_capture.py`, `compare_idle_autofly_manual.py`, `CompareFlightF5.py`, `AnalyzeEnterLit.py`, `AnalyzePhase54Scorecard.py`–`AnalyzePhase57Scorecard.py`, `analyze_stop_hang_dive.py`.
- Основной визуальный маршрут — давно используемый M335: World_164, start `[120,56,56]`, cruise y=70, yaw `180°`, pitch `−30°`, scale 1, no-teleport. Условия камеры, высоту и Z не подбирать заново. M379 достиг checkpoint 8 192 на fixed daylight, но render gates остались FAIL. M380 повторил те же условия после pool/fallback fix: 10 080 блоков, нормальная скорость 5.19287 blocks/s, checkpoint 8 192, `process_rc=0`; render gates всё ещё FAIL (24/39), post-stop convergence не выполнена. Дневной повтор запускается через `flight_sim_fixed_day.py`.
- Collision-control M368 — исторический default `product-174657-far` (`cruise y=56`, pitch 0°, yaw 180°): пользователь видел остановку у дерева около x=−2 832. Этот профиль нужен только для проверки нового obstacle detour, не для оценки цвета/рендеринга. Flight-only detour включён в Release `6d06cef2`; M377/M378 опасности не обнаружили, поэтому обход в этих runs не срабатывал. Watchdog после 8 секунд устойчивой блокировки сохраняет диагностический отчёт.
- Обход уже включён по умолчанию в flight-sim: при угрозе столкновения он проверяет прогнозируемый сегмент, перебирает боковые планы и при успехе возвращается к исходной линии. M380 на известном дереве около x≈−2 832 зарегистрировал hazard `5.25` блока, обход вправо на 3 блока, pass-distance `10.25`, `detours_completed=1`, `plan_failures=0`. Кадр 40 показывает дерево очень близко справа, но в сопоставимых периодах requested/applied movement совпадал, blocked substeps и ground contacts были 0; маршрут продолжился до focus x=−623. Поэтому M380 не фиксирует остановку у дерева, хотя операторский кадр выглядел как столкновение. Если следующий прогон действительно заблокируется, смотреть `obstacle_avoidance.attempts/detours_started/detours_completed/plan_failures`, `collision_stop_triggered` и movement/collision counters; M335 камеру и коридор не менять.
- High-altitude и боковые полосы — только старые stress diagnostics: M369/y96 не достиг far checkpoint и вне eye-level proxy corridor; M371/M372 z224/pitch0 не являются визуальным acceptance. Их отчёты оставлены для истории, параметры не предлагаются как новые условия съемки.
- M371 collision probe: `python tools/flight_sim_run.py --scenario product-174657-far --world World_164 --visible --cruise-eye-y 70 --product-start-position 120 56 224 --fly-phase-sec 700 --stop-after-blocked-sec 8 --report bin/suite_reports/engine_refactor/m371_world164_z224_y70_route_probe_20261004.json`. Маршрут прошёл `3 424` блока, focus `(7,14)→(-207,14)`, collision counters нулевые; process rc0, analyzer gates FAIL. Trace `ready_wait_ms` p50/p95/max `190/1 196/15 016 ms` на 1 633 procedural commits, но focus voxel census был выключен. M372 добавил `CUBA_VISUAL_BLACK_TRACE=1`, `CUBA_WORLD_COLUMN_SOURCE_TRACE=1`, frame captures и полный route/world manifest.
- M372 long trace: `python tools/flight_sim_run.py --scenario product-174657-far --world World_164 --visible --cruise-eye-y 70 --product-start-position 120 56 224 --fly-phase-sec 2400 --seconds 2440 --stop-after-blocked-sec 8 --report bin/suite_reports/engine_refactor/m372_world164_z224_y70_long_20261004.json --process-timeout 3000`; env включал visual/focus/source tracing и capture каждые 15 s. Прошёл `6 960` блоков без collision до far gate `8 192`, `process_rc=0`, analyzer `FAIL`. Census valid в 898/1 135 periods; max `51` camera-band solid slices без drawable и `25` без work owner. Один resident non-air срез имел geom revision requested, но не имел активной стадии/queue/ticket. `ready_wait_ms` p50/p95/max `272/3 696/34 209 ms` на 2 914 procedural commits; `total_ms` p95/max `17 305/62 690 ms`. Кадры почти целиком неба из-за pitch `0°`, поэтому они не являются visual acceptance. Артефакты: [report](../../bin/suite_reports/engine_refactor/m372_world164_z224_y70_long_20261004.json), [perf trace](../../bin/logs/perf_20261004-002601_19304.jsonl), [frames](../../bin/logs/m372_world164_z224_y70).
- M377 repeatable visual-profile run: same M335 start/yaw/pitch/eye-height at Release, no teleport, scale 1. It covered `6 640` blocks in 1 800 s, with no collision or avoidance event; product gates remained FAIL. Report: [M377](../../bin/suite_reports/engine_refactor/m377_world164_canonical_y70_pitchm30_obstacle_avoid_20261004.json).
- M378 long trace repeated those exact route/camera values for 2 400 s. Result: `7 680` blocks, not the `8 192` checkpoint; analyzer `pass=false`, median fly wall `104.25 ms`, stream phase `45.64 ms`, `chunk_not_ready` median `24`, dirty median `462`, stop convergence false. Source log recorded `2 014` disk completions plus `1 508` disk misses/procedural commits. The 32 768 pixel and 8 192 screen-ray traces show strong night-factor correlation for dark sampled surfaces, so use frozen daylight for the next visual run. No collision or avoidance attempt occurred. Artifacts: [report](../../bin/suite_reports/engine_refactor/m378_world164_canonical_y70_pitchm30_trace_20261004.json), [trace](../../bin/logs/perf_20261004-025426_16928.jsonl), [captures](../../bin/logs/m378_world164_canonical_y70_pitchm30), [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-025421.16928).
- M380 repeated the M335 daylight route for 2 800 s. It covered `10 080` blocks and passed checkpoint `8 192`; speed/yaw/pitch checks passed, while renderer/product gates remained FAIL (24/39) and stop convergence failed. Same-coordinate pool evidence: M379 used `256.98/320 MiB` with `213–229` OOM retains; M380 used `89.36–89.58/156.91–157.10 MiB` with zero OOM retains. This fixes the pool-pressure symptom without yet fixing visual readiness or frame time. Among 191 low-luma pixels, all had an opaque MDI pass; 180 depth surfaces matched a source triangle within 0.1 block, but CPU voxel-ray distance matched depth within 0.5 block for only 20. Treat the voxel ray and framebuffer depth as separate witnesses until the DDA/cutout surface mapping is joined for one pixel. The selected far screen-ray candidate `(-550,3,3)` had light debt and an unbuilt mesh; it received a FirstMesh ticket and direct dirty entry but had not been scheduled at the logged instant. Full watched-schedule rows were evicted from the bounded ring by the end of the 47-minute run. Artifacts: [M380 report](../../bin/suite_reports/engine_refactor/m380_world164_m335_poolfix_20261004.json), [pixel summary](../../bin/suite_reports/engine_refactor/m380_renderer_pixel_trace_20261004.json), [perf trace](../../bin/logs/perf_20261004-052750_14968.jsonl), [captured frames](../../bin/logs/m380_world164_m335_poolfix), [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-052746.14968).
- Для сопоставления disk reload и procedural creation задать `CUBA_WORLD_COLUMN_SOURCE_TRACE=1` в окружении процесса; runner сохраняет трассу `WorldColumnSource` вместе с обычными flight-артефактами. Сопоставлять по координатам с ray/pixel и mesh lifecycle; один source event не объясняет цвет.
- Для unload/reload diagnostic можно задать `--fly-phase-sec 300 --reverse-course-after-sec 150`: autopilot разворачивается после 150 секунд того же no-teleport полёта, чтобы вернуться по зоне, которую уже прошёл и мог выгрузить. В `flight_sim_report.json` сохраняются целевой yaw, количество отклонений heading и факт разворота.
- Для содержательной проверки persistence включить `CUBA_WORLD_COLUMN_SOURCE_TRACE=1`. На 2026-10-03 старая async ветка генерировала сохранённые колонки; исправленный круг дал 508 disk completion и 46 disk misses на том же маршруте. Render gates всё ещё требуют отдельного анализа и повторного far run.
- Для исследования cold saved-world entry: `--scenario fz-cold-enter --world World_164 --visible`. После появления phase timing использовать отчёт только как startup/load контроль, а не как far-distance acceptance.
- M370 fresh-seed CLI reference: `python tools/integration_test_worldgen.py --seeds 3650471212 --radius-chunks 5 --skip-determinism --smoke --keep-worlds`. Новый мир `CI_3650471212` создан последовательно на текущем Release; фазы заняли `67.962 s`, включая `generate_columns=23.021 s` и `prepare_view=25.346 s`. CLI принудительно отключает async generation/I/O и использует один worker, поэтому этот сценарий не измеряет интерактивную загрузку или обычный многопоточный режим.

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
