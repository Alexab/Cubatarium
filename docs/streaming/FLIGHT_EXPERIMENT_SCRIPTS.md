# Experimental flight and analysis scripts

Обновлено: 2026-10-08. Цель каталога — сохранить и объяснить поддерживаемые flight tools и одноразовые анализаторы, которые накопились в рабочем дереве во время streaming/rendering расследований.

## Поддерживаемый поток

- [`tools/flight_sim_run.py`](../../tools/flight_sim_run.py) — единственный общий запускатель. Он настраивает flight-sim, сохраняет perf JSONL/manifest/report, восстанавливает временно изменённые `config.json` и исходный `users.json`, повторно выставляет контрольную позицию перед каждым `--repeat` и вызывает анализатор.
- [`tools/flight_sim_fixed_day.py`](../../tools/flight_sim_fixed_day.py) — контролируемый дневной запуск поверх общего runner. Временно задаёт `time_of_day=0.25`, `time_frozen=true`, clear weather, нулевую облачность и выключенную авто-погоду; в `finally` восстанавливает исходный `world_data.json` побайтно. Аргументы маршрута передаются после `--`. Пример: `python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m379_world164_m335_fixed_day --report bin/suite_reports/engine_refactor/m379_world164_m335_fixed_day.json`. Не менять профиль камеры ради «более красивого» кадра.
- [`tools/flight_sim_analyze.py`](../../tools/flight_sim_analyze.py) — расчёт агрегатов и ворот из JSONL.
- [`tools/analyze_renderer_pixel_trace.py`](../../tools/analyze_renderer_pixel_trace.py) — сводит pixel/ray witnesses, draw-state, pool OOM, screen-ray debt и для dark samples сохраняет отдельные DDA и framebuffer depth-hit evidence, включая mesh revisions, MDI draw state, dirty-queue ownership/age, work-owner flags и light/demand state; запускается как `python tools/analyze_renderer_pixel_trace.py bin/logs/perf_<run>.jsonl --threshold 96 --json-out bin/suite_reports/engine_refactor/<run>_renderer_pixel_trace.json`. M380 сохранил 32 768 pixel probes, 191 samples ниже luma 32; M388 сохранил 32 768 dense probes и связал sampled dark pixels с drawable MDI surface и low/provisional light: [M388 pixel/light analysis](../../bin/suite_reports/engine_refactor/m388_renderer_pixel_trace_20261004.json).
- [`tools/compare_renderer_pixel_routes.py`](../../tools/compare_renderer_pixel_routes.py) — потоково сравнивает два или больше perf JSONL по одинаковым диапазонам camera X и пространственным бинам; считает несколько luma порогов, валидный opaque depth, stale published geometry, unsettled light demand и block IDs тёмных поверхностей. Нулевые sentinel probe IDs исключаются. Пример для M400/M401: `python tools/compare_renderer_pixel_routes.py --label M400 --perf-jsonl bin/logs/perf_20261005-084624_28876.jsonl --label M401 --perf-jsonl bin/logs/perf_20261005-103140_37252.jsonl --min-x -8192 --max-x -1024 --bin-width 512 --json-out bin/suite_reports/engine_refactor/m400_m401_pixel_x_bins_20261005.json`. Низкая яркость не считается пустым чанком без DDA/depth corroboration.
- [`tools/analyze_world_column_source_trace.py`](../../tools/analyze_world_column_source_trace.py) — сводит `WorldColumnSource` INFO events отдельно для disk и procedural lifecycle: cancellations, completion, worker/read/apply latency, unmatched coordinates, top slow requests и раздельные `scheduler_queue_ms` / `worker_pool_queue_ms`. Использовать все INFO log parts одного PID. Для повторяемого маршрута добавлять `--focus-z 3 --z-radius 5`: summary содержит procedural latency только для этого горизонтального коридора, чтобы дальние фоновые coordinates не искажали вывод о видимых чанках. Пример исторического M392: `python tools/analyze_world_column_source_trace.py bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-205456.34968 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-211013.34968 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-212859.34968 --json-out bin/suite_reports/engine_refactor/m392_world_column_source_trace_20261004.json`. Queued без completion нельзя считать живым владельцем без явного cancel event.
- M389 проверил, что scheduler drain активен на прежнем M335: 9 168 блоков, median `5.18555 blocks/s`, multi-commit до 3 колонок/кадр с budget `12 ms`; renderer gates остаются FAIL. Dense trace: 115/32 768 dark probes (M388: 165), но большинство имели settled light и видимую MDI-геометрию. Повторный disk-load тест не состоялся: `(-522,0,3)` снова дал `disk_miss`, а flight harness отключил autosave. Артефакты: [M389 report](../../bin/suite_reports/engine_refactor/m389_world164_m335_persisted_drain_20261004.json), [pixel/light analysis](../../bin/suite_reports/engine_refactor/m389_renderer_pixel_trace_20261004.json), [perf trace](../../bin/logs/perf_20261004-165636_31704.jsonl), [GUI frames](../../bin/logs/m389_world164_m335_persisted_drain).
- Для приглушённых, не почти чёрных поверхностей те же traces пересчитаны offline с `--threshold 64` и `--threshold 96`. При `<96` M388/M389 дали `1 112/1 032` samples; provisional light `219/177`, unsettled demand `257/281`. M389 source-face IDs: `tree_leaves` 504, `sand` 244, `grass` 96, `tree_log` 77. Это сочетает природно тёмные материалы с видимой provisional/unsettled геометрией; не является pixel-area или строгим A/B gate. Подробнее: [M389/M388 luma follow-up](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md#M389-follow-up-wider-luma-thresholds-capture-dim-surfaces).
- Уточнение по M389/M390: 271/281 unsettled и 144/177 provisional samples M389 `<96` имеют RGB-change от transparent pass; конкретный pending sample — sand на y=46 под sea level. В M390 тёмно-синие полигоны совпали с глобальным fog color `(13,38,89)`. Тот же M335 использует RD4, fog start≈17, full blend≈36 блоков; поэтому затуманенный дальний рельеф не считать автоматически чёрным чанком. Сохранять fog/color/depth/light joins в pixel trace.
- M390 — точный M335, видимый Release/no-teleport, плюс relight-owner audit и dense pixel trace; 8 560 блоков, нормальная скорость `5.18555`, collision counters нулевые. Renderer acceptance FAIL: dirty max `1 313`, visible-black max `21`, void-near max `1 228`, unlit max `41`. Source trace зафиксировал очередь генерации далеко позади камеры. Темно-синие полигоны — точный fog color, но остаются настоящие focus-mesh/void долги. Первая правка отменяет procedural generation за пределами retention radius. [План и детали](ENGINE_REMEDIATION_PLAN_2026-10-03.md#M390-completed--M335-exposes-stale-generation-work-and-distant-visual-debt).
- M391 repeated the identical M335 profile and reached `8 800` blocks at `5.18555 blocks/s`, with no collision or ground contact. Renderer acceptance still failed. Among `2 209` unique disk requests, `1 779` completed; the other `430` had no completion event and were 188–379 chunks behind final focus. Worker queue and file read p95 were under 2 ms, while per-column result wait was hundreds to thousands of seconds. Commit `47b9ab04` now cancels disk owners outside the existing retention radius and prunes canceled ready results. GUI frame 167 also shows unresolved blue triangles across the shoreline; this remains a separate visual symptom. See [M391 report](../../bin/suite_reports/engine_refactor/m391_world164_m335_cancel_stale_loads_20261004.json), [M391 pixel trace](../../bin/suite_reports/engine_refactor/m391_renderer_pixel_trace_l96_20261004.json), [plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md#M391-completed--disk-load-owners-also-outlive-the-moving-retention-ring), and [audit](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md#M391-pending-disk-loads-become-stale-owners-outside-the-retention-ring).
- M392 completed on the unchanged visible Release M335. It covered `9 968` blocks at `5.19287 blocks/s`, no teleport, no collision/detour, and restored world files byte-for-byte. Analyzer acceptance remains FAIL: Red pressure throughout, median fly wall `91.99 ms`, dirty median/max `835/1 484`, and failed post-stop recovery. Disk cancellation now accounts for all `2 292` queued coordinates (`1 859 complete`, `433 cancelled`), but completed results still waited median `2.51 s`, p95 `100.84 s` with ready queue p95 `180`/max `458` slices. Of 32 768 pixel probes, `<32` luma was `129`, `<64` `658`, `<96` `1 076`; darkest pixels all hit drawable opaque MDI, `91/129` had geometry revision newer than published, and none of the `<32` pixels changed in transparent composition. One near x `−7 114` is provisional-light `sky=0`. Saved artifacts: [flight report](../../bin/suite_reports/engine_refactor/m392_world164_m335_cancel_stale_disk_20261004.json), [pixel trace](../../bin/suite_reports/engine_refactor/m392_renderer_pixel_trace_l96_20261004.json), [source trace](../../bin/suite_reports/engine_refactor/m392_world_column_source_trace_20261004.json), [perf trace](../../bin/logs/perf_20261004-205500_34968.jsonl), [GUI frames](../../bin/logs/m392_world164_m335_cancel_stale_loads). Repeat the established M335 command unchanged: `$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBATARIUM_RELIGHT_AUDIT='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m392_world164_m335_cancel_stale_loads'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m392_world164_m335_cancel_stale_disk --report bin/suite_reports/engine_refactor/m392_world164_m335_cancel_stale_disk_20261004.json --process-timeout 3000`.
- M393 validated `e186c63c` on the identical visible Release/no-teleport M335 route: `9 312` blocks, median `5.18555 blocks/s`, zero blocked substeps/ground contacts, world restored byte-for-byte. Disk-result p95 wait improved `100.84→48.21 s`, but renderer gates still failed and median fly wall worsened `91.99→110.93 ms`. Dense 32 768 probes had `<32/<64/<96` luma counts `268/1 025/1 590` (M392 `129/658/1 076`); most dark probes hit drawable opaque MDI, with stale published geometry and preview-light witnesses. The saved frame near x `−7 000` shows a dark, sharply bounded patch; this is a visual symptom, not proof of absent world data. The app returned 0; the runner exited 1 because stop-lines failed. Artifacts: [M393 flight report](../../bin/suite_reports/engine_refactor/m393_world164_m335_reserved_disk_apply_20261004.json), [pixel trace](../../bin/suite_reports/engine_refactor/m393_renderer_pixel_trace_l96_20261004.json), [source trace](../../bin/suite_reports/engine_refactor/m393_world_column_source_trace_20261004.json), [perf trace](../../bin/logs/perf_20261004-220021_37424.jsonl), [GUI frame](../../bin/logs/m393_world164_m335_reserved_disk_apply/frame_128.png). Reproduction keeps the established command and changes only phase/report/capture names: `$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBATARIUM_RELIGHT_AUDIT='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m393_world164_m335_reserved_disk_apply'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m393_world164_m335_reserved_disk_apply --report bin/suite_reports/engine_refactor/m393_world164_m335_reserved_disk_apply_20261004.json --process-timeout 3000`.
- M394 (5 Oct) used the same visible Release/no-teleport M335, with no camera, route, speed, altitude, time, or weather changes. It reached `9 232` blocks at `5.18555 blocks/s`; predicted obstacle avoidance completed `3/3` detours with no plan failure, replan, blocked stop, or ground contact. The app returned 0; the runner exited 1 because only `14/39` acceptance gates passed. Dense probes were `<32/<64/<96` luma `269/1 069/1 673`; most dark probes hit drawable opaque MDI, while `956/1 673` had published geometry behind the CPU mesh revision. The Red load cap experiment did not reduce procedural request-to-worker p95 (`29.03 s`) or disk result-wait p95 (`55.51 s`); do not treat it as a rendering fix. Artifacts: [analysis report](../../bin/suite_reports/engine_refactor/m394_world164_m335_red_generation_cap_20261004.json), [flight/obstacle report](../../bin/suite_reports/engine_refactor/m394_flight_sim_20261005.json), [pixel trace](../../bin/suite_reports/engine_refactor/m394_renderer_pixel_trace_l96_20261005.json), [source trace](../../bin/suite_reports/engine_refactor/m394_world_column_source_trace_20261005.json), [perf trace](../../bin/logs/perf_20261004-231440_30796.jsonl).
- M395 kept M335's exact camera, weather, speed, and no-teleport settings. The app exited 0 at `5.19287 blocks/s` median, but the avoidance waypoint overshot: focus moved `(7,3)→(-424,129)` and camera Z `56→2065`. Avoidance reported 5 started / 4 completed detours and 1 replan; `MoveAside`/`ReturnToRoute` can hold a fixed side key after missing a `0.45`-block tolerance. Treat its `22/39` renderer result as off-route diagnostic, not M335 acceptance. Artifacts: [run report](../../bin/suite_reports/engine_refactor/m395_world164_m335_generation_queue_split_20261005.json), [flight control](../../bin/suite_reports/engine_refactor/m395_flight_control_report_20261005.json), [pixel trace](../../bin/suite_reports/engine_refactor/m395_renderer_pixel_trace_l96_20261005.json), [z=3 source](../../bin/suite_reports/engine_refactor/m395_world_column_source_z3_20261005.json), [z=129 source](../../bin/suite_reports/engine_refactor/m395_world_column_source_z129_20261005.json), [perf trace](../../bin/logs/perf_20261005-003751_40828.jsonl), [GUI frames](../../bin/logs/m395_world164_m335_generation_queue_split).
- [`tools/flight_sim_suite.py`](../../tools/flight_sim_suite.py) — запуск групп сценариев; [`tools/flight_sim_iterate.py`](../../tools/flight_sim_iterate.py) — последовательные итерации фиксов.
- Диагностические поддерживаемые модули: `flight_sim_baseline.py`, `flight_sim_checkpoint.py`, `flight_sim_diag.py`, `flight_sim_eval.py`, `flight_sim_parity.py`, `flight_sim_phase_gate.py`, `flight_sim_timeline_analyze.py`, `perf_capture.py`, `compare_idle_autofly_manual.py`, `CompareFlightF5.py`, `AnalyzeEnterLit.py`, `AnalyzePhase54Scorecard.py`–`AnalyzePhase57Scorecard.py`, `analyze_stop_hang_dive.py`.
- Основной визуальный маршрут — давно используемый M335: World_164, start `[120,56,56]`, cruise y=70, yaw `180°`, pitch `−30°`, scale 1, no-teleport. Условия камеры, высоту и Z не подбирать заново. M379 достиг checkpoint 8 192 на fixed daylight, но render gates остались FAIL. M380 повторил те же условия после pool/fallback fix: 10 080 блоков, нормальная скорость 5.19287 blocks/s, checkpoint 8 192, `process_rc=0`; render gates всё ещё FAIL (24/39), post-stop convergence не выполнена. Дневной повтор запускается через `flight_sim_fixed_day.py`.
- Collision-control M368 — исторический default `product-174657-far` (`cruise y=56`, pitch 0°, yaw 180°): пользователь видел остановку у дерева около x=−2 832. Этот профиль нужен только для проверки нового obstacle detour, не для оценки цвета/рендеринга. Flight-only detour включён в Release `6d06cef2`; M377/M378 опасности не обнаружили, поэтому обход в этих runs не срабатывал. Watchdog после 8 секунд устойчивой блокировки сохраняет диагностический отчёт.
- Обход уже включён по умолчанию в flight-sim: при угрозе столкновения он проверяет прогнозируемый сегмент, перебирает боковые планы и при успехе возвращается к исходной линии. M380 на известном дереве около x≈−2 832 зарегистрировал hazard `5.25` блока, обход вправо на 3 блока, pass-distance `10.25`, `detours_completed=1`, `plan_failures=0`. Кадр 40 показывает дерево очень близко справа, но в сопоставимых периодах requested/applied movement совпадал, blocked substeps и ground contacts были 0; маршрут продолжился до focus x=−623. Поэтому M380 не фиксирует остановку у дерева, хотя операторский кадр выглядел как столкновение. Если следующий прогон действительно заблокируется, смотреть `obstacle_avoidance.attempts/detours_started/detours_completed/plan_failures`, `collision_stop_triggered` и movement/collision counters; M335 камеру и коридор не менять.
- Обновление после наблюдения оператора: сообщалось о видимом касании дерева и остановке. M389 report записывает `attempts=0` и `collision_stop_triggered=false`, но пока нет надёжной привязки этого наблюдения к M389 process. При следующем M335 повторе сопоставлять screenshot, координаты камеры, blocked substeps/ground contacts и `obstacle_avoidance` из того же run; менять маршрутные условия не требуется. Если возникает реальная блокировка, harness должен делать доступные bounded обходы и перепланирование прежде watchdog-stop.
- High-altitude и боковые полосы — только старые stress diagnostics: M369/y96 не достиг far checkpoint и вне eye-level proxy corridor; M371/M372 z224/pitch0 не являются визуальным acceptance. Их отчёты оставлены для истории, параметры не предлагаются как новые условия съемки.
- M371 collision probe: `python tools/flight_sim_run.py --scenario product-174657-far --world World_164 --visible --cruise-eye-y 70 --product-start-position 120 56 224 --fly-phase-sec 700 --stop-after-blocked-sec 8 --report bin/suite_reports/engine_refactor/m371_world164_z224_y70_route_probe_20261004.json`. Маршрут прошёл `3 424` блока, focus `(7,14)→(-207,14)`, collision counters нулевые; process rc0, analyzer gates FAIL. Trace `ready_wait_ms` p50/p95/max `190/1 196/15 016 ms` на 1 633 procedural commits, но focus voxel census был выключен. M372 добавил `CUBA_VISUAL_BLACK_TRACE=1`, `CUBA_WORLD_COLUMN_SOURCE_TRACE=1`, frame captures и полный route/world manifest.
- M372 long trace: `python tools/flight_sim_run.py --scenario product-174657-far --world World_164 --visible --cruise-eye-y 70 --product-start-position 120 56 224 --fly-phase-sec 2400 --seconds 2440 --stop-after-blocked-sec 8 --report bin/suite_reports/engine_refactor/m372_world164_z224_y70_long_20261004.json --process-timeout 3000`; env включал visual/focus/source tracing и capture каждые 15 s. Прошёл `6 960` блоков без collision до far gate `8 192`, `process_rc=0`, analyzer `FAIL`. Census valid в 898/1 135 periods; max `51` camera-band solid slices без drawable и `25` без work owner. Один resident non-air срез имел geom revision requested, но не имел активной стадии/queue/ticket. `ready_wait_ms` p50/p95/max `272/3 696/34 209 ms` на 2 914 procedural commits; `total_ms` p95/max `17 305/62 690 ms`. Кадры почти целиком неба из-за pitch `0°`, поэтому они не являются visual acceptance. Артефакты: [report](../../bin/suite_reports/engine_refactor/m372_world164_z224_y70_long_20261004.json), [perf trace](../../bin/logs/perf_20261004-002601_19304.jsonl), [frames](../../bin/logs/m372_world164_z224_y70).
- M377 repeatable visual-profile run: same M335 start/yaw/pitch/eye-height at Release, no teleport, scale 1. It covered `6 640` blocks in 1 800 s, with no collision or avoidance event; product gates remained FAIL. Report: [M377](../../bin/suite_reports/engine_refactor/m377_world164_canonical_y70_pitchm30_obstacle_avoid_20261004.json).
- M378 long trace repeated those exact route/camera values for 2 400 s. Result: `7 680` blocks, not the `8 192` checkpoint; analyzer `pass=false`, median fly wall `104.25 ms`, stream phase `45.64 ms`, `chunk_not_ready` median `24`, dirty median `462`, stop convergence false. Source log recorded `2 014` disk completions plus `1 508` disk misses/procedural commits. The 32 768 pixel and 8 192 screen-ray traces show strong night-factor correlation for dark sampled surfaces, so use frozen daylight for the next visual run. No collision or avoidance attempt occurred. Artifacts: [report](../../bin/suite_reports/engine_refactor/m378_world164_canonical_y70_pitchm30_trace_20261004.json), [trace](../../bin/logs/perf_20261004-025426_16928.jsonl), [captures](../../bin/logs/m378_world164_canonical_y70_pitchm30), [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-025421.16928).
- M380 repeated the M335 daylight route for 2 800 s. It covered `10 080` blocks and passed checkpoint `8 192`; speed/yaw/pitch checks passed, while renderer/product gates remained FAIL (24/39) and stop convergence failed. Same-coordinate pool evidence: M379 used `256.98/320 MiB` with `213–229` OOM retains; M380 used `89.36–89.58/156.91–157.10 MiB` with zero OOM retains. This fixes the pool-pressure symptom without yet fixing visual readiness or frame time. Among 191 low-luma pixels, all had an opaque MDI pass; 180 depth surfaces matched a source triangle within 0.1 block, but CPU voxel-ray distance matched depth within 0.5 block for only 20. Treat the voxel ray and framebuffer depth as separate witnesses until the DDA/cutout surface mapping is joined for one pixel. The selected far screen-ray candidate `(-550,3,3)` had light debt and an unbuilt mesh; it received a FirstMesh ticket and direct dirty entry but had not been scheduled at the logged instant. Full watched-schedule rows were evicted from the bounded ring by the end of the 47-minute run. Artifacts: [M380 report](../../bin/suite_reports/engine_refactor/m380_world164_m335_poolfix_20261004.json), [pixel summary](../../bin/suite_reports/engine_refactor/m380_renderer_pixel_trace_20261004.json), [perf trace](../../bin/logs/perf_20261004-052750_14968.jsonl), [captured frames](../../bin/logs/m380_world164_m335_poolfix), [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-052746.14968).
- M380 source-trace follow-up: the far-frontier column `(-550,0,3)` was a disk miss and procedural commit, not a failed reload. It generated in about `82 ms`, but renderer-gate samples then showed a drawable `(-550,3,3)` slice using provisional light until its field-light and published revisions caught up about 83 route blocks later. This is a concrete candidate for the user's “dim chunk” observation. Across the route, generation queues had a long tail (queue p95/max `28.69/62.90 s`; ready-result wait p95/max `7.30/32.00 s`) despite fast per-result generation. Low-luma pixel probes were mostly depth-visible `tree_leaves` surfaces with sky light 1, so that pixel sample does not establish broad terrain darkness. The plan now prioritizes exact screen-ray relight admission from the existing bounded visible FIFO and retains M335 unchanged; follow-up details are in the [audit](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md) and [remediation plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md).
- M381 repeated the same daylight M335 parameters on `World_164`, Release, visible GUI and no teleport. It traversed `646` chunks without stopping. The run found one obstacle and completed one right-side bypass. Renderer gates still failed (`22/39`), with median frame wall near `91 ms`, ready batch up to `73`, and every procedural commit capped at one per frame. The run also captured screen-ray relight admissions and pixel witnesses; sparse dark pixels were valid draw-ready surfaces dominated by foliage, so this did not explain the larger muted patches. See the [M381 analyzer report](../../bin/suite_reports/engine_refactor/m381_world164_m335_visible_relight_20261004.json), [pixel analysis](../../bin/suite_reports/engine_refactor/m381_renderer_pixel_trace_20261004.json), [flight frames](../../bin/logs/m381_world164_m335_visible_relight), [perf trace](../../bin/logs/perf_20261004-072621_4616.jsonl), and [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-072616.4616).
- M381 exact repeat command: `$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m381_world164_m335_visible_relight'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m381_world164_m335_visible_relight --report bin/suite_reports/engine_refactor/m381_world164_m335_visible_relight_20261004.json --process-timeout 3000`. This command records the established M335 setup; keep its world, start, camera angles, speed scale and daylight behavior fixed for direct comparisons.
- M382 repeated the M335 route with async source-stage tracing. It completed the planned phase at focus x=−608 (615 chunks, 31 fewer than M381), with median wall frame 104.2 ms and analyzer FAIL. Disk reads were fast (p95 1.21 ms), while completed-result wait reached p95 70.37 s and the ready slice queue reached 1,710; newest applied disk columns lagged around x=−256. Procedure request queue and ready-result wait also dominated over generation. M382's report records zero obstacle probes and no collision stop, while the operator reported seeing a stop at a tree; classify this discrepancy only after correlating final GUI frames with movement telemetry. See the [M382 analyzer report](../../bin/suite_reports/engine_refactor/m382_world164_m335_io_stage_trace_20261004.json), [pixel analysis](../../bin/suite_reports/engine_refactor/m382_renderer_pixel_trace_20261004.json), [flight report](../../bin/flight_sim_report.json), [perf trace](../../bin/logs/perf_20261004-084924_6576.jsonl), and [GUI frames](../../bin/logs/m382_world164_m335_io_stage_trace).
- M382 exact repeat command: `$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m382_world164_m335_io_stage_trace'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m382_world164_m335_io_stage_trace --report bin/suite_reports/engine_refactor/m382_world164_m335_io_stage_trace_20261004.json --process-timeout 3000`.
- M383 verified the bounded near-focus disk completion drain on the exact M335 conditions. Keep route, camera, speed, daylight, start, and capture conditions fixed; next investigate procedural queue service and the multi-frame mesh-debt lifecycle. Do not tune filming conditions to improve metrics.
- Для сопоставления disk reload и procedural creation задать `CUBA_WORLD_COLUMN_SOURCE_TRACE=1` в окружении процесса; runner сохраняет трассу `WorldColumnSource` вместе с обычными flight-артефактами. Сопоставлять по координатам с ray/pixel и mesh lifecycle; один source event не объясняет цвет.
- M383 used the same visible Release/no-teleport M335: `World_164`, `[120,56,56]`, eye y `70`, yaw `180°`, pitch `−30°`, scale `1`, daylight, 2 800 s fly + 20 s settle. Exact command: `$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m383_world164_m335_focus_io'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m383_world164_m335_focus_io --report bin/suite_reports/engine_refactor/m383_world164_m335_focus_io_20261004.json --process-timeout 3000`. Release commit `5b185435`, clean manifest, exit `0`, checkpoint 8 192 crossed, focus x `7→−588`. Disk result-wait p95 improved `70.37→26.05 s` over M382 and applied disk frontier advanced `−256→−335`, but max wait worsened `676.6→706.3 s`; ready backlog and renderer gates remain unresolved. The user reported seeing a tree collision/stop; the analyzer does not contain collision fields and the available summary does not confirm blocked movement, so correlate movement counters with final GUI frames on the next exact repeat. The reactive detour fallback is present but M383 did not establish that it was exercised. See the [M383 analyzer report](../../bin/suite_reports/engine_refactor/m383_world164_m335_focus_io_20261004.json), [pixel analysis](../../bin/suite_reports/engine_refactor/m383_renderer_pixel_trace_20261004.json), [perf trace](../../bin/logs/perf_20261004-100630_31840.jsonl), [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-100625.31840), [frames](../../bin/logs/m383_world164_m335_focus_io), [audit](ENGINE_RENDERING_REFACTOR_AUDIT_2026-09-24.md), and [plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md).
- M384 repeated M335 after the scheduler's bidirectional priority update. It crossed the far gate at focus x `−607`, but renderer acceptance failed (`97.96%` unfinished visual, Dirty median/max `715/1346`, wall median `106.47 ms`, near-void `1553`, visible-black `46`). Procedural source p95 queue/generation/ready-wait/apply were `35.96 s / 180.5 ms / 9.06 s / 9.79 ms`. All 2 826 commits logged the same priority at generation start and commit; code review found the refresh callback short-circuited for pending columns, so M384 does not establish the patch's effect. It did verify one successful right detour (hazard `2.25`, offset `3`, pass `7.25`), with zero plan failures, blocked substeps, or ground contacts. Exact command: `$env:CUBA_VISUAL_BLACK_TRACE='1'; $env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'; $env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m384_world164_m335_priority_refresh'; python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m384_world164_m335_priority_refresh --report bin/suite_reports/engine_refactor/m384_world164_m335_priority_refresh_20261004.json --process-timeout 3000`. See [report](../../bin/suite_reports/engine_refactor/m384_world164_m335_priority_refresh_20261004.json), [flight report](../../bin/flight_sim_report.json), [perf trace](../../bin/logs/perf_20261004-112008_7628.jsonl), [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-112004.7628), and [frames](../../bin/logs/m384_world164_m335_priority_refresh).
- M385 is the post-fix repeat for the queued priority refresh. It used the identical visible Release/no-teleport M335 conditions and completed normally: focus `7→−591`, 9 568 blocks, 8 192 checkpoint crossed, no collision stop, no detour needed. The renderer gates remain failed. Actual near-focus holes had median `0` and max `1`; `unfinished_visual` is a broader loaded-column/no-mesh census and is not a pixel-hole rate. The exact command and the request, ready-result, and apply backlog measurements are recorded in the [plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md). Artifacts: [M385 report](../../bin/suite_reports/engine_refactor/m385_world164_m335_priority_refresh_live_20261004.json), [perf trace](../../bin/logs/perf_20261004-122227_40432.jsonl), [source log](../../bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261004-122223.40432), [GUI frames](../../bin/logs/m385_world164_m335_priority_refresh_live).
- M386 kept the same M335 settings and crossed 9 920 blocks without a collision stop. The ready drain did not activate: all 2 814 procedural commits still logged a one-commit cap and zero apply budget because readiness was sampled before the scheduler drained its completion queue. A follow-up uses total generation backlog as the fast-flight signal and caps actual draining at 3 commits / 12 ms target. Pixel/ray traces recorded unloaded-chunk witnesses on some sampled rays, but sky samples and rays beyond the nearest surface prevent attributing the visible gaps from that alone. Details and artifacts: [plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md), [M386 report](../../bin/suite_reports/engine_refactor/m386_world164_m335_apply_budget_20261004.json), [trace](../../bin/logs/perf_20261004-134106_19096.jsonl), [frames](../../bin/logs/m386_world164_m335_apply_budget).
- M387 retained M335 and completed 10 128 blocks at median speed 5.193 blocks/s; no collision or bypass event occurred. Its backlog-count guard also failed: 2 051/2 946 commits had batches >1, but every event kept a one-commit limit and zero apply budget. The final correction decides from the actual ready-vector size inside scheduler `Tick`. Pixel traces had only four scanlines and missed the dark polygons visible in frames 148/167; M388 should enable existing dense-pixel tracing without changing route/camera/daylight. [Plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md), [M387 report](../../bin/suite_reports/engine_refactor/m387_world164_m335_backlog_drain_20261004.json), [perf trace](../../bin/logs/perf_20261004-144425_7548.jsonl), [frames](../../bin/logs/m387_world164_m335_backlog_drain).
- M388 used the same M335 at median 5.18555 blocks/s and crossed 9 104 blocks; no collision occurred. Dense 8×20 pixel probes found drawn opaque MDI geometry at all 165 dark samples, usually with sky-light 0/1; 38 samples showed provisional preview and 47 unsettled demand. One matching far column `(−522,0,3)` missed disk and was generated before the dark surface appeared. The drain still did not activate because this world's boost threshold is 6.0, above M335 speed; M389 switches its eligibility to the existing 1.5 prefetch movement threshold and checks the now-persisted corridor. [Plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md), [M388 report](../../bin/suite_reports/engine_refactor/m388_world164_m335_scheduler_drain_20261004.json), [pixel/light analysis](../../bin/suite_reports/engine_refactor/m388_renderer_pixel_trace_20261004.json), [perf trace](../../bin/logs/perf_20261004-154622_31036.jsonl), [frames](../../bin/logs/m388_world164_m335_scheduler_drain).
- Obstacle bypass remains enabled in the flight harness. The latest code also discards and replans a waypoint path after a fully blocked movement substep or flight-ground contact, because streamed collision geometry may appear after the initial path check. The flight report records `obstacle_avoidance.detour_replans`. This changes harness recovery only; keep the M335 course, speed and camera unchanged while checking it.
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

### M396: полный M335 с включённым обходом препятствий

Для M396 использовался `tools/flight_sim_fixed_day.py`, который временно задаёт
фиксированный ясный день и восстанавливает `world_data.json` побайтно. Камера и
условия остались прежними: World_164, `[120,56,56]`, eye `70`, yaw `180°`, pitch
`−30°`, no teleport, speed scale `1`, 2 800 s полёта и 20 s остановки. Штатный
predictive obstacle avoidance включён; в этом запуске он выполнил два детура и
вернулся на исходную линию.

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m396_world164_m335_detour_closed_loop'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m396_world164_m335_detour_closed_loop --report bin/suite_reports/engine_refactor/m396_world164_m335_detour_closed_loop_20261005.json --process-timeout 3000
```

Flight-sim control summary: `bin/suite_reports/engine_refactor/m396_flight_control_report_20261005.json`.
Renderer/source compact reports are listed in the remediation plan. The app
returned `process_rc=0`, but renderer acceptance failed; treat M396 as the latest
same-line diagnostic baseline, not as a passing fix.

### M397: unchanged M335 plus FirstMesh frontier diagnostics

M397 used the same World_164 M335 camera, world, lighting, speed, and route duration as previous repeatable runs. It changed no capture conditions. The GUI flight completed at approximately `5.19 blocks/s`, reached x `−568` / `9 200` blocks, and did not stop on collision. Release build manifest is in the run report.

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m397_world164_m335_firstmesh_frontier'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m397_world164_m335_firstmesh_frontier --report bin/suite_reports/engine_refactor/m397_world164_m335_firstmesh_frontier_20261005.json --process-timeout 3000
```

Report: [renderer run summary](../../bin/suite_reports/engine_refactor/m397_world164_m335_firstmesh_frontier_20261005.json); [frontier trace](../../bin/suite_reports/engine_refactor/m397_firstmesh_frontier_trace_20261005.json); [pixel/depth join](../../bin/suite_reports/engine_refactor/m397_renderer_pixel_trace_20261005.json); [source-stage trace](../../bin/suite_reports/engine_refactor/m397_world_column_source_z3_20261005.json). The 512-entry frontier ring did not cover the last 330 scheduler frames, so its 63 exact scheduler joins diagnose only those samples. Strict-dark pixel probes hit rendered opaque geometry; they are not empty-chunk counts. Disk file reads were fast while disk result-wait and procedural scheduler-queue tails were long, a separate producer-stage follow-up.

### M398: FirstMesh reserve и видимые drawable remesh отказы по budget

M398 сохранял точные World_164/M335 параметры из предыдущих запусков. До запуска Release executable собирался на `bc32466b`; запуск завершился без collision block/detour. Первый mesh experiment снизил matched FirstMesh age `median/p95/max` с `75/259.45/279` до `11/33.65/40`, но renderer acceptance и stop convergence остались failed.

Для анализа нужны compact artifacts из `bin/suite_reports/engine_refactor/`:

- `m398_firstmesh_frontier_trace_20261005.json` сравнивает одинаковую focus-X полосу двух запусков.
- `m398_renderer_pixel_trace_20261005.json` — summary без миллионов байт per-pixel samples; полные `*_full_tmp_*` остаются локальными и не коммитятся.
- `m398_mesh_schedule_trace_20261005.json` фиксирует watched drawable priority-remesh tickets, которые остались Dirty-only после отказа общего tick budget.
- `m398_world_column_source_z3_20261005.json` разделяет file-read, result-wait, procedural scheduler, worker, generation и apply.

Pixel probes с luminance `<32` имели видимый opaque MDI depth surface, но у большинства stale samples CPU geometry rev опережала опубликованную и Dirty оставался единственным owner. Лума `<96` не является чёрным-chunk gate. В next iteration разрешён только один extra over-budget slot для aged near-focus screen-ray priority-remesh при наличии pipeline и snapshot capacity. Общий schedule cap и M335 camera/flight conditions остаются прежними.

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m398_world164_m335_aged_firstmesh_reserve'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m398_world164_m335_aged_firstmesh_reserve --report bin/suite_reports/engine_refactor/m398_world164_m335_aged_firstmesh_reserve_20261005.json --process-timeout 3000
```

Control values: median speed `5.186 blocks/s`, focus X `7…−563`, 9 120 blocks. GUI captures and 605 MB raw perf log are local; world file was restored byte-for-byte. See the linked compact run, pixel, schedule, frontier, and source reports above.

### M399: unchanged M335 with drawable ScreenRayRemesh reserve

M399 использовал тот же World_164, start `[120,56,56]`, eye `70`, yaw `180°`, pitch `−30°`, fixed clear day, no teleport, scale `1`, 2 800 s flight + 20 s stop, видимый GUI и predictive obstacle avoidance. Release source commit — `a624dc5f`. Фактическая скорость около `5.19 blocks/s`, focus X `7…−583`; collision block и detour не потребовались. Run завершился с `process_rc=0`, но acceptance failed (`23/39`), stop convergence false.

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m399_world164_m335_screenray_remesh_reserve'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m399_world164_m335_screenray_remesh_reserve --report bin/suite_reports/engine_refactor/m399_world164_m335_screenray_remesh_reserve_20261005.json --process-timeout 3000
```

Компактные артефакты: [run](../../bin/suite_reports/engine_refactor/m399_world164_m335_screenray_remesh_reserve_20261005.json), [pixel/depth](../../bin/suite_reports/engine_refactor/m399_renderer_pixel_trace_20261005.json), [ScreenRayRepair](../../bin/suite_reports/engine_refactor/m399_screen_ray_repair_trace_20261005.json), [scheduler](../../bin/suite_reports/engine_refactor/m399_mesh_schedule_trace_20261005.json), [source stage](../../bin/suite_reports/engine_refactor/m399_world_column_source_z3_20261005.json). Full pixel arrays, raw JSONL and captures are local; do not include them in the commit.

### M400: same M335 with age-free exact ScreenRay reserve

M400 сохранён как точное повторение M335 на Release commit `471e2aa2`. Профиль не менялся. Процесс завершился с `process_rc=0`, acceptance false, и восстановил world data byte-for-byte. Предиктивный obstacle avoidance был включён; `camera_move_blocked_substeps=0`. Фактический endpoint — focus X `−501` / `8 128` блоков, поэтому far-distance gate не пройден; на этом run не делайте вывод о поведении за `−501`.

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m400_world164_m335_fresh_screenray_reserve'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m400_world164_m335_fresh_screenray_reserve --report bin/suite_reports/engine_refactor/m400_world164_m335_fresh_screenray_reserve_20261005.json --process-timeout 3000
```

Run [summary](../../bin/suite_reports/engine_refactor/m400_world164_m335_fresh_screenray_reserve_20261005.json); source logs: `Cubatarium.exe*.INFO.*.28876`, perf log: `perf_20261005-084624_28876.jsonl`. Compact derivatives: [pixel/depth](../../bin/suite_reports/engine_refactor/m400_renderer_pixel_trace_20261005.json), [matched route pixels vs M399](../../bin/suite_reports/engine_refactor/m399_m400_matched_route_pixel_comparison_20261005.json), [screen-ray repair](../../bin/suite_reports/engine_refactor/m400_screen_ray_repair_trace_20261005.json), [schedule flags](../../bin/suite_reports/engine_refactor/m400_mesh_schedule_trace_20261005.json), [source-stage timing](../../bin/suite_reports/engine_refactor/m400_world_column_source_z3_20261005.json). Full pixel arrays, raw logs and GUI captures remain local.

### M401: exact M335 after disk-slice apply fast path

M401 stayed on the same World_164 route, camera, speed, weather, no-teleport
mode, and 2 800 s + 20 s timing. It reached 8 352 blocks / focus X `−515`
with zero blocked movement substeps. This is the first full M335 repeat after
`ApplyToChunk`; renderer acceptance still failed. The optimization reduced
matched-corridor stream/apply timings modestly but did not clear the dark-pixel
or source-result backlog. Full comparison and artifacts are recorded in the
[remediation plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m401--same-m335-after-disk-slice-apply-fast-path-2026-10-05).

The shared-corridor analyzer command and M401/M400 results are also recorded in
the plan's spatial-pixel section. These luma rates are image witnesses; they do
not count empty chunks.

### M402: exact M335, obstacle hold available, visible run became invalid and hung

M402 was built Release on commit `5d8a091f` and used the established M335
settings unchanged. It ran to focus X `−435` / player X about `−6949`, with no
blocked movement or ground contact at the final complete period. This was a
partial streaming run and not renderer acceptance: the application framebuffer
had a non-positive dimension for about seven minutes, scheduled PNG capture failed during that
interval, and the final capture is black. The app later stopped responding and
had to be force-terminated. The fixed-day wrapper restored `world_data.json`
byte-for-byte. Preserve its streaming/frontier telemetry as partial diagnostics;
do not treat it as a passing visible flight or as evidence that collision
stopped the route.

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m402_world164_m335_safe_detour_hold'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m402_world164_m335_safe_detour_hold --report bin/suite_reports/engine_refactor/m402_world164_m335_safe_detour_hold_20261005.json --process-timeout 3000
```

The wrapper's generic `run_outcome=crash` is a consequence of the manual
process termination. The last JSONL record is truncated and fails automatic
adequacy parsing. The exact framebuffer failure, frontier state and
postmortem are documented in the [remediation plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m402--partial-m335-flight-zero-sized-framebuffer-then-hung-app-2026-10-05).
The complete local capture and log paths are linked there.

### M403: exact M335 source/unload baseline

M403 is the same fixed-day visible M335 route and camera settings. Only source
tracing was enabled; dense pixel capture was off. The process stayed responsive
and exited 0 after 553 traveled chunks. It recorded 20,648 resident chunk
slices, a 113.036 ms median frame, and no `WorldColumnSave` lifecycle events.
The analyzer failed, so this is a failure baseline rather than acceptance.

```powershell
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m403_world164_m335_column_save_trace --report bin/suite_reports/engine_refactor/m403_world164_m335_column_save_trace_20261005.json --process-timeout 3000
```

Reports: [flight gates and perf](../../bin/suite_reports/engine_refactor/m403_m335_source_unload_baseline_20261005.json),
[source lifecycle](../../bin/suite_reports/engine_refactor/m403_world_column_source_trace_20261005.json),
[source X bins versus M400–M402](../../bin/suite_reports/engine_refactor/m400_m401_m402_m403_world_column_sources_x_20261005.json).
The corresponding raw perf and INFO logs remain in `bin/logs`.

### M404: bounded-unload retry with pixel/source capture

M404 kept the M335 world, camera, day, and no-teleport route unchanged. It ran
the full 2,800-second flight and 20-second stop on Release commit `e9519c36`,
with runtime unload mode 4. The analyzer failed at 163.0 ms median wall time
(6.13 FPS), 4,240 resident chunk slices, and persistent unfinished-visual debt.
Across rotated INFO logs it queued 17,365 saves for 1,189 unique columns and
recorded 70,343 slice writes. A few perf records had nonzero unload counters,
with 25 removals across seven period summaries; spike/blink rows overlap these
summaries. The resident set still grew. See the
[M404 findings and follow-up](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m404--bounded-unload-failed-repeated-save-before-veto-2026-10-05).

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m404_world164_m335_bounded_unload'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m404_world164_m335_bounded_unload --report bin/suite_reports/engine_refactor/m404_world164_m335_bounded_unload_20261005.json --process-timeout 3000
```

The wrapper restored `World_164/world_data.json` byte-for-byte, but the flight
legitimately persisted distant terrain columns. M405 used the same route and
capture profile after the eviction-order follow-up was built. It must be read
as a mixed persisted/procedural run, not a cold-generation repeat. The M404
perf file is `perf_20261005-143028_11792.jsonl`; the INFO log rotated to
`Cubatarium.exe*.INFO.*.11792`; image captures are in the directory above.

### M405: bounded-save verification, mixed source run, and active-work veto

M405 ran the established visible no-teleport M335 route on Release commit
`927dafb1`, with runtime unload mode 4. It completed normally and produced
1,373 periods; median wall time was 114.751 ms. Save logging recorded 1,827
unique queued columns and 7,352 successful slice writes with no duplicate
queued columns. Source logging recorded 2,591 disk completions, 2,608 disk
misses, and 2,590 procedural commits. However, 1,241 of 1,346 unload candidate
rows were vetoed by active visual work; resident chunks ended at 13,779 slices.
See the [M405 audit](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m405--save-storm-reduced-pending-work-still-pins-resident-columns-2026-10-05).

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m405_world164_m335_unload_veto_budget'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m405_world164_m335_unload_veto_budget --report bin/suite_reports/engine_refactor/m405_world164_m335_unload_veto_budget_20261005.json --process-timeout 3000
```

The fixed-day wrapper restored `World_164/world_data.json` to its baseline
SHA-256 `0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`;
the saved terrain slice files are intentional output. M405 perf data is
`perf_20261005-154651_38480.jsonl`, rotated INFO logs are
`Cubatarium.exe*.INFO.*.38480`, and captures are in
`bin/logs/m405_world164_m335_unload_veto_budget`.

M406 should repeat the exact route and capture profile after the out-of-keep
work-cancellation change is built. Compare candidate/veto/unload and
active-work-invalidated counters, resident-set peak/end, and visual debt; do
not select new flight parameters to improve the capture.

### M406: out-of-keep cancellation verification and freeze capture

M406 ran successfully on Release commit `2f249a5a` with the same route as
M405. It recorded 289 unload candidates, zero vetoes, 201 active-work
invalidations, 1,173 removed slices, 79.082 ms median wall time, and a 458
ready-load queue peak. It also captured 1.093 s and 1.552 s frame spikes; the
second had 1.265 s of wall time outside existing phase attribution. The
analyzer still failed visual gates. See the
[M406 analysis and M407 plan](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m406--residency-recovered-streaming-latency-and-visual-debt-remain-2026-10-05).

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m406_world164_m335_out_of_keep_cancel'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m406_world164_m335_out_of_keep_cancel --report bin/suite_reports/engine_refactor/m406_world164_m335_out_of_keep_cancel_20261005.json --process-timeout 3000
```

The report records `run_outcome=success`, `process_rc=0`, and
`hang_killed=false`. At the user's report, Windows still considered the
window responsive and captures/metrics advanced, although frame spikes were
severe. `Responding=False` appeared only near the planned stop/exit phase; the
app then exited normally. The fixed-day wrapper restored
`bin/worlds/World_164/world_data.json` to SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`. Perf
data is `bin/logs/perf_20261005-172424_2536.jsonl`, rotated INFO logs use PID
2536, and captures are in the directory above.

### Compare source mix and queue delay by route position

Use this after M335 runs to distinguish stored column results from procedural
disk misses at the same chunk-X coordinates. Reuse rotated INFO logs under one
run label. `M402` is partial and must be interpreted with its visible-window
failure documented above.

```powershell
python tools/compare_world_column_sources_by_x.py `
  --run M400=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-084619.28876 `
  --run M400=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-090253.28876 `
  --run M400=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-093314.28876 `
  --run M401=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-103135.37252 `
  --run M401=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-104836.37252 `
  --run M401=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-111747.37252 `
  --run M402=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-113816.36848 `
  --run M402=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-115345.36848 `
  --run M403=bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-131803.9428 `
  --min-chunk-x -560 --max-chunk-x 8 --bin-chunks 16 `
  --json-out bin/suite_reports/engine_refactor/m400_m401_m402_m403_world_column_sources_x_20261005.json
```

The run output is in the [M400–M403 source-map report](../../bin/suite_reports/engine_refactor/m400_m401_m402_m403_world_column_sources_x_20261005.json).
The script reports file reads, ready-result waits, scheduler queue, worker wait,
generation and apply independently. `procedural/disk_miss` means no disk source
was found for that request; it does not prove whether the column was never
saved, remained resident, or had a pending/failed save. The X bins aggregate Z,
so use the exact `(cx,cz)` pairs in each bin. With the updated Release binary,
`CUBA_WORLD_COLUMN_SOURCE_TRACE=1` also records async save queue results,
per-slice write completions/failures, the target folder and fast-shutdown
pending-I/O counts. The report joins those save events by `(cx,cz)`. Set the
environment variable in the same shell that starts the fixed M335 runner.

### M407b: full-route async-decode replay and post-run probes

M407's 34-minute timing anomaly was later confirmed by the user as Windows
sleep or lock. M407b repeated the exact M335 route without changing speed, camera,
world, or duration. A temporary inline keep-awake helper prevented another
sleep. Its reusable counterpart is now `tools/flight_sim_keep_awake.ps1`;
start it hidden before a long visible flight:

```powershell
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
```

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m407b_world164_m335_async_decode_budget_wake_guard --report bin/suite_reports/engine_refactor/m407b_world164_m335_async_decode_budget_wake_guard_20261005.json --process-timeout 3000
```

The original M407b launch did not set `CUBA_FLIGHT_CAPTURE_DIR`. The
renderer reads that setting during startup, so capture could not be enabled
mid-flight. After normal shutdown, recover the pixel ring and column source
events with:

```powershell
py tools/analyze_renderer_pixel_trace.py bin/logs/perf_20261005-193758_42096.jsonl --json-out bin/suite_reports/engine_refactor/m407b_renderer_pixel_trace_20261005.json
py tools/analyze_world_column_source_trace.py bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-193754.42096 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-195018.42096 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-200408.42096 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-201733.42096 --json-out bin/suite_reports/engine_refactor/m407b_source_trace_20261005.json --focus-z 3 --z-radius 5
```

The pixel trace is a sparse framebuffer sample with world/draw/light joins,
not a substitute for full PNG review. Set `CUBA_FLIGHT_CAPTURE_DIR` before
launch on subsequent runs. M407b generated new far-terrain slice files; the
next M335 replay therefore has a different disk/procedural mix even though
its route, camera and movement speed remain unchanged.

### M408: ranked result batching with full-frame and source traces

M408 used the same visible/no-teleport M335 route, Release commit `273c796f`,
and confirmed `flight_move_speed_scale=1`. The run completed the full route and
saved 189 PNG captures. Start keep-awake hidden and set all trace/capture
variables before the wrapper launches the app:

```powershell
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m408_world164_m335_batch_ranked_disk_results'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m408_world164_m335_batch_ranked_disk_results --report bin/suite_reports/engine_refactor/m408_world164_m335_batch_ranked_disk_results_20261005.json --process-timeout 3000
```

After normal shutdown, analyze pixel rings at both the dark threshold and the
broader dim-pixel threshold, plus all rotated INFO logs for the app PID:

```powershell
py tools/analyze_renderer_pixel_trace.py bin/logs/perf_20261005-210152_32396.jsonl --json-out bin/suite_reports/engine_refactor/m408_renderer_pixel_trace_20261005.json
py tools/analyze_renderer_pixel_trace.py bin/logs/perf_20261005-210152_32396.jsonl --threshold 96 --json-out bin/suite_reports/engine_refactor/m408_renderer_pixel_trace_l96_20261005.json
py tools/analyze_world_column_source_trace.py bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-210148.32396 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-211326.32396 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-212617.32396 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-214102.32396 --focus-z 3 --z-radius 5 --json-out bin/suite_reports/engine_refactor/m408_source_trace_20261005.json
```

M408 improved median flight wall time to 56.11 ms and streaming phase to
47.33 ms, but retained a 27-item median missing-resident/readiness debt and
failed zero-debt stop convergence (25 remained). Pixel probes had valid MDI
surfaces even for all `<32` luminance samples. See the [plan's M408 analysis](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m408-results--faster-streaming-phase-visual-debt-remains-2026-10-05).

### M408 deep-trace extraction and M409 compact screen-ray trace

Re-summarize the M408 visual traces with the saved streaming analyzer:

```powershell
py tools/analyze_visual_coverage_trace.py bin/logs/perf_20261005-210152_32396.jsonl --json-out bin/suite_reports/engine_refactor/m408_visual_coverage_trace_20261005.json
```

M408 screen-ray rows show that the selector repaired known resident hits; it
does not classify unloaded rays as expected terrain. The
`draw_oracle_missing_resident_n` field is copied from `unfinished_visual`, so
it is not a second draw oracle. Camera-band peak rows are exact loaded
coordinates, but M408 did not record a same-frame pixel join for its nine
ownerless samples. See the [deep-trace analysis](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m408-deep-trace-review--readiness-is-not-draw-evidence-2026-10-05).

M409 changes trace serialization only; flight conditions stay on M335. The
screen-ray record is reduced to its sampled pixel, hit coordinate, state,
ownership/debt flags, and selection outcome. Run the existing analyzer after
shutdown to confirm the records remain parseable and the output size is
bounded. Keep the GUI visible and capture frames:

```powershell
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m409_world164_m335_compact_screen_rays'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m409_world164_m335_compact_screen_rays --report bin/suite_reports/engine_refactor/m409_world164_m335_compact_screen_rays_20261005.json --process-timeout 3000
```

After shutdown, run `tools/analyze_visual_coverage_trace.py` on the new perf
JSONL and compare file size, screen-ray counts, route distance/speed, saved
images, and renderer traces with M408. Do not interpret a smaller perf file as
a rendering fix; this iteration validates the evidence pipeline before
choosing the owning render or streaming stage.

M409 completed normally at 13,472 blocks and 5.19653 blocks/s; `process_rc=0`
and no forced kill. The user confirmed the earlier M407 long pause was caused
by system sleep/lock. M409 did not pass acceptance (`27/39` gates). It recorded
7,467 disk completions, 225 procedural commits, a 292-result ready-queue high
water, and a one-time 286.18 ms cold directory-index scan on the first disk
request. Disk read p95 was 7.02 ms; procedural worker-pool wait was negligible
while request-to-scheduler-start reached 255.89 ms p95. Pixel probes below
luma 32 all had valid depth and visible MDI surfaces. Treat these as separate
open items: entry hitch, aging in result/admission queues, and dim but drawable
surfaces.

The compact screen-ray serializer reduced M409's raw perf log to 403.67 MiB
from M408's 490.55 MiB. Dense pixel probes still account for 193.63 MiB, so
record telemetry volume when comparing future frame timings. Full M409
analysis commands (suppress the analyzers' redundant stdout JSON):

```powershell
py tools/analyze_renderer_pixel_trace.py bin/logs/perf_20261005-224948_35096.jsonl --json-out bin/suite_reports/engine_refactor/m409_renderer_pixel_trace_20261005.json > $null
py tools/analyze_renderer_pixel_trace.py bin/logs/perf_20261005-224948_35096.jsonl --threshold 96 --json-out bin/suite_reports/engine_refactor/m409_renderer_pixel_trace_l96_20261005.json > $null
py tools/analyze_visual_coverage_trace.py bin/logs/perf_20261005-224948_35096.jsonl --json-out bin/suite_reports/engine_refactor/m409_visual_coverage_trace_20261005.json
py tools/analyze_world_column_source_trace.py bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-224944.35096 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-230113.35096 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-231339.35096 bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-232827.35096 --focus-z 3 --z-radius 5 --json-out bin/suite_reports/engine_refactor/m409_source_trace_20261005.json
```

The cold scan's p95 was only 0.0288 ms, but its 286.18 ms maximum came from
the first request at `(7,0,3)` while the directory index was built. M410
moved that work to asynchronous world-folder warmup; first-discovery latency
fell to 0.0141 ms. Keep the exact M335 capture conditions and keep the
new-world cold-generation run periodic and secondary.

### M410: verify asynchronous disk-index warmup and trace dark surfaces

Build the Release executable, then run the unchanged M335 route. The hidden
keep-awake helper prevents a system sleep from being misread as an engine
stall. `--visible` keeps the GUI available for operator review. The route
wrapper restores `World_164` metadata after the run.

```powershell
cmake --build bin --config Release --target Cubatarium --parallel 8
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m410_world164_m335_async_disk_index'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m410_world164_m335_async_disk_index --report bin/suite_reports/engine_refactor/m410_world164_m335_async_disk_index_20261005.json --process-timeout 3000
```

After shutdown, analyze the framebuffer/depth probes, camera-band ownership,
and disk/procedural source events:

```powershell
py tools/analyze_renderer_pixel_trace.py bin/logs/perf_20261005-235830_42324.jsonl --json-out bin/suite_reports/engine_refactor/m410_renderer_pixel_trace_20261005.json > $null
py tools/analyze_renderer_pixel_trace.py bin/logs/perf_20261005-235830_42324.jsonl --threshold 96 --json-out bin/suite_reports/engine_refactor/m410_renderer_pixel_trace_l96_20261005.json > $null
py tools/analyze_visual_coverage_trace.py bin/logs/perf_20261005-235830_42324.jsonl --json-out bin/suite_reports/engine_refactor/m410_visual_coverage_trace_20261005.json
py tools/analyze_world_column_source_trace.py bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261005-235826.42324 --focus-z 3 --z-radius 5 --json-out bin/suite_reports/engine_refactor/m410_source_trace_20261005.json
```

M410 completed normally at 13,488 blocks and 5.19653 blocks/s, with 189
captures, `process_rc=0`, and no forced kill. First disk discovery was
0.0141 ms versus M409's 286.18 ms cold scan; all 7,578 calls remained under
1 ms. Release build succeeded. Acceptance still failed 12/39 gates: median
unfinished/not-ready debt was 27, and stop ended with 26 not-ready items and
140 focus-dirty chunks. Disk reads were fast (0.98 ms median / 1.32 ms p95),
while completed-result wait remained 627 ms median / 12.76 s p95 / 62.18 s
maximum with ready-load high-water 376. These queue ages require lifecycle
ownership tracing before a quota or ranking change.

All 261 sampled pixels below luma 32 had visible depth and MDI geometry; 229
source-face joins mapped to `tree_leaves`. Do not infer missing terrain or
bad light from low luma alone. The per-run report and captures are in
`bin/suite_reports/engine_refactor/m410_world164_m335_async_disk_index_20261005.json`
and `bin/logs/m410_world164_m335_async_disk_index/` respectively.

### M411: low-trace M335 performance control

Run the same visible, no-teleport M335 settings with only ordinary flight
metrics and image captures. Do not set `CUBA_VISUAL_BLACK_TRACE` or
`CUBA_WORLD_COLUMN_SOURCE_TRACE` for this control. Keep the machine awake so
sleep/lock time cannot be mistaken for an engine stall:

```powershell
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m411_world164_m335_low_trace'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m411_world164_m335_low_trace --report bin/suite_reports/engine_refactor/m411_world164_m335_low_trace_20261006.json --process-timeout 3000
```

M411 exited normally (`process_rc=0`, `hang_killed=false`), reached 864
chunks/13,824 blocks, and saved 189 images. The wrapper restored the original
world metadata SHA256. The perf JSONL is 50.25 MB; median wall / streaming /
mesh-emerge times were 52.94/44.33/17.99 ms. This is about 4 ms faster at the
median than traced M410, but it is not a clean A/B because the disk chunk mix
and file cache changed. The full report still fails 12/39 gates with median
27 unfinished/not-ready items and failed stop convergence. Representative
captures: `frame_000.png`, `frame_094.png`, `frame_188.png`.

### M412: sparse pixels synchronized to camera-band peaks

This replays the same M335 route. `CUBA_VISUAL_BLACK_TRACE=1` enables
diagnostics; dense pixels and source-column tracing stay disabled. The
diagnostic code synchronizes one sparse pixel/depth probe to a camera-band
peak, in addition to its regular low-rate samples.

```powershell
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m412_world164_m335_peak_sync'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m412_world164_m335_peak_sync --report bin/suite_reports/engine_refactor/m412_world164_m335_peak_sync_20261006.json --process-timeout 3000
```

Analyze peak rows against exact-frame voxel rays, depth surfaces, and frustum
rows. The analyzer also summarizes all 80 pixels in each matching 4-by-20
probe frame; an unmatched sparse sample remains inconclusive:

```powershell
py tools/analyze_camera_band_pixel_join.py bin/logs/perf_20261006-022416_22260.jsonl --json-out bin/suite_reports/engine_refactor/m412_camera_band_pixel_join_20261006.json > $null
```

M412 completed 861 chunks, returned `process_rc=0`, was not force-killed, and
saved 189 images. It passed 28/39 acceptance gates and still had 27 median
unfinished items plus failed stop convergence. The two peak frames each had
59 depth samples and no sampled target-chunk surface. Neither emitted a
frustum trace row, so add an explicit frustum sample summary on a future
forensic flight before treating absent candidate rows as zero coverage. The
raw log was 412.60 MB; use M411 low-trace runs for frame-time comparisons.

### M413–M415: per-frame frustum summaries and peak membership

Use the established visible M335 command on `World_164`; M413–M415 changed
only the phase, report, and capture names. Keep the machine awake. M415's
targeted run settings were:

```powershell
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m415_world164_m335_peak_frustum_membership'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m415_world164_m335_peak_frustum_membership --report bin/suite_reports/engine_refactor/m415_world164_m335_peak_frustum_membership_20261006.json --process-timeout 3000
```

Join the retained peak coordinates with exact-frame pixel and geometric
frustum witnesses:

```powershell
py tools/analyze_camera_band_pixel_join.py bin/logs/perf_20261006-054508_8136.jsonl --json-out bin/suite_reports/engine_refactor/m415_camera_band_pixel_join_20261006.json > $null
```

M413's summary was initially written into the generic ring and mostly
overwritten; `6287d297` gave summaries their own ring, validated by M414 (499
summary epochs, 43 sampled-candidate frames). M415 then found that all 18
no-drawable and all 8 unowned peak chunks intersected the exact frustum, while
none had a sampled depth or voxel-ray hit in their coordinates. This narrows
the next diagnostic to the projected screen rectangle and exact renderer
submission/cull state for each target. It does not establish that the target
pixels are exposed; the existing 4-by-20 sample can miss them. M415 completed
861 chunks/13,776 blocks at 5.19653 blocks/s, captured 189 images, restored
the world file byte-for-byte, and returned normally, but the acceptance
report still failed 12/39 gates. Its visual trace was 392.63 MB; use the
low-trace M335 profile for performance comparisons.

### M416: projected screen coverage and exact target render state

Build only the Release executable, then repeat the established visible,
no-teleport M335 settings on `World_164`. The opt-in visual trace adds one
projected-AABB/render-state row per retained camera-band peak coordinate; the
camera-band pixel join checks which sparse pixel/depth samples fall inside
each projected rectangle. Keep the machine awake. M407's earlier 34-minute
pause was confirmed by the user as system sleep/lock.

```powershell
cmake --build bin --config Release --target Cubatarium --parallel 8
Start-Process -FilePath powershell.exe -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', 'tools/flight_sim_keep_awake.ps1')
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m416_world164_m335_peak_screen_coverage'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m416_world164_m335_peak_screen_coverage --report bin/suite_reports/engine_refactor/m416_world164_m335_peak_screen_coverage_20261006.json --process-timeout 3000
py tools/analyze_camera_band_pixel_join.py bin/logs/perf_20261006-065527_31132.jsonl --json-out bin/suite_reports/engine_refactor/m416_camera_band_pixel_join_20261006.json > $null
```

The Release build and static executable verification passed; no tests were run.
The visible flight completed 863 chunks / 13,808 blocks at 5.19653 blocks/s,
with 189 captures, `process_rc=0`, no force kill, and byte-for-byte world-data
restore. Acceptance remained FAIL (28/39 gates) and post-stop convergence
failed. Median wall / stream-phase / mesh-emerge times were 51.65 / 43.13 /
17.65 ms; median unfinished readiness was 27. The detailed trace was about
412 MB and should not be used as a low-trace performance baseline.

The join contains 91 route-wide peak render probes. All target AABBs were
resident, inside the geometric frustum, and projected to valid screen
rectangles. Pixel probes overlapped 79/91 rectangles; 39 rectangles contained
depth-surface samples, but none of those samples or exact voxel-ray hits were
attributed to the target chunk. At the latest no-drawable peak, 11/15 targets
had a FirstMesh dirty entry (one at queue index 73) and 4/15 had neither a
dirty entry nor ColumnFlow mesh ticket. The latest five unowned targets had no
dirty entry, flow ticket, or mesh revision. Treat this as evidence of missing
mesh ownership/service for resident solid chunks, not proof of exposed pixels:
projected chunk AABBs are conservative and sampled depth may belong to a nearer
chunk. Frame 120 also shows triangular shoreline/water artifacts.

Artifacts: [run report](../../bin/suite_reports/engine_refactor/m416_world164_m335_peak_screen_coverage_20261006.json),
[pixel/rectangle join](../../bin/suite_reports/engine_refactor/m416_camera_band_pixel_join_20261006.json),
raw perf `bin/logs/perf_20261006-065527_31132.jsonl`, and captures in
`bin/logs/m416_world164_m335_peak_screen_coverage/`.

The v8 join also follows the exact target chunk across later frames. For
`(-173,3,2)`, 13 later voxel-ray hits had either a nearer opaque depth surface
(11) or a same-chunk depth hit matching within 0.003 blocks (2); none showed
a farther surface or ray gap. By epoch 16,500 the slice was drawable and
mesh-satisfying, though geometry revision 5 was still ahead of published
revision 4 and its priority remesh entry was at index 13/252 with age 41.

Across all 32,768 sparse pixel samples, 287 were below luma 32 and 1,815 below
96. Every one had opaque depth and a draw-ready drawable; none reported
pending light. All valid light witnesses for these dark/dim pixels had current
published light revisions and median sky light 1.0. The low-luma sources were
mostly `tree_leaves` (572) and `tree_log` (573). This does not explain the
reported muted patches; future probes should record the fragment fog factor,
light-preview flag, precipitation, and wetness before interpreting the color.

### M417: fog-attribution replay interrupted by Hibernate and forced reboot

M417 reused the same visible, no-teleport M335 profile, `World_164`, Release
executable, and route parameters as M416. It was intended to capture the
fragment fog factor/color alongside sparse screen pixels. The user observed
the screen near capture 100 and reported no black holes or empty chunks; this
is useful qualitative evidence for that interval, but does not replace the
pixel/depth and streaming counters.

The computer entered Hibernate, then hung and required a forced restart. The
user confirmed the system sleep/lock event. Treat M417 as externally
interrupted: the game process was terminated by reboot, so the absent final
flight report and missing shutdown dump are not evidence of an engine hang.
The preserved capture directory is
`bin/logs/m417_world164_m335_fog_attribution_interrupted_reboot/` (106 PNGs,
`frame_000` through `frame_105`). Its partial performance log is
`bin/logs/perf_20261006-084144_42920.jsonl` (6,931,430 bytes at recovery).
The generic `bin/flight_sim_report.json` was stale from M416; do not attribute
it to M417.

The reboot exposed a diagnostic durability gap: pixel/fog and visual-black
records live in a bounded in-memory ring and are serialized by
`FramePerfMonitor::Shutdown()`. They therefore did not reach the partial M417
perf file. A visible flight capture can survive while its high-value
pixel/fog trace is lost on abnormal termination. Add periodic, deduplicated
trace checkpoints before relying on this trace for another long flight.

The fixed-day wrapper left its backup
`bin/worlds/World_164/world_data.json.fixed-day-backup`. Recovery restored the
world metadata byte-for-byte from that backup, verified against the original
SHA-256 `0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`,
then removed the stale backup. The keep-awake helper was relaunched after
restart; it prevents idle sleep while the game runs but cannot override an
explicit Hibernate request. M418 repeats the same M335 route with new artifact
names so the interrupted M417 captures remain intact.

### M418: M335 replay with fog/pixel attribution after reboot

M418 used the same Release executable and visible no-teleport route on
`World_164`: start `(120,56,56)`, eye Y 70, yaw 180, pitch -30, 2,800-second
flight phase, 20-second stop phase. It exited normally (`process_rc=0`,
`hang_killed=false`), completed 13,584 blocks at 5.19653 blocks/s, saved 189
captures, and restored `world_data.json` to SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`. The
Windows System log recorded Kernel-Power 41 at 10:08:15 before this replay,
consistent with the reported forced restart after Hibernate. No tests were
run and no Release rebuild was needed for this diagnostic replay.

Reproduction command (keep-awake helper must be running):

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m418_world164_m335_fog_attribution'
py tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m418_world164_m335_fog_attribution --report bin/suite_reports/engine_refactor/m418_world164_m335_fog_attribution_20261006.json --process-timeout 3000
py tools/analyze_camera_band_pixel_join.py bin/logs/perf_20261006-102124_22924.jsonl --json-out bin/suite_reports/engine_refactor/m418_camera_band_pixel_join_20261006.json > $null
```

The route report has `run_outcome=success`, while the visual acceptance set
still fails 12/39 gates and post-stop convergence fails 5/12. Keep those
results separate: the report defines `unfinished_visual` as readiness/debt,
not a blank framebuffer pixel. Median flight wall time was 55.91 ms (17.89
effective FPS); stream phase was 47.24 ms and mesh emergence 19.61 ms. Across
the first/middle/last 400 period rows, median wall time rose 40.19 → 56.98 →
67.72 ms, stream phase 29.16 → 46.57 → 59.94 ms, and mesh emergence 10.87 →
19.14 → 26.85 ms, while render time stayed near 5–7 ms. There were zero
`stream_disk_complete_n` and zero `stream_gen_commit_n` rows in all 1,393
periods. This repeated route diagnoses resident-world stream/mesh service,
not cold disk loading or procedural generation.

The captured air-distance fog uniforms had median start 17.28 blocks and end
36 blocks (end was 36 in every sample; the effective fog render distance was
4 chunks with the default 28-block end margin). Of 17,608 opaque depth samples
with a valid fog factor, 7,807 had factor above 0.9; their median distance was
53.71 blocks and median RGB distance to fog color was 8.58. The fog-factor to
RGB-distance correlation was -0.837. This strongly supports ordinary distance
fog as the cause of the pale blue, far “empty” views. It does not establish
that the 4-chunk render distance is the desired product setting.

Across 32,768 sparse pixel samples, 280 were below luma 32 and 1,781 below
96. Every dark/dim sample had opaque depth, draw-ready geometry, and no pending
light. Where a face-light witness was available, revisions matched (256/256
dark and 1,679/1,679 dim). All 280 dark samples had fog factor 0, 279 voxel-ray
hits, and zero ray gaps; 244 were sourced from block ID 572 (`tree_leaves`).
Their median RGB distance from fog color was 265.84. The dim group's median
fog factor was also 0; only 186/1,781 exceeded 0.25. Thus these
sampled dark foliage pixels are not missing mesh, stale light, or fog-black
pixels. The trace does not capture pre-fog material RGB, and sparse probes do
not certify every visible surface.

The camera-band join contains 18 no-drawable peak rows and 7 unowned peak rows,
all resident non-air targets inside the geometric frustum. All 18 no-drawable
trace rows had a FirstMesh dirty owner. In same-epoch renderer probes, 17 were
still non-drawable, while one was already drawable/satisfying with two visible
MDI commands; this is a state-transition/timing caveat for peak telemetry. The
7 unowned rows had no mesh-work owner and mesh revision zero. Projected
rectangles contained some pixel samples in 119/144 peak probes, but no
same-frame sample had target-chunk depth or an exact target voxel hit. Across
later route samples there were 29 target voxel-ray hits: 11 matched target
depth and 18 were occluded by a nearer surface; none found a farther depth
surface. This repeats the M416 limitation: transient non-drawable/ownerless
data exists, but the sparse pixel evidence does not prove an exposed hole.

The full trace is about 445 MB. The bounded visual/pixel ring was successfully
written at normal shutdown, avoiding M417's data loss. Keep the M417
interruption record and add deduplicated periodic trace checkpoints before the
next long forensic run. Use the repeatable M335 route for regressions, then a
periodic cold/new-world run to exercise disk reads and generation.

Artifacts: [M418 flight report](../../bin/suite_reports/engine_refactor/m418_world164_m335_fog_attribution_20261006.json),
[M418 fog/pixel join](../../bin/suite_reports/engine_refactor/m418_camera_band_pixel_join_20261006.json),
raw perf `bin/logs/perf_20261006-102124_22924.jsonl`, and 189 captures in
`bin/logs/m418_world164_m335_fog_attribution/`.
## M420 - M335 far route with ColumnFlow class telemetry (2026-10-06)

Release build: `bin/Cubatarium.exe`, binary SHA-256
`6e28736b2fbe055371eeb1449ac2f68921ea9f9df8b365466946e2510336bb57`, source
commit `afe4192f96bb23253f2a73e587a298fb33da9a97`. The runner completed the
no-teleport, visible 2,800-second M335 route at median `5.19653 blocks/s`,
covering 13,472 blocks west (focus X 7 to -835); it recorded 1,393 periods and
189 GUI captures. No collision was recorded. Windows briefly marked the app
not responding as it exited; the app then closed normally, the runner was not
killed, and the fixed-day wrapper restored `World_164/world_data.json`
byte-for-byte (SHA-256 `0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`).

This is a resident-world throughput baseline, not a cold storage or generation
test: `stream_disk_complete_n` and `stream_gen_commit_n` were zero throughout.
Median frame wall was 56.29 ms (17.76 FPS), world streaming phase 47.40 ms,
mesh emergence 20.33 ms, and render 7.06 ms. `gpu_not_ready` was the dominant
completion stall and `stream` the dominant wall stage. Toward the end, the
streaming phase rose into the 60-90 ms range while the camera continued at the
configured speed overall. Captures show terrain and trees nearby and a
fog-dominated distant horizon; these samples do not prove every transient
readiness signal was visually exposed.

Readiness signals need careful interpretation. The report's hole-key is
`unfinished_visual`, explicitly a readiness/debt count rather than a blank
framebuffer pixel. Its raw hole-rate gate fails, while effective hole blink
rate is 0, mid-corridor `visual_holes` median is 0, and visible-black median is
0 (maximum 18). Near-focus mesh-miss signals occur transiently; pixel/depth
evidence is still required before calling them exposed holes. ColumnFlow live
Relight median was 21, its dispatched median 0, and total live queue median 26.
This is a service/fairness risk, not yet a demonstrated cause of the captured
appearance. Chunk-not-ready median was 27, but focus-not-loaded and post-stop
focus-miss checks cleared.

The report's manifest acceptance failed because source telemetry changes were
made while this binary was running (`dirty_diff_hash` was not clean); process
exit was 0 and route adequacy passed. The binary and source commit are pinned
above, so these metrics remain useful as a baseline but are not a clean-tree
acceptance run. Old `async_io_ms` values are not disk latency: the field was
overwritten with the full `TickAsyncChunkSystems` wall time. The next build
adds explicit phase and result-drain timings.

Artifacts: [M420 acceptance report](../../bin/suite_reports/engine_refactor/m420_world164_m335_flow_kind_telemetry_20261006.json),
[ColumnFlow windows](../../bin/suite_reports/engine_refactor/m420_columnflow_checkpoint_20261006.json),
raw perf `bin/logs/perf_20261006-133528_32068.jsonl`, and captures in
`bin/logs/m420_world164_m335_flow_kind_telemetry/` (for example
`frame_182.png`).

## M421/M422 - selecting a genuinely cold world and cold-start stall (2026-10-06)

M421 was invoked with `--world World_M421_Cold_20261006`, but the
`product-174657-far` replay path silently replaced it with `World_164`. Its
manifest proves the actual world was `World_164` (seed `3650471197`), so treat
the 600-second, 3,008-block, `5.19653 blocks/s` route as a short repeated-world
control only. It is not evidence about cold disk or procedural generation.
The run nevertheless caught a harness defect: the product replay now preserves
an explicitly selected world and pins/restores that world's `users.json`.

M422 exercised the corrected path with `World_M421_Cold_20261006`, numeric seed
`3650472197`, spawn `[120,56,56]`, no `chunks/`, `chunks.json`, or `users.json`,
and the established M335 camera settings. The Release manifest is pinned to
`afbd2572` and confirms the selected cold world. Metadata load and procedural
terrain fill completed; the log reports 2,310,487 non-air blocks and 805
resident chunks. However, the world never left cooperative `EnterLit` within
about 891 seconds. The runner returned `process_rc=1`, `run_outcome=harness_fail`,
and zero flight periods/captures; do not classify this as a flight regression.

The persisted `enter_lit` trace has 626 rows. Dirty count began at 31, peaked at
208, then remained about 85-121; the soft-enter fallback first reported 106 at
150 seconds and requires the residual to fall to 32 or less. At the end,
`underfeet_present_ready=1` and `visibility_debt=0`, but
`spawn_mesh_ring_ready=0`, `ring_not_ready=31-46`, `first_presentable_ms=-1`,
and `gate_elapsed_ms` was about 890 seconds. GPU kick/finish continued, while
the same dirty/ring state recurred. The app window remained responsive. This
confirms a cold-start convergence stall before the camera can fly; the fixed
global dirty cap is the exit blocker, not yet the proven origin of the dirty
work. Preserve the safety gate until the outstanding mesh/column owners and
empty-mesh outcomes are identified.

M422 artifacts: [flight report](../../bin/suite_reports/engine_refactor/m422_world_m335_cold_streaming_20261006.json),
raw perf header `bin/logs/perf_20261006-150113_33632.jsonl` (no periods),
enter trace `bin/logs/enter_lit_20261006-150116.jsonl`, and INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261006-150109.33632`.
The fixed-day wrapper restored `world_data.json` byte-for-byte. The generated
`column_light.json` remains in the isolated diagnostic world.

## M423-M426 - cold EnterLit owners and soft-settle boundary (2026-10-06)

The next step added three diagnostic commits: `a06174a6` records the first
spawn-ring miss and full camera-band census; `960b178d` records non-air count
and content revision at the exact gate-miss coordinate; `a1958820` records the
oldest camera-band dirty entry's queue index, chunk data, drawable/satisfying
state, and render-demand lifecycle. Release executable SHA-256 for M424-M426:
`d18fc8336eeebc0debe1e29fcf8e04914d3f9cdc27292a22f5239ede1cbb66ef`.

M423 used a genuinely cold metadata-only world with seed `3650473197`, visible
GUI, the M335 start/yaw/pitch/eye settings, and no teleport. This was the
product replay wrapper but the environment omitted
`CUBA_VISUAL_BLACK_TRACE=1`; the manifest correctly says `visual_black_trace`
false. The app stayed in EnterLit until the 420-second harness limit, with no
route periods. At about 120 seconds, the first gate miss repeatedly returned
`(1,2,3)`: its GPU buffer was resident but had zero quads, and its published
geometry revision lagged desired by one. This run cannot classify whether that
chunk's zero-quad image was valid occlusion because its occupancy census was
disabled. Report: [M423](../../bin/suite_reports/engine_refactor/m423_world_cold_enter_diagnostic_20261006.json);
EnterLit JSONL: `bin/logs/enter_lit_20261006-155333.jsonl`.

M424 repeated on cold seed `3650474197`, with visible GUI and both visual-black
and column-source traces enabled. It ran no-flight/no-teleport for the startup
diagnostic and reached the 210-second process limit with the gate still closed.
At the last sample (197 seconds), 116 solid slices were in the camera band;
12 lacked drawable meshes, 10 had pending work, none were unowned, and 62 were
dirty. The oldest visible dirty age was 302 frames. A sampled gate miss at
`(2,2,4)` had 4,096 non-air blocks, a zero-quad GPU mesh, and desired geometry
revision one newer than published; its FirstMesh dirty entry was still present.
Therefore this particular empty mesh was not an empty voxel chunk. The trace
does not prove that the full-solid slice should draw: it may be fully enclosed.
Report: [M424](../../bin/suite_reports/engine_refactor/m424_world_cold_enter_census_20261006.json);
EnterLit JSONL: `bin/logs/enter_lit_20261006-161818.jsonl`.

M425 used cold seed `3650475197`, visual-black trace, no flight movement, and a
140-second harness limit. At 120 seconds, underfeet was present, visibility
debt was zero, all 52 camera-band solid slices had drawable/satisfying output,
and the camera-band census had no missing mesh or pending work. Still,
`spawn_mesh_ring_ready=0`, `ring_not_ready=16`, and 26-27 total dirty slices
remained. The oldest dirty slice was a drawable, satisfying chunk with an
active priority-remesh attempt and desired revision one above published; the
near async blocker toggled between zero and one. This is evidence that dirty
and async state can outlive a fully drawable camera band. The run ended before
the ~150-second soft-settle check, so it did not test whether the existing
32-dirty cap would allow exit. The harness had already saved a `chunks/`
directory under this test world; do not reuse M425 as a cold seed. Report:
[M425](../../bin/suite_reports/engine_refactor/m425_world_cold_oldest_dirty_20261006.json);
EnterLit JSONL: `bin/logs/enter_lit_20261006-164200.jsonl`.

M426 used another metadata-only world (seed `3650476197`) and ran through the
soft-settle threshold. At 150 seconds the engine logged
`soft_settle_blocked_dirty_residual n=100`; at the last saved sample (191
seconds), dirty was 99, underfeet was present, visibility debt was zero, and
the camera band still had 12 no-drawable slices, 10 pending slices, and zero
unowned slices. The gate remained closed and the harness ended the run at 210
seconds. This validates the 32-item check as the immediate exit blocker for
this seed, while the persistent camera-band misses mean removing that check
would be unsafe. Report: [M426](../../bin/suite_reports/engine_refactor/m426_world_cold_softsettle_20261006.json);
EnterLit JSONL: `bin/logs/enter_lit_20261006-165005.jsonl`; INFO record is in
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261006-164956.24252`.

These are cold-start diagnostics, not long no-teleport flight acceptance runs:
M423/M424/M426 produced zero route periods; M425 produced four diagnostic
periods but no camera movement. None changes the established M335 route
parameters or replaces the `World_164` repeated-world regression lane. The
next experiment should persist exact near-async coordinates and vertical band,
then follow demand/revision transitions for the oldest dirty and first
unpresentable camera-band slices. Keep the residual guard until those slices
are proven presentable or their work is shown to be safely retain-old-image
background remeshing.

## M427-M428 - validate the presentability gate at the established locus (2026-10-06)

M427 used a metadata-only seed but had no per-world `users.json`, so the
M335-start camera locus was not pinned. It stayed at focus `(2,-2)` with zero
chunks traveled. Treat it only as an EnterLit diagnostic, not a reproducible
M335-locus check; the nearby tree and the run's timeout do not indicate that
the route collided or failed.

M428 repeated the cold-start check with a per-world user at `[120,56,56]`,
yaw `180`, pitch `-30`, and visible GUI. It ran 30 telemetry periods on seed
`3650478197`; `player_x/y/z` stayed at `(120,47,56)`, and requested/applied
horizontal movement stayed at zero. Thus the log contains no player movement
or flight, despite the operator observing an object in water during the run.
Keep that observation separate from route-collision evidence until a moving
M335 report captures it.

The M428 cold start produced `first_presentable_ms=5168.17` in one EnterLit
trace, while a later EnterLit trace ended with `live_blockers`,
`underfeet_present_ready=0`, `spawn_mesh_ring_ready=0`, and visibility debt 15.
The run report says `success`, but its route scenario is empty and it is not an
acceptance replay. The report's `unfinished_visual` stayed nonzero (60 at the
end); this is readiness debt, not proof of blank pixels. In its sampled
periods, fully-dark and black-sticky counts remained zero, while the trace
recorded 16 stale-lit visible-black candidates. Keep the distinction between
not-ready, stale-light, and measured fully-dark output in later attribution.

Artifacts: [M427 report](../../bin/suite_reports/engine_refactor/m427_world_cold_presentability_20261006.json),
[M428 report](../../bin/suite_reports/engine_refactor/m428_world_cold_m335_locus_20261006.json),
EnterLit traces `bin/logs/enter_lit_20261006-172658.jsonl`,
`bin/logs/enter_lit_20261006-172710.jsonl`,
`bin/logs/enter_lit_20261006-173410.jsonl`, and
`bin/logs/enter_lit_20261006-173422.jsonl`; M428 perf `bin/logs/perf_20261006-173407_30872.jsonl`.
The next acceptance run is the unchanged visible, no-teleport M335 route on
`World_164`; no cold-start result substitutes for it.

## M429 - M335 long-run visual and disk-result audit (2026-10-06)

M429 completed the established visible, no-teleport `product-174657-far` route
on `World_164` with the committed clean Release build (`9b0c9d3f`, executable
SHA-256 `d51a792c3cfa9fc2ebd5d7def730520fc5b679af906676c58d735f354a6d0c99`).
The route kept the M335 start `[120,56,56]`, eye Y `70`, yaw `180`, pitch
`-30`, speed scale `1`, and 2,800-second flight. The app exited normally
(`process_rc=0`, `hang_killed=false`); route adequacy passed at 5.19653 blocks/s,
focus X `7 -> -798`, 12,880 blocks, 1,390 periods, and 189 screenshots. One
brief obstacle contact caused partial X blocking and a small Z deviation; the
camera subsequently resumed the route. The runner restored
`World_164/world_data.json` byte-for-byte (SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`).

Exact invocation (PowerShell, run from the repository root):

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='1'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\logs\m429_world164_m335_presentability'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m429_world164_m335_presentability --report bin/suite_reports/engine_refactor/m429_world164_m335_presentability_20261006.json --process-timeout 3000
```

The operator reports that the current appearance looks sufficiently good.
This agrees with the sampled captures and pixels: no sampled point had mean
RGB below 16; 294/32,768 points were below luminance 32, and all 294 had a
valid opaque depth surface, drawable mesh, and visible MDI pass. Among those
dark samples, 250 source-face witnesses were within 0.1 blocks of the depth
surface; 265 had valid light samples, and 265 had matching light revisions.
There were 1,922 additional samples below luminance 96, also all on draw-ready
surfaces. These points were not fog-blackened (median fog factor 0; none of the
<32 samples had fog factor >0.25). Sparse sampling cannot certify every screen
pixel or exclude a brief defect between captures; the conclusion is “no
sampled black hole, with observed scenes looking good,” not full framebuffer
coverage. The retained 32,768-pixel ring covers only 205 late-route probes.

The structural traces still show transient work: focus-slice samples were all
terrain-complete, while the camera-band peak census recorded 16 solid
no-drawable slices and 3 solid slices without a FirstMesh owner. Pixel samples
at those peaks did not hit the target depth; the nearest sampled surfaces were
in front of the targets. This is a readiness/ownership risk, not proof that the
missing slices appeared as holes. Of 1,009 screen-ray repair candidates, 987
had an already-satisfying drawable plus repairable geometry debt, so most
candidate pressure was stale-mesh refresh rather than absent first mesh.

`WorldColumnSource` recorded 2,175 unique disk queue/completion coordinates,
zero procedural generation commits, and zero unmatched queued coordinates.
File reads were fast (median/p95 1.09/1.68 ms). The worker queue was also modest
at median/p95 2.53/132.46 ms, while the completed-result wait from worker finish
until main-thread consumption was median/p95 655 ms/9.60 s, maximum 60.61 s.
That `result_wait_ms` is accumulated across vertical slices for one column;
the separate worst-single-slice field (`result_wait_max_ms`) had median/p95
195/2,400 ms and maximum 15.20 s. At shutdown the source log still showed 28
ready slices and 7 pending disk columns. The manifest's `cold_warm_mode` label
is not an OS or storage-cache guarantee: the source trace confirms that this
route exercised persisted chunks, not procedural generation. The measured
delay is after worker completion and before main-thread application, not disk
latency.

Code review explains a plausible service constraint to measure next:
`TickAsyncChunkIo` ranks the completed queue and drains at most four results per
tick; on a frame above 24 ms it also uses a four-slice/4 ms budget, and the
near-stream-over-budget fallback applies one result with a 2.5 ms target. M429
reported median `async_chunk_io_drain_ms=9.97 ms` and median
`async_chunk_systems_ms=26.52 ms`. One slice/finalization may overrun the target,
and queue ranking/finalization are included in the drain wall time. This makes
main-thread service/backlog a strong optimization candidate, but does not yet
prove it caused the prior dim/empty appearance. Treat M429's 61.63 ms median
frame wall as diagnostic-only because the run enabled dense pixel readback and
per-column source logging.

The runner's route and manifest checks passed, but the product quality report
remains `pass=false`: post-stop convergence and multiple readiness/performance
gates failed. Its `holes_rate=1.0` is keyed to `unfinished_visual`, so it is not
a pixel-hole rate. Keep visual evidence, readiness debt, and frame cost as
separate acceptance axes.

Artifacts: [M429 report](../../bin/suite_reports/engine_refactor/m429_world164_m335_presentability_20261006.json),
[pixel summary](../../bin/suite_reports/engine_refactor/m429_pixel_trace_summary_20261006.json),
[camera-band/pixel join](../../bin/suite_reports/engine_refactor/m429_camera_band_pixel_join_20261006.json),
[visual trace summary](../../bin/suite_reports/engine_refactor/m429_visual_coverage_20261006.json),
[disk-source summary](../../bin/suite_reports/engine_refactor/m429_world_column_source_trace_20261006.json),
raw perf `bin/logs/perf_20261006-174353_35164.jsonl`, INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261006-174349.35164`,
and captures in `bin/logs/m429_world164_m335_presentability/`.

## M430 - low-instrumentation M335 near-route control (2026-10-06)

M430 replayed the same visible, no-teleport Release M335 start, yaw, pitch,
eye height, and speed on `World_164`, with a 600-second flight phase. All
optional visual-black, dense-pixel, source-column, relight-audit, and screenshot
capture environment flags were unset. The app exited normally
(`run_outcome=success`, `process_rc=0`, `hang_killed=false`); the report's
product-quality gates still returned `pass=false`. The route passed movement
adequacy at 5.19653 blocks/s, focus X `7 -> -185`, 192 chunks / 3,072 blocks,
315 periods. There were zero blocked movement substeps and zero ground
contacts. This is a low-instrumentation near-route timing control, not a far
route acceptance run: it did not reach the 8,192-block checkpoint and has no
pixel trace or screenshot coverage.

The first-locus timing was lower than the heavily instrumented M429 run:
median/p95 frame wall was 34.76/45.34 ms, world streaming phase 23.93/35.65 ms,
async chunk systems 12.18/18.39 ms, and mesh emergence 8.73/15.27 ms. The
main-thread async chunk-I/O drain still cost median/p95 7.97/11.62 ms, with a
14.46 ms maximum, even with source logging and dense pixel readback disabled.
This confirms that the M429 drain cost is not solely caused by its opt-in pixel
sampling, but M430's shorter, near-start segment cannot be compared directly
with M429's 12,880-block route or used to explain M429's far-route timing.

Visual readiness remained distinct from actual black pixels: the report's
`unfinished_visual` median was 27 and `chunk_not_ready` median 27, while
`visible_black_focus_n` median was 0 and maximum 18. No pixel capture was
enabled, so these are internal candidates/debt only. Post-stop convergence
remained false with 27 missing/readiness items; do not treat M430 as proof of a
pixel hole or of a fully settled renderer. Its failed `holes_rate` gate is
still driven by `unfinished_visual`.

Artifacts: [M430 report](../../bin/suite_reports/engine_refactor/m430_world164_m335_low_instrumentation_20261006.json),
raw perf `bin/logs/perf_20261006-193728_26092.jsonl`. The manifest records
Release executable SHA-256
`d51a792c3cfa9fc2ebd5d7def730520fc5b679af906676c58d735f354a6d0c99`, no
optional traces, no teleport, and a clean source tree. `World_164/world_data.json`
was restored byte-for-byte.

## M431 - full M335 low-instrumentation control and generation frontier (2026-10-06)

M431 extended the exact visible, no-teleport M335 setup from M430 to the full
2,800-second flight on `World_164`. Scenario `product-174657-far`; start
`[120,56,56]`, eye Y `70`, yaw `180`, pitch `-30`, speed scale `1`, daylight,
normal 20-second stop phase, no teleport. All optional visual-black, dense
pixel, world-column source, relight audit, and screenshot-capture flags were
unset. Build was Release at source `0a963e14`, with executable SHA-256
`d51a792c3cfa9fc2ebd5d7def730520fc5b679af906676c58d735f354a6d0c99`.

The run manifest reports `process_rc=0`, `run_outcome=success`,
`hang_killed=false`, and route adequacy PASS. It covered 1,399 periods (1,397
steady), 1,382 fly periods, median speed `5.19653 blocks/s`, focus X
`7 -> -864`, and 13,936 blocks, crossing the 8,192-block checkpoint. There
were zero blocked movement substeps and zero ground contacts. The flight
runner itself returned exit code 1 because product gates were not met: report
`pass=false`, 27/39 gates and 10/12 stop gates passed. Do not interpret that as
an app crash or as a route failure.

### Matched route-distance timing

These medians are computed from raw `kind=period` rows grouped by `focus_cx`;
period rows are interval means, not individual-frame percentiles:

| Focus band | Periods | Frame wall median / p95 | World-streaming phase median / p95 | Async chunk systems median | Async I/O drain median | Mesh emerge median |
|---|---:|---:|---:|---:|---:|---:|
| Near `x >= -200` | 330 | `34.95 / 48.36 ms` | `23.85 / 38.21 ms` | `12.10 ms` | `7.85 ms` | `8.82 ms` |
| Mid `-600 < x < -200` | 620 | `50.50 / 64.62 ms` | `42.86 / 56.92 ms` | `21.36 ms` | `9.62 ms` | `17.00 ms` |
| Far `x <= -600` | 449 | `67.78 / 89.88 ms` | `61.58 / 82.34 ms` | `30.36 ms` | `10.29 ms` | `26.10 ms` |

The late-route slowdown persists with low instrumentation. From near to far,
the median frame wall rises about `32.84 ms`; async chunk systems rises
`18.26 ms` and mesh emergence rises `17.27 ms`. The I/O drain rises only
`2.44 ms`; `update_streaming_ms` rises `0.49 ms`. This indicates that the
M429 disk-result wait is not by itself the late-route performance cause.
Selected mesh subtimers (prep `1.18`, dirty tick `3.40`, snapshot `1.69`,
schedule `1.77`, GPU kick `1.27`, GPU finish `0.61`, async drain `0.34 ms`
median in the far band) do not explain the enclosing `26.10 ms` median. Existing
async subfields also leave most of its `30.36 ms` unresolved after I/O drain
`10.29`, relight drain `1.31`, and pressure refresh `1.33 ms`. Some subtimers
can overlap or nest; add an explicit timing tree before summing them.

### Visual and acceptance caveats

No screenshots or pixel probes were collected in M431. `visible_black_focus_n`
median 0 / max 22, `visual_holes` p95/max 1, `unfinished_visual` median 27 /
p95 63 / max 83, and `chunk_not_ready` median 27 are internal candidates or
readiness debt, not framebuffer coverage. The analyzer's `holes_rate=1.0` is
based on `unfinished_visual`. The user reports that visuals now look
sufficiently good; M429's sparse pixel evidence is compatible with that but
covers only part of the route. M431 cannot certify the newly reached segment.

### Newly generated coordinates and interpretation

The INFO log contains 171 worker-side `ChunkPopulate` records at X
`-850..-868` (19 X columns by 9 Z columns), beyond M429's endpoint. Per-call
`total_ms` was median/p95/max `95.27/128.32/250.12`; sample median/p95
`18.26/21.26`, terrain `6.76/8.18`, post-processing `30.38/40.92`, and sealing
`32.72/56.24 ms`. These are worker timings, not direct frame costs. With
`CUBA_WORLD_COLUMN_SOURCE_TRACE` off, the run cannot join generation with
request queue, worker pool, result application, disk source, or frame cost.
The route persisted newly created west-edge data, so subsequent `World_164`
replays may exercise disk reads there; cold generation must be checked on a
separate fresh seed/world.

The report failed product acceptance despite a successful app/route. It ended
with post-stop convergence false and 26 not-ready items. Preserve three
separate verdicts: user's positive visible assessment; readiness telemetry
that remains unresolved; and a reproducible late-route performance increase.

Artifacts: [M431 report](../../bin/suite_reports/engine_refactor/m431_world164_m335_full_low_instrumentation_20261006.json),
raw perf `bin/logs/perf_20261006-195246_18892.jsonl`, INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261006-195242.18892`.
Optional trace flags were false and frame-capture directory was null in the
manifest. The report/manifest and executable should be retained with the
existing ignored `bin/` artifacts; the tracked documentation is the portable
record of settings and conclusions.

## M432 - full M335 timing resample with phase timers (2026-10-06)

M432 repeated the same visible, no-teleport 2,800-second M335 route on
`World_164`, using the Release build from `cc94e562` and the `product-174657-far`
scenario. Start `[120,56,56]`, eye Y `70`, yaw `180`, pitch `-30`, speed scale
`1`, normal 20-second stop, no teleport. Optional pixel/source traces and
captures were disabled. The executable SHA-256 was
`51ad3dc79e90317450add103e698a1472c01d10f50c083324995e1d74ddc6b06`.

The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`). The report contains 1,397 periods (1,395 steady), median
movement speed `5.19653 blocks/s`, focus X `7 -> -857`, 864 route chunks / 13,824
blocks, and both route checkpoints. Blocked movement substeps and ground
contacts were zero. The runner returned nonzero because product/readiness gates
remain red (`pass=false`); this was not a crash or route failure.

Raw `kind=period` rows are interval means. The more distant half is split into
two bands to expose the additional westward growth that a single far median
would hide:

| Focus band | Periods | Frame wall median / p95 | World-streaming median / p95 | Async systems median / p95 | Async post-scheduler median | I/O drain median | Mesh emerge median | Mesh post-telemetry median |
|---|---:|---:|---:|---:|---:|---:|---:|
| Near `x >= -200` | 322 | `37.06 / 53.03 ms` | `26.48 / 43.65 ms` | `13.76 / 20.88 ms` | `12.28 ms` | `8.94 ms` | `9.04 ms` | `2.13 ms` |
| Mid `-600 < x < -200` | 624 | `53.65 / 70.52 ms` | `46.42 / 61.83 ms` | `23.38 / 29.10 ms` | `21.12 ms` | `10.23 ms` | `18.53 ms` | `10.20 ms` |
| Far east `-740 < x <= -600` | 230 | `64.84 / 89.90 ms` | `58.57 / 80.48 ms` | `29.02 / 36.08 ms` | `26.41 ms` | `9.51 ms` | `24.66 ms` | `16.33 ms` |
| Far west `x <= -740` | 205 | `72.83 / 95.94 ms` | `65.88 / 87.61 ms` | `33.07 / 44.32 ms` | `30.85 ms` | `10.40 ms` | `28.17 ms` | `19.99 ms` |

From near to far west, wall median grows `35.77 ms`; async post-scheduler
grows `18.57 ms`, while I/O drain grows `1.46 ms`. Mesh post-telemetry grows
`17.86 ms`. The actual chunk scheduler tick is near zero at the median across
all bands (`0.00 ms`); therefore, the broad post-scheduler phase is not the
`ChunkLoadScheduler::Tick` itself. The full-run analyzer reports wall median
`55.466 ms`, effective flying FPS `18.04`, and max wall `316.746 ms`.

This run shows a strong source-level candidate for duplicated hot-path
diagnostics. `SampleColumnEmergeStageTelemetry()` is called once from
`TickAsyncChunkSystems()` and again from `TickMeshEmerge()` in the same frame.
It walks `ColumnEmergeStates`, queries the focus-ring job stages, and scans the
chunk-demand records for unsatisfied work and stop convergence. The source
search found those sampled counters used by performance telemetry/tests, not by
production policy. The sample also runs bounded demand-store reconciliation
and orphan cancellation, which mutate state; a cleanup must preserve those
two maintenance opportunities while removing only the duplicate census. The
growing `column_lighting_n` median (`17` near, `142` mid, `220` far east, `246`
far west) tracks the same direction as the post-telemetry time. This is a
well-supported optimization hypothesis, not yet a causal measurement; the
next build will time the remaining single census explicitly.

The user's current visual assessment is positive. M432 itself collected no
pixels. Internal `visible_black_focus_n` median was 0 / max 18 and
`unfinished_visual` median was 28; neither substitutes for framebuffer
evidence. The analyzer's `holes_rate=1.0` still uses `unfinished_visual`, and
post-stop convergence remains false. No new visual defect was reported during
this run, so future full timing controls should keep expensive pixel capture
off unless the symptom returns.

Artifacts: [M432 report](../../bin/suite_reports/engine_refactor/m432_world164_m335_phase_timing_20261006.json),
raw perf `bin/logs/perf_20261006-211058_31136.jsonl`, INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261006-211054.31136`.
The manifest records source `cc94e562`, the executable hash above, clean source,
Release, no teleport, and disabled optional traces/captures. The runner restored
`world_data.json` byte-for-byte. Keep these ignored artifacts and this tracked
record together.

## M433 - remove duplicate census and measure remaining cost (2026-10-06)

M433 repeated the same visible, no-teleport 2,800-second M335 setup on
`World_164`, now using source commit `5b6cce71` and a Release executable with
SHA-256 `8332dd59d09d8cd7f27633f196bcb83cf365f24e343c6cc38d1d1ac8b90325ad`.
Route settings were unchanged: start `[120,56,56]`, eye Y `70`, yaw `180`,
pitch `-30`, speed scale `1`, daylight preset, 20-second stop, no teleport.
Optional visual/source traces and captures remained disabled.

The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`). The route analyzer still returned `pass=false` because
product/readiness gates did not converge. It recorded 1,400 periods (1,398
steady), median speed `5.19653 blocks/s`, focus X `7 -> -882`, 14,224 blocks,
zero blocked movement substeps, and zero ground contacts. Total traveled
distance varied from M432 despite identical settings, so the comparison below
uses focus-distance bands rather than total blocks.

| Focus band | Periods | Frame wall median / p95 | World-streaming median / p95 | Async systems median | Async post-scheduler median | I/O drain median | Mesh emerge median | Census sample median |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Near `x >= -200` | 322 | `34.31 / 42.90 ms` | `23.26 / 33.70 ms` | `11.86 ms` | `10.35 ms` | `8.38 ms` | `8.22 ms` | `1.89 ms` |
| Mid `-600 < x < -200` | 618 | `46.51 / 61.85 ms` | `39.17 / 53.15 ms` | `18.38 ms` | `16.14 ms` | `10.31 ms` | `16.76 ms` | `9.31 ms` |
| Far east `-740 < x <= -600` | 220 | `55.50 / 72.38 ms` | `49.56 / 65.08 ms` | `21.80 ms` | `19.21 ms` | `10.41 ms` | `23.96 ms` | `15.95 ms` |
| Far west `x <= -740` | 224 | `61.05 / 73.32 ms` | `54.72 / 66.59 ms` | `23.76 ms` | `21.32 ms` | `10.54 ms` | `27.29 ms` | `19.48 ms` |

Against M432's matching bands, frame-wall medians improved by about
`2.75/7.14/9.34/11.78 ms` near through far west (approximately
`7/13/14/16%`). In far west, async post-scheduler fell from `30.85` to
`21.32 ms`, while I/O drain was effectively unchanged (`10.40 -> 10.54 ms`).
Full-route wall median fell from `55.466` to `47.634 ms`, effective flying FPS
rose from `18.04` to `21.01`, and analyzer spike count fell from `663` to `87`.
The repeat therefore confirms a material improvement from removing the
duplicate full census while retaining both demand-maintenance passes.

The new `column_emerge_stage_sample_ms` field confirms the remaining mesh cost:
its median is `19.48 ms` in far west and nearly equals the enclosing
`mesh_emerge_post_telemetry_ms` (`19.49 ms`). It grows with the active column
map and explains nearly the entire post-telemetry subphase. Because sampled
counts are read by telemetry/tests rather than production policy, running this
full O(N) census every frame is unnecessary. The next bounded step is a
time-based 250 ms census cadence, while keeping both bounded reconciliation
and orphan-cancel calls at their existing every-frame cadence. Log sample count
and age so the cached snapshot's freshness remains visible.

The current visual assessment remains positive. M433 had no screenshots or
pixel probes; its internal `visible_black_focus_n` median was 0 / max 18 and
`unfinished_visual` median 27. The analyzer still uses `unfinished_visual` for
`holes_rate`, and post-stop convergence remained false. No fresh-world source
trace was enabled; the cold-generation lane remains separate.

Artifacts: [M433 report](../../bin/suite_reports/engine_refactor/m433_world164_m335_census_cleanup_20261006.json),
raw perf `bin/logs/perf_20261006-221915_29780.jsonl`, INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261006-221911.29780`.
The manifest records source `5b6cce71`, the hash above, clean source, Release,
the same route settings, and disabled optional traces/captures. The runner
restored `world_data.json` byte-for-byte.

## M434 - validate 250 ms census cadence on full M335 route (2026-10-06)

M434 repeated the unchanged visible, no-teleport 2,800-second M335 profile on
`World_164`, using Release source commit `a5ae29b3` and executable SHA-256
`66c712da62ffeec9cad98e60301a52461c49cf7cde4fc635164e6ad7c2b7654f`.
Settings remained start `[120,56,56]`, eye Y `70`, yaw `180`, pitch
`-30`, speed scale `1`, 20-second stop, no teleport. Optional pixel,
visual-black, and column-source traces were disabled.

The app and route completed normally (`process_rc=0`,
`run_outcome=success`, `hang_killed=false`); the analyzer returned
`pass=false` for product/readiness gates. It recorded 1,402 periods (1,400
steady; 1,385 fly), median movement speed `5.19653 blocks/s`, focus X
`7 -> -886`, 14,288 blocks, player Y delta `0`, zero blocked movement
substeps, and zero ground contacts. The manifest records clean source,
Release, `World_164`, the same route hash, and disabled optional traces. The
runner restored `world_data.json` byte-for-byte.

| Focus band | Periods | Frame wall median / p95 | World-streaming median / p95 | Async systems median | Async post-scheduler median | I/O drain median | Mesh emerge median | Mesh post-telemetry median | Census amortized median | Census sample count / frame | Snapshot age median |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Near `x >= -200` | 329 | `33.66 / 41.31 ms` | `22.73 / 31.85 ms` | `11.84 ms` | `10.37 ms` | `8.24 ms` | `7.88 ms` | `1.41 ms` | `0.123` | `117 ms` |
| Mid `-600 < x < -200` | 618 | `42.13 / 56.20 ms` | `33.73 / 47.56 ms` | `17.36 ms` | `15.14 ms` | `9.63 ms` | `13.10 ms` | `5.68 ms` | `0.159` | `115 ms` |
| Far east `-740 < x <= -600` | 218 | `48.62 / 65.62 ms` | `42.94 / 58.66 ms` | `22.21 ms` | `19.66 ms` | `10.87 ms` | `16.75 ms` | `9.33 ms` | `0.178` | `115 ms` |
| Far west `x <= -740` | 237 | `52.06 / 62.82 ms` | `45.74 / 55.37 ms` | `24.05 ms` | `21.43 ms` | `10.83 ms` | `18.32 ms` | `11.39 ms` | `0.186` | `116 ms` |

Compared to matched M433 bands, frame-wall medians improved by approximately
`2/9/12/15%` from near through far west. Full-route wall median dropped
`47.63 -> 43.06 ms`, effective flying FPS rose `21.01 -> 23.25`, and
spikes fell `87 -> 37`. Far-west census cost fell from `19.48 ms` per
frame to `1.84 ms` amortized, with the same bounded demand maintenance still
running each frame. Its sample-age median of `116 ms` is consistent with a
250 ms interval. The enclosing mesh post-telemetry phase fell from `19.49`
to `11.39 ms`.

The next bottleneck is not yet isolated: far-west async post-scheduler remains
`21.43 ms` median (M433: `21.32 ms`) and total world-streaming phase is
`45.74 ms`. Analyzer spike maximum was `342.06 ms`, classified as stream.
Do not change async scheduling or work budgets until its post-scheduler
subphases are timed.

Readiness remains open. Product adequacy passed and movement/collision control
passed, but analyzer `pass=false`: post-stop convergence failed
(`missing_zero`, effective holes zero, falling pending/dirty, and demand
convergence); the dual-lane gate saw max unlit `41`; eye-proxy gates reported
stale-visual and blink-proxy failures. The report's `holes_rate=1.0` is
derived from `unfinished_visual` debt, not pixels. Internal
`visible_black_focus_n` had median `0`, maximum `18`, and brief nonzero
samples that returned to zero. The operator reports that visuals currently
look acceptable. No framebuffer or source trace was collected, so this run
neither proves nor disproves visible dark/blank pixels.

Artifacts: [M434 report](../../bin/suite_reports/engine_refactor/m434_world164_m335_census_250ms_20261006.json),
raw perf `bin/logs/perf_20261006-231755_26476.jsonl`, INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261006-231752.26476`.
The report manifest records source `a5ae29b3`, the executable hash above,
clean Release source, and the unchanged route. Keep these ignored run outputs
with this tracked record.

## M435 - cold-seed generation and startup trace (2026-10-07)

M435 used a new metadata-only world, `World_M435_Cold_20261007`, numeric seed
`3650479197`. It was prepared with the established spawn `[120,56,56]` and
the same visible M335 camera and movement settings, but a 600-second flight
phase because this was the separate cold-start lane. There were no chunk
files or lighting cache at launch. Column-source tracing was enabled;
visual-black/pixel capture was disabled. The Release binary is the unchanged
M434 executable with SHA-256
`66c712da62ffeec9cad98e60301a52461c49cf7cde4fc635164e6ad7c2b7654f`.

The app stayed responsive and the cold seed became presentable in about
`28.9 s` in the first EnterLit trace session. The route then ran at median
`5.19653 blocks/s`, held player Y at `70`, covered 3,072 blocks, and recorded
zero blocked movement substeps and zero ground contacts. The app exited
normally (`process_rc=0`, `run_outcome=success`, `hang_killed=false`);
the analyzer returned `pass=false` for product/readiness gates. The route
does not satisfy the 8,192-block far-flight checkpoint, by design. Wall
median was `28.94 ms`, there were 14 spikes, and the maximum wall sample was
`586.63 ms` (stream classified). Eye-proxy, empty-world, and EnterLit
dirty-residual gates passed; post-stop convergence failed and dual-lane
readiness failed at max unlit `39`.

The source analyzer saw 3,600 events: 1,800 procedural disk misses and 1,800
procedural commits; there were no disk-read events because the world started
without saved chunks. All commits reported four worker threads and generation
start cap two per frame.

| Procedural source metric | Median | p95 | Maximum |
|---|---:|---:|---:|
| Terrain generation | `125.29 ms` | `186.69 ms` | `260.95 ms` |
| Scheduler queue wait | `7.89 ms` | `129.60 ms` | `16,807.53 ms` |
| Worker-pool queue wait | `0.03 ms` | `124.01 ms` | `271.23 ms` |
| Ready-result wait | `48.41 ms` | `175.11 ms` | `14,609.35 ms` |
| Main-thread apply | `7.71 ms` | `30.12 ms` | `61.06 ms` |
| Request-to-commit total | `240.77 ms` | `446.80 ms` | `17,693.23 ms` |

The large maximums belong to source/ready queue waits, not procedural
generation or worker-pool wait medians. This supports instrumenting scheduler
admission and ready-result ownership before increasing worker count or
changing generation budgets.

Persistence emitted 1,771 queued column saves, 7,262 slice writes, and 43
`discard_incomplete` outcomes. The save backlog peaked at four columns and
ended at zero. None of the 43 discarded coordinates was later queued or
written during this run; `RequestAsyncTerrainColumnSave()` calls
`RemoveTerrainColumnFromDisk()` when `IsTerrainChunkComplete()` is false.
The resulting world contains 7,262 `.cchunk` files and is now a disk-backed
warm replay fixture, not a cold fixture. Replay it once with the same 600
seconds and source trace to measure actual disk hits and whether those 43
coordinates regenerate.

The report's `visible_black_focus_n` median was `5`, maximum `34`, and
blink rate `0.103`; short no-ticket candidates appeared, while
`visible_black_stalled_n`, fully-dark-stalled, and black-sticky remained
zero. The operator currently reports acceptable appearance, but M435 captured
no framebuffer pixels. Treat these as candidate/readiness metrics, not proof
of dark pixels or proof of visual correctness.

Artifacts: [M435 flight report](../../bin/suite_reports/engine_refactor/m435_world_m435_cold_source_trace_20261007.json),
[source report](../../bin/suite_reports/engine_refactor/m435_world_column_source_trace_20261007.json),
raw perf `bin/logs/perf_20261007-001129_3520.jsonl`, INFO trace
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-001126.3520`,
and EnterLit traces `bin/logs/enter_lit_20261007-001132.jsonl` and
`bin/logs/enter_lit_20261007-001206.jsonl`. The wrapper restored the new
world's original `world_data.json` bytes and `users.json` after the run.

## M436 - same-seed disk-backed replay (2026-10-07)

M436 repeated the M435 600-second visible, no-teleport route on
`World_M435_Cold_20261007` after M435 had written its generated columns.
World seed, start `[120,56,56]`, eye Y `70`, yaw `180`, pitch `-30`,
speed scale `1`, and stop settings were unchanged. The executable was still
the Release binary with SHA-256
`66c712da62ffeec9cad98e60301a52461c49cf7cde4fc635164e6ad7c2b7654f`.
Column-source trace remained enabled; pixel, visual-black, and framebuffer
capture remained disabled.

The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`). It recorded 316 periods (314 steady), median speed
`5.19653 blocks/s`, focus `7 -> -185`, 3,072 blocks, stable player Y, zero
blocked movement substeps, and zero ground contacts. Wall median was
`24.31 ms`, effective flying FPS `41.50`, and there were six spikes (maximum
wall sample `434.34 ms`). The analyzer still returned `pass=false` for
product/readiness gates; post-stop convergence and dual-lane/eye-proxy/A24
readiness did not pass. The route intentionally does not satisfy the
8,192-block far-flight checkpoint.

The first M436 EnterLit session recorded first-presentable at `134 ms`;
M435's first cold-generation session recorded it at `28.9 s`. This is a
within-session diagnostic timing, not launch-to-first-frame time, but it is
consistent with saved columns making startup presentable sooner.

This was a disk-backed route, as proven by the source trace: 1,733 disk
coordinates were queued and all 1,733 completed, with zero unmatched disk
requests. The route also encountered 143 coordinates without saved data;
all 143 became procedural commits. The M435 save report had 1,771 queued
columns, so M436 reloaded 1,733 of those; 38 persisted columns were not
requested on the repeated path. The 43 M435 `discard_incomplete` coordinates
were not among M436's 143 procedural misses.

| Disk-column metric | Median | p95 | Maximum |
|---|---:|---:|---:|
| End-to-end column load | `172.87 ms` | `8,983.28 ms` | `9,395.70 ms` |
| Worker-queue wait (sum across slices) | `9.55 ms` | `2,124.21 ms` | `31,464.21 ms` |
| Physical file read | `0.96 ms` | `1.23 ms` | `3.92 ms` |
| Completed-result wait (sum across slices) | `610.18 ms` | `23,376.02 ms` | `44,635.55 ms` |
| Deserialize plus apply | `6.18 ms` | `9.84 ms` | `29.63 ms` |

Disk discovery was `0.016/0.032 ms` median/p95. The actual file reads are
fast; long tails occur in worker queueing and after results become available.
The source trace reported four I/O workers. Its result-wait field sums
vertical slices for each column, so it is not one slice's latency. There were
1,733 queued and 1,733 completed disk coordinates and no unmatched requests
at shutdown.

On the same 3,072-block path, M435 fresh generation had wall median
`28.94 ms`, visible-black focus median `5`, and blink rate `0.103`.
M436's disk-backed pass had `24.31 ms`, median visible-black focus `0`,
and blink rate `0.019`. M436's maximum visible-black focus was `30`, but
black-sticky, no-ticket, and stalled maxima were zero. This is consistent with
the saved corridor behaving better than first-time generation, but both runs
lack framebuffer evidence and therefore cannot prove the visible appearance
or establish causation.

The manifest says `cold_warm_mode=cold` and `warm_protocol=null` because
this invocation did not claim the runner's explicit warm protocol. That field
does not mean there were no files: M436 source events prove persisted
`.cchunk` data was loaded. It also makes no claim about the OS page cache.
Use source outcomes, not this protocol label, to distinguish disk from
procedural data in this pair.

Artifacts: [M436 flight report](../../bin/suite_reports/engine_refactor/m436_world_m435_disk_replay_20261007.json),
[source report](../../bin/suite_reports/engine_refactor/m436_world_column_source_trace_20261007.json),
raw perf `bin/logs/perf_20261007-002815_27528.jsonl`, INFO trace
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-002811.27528`,
and EnterLit traces `bin/logs/enter_lit_20261007-002829.jsonl` and
`bin/logs/enter_lit_20261007-002832.jsonl`. The wrapper restored
`World_M435_Cold_20261007` metadata and user state after the run.

## M437 - full M335 route and async chunk-I/O timing (2026-10-07)

M437 ran the unchanged 2,800-second visible, no-teleport M335 route on
`World_164`, with the same start `[120,56,56]`, eye Y `70`, yaw `180`, pitch
`-30`, speed scale `1`, 20-second stop phase, and 8-second blocked-stop
threshold. It used the Release executable built from `d7c18cb3` (the later
`39ea5c40` commit added only the analyzer and documentation), SHA-256
`33111b36ea8b0b5d2bd6cd1598ac9a089b2b33bb61aef84a675f8fb3b7a9c7e4`.
Async chunk-I/O phase counters were enabled; pixel/framebuffer, visual-black,
column-source, and relight traces were disabled. The GUI was visible.

The process exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`). It recorded 1,401 periods (1,399 steady), 89 frames over
100 ms, focus `7 -> -886`, 14,288 blocks, and 5.10 blocks/s. The 0- and
8,192-block checkpoints were reached. Movement telemetry recorded zero
blocked substeps and zero ground contacts. Internal visible-black focus median
was `0`, maximum `18`; no framebuffer pixels were recorded. The operator
reported that visuals currently look acceptable.

| Band | Periods | Wall median / p95 (ms) | Streaming median / p95 (ms) | Async post median (ms) | I/O drain median (ms) |
|---|---:|---:|---:|---:|---:|
| Near | 327 | 34.95 / 46.43 | 24.39 / 36.76 | 11.13 | 8.27 |
| Mid | 620 | 43.78 / 57.68 | 35.81 / 49.48 | 15.93 | 9.57 |
| Far east | 218 | 47.82 / 65.35 | 42.26 / 57.53 | 18.50 | 9.27 |
| Far west | 236 | 52.65 / 69.13 | 46.13 / 61.62 | 20.99 | 9.96 |

Band medians are broadly comparable with M434; far-west wall p95 rose by
roughly 6 ms and the spike count rose from 37 to 89. Async post and I/O-drain
medians did not show a broad worsening. The subphase values in period rows
are point samples at the end of each period, not interval means; spike rows
are single frames over 100 ms. `world_apply_ms` and `column_finalize_ms` are
nested inside `result_processing_ms`.

The main-thread `SaveColumnLightFlagsIfDirty()` writes the full
`column_light.json` file synchronously. It took more than 10 ms on 79/89 spike
frames, more than 20 ms on 17/89, with a maximum of 102.436 ms. This is a
frequent, directly measured hitch contributor, but not the only source: some
far-west spikes also contained 29-55 ms mesh-emerge work. Queue result selection
exceeded 1 ms on 14/89 spikes; measure lock wait separately from ranking and
dequeue before revisiting queue policy.

The analyzer returned `pass=false` because post-stop missing/effective-hole,
pending/not-ready/focus-dirty falling, and demand-stop convergence gates did
not pass. The readiness debt counters are not pixel evidence. M437 did reach
the far checkpoint and remained collision-free by its movement counters; it
does not diagnose the earlier interactive report of a water fall. `cold` in
the manifest is not proof of storage origin, and source tracing was disabled
for this route.

The repeatable invocation was:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m437_world164_m335_io_service_timing --report bin/suite_reports/engine_refactor/m437_world164_m335_io_service_timing_20261007.json --process-timeout 3000
```

Artifacts: [M437 flight report](../../bin/suite_reports/engine_refactor/m437_world164_m335_io_service_timing_20261007.json),
phase summary `bin/suite_reports/engine_refactor/m437_io_phase_summary_20261007.json`,
raw perf `bin/logs/perf_20261007-010003_36844.jsonl`, and INFO trace
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-014729.36844`.

Retrospective caveat: this invocation set `CUBATARIUM_RELIGHT_AUDIT=0`, but
several engine sites treated variable presence as enabled; this INFO trace has
1,042 `[RelightAudit]` lines. M437's timing and readiness data are therefore
instrumented and are not a clean ordinary-runtime baseline.

## M438 - async light-flags writer, full M335 route (2026-10-07)

M438 repeated the same visible, no-teleport 2,800-second route on
`World_164`, after M437 had exercised the async column-light-flags writer.
The manifest records Release, commit `bc7cb09adc1d2e7552c2aa776deb4c3409b0ebb9`,
clean tree, and executable SHA-256
`e48c4d4df9480abfc3d83ba8fb1ce5e53b97922fe3039833a9d1747ae997c275`.
Route, start `[120,56,56]`, eye Y `70`, yaw `180`, pitch `-30`, speed scale
`1`, stop duration, and blocked-stop threshold matched M437. GUI was visible;
teleport and visual/source/framebuffer traces were off.

Retrospective caveat: `CUBATARIUM_RELIGHT_AUDIT=0` also enabled presence-based
audit checks in this build. The process INFO log contains 74,600
`[RelightAudit]` lines. Treat the global timing distributions, spike counts,
and readiness gates below as instrumented observations; they cannot establish
the ordinary-runtime effect of the async writer. Commit `6771b44e` fixes the
false-value handling, and the M439 manifest records the resolved audit state.

The executable exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`), recording 1,403 periods (1,401 steady), 129 frames over
100 ms, and 14,272 blocks. Median cruise speed was `5.19653 blocks/s`, focus
`[7,3] -> [-885,3]`; player Y stayed at 70. Movement telemetry recorded zero
blocked substeps and zero ground contacts. The 0- and 8,192-block checkpoints
were reached. Internal visible-black focus median was `0`, maximum `18`;
these are not pixel samples. The operator's current visual assessment remains
positive.

| Band | Periods | Wall median / p95 (ms) | Streaming median / p95 (ms) | Async post median (ms) | I/O drain median (ms) |
|---|---:|---:|---:|---:|---:|
| Near | 328 | 28.58 / 40.50 | 17.02 / 29.78 | 3.68 | 1.25 |
| Mid | 622 | 38.76 / 51.48 | 28.63 / 41.97 | 8.03 | 1.41 |
| Far east | 217 | 42.74 / 59.04 | 35.15 / 51.09 | 11.23 | 1.56 |
| Far west | 236 | 45.35 / 60.81 | 37.73 / 52.88 | 12.81 | 1.45 |

Against the matched M437 bands, wall medians improved by about 5.0–7.3 ms;
async-I/O drain medians fell from 8.27–9.96 ms to 1.25–1.56 ms. The
main-thread light-flags snapshot field is a point sample, not writer I/O time.
Its maximum on period-end samples was 3.32 ms. Among 129 individual spike
frames only one exceeded 10 ms (11.77 ms); M437 had 79/89 above 10 ms, with
102.436 ms maximum. This confirms that the synchronous full-file write no
longer drives the common spikes.

The full-run tail nevertheless worsened: 129 spikes versus 89 in M437,
maximum wall sample `505.797 ms` versus `296.277 ms`. The worst M438 frame near
`x=-5,863` spent `490.05 ms` in streaming, `400.39 ms` in relight capture and
apply, and `71.92 ms` in mesh emergence; light-flags save was `0.84 ms`. A
later cluster near `x=-10,700` combined relight/emergence with one
`11.77 ms` point-sampled snapshot. `spike_bucket_counts` classified 111 as
stream, 17 as emerge, and one as other. These timings identify the next
instrumentation target but do not by themselves justify changing budgets.

The analyzer returned `pass=false`. Stop convergence failed for missing and
effective holes, pending/not-ready/focus-dirty fall, and demand-stop. Dual-lane
`unlit_max` reached 24; eye-proxy stale-visual/blink checks and A24's
near-focus-hole check also failed. `unfinished_visual`/void counters are
readiness debt, not proof of blank pixels. M438 did not capture the framebuffer.

The manifest says `cold`, but column-source tracing was disabled, so classify
neither disk nor procedural origin from this route. After shutdown, the writer
left valid `format_version: 1` JSON with 8,742 complete columns (81,062 bytes)
and no `.tmp` file. The runner restored `world_data.json` and `users.json` to
their recorded pre-run SHA-256 hashes.

The repeated invocation was:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m438_world164_async_light_flags_save --report bin/suite_reports/engine_refactor/m438_world164_async_light_flags_save_20261007.json --process-timeout 3000
```

Artifacts: [M438 flight report](../../bin/suite_reports/engine_refactor/m438_world164_async_light_flags_save_20261007.json),
phase summary `bin/suite_reports/engine_refactor/m438_io_phase_summary_20261007.json`,
raw perf `bin/logs/perf_20261007-015927_21788.jsonl`, and final
`bin/worlds/World_164/column_light.json`.

### Reusable analyzer for async chunk-I/O phases

`tools/analyze_async_chunk_io_perf.py` reads a `perf_*.jsonl` file and groups
the established near, mid, far-east, and far-west focus bands. It reports
legacy interval-mean wall/streaming fields separately from the new
async-I/O subphase values. The subphase values in `kind=period` rows are
point-in-time samples from the final frame of each period; they are useful for
frequency and outlier inspection but are not interval means or true per-frame
percentiles. `kind=spike` remains a single-frame record for frames over
100 ms. `world_apply_ms` and `column_finalize_ms` are nested in
`result_processing_ms`, so do not add them to that parent duration.

Example for the live M437 recording:

```powershell
python tools/analyze_async_chunk_io_perf.py `
  bin/logs/perf_20261007-010003_36844.jsonl `
  --out bin/suite_reports/engine_refactor/m437_io_phase_summary.json
```

## M439 - clean full M335 route and census baseline (2026-10-07)

M439 repeated the established visible, no-teleport M335 route on `World_164`
with the Release executable from commit `2cdf80c5`, SHA-256
`8ef9e1c625cb57f3b6a58b11aeb9f66e5c1d9a670b622cb729b8653401869cfd`. The
route settings matched M437-M438: start `[120,56,56]`, eye Y `70`, yaw `180`,
pitch `-30`, speed scale `1`, 2,800-second cruise, 20-second stop, and
8-second blocked-stop threshold. GUI was visible, teleport and detailed
visual/source/framebuffer traces were off, and the manifest recorded
`relight_audit=false`. The INFO log had zero `[RelightAudit]` lines.

The process exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`) and manifest acceptance passed. It recorded 1,404 periods
(1,402 steady), 23 frames over 100 ms, 14,320 blocks, focus `[7,3] ->
[-888,3]`, median speed 5.1965 blocks/s, and player Y 70. It reached the
8,192-block checkpoint; blocked movement substeps and ground contacts were
both zero. Median wall time was 39.06 ms and median fly wall time was 38.96
ms. Analyzer `pass=false` (26/39 general gates, 9/12 stop gates) reflects
readiness and convergence thresholds, not a process failure. User appearance
feedback was positive; no framebuffer pixels were captured.

| Band | M439 wall median / p95 (ms) | Streaming median / p95 (ms) |
|---|---:|---:|
| Near | 27.02 / 35.67 | 15.28 / 23.83 |
| Mid | 38.36 / 48.96 | 27.00 / 39.43 |
| Far east | 42.51 / 53.87 | 34.67 / 45.70 |
| Far west | 46.37 / 57.64 | 38.84 / 49.28 |

The phase census is stored in `m439_emerge_census_summary_20261007.json`.
Per-census cost is estimated by dividing period-average cost by the
period-average sample count; the ratio is a period mean, not a percentile of
individual calls. Far-west total census cost was about 10.92 ms median and
13.20 ms p95. The largest direct sampled census frame was about 46.99 ms at
`focus_cx=-775`. M439 did not yet split the census into per-component costs,
so this outlier was not attributed to a specific scan. This was diagnostic
overhead and did not justify changing demand policy.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m439_world164_clean_audit_relighttelemetry --report bin/suite_reports/engine_refactor/m439_world164_clean_audit_relighttelemetry_20261007.json --process-timeout 3000
```

Artifacts: [M439 flight report](../../bin/suite_reports/engine_refactor/m439_world164_clean_audit_relighttelemetry_20261007.json),
phase summary `bin/suite_reports/engine_refactor/m439_io_phase_summary_20261007.json`,
census summary `bin/suite_reports/engine_refactor/m439_emerge_census_summary_20261007.json`,
raw perf `bin/logs/perf_20261007-030343_8152.jsonl`, and INFO trace
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-030339.8152`.

## M440 - incremental emerge-stage census (2026-10-07)

M440 repeated the same full M335 route with the Release executable from commit
`d96a52be`, SHA-256
`709a45adfaa9402b138f8aff1cca7aaa5b451f2857e22aaa34c255f46de9d9fa`. The
change maintained Lighting/Meshing/RenderReady census counts as stage records
changed, avoiding a full `ColumnEmergeStates` map walk on each 250 ms sample.
It did not alter chunk scheduling, publication, or render policy. GUI was
visible and all optional visual, source, and relight traces were off; manifest
acceptance passed and `[RelightAudit]` count was zero.

The process exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`). It recorded 1,404 periods (1,402 steady), 29 frames over
100 ms, 14,304 blocks, focus `[7,3] -> [-887,3]`, and about 5.2 blocks/s.
The 0- and 8,192-block checkpoints were reached. Movement telemetry again
recorded zero blocked substeps and ground contacts. Median wall time was
39.77 ms; analyzer `pass=false` (25/39 general gates, 10/12 stop gates).
This differs from M439 by normal route variance and is not a performance
claim. User appearance feedback remained positive; this run also captured no
framebuffer pixels.

The incremental emerge-stage census was not the dominant measured cost. Its
far-west total was about 10.74 ms median / 12.81 ms p95; demand breakdown was
10.59 / 12.68 ms. A directly sampled far-west frame reached 31.18 ms total
census, 30.61 ms of which was demand breakdown. Commit `48d6a4c7` changes
that read to a cached counter updated on record transitions; M441 measures
that follow-up on the same route. M440 also contained one isolated
approximately 94 ms transparent-pass sample near the far route, with 682
batches and a changed sort revision. This does not prove the sort caused it.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m440_world164_incremental_emerge_census --report bin/suite_reports/engine_refactor/m440_world164_incremental_emerge_census_20261007.json --process-timeout 3000
```

Artifacts: [M440 flight report](../../bin/suite_reports/engine_refactor/m440_world164_incremental_emerge_census_20261007.json),
I/O phase summary `bin/suite_reports/engine_refactor/m440_io_phase_summary_20261007.json`,
census summary `bin/suite_reports/engine_refactor/m440_emerge_census_summary_20261007.json`,
raw perf `bin/logs/perf_20261007-040227_17840.jsonl`, and INFO trace
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-040224.17840`.

### Emerge census analyzer

`tools/analyze_emerge_census_perf.py` groups census measurements by the
established near, mid, far-east, and far-west bands. For `kind=period` rows it
divides average cost by average census count to estimate mean cost per census;
that is not a percentile of individual calls. `kind=spike` rows report direct
single-frame cost and are sorted by total census duration. Fields absent from
older perf files are omitted from the band summary.

```powershell
python tools/analyze_emerge_census_perf.py `
  bin/logs/perf_20261007-040227_17840.jsonl `
  --out bin/suite_reports/engine_refactor/m440_emerge_census_summary.json
```

## M441 - cached demand census full M335 route (2026-10-07)

M441 measured commit `bfe8f5f5` with Release executable SHA-256
`f1aa8077b47ffe89144aca47319a787fb29a58e5a3ba47ca1d1627c7c7715fbf`. It
repeated the visible, no-teleport M335 route on `World_164`: start
`[120,56,56]`, eye Y `70`, yaw `180`, pitch `-30`, speed scale `1`, 2,800-second
cruise, 20-second stop, and 8-second blocked-stop threshold. Visual, source,
relight, and framebuffer traces were disabled. The INFO log contained zero
`[RelightAudit]` lines. Manifest acceptance passed; `cold_warm_mode=cold`
does not identify whether individual columns were loaded from disk or
generated, because source tracing was disabled.

The GUI process exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`). It recorded 1,403 periods (1,401 steady), 29 frames over
100 ms, 14,304 blocks, focus `[7,3] -> [-887,3]`, and 5.1965 blocks/s. It
reached the 8,192-block checkpoint and recorded zero blocked movement
substeps and ground contacts. Median wall time was 40.95 ms, including 40.88
ms during cruise. The largest wall sample was 295.94 ms at route start; the
largest later samples were 119.65 ms at `focus_cx=-385` and 118.42 ms at
`-653`. The run wrapper returned code 1 because the analyzer's product gates
failed; this was not an application or flight-process failure.

| Band | Wall median / p95 (ms) | Streaming median / p95 (ms) | Async post-scheduler median / p95 (ms) | I/O drain median / p95 (ms) |
|---|---:|---:|---:|---:|
| Near | 29.26 / 36.57 | 16.92 / 24.46 | 3.48 / 5.79 | 1.52 / 2.89 |
| Mid | 40.02 / 51.18 | 29.40 / 41.80 | 8.45 / 13.33 | 1.73 / 3.74 |
| Far east | 46.19 / 65.46 | 38.58 / 56.33 | 12.87 / 18.21 | 1.95 / 3.92 |
| Far west | 45.98 / 58.05 | 38.54 / 49.48 | 13.72 / 17.47 | 1.77 / 3.31 |

The cached demand-breakdown census reduced its far-west estimate from M440's
10.59 ms median to 0.00107 ms (approximately 9,900 times). In M441, far-west
demand-stop cost was 0.063 / 0.308 ms median / p95; far-east was 0.185 / 0.754
ms. This means the query scan cost was removed, but the overall route does not
show a consistent frame-time gain: near, mid, and far-east medians rose
slightly against M440, while far west fell. Storage source was not recorded,
so do not attribute this mixed change to the cache.

At `focus_cx=-653`, an 118.42 ms frame spent 41.86 ms in mesh emerge, split
into 15.90 ms coordinator and 25.95 ms post-tick telemetry. The stage sampler
accounted for only 0.069 ms and demand breakdown for 0.0007 ms. A separate
far-route spike had 39.11 ms post-tick telemetry. This narrows the next
diagnostic task to the post-tick snapshots/getters; do not adjust queue or
mesh budgets from these observations alone. M441 did not reproduce M440's
isolated 94 ms transparent-pass sample.

The analyzer returned `pass=false` (24/39 general gates and 8/12 stop gates),
including readiness, hole-proxy, wall-time, and post-stop convergence gates.
The operator reports that the current appearance is acceptable. No pixels
were captured, so the report's hole and visible-black values remain internal
renderer proxies rather than evidence of actual blank pixels.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m441_world164_cached_demand_census --report bin/suite_reports/engine_refactor/m441_world164_cached_demand_census_20261007.json --process-timeout 3000
```

Artifacts: [M441 flight report](../../bin/suite_reports/engine_refactor/m441_world164_cached_demand_census_20261007.json),
I/O phase summary `bin/suite_reports/engine_refactor/m441_io_phase_summary_20261007.json`,
census summary `bin/suite_reports/engine_refactor/m441_emerge_census_summary_20261007.json`,
raw perf `bin/logs/perf_20261007-050000_36260.jsonl`, and INFO trace
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-045956.36260`.
These generated artifacts are kept under `bin`; do not add raw perf logs to Git.

Analyzer commands:

```powershell
python tools/analyze_emerge_census_perf.py `
  bin/logs/perf_20261007-050000_36260.jsonl `
  --out bin/suite_reports/engine_refactor/m441_emerge_census_summary_20261007.json

python tools/analyze_async_chunk_io_perf.py `
  bin/logs/perf_20261007-050000_36260.jsonl `
  --out bin/suite_reports/engine_refactor/m441_io_phase_summary_20261007.json
```

## M442 - post-tick attribution full M335 route (2026-10-07)

M442 used the Release executable from commit `b804c940`, SHA-256
`0da2d21e44095def692726b166324404d6d6f137b57051427c414969e0647220`. It
repeated the same visible, no-teleport M335 route on `World_164`: start
`[120,56,56]`, eye Y `70`, yaw `180`, pitch `-30`, speed scale `1`, 2,800-second
cruise, 20-second stop, and 8-second blocked-stop threshold. Source, visual,
relight, and framebuffer traces were off. The manifest was accepted and the
INFO log had zero `[RelightAudit]` lines. The `cold_warm_mode` manifest value
is not disk/procedural provenance when source tracing is disabled.

The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`); the runner's code 1 was due to failed analyzer gates.
The route recorded 1,404 periods (1,402 steady), 10 frames over 100 ms,
14,320 blocks, focus `[7,3] -> [-888,3]`, speed 5.19653 blocks/s, and reached
the 8,192-block checkpoint. Blocked movement substeps and ground contacts
were zero. Median wall time was 38.87 ms, with 38.82 ms during cruise. The
303.27 ms maximum was at startup; route maxima were 126.95 ms at `-198`,
108.54 ms at `-691`, and 107.90 ms at `-515`. The wrapper restored
`world_data.json` and `users.json` after the run.

The Release build adds six `mesh_emerge_post_*_ms` fields to JSONL:
`stage_sample`, `gpu_counts`, `mesh_snapshot`, `capture_store`, `tail_snapshot`,
and `unattributed`. These are per-frame closeout durations; period rows are
period means and spike rows are individual frames. The stage-sample wrapper
includes work before the rate-limited 250 ms census, while
`column_emerge_stage_sample_ms` times the census itself.

| Band | Post-stage sample median / p95 (ms) | Other measured snapshots combined, median (ms) | Async post-scheduler median / p95 (ms) | Wall median / p95 (ms) |
|---|---:|---:|---:|---:|
| Near | 1.62 / 3.10 | ~0.006 | 3.54 / 6.02 | 28.58 / 37.86 |
| Mid | 6.00 / 8.28 | ~0.010 | 7.93 / 11.09 | 37.86 / 48.73 |
| Far east | 9.15 / 10.54 | ~0.011 | 11.70 / 14.78 | 42.26 / 55.53 |
| Far west | 10.82 / 13.84 | ~0.014 | 13.22 / 16.29 | 44.82 / 61.30 |

Post-stage timing is almost entirely in the first interval: the GPU, mesh,
capture-store, tail, and unattributed fields are each only around 0.001-0.005
ms median. At `-198`, a 126.95 ms frame spent 27.80 ms in
`mesh_emerge_post_stage_sample_ms`, with 0 ms in the internal census timer.
At `-888`, a 100.80 ms frame spent 40.83 ms in that same interval. History
shows why: `5b6cce71` moved `MaintainChunkRenderDemandStore()` to the async
stage and made the post-emerge call telemetry-only; `a5ae29b3` then added the
maintenance call again before the census's 250 ms sampling gate. The duplicate
upkeep is therefore charged every frame, not every 250 ms. M443 removes this
second call and measures the remaining async-stage upkeep directly.

Some large frames have an independent I/O profile. At `-115`, two ~100 ms
frames spent 68.5-80.8 ms in async post-scheduler work; result selection took
30.5-38.5 ms and I/O drain 55.9-65.6 ms. At `-515`, one 107.90 ms frame had
45.65 ms in async post-scheduler work, including 38.00 ms I/O drain and 13.26
ms result selection. These I/O subphases are nested; do not sum them. M442
does not establish a storage-source cause because source events were not
enabled.

Analyzer `pass=false` (26/39 general gates, 9/12 stop gates) reported internal
readiness/hole and frame-time failures. The operator's current visual
assessment is acceptable; `visible_black_focus_n` median was 0. No pixels were
captured, so readiness fields remain proxies and not proof of on-screen
blankness. M442 had slightly lower overall median wall time than M441, but
different persisted world data/source mix is uncontrolled; do not attribute
that difference to the instrumentation or a renderer improvement.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m442_world164_post_telemetry_attribution --report bin/suite_reports/engine_refactor/m442_world164_post_telemetry_attribution_20261007.json --process-timeout 3000
```

Artifacts: [M442 flight report](../../bin/suite_reports/engine_refactor/m442_world164_post_telemetry_attribution_20261007.json),
I/O phase summary `bin/suite_reports/engine_refactor/m442_io_phase_summary_20261007.json`,
census summary `bin/suite_reports/engine_refactor/m442_emerge_census_summary_20261007.json`,
raw perf `bin/logs/perf_20261007-055840_36308.jsonl`, and INFO trace
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-055836.36308`.
Keep the generated raw artifacts under `bin`; do not stage them.

## M443 — single demand upkeep full M335 route (2026-10-07)

M443 measured the removal of duplicate post-emerge demand-store upkeep. It used
the visible no-teleport M335 route on `World_164` with Release executable
SHA-256 `a1a2cb9a38288a9e87c7dfb6a6e95683be0086f1aecdfea32d36156c963ccc49`;
the accepted manifest identifies source `c390532d`. The process succeeded
(`process_rc=0`), while analyzer gates remained red (`pass=false`). It recorded
1,406 periods (1,404 steady), 52 spikes, 14,320 blocks (`focus 7 -> -888`),
5.19653 blocks/s, and zero blocked movement substeps or ground contacts.
Whole-route wall median was 32.37 ms. The distance-growing post-tick closeout
interval fell to 0.086 ms p95. Demand maintenance remained expensive: far-west
period median / p95 / max was 10.037 / 12.843 / 24.347 ms, and the maximum
spike sample was 120.965 ms. This run motivated the bounded traversal change.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m443_world164_single_demand_upkeep --report bin/suite_reports/engine_refactor/m443_world164_single_demand_upkeep_20261007.json --process-timeout 3000
```

Artifacts: report `bin/suite_reports/engine_refactor/m443_world164_single_demand_upkeep_20261007.json`,
raw perf `bin/logs/perf_20261007-065526_34736.jsonl`. The wrapper restores
`world_data.json` and `users.json`, but not the terrain database; source trace
was off. The manifest's `cold` label is not column-source provenance.

## M444 — bounded demand maintenance full M335 route (2026-10-07)

M444 verified commit `38dbc516`'s stable keyed reconciliation cursor and
removal of the second full-map orphan scan. Release executable SHA-256 was
`5af2c387443382b44cd0d6b764f7a0c18b704b9ce72573e50aa10d48ac1bf7ce`.
Manifest accepted; process succeeded (`process_rc=0`); analyzer `pass=false`.
It recorded 1,408 periods (1,406 steady), 26 spikes, 14,304 blocks
(`focus 7 -> -887`), 5.19653 blocks/s, and zero blocked substeps or ground
contacts. Whole-route wall median was 26.028 ms. Far-west demand maintenance
fell to 0.026 ms median, 0.034 ms p95, and 0.109 ms maximum ordinary period;
its maximum spike sample was 1.311 ms. Overall wall-time improvement from M443
is not fully causal because the wrapper does not restore terrain state and
source tracing was disabled.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m444_world164_bounded_demand_maintenance --report bin/suite_reports/engine_refactor/m444_world164_bounded_demand_maintenance_20261007.json --process-timeout 3000
```

Artifacts: report `bin/suite_reports/engine_refactor/m444_world164_bounded_demand_maintenance_20261007.json`,
raw perf `bin/logs/perf_20261007-084709_30824.jsonl`. See the remediation plan
for the focus `-816` GPU-finish outlier and its limits.

## M445 — GPU process profile full M335 route (2026-10-07)

M445 repeated M335 from commit `427f375a`, using the same Release executable
hash as M444. The manifest was accepted, the visible process exited normally
(`process_rc=0`, `hang_killed=false`), and the analyzer returned `pass=false`
(27/39 general gates). It recorded 1,408 periods (1,406 steady), 34 spikes,
14,320 blocks (`focus 7 -> -888`), and 5.19653 blocks/s. Movement collision
counters remained zero. Whole-route wall median was 26.0489 ms. Visual readiness
proxies remained red (`unfinished_visual_rate=1` and `visual_holes_rate=1`),
while visible-black focus median was 0 and maximum 22; there was no framebuffer
capture, so these are not pixel findings.

The diagnostic env flag sampled every eighth eligible
`ProcessPendingGpuMeshes()` call and wrote 30,375 rows. Seven profile samples
exceeded 10 ms and two exceeded 20 ms. Maximum total was 33.281 ms, with
32.467 ms in CPU input preparation; the maximum `quad_finish` subphase was
0.357 ms. One 26.811 ms `KickComputePasses()` sample was not explained by the
currently recorded inner phases. Profile rows have sequence but no timestamp or
chunk coordinate, so they cannot yet be matched exactly to a frame spike.
Profiling overhead makes this a diagnostic run rather than a clean performance
acceptance run.

At the M444 `focus=-816` region, M445's wall maximum across `-812..-820` was
40.19 ms and GPU finish maximum was 7.58 ms; the old 178.68 ms GPU-finish peak
did not recur. Other far-route frames still reached 197.05 ms at `-823`
(60.40 ms mesh-dirty tick, 25.41 ms GPU finish), and 157.42 ms at `-824`
(51.50 ms async I/O drain, including 46.82 ms save drain). A separate
`focus=-635` spike had 95.79 ms GPU kick; at `-704` dirty tick reached 99.12
ms, with snapshot and scheduling intervals around 91 ms. These phase scopes can
be nested and must not be summed. See the plan for the complete interpretation.

Exact invocation (the long-route parameters are unchanged):

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='1'
$env:CUBA_GPU_PROCESS_PROFILE_PATH='E:\Work\Home\Cubatarium\bin\logs\m445_gpu_process_profile_20261007.jsonl'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m445_world164_gpu_process_profile --report bin/suite_reports/engine_refactor/m445_world164_gpu_process_profile_20261007.json --process-timeout 3000
```

Artifacts: report `bin/suite_reports/engine_refactor/m445_world164_gpu_process_profile_20261007.json`,
raw perf `bin/logs/perf_20261007-094250_32276.jsonl`, GPU profile
`bin/logs/m445_gpu_process_profile_20261007.jsonl`, and INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-094247.32276`.
These generated artifacts remain under `bin`; do not stage raw logs.

## M446 — GPU and dirty/snapshot attribution after hibernate (2026-10-07)

M446 used the same M335 arguments and Release EXE from clean commit `5443c6ae`
(SHA-256 `0c9f511243ad5b6c7c6c48e3d1afd9c8dbd9e43d099f8779240a5876d93aed60`).
The process returned normally, but this was not a full-route comparison. INFO
period logging has an `806.08 s` gap from `11:39:05` to `11:52:31` while focus
remained `-473`, consistent with the operator-confirmed hibernate. The app later
ended the configured timed flight at focus `-615`, x=`-9828`: `9952` blocks
traveled versus the full-M335 `14320`; endpoint `-888` was not reached. The
8,192-block coverage checkpoint passed, which is insufficient to establish a
complete M335 run. The harness currently reports success by process exit and
does not invalidate the timed route when sleep consumes part of its duration.

There are 1,000 periods (998 steady) and 510 spike samples. Movement speed proxy
was `5.19287 blocks/s`; blocked substeps and ground contacts were zero. Analyzer
returned `pass=false` (14/39 general and 5/12 stop gates). The `30.78 ms` median
is not directly comparable with M445's `26.05 ms`: M446 had GPU/CPU profiling
enabled and lost about 806 seconds of active travel to hibernate. Visual proxies
remain readiness indicators only; no framebuffer was captured. Column source
tracing was disabled, and terrain storage was not cleared by the wrapper.

The corrected dirty-stage timers show policy pruning is not the broad hot path:
pre-prune median/p95/max `0.322/1.038/13.541 ms`; actual prune
`0.036/0.328/12.663 ms`. Snapshot and scheduling remain more concerning:
schedule median/p95/max `1.929/11.000/85.901 ms`, snapshot
`1.736/9.453/82.747 ms`. At `focus=-477`, dirty tick was `86.63 ms`, schedule
`85.90 ms`, snapshot `82.75 ms`; other 50–70 ms snapshots clustered around
`-474..-479`. These intervals are nested and cannot be summed.

The GPU process profile sampled every eighth eligible call and wrote 17,636
records. 51 rows had `total_ms>10`, 8 had `total_ms>20`; the maximum process
call was `67.57 ms`, including `65.75 ms` revision validation. Maximum kick was
`49.60 ms` for chunk `[-475,1,2]`, almost all CPU occupancy packing
(`49.32 ms`). A second `20.12 ms` kick at `[-488,1,-1]` was mostly block packing.
Maximum `quad_finish` was `0.971 ms`. Async I/O drain median/p95/max was
`1.58/3.86/15.05 ms`; the M445 51.50 ms save-drain event did not recur.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='1'
$env:CUBA_GPU_PROCESS_PROFILE_PATH='E:\Work\Home\Cubatarium\bin\logs\m446_gpu_process_profile_attributed_20261007.jsonl'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --phase-id m446_world164_gpu_process_profile_attributed --report bin/suite_reports/engine_refactor/m446_world164_gpu_process_profile_attributed_20261007.json --process-timeout 3000
```

Artifacts: report
`bin/suite_reports/engine_refactor/m446_world164_gpu_process_profile_attributed_20261007.json`,
perf `bin/logs/perf_20261007-111339_32128.jsonl`, GPU profile
`bin/logs/m446_gpu_process_profile_attributed_20261007.jsonl`, and INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-111336.32128`.
Raw outputs remain under `bin`; do not stage them. For future full-route
comparisons, require the expected endpoint or at least 14,300 travel blocks in
addition to normal process exit.

## M447 — complete M335 with suspend-aware runner (2026-10-07)

M447 repeated the established visible Release/no-teleport M335 profile on
`World_164`, with the hibernate-aware flight clock and a hard
`--minimum-travel-blocks 14300` acceptance gate. It used commit `c7a4f76d`,
EXE SHA-256
`f9d9c84e14baf986c3ada43b445ffa39f6ba7b42e02b88140f248a92d1581098`.
Manifest was accepted; process exit was normal (`process_rc=0`,
`run_outcome=success`, `hang_killed=false`). It reached focus `7 -> -888`,
traveled 14,320 blocks, and measured `5.19653 blocks/s`. Wall and active elapsed
were both `2853.47 s`; no clock gap was excluded. Four predicted obstacle
detours completed; there were no blocked movement substeps, ground contacts,
or collision stop.

The report contains 1,407 periods (1,405 steady) and 63 spike samples. Median
wall was `26.7576 ms`, median flight wall `26.7274 ms`, and median streaming
phase `14.0182 ms`. Startup reached `331.486 ms`. Route spikes included
`68.3369 ms` at focus `-702` (`41.366 ms` streaming phase), `50.7185 ms` near
`-511`, and `53.7772 ms` at endpoint `-888`. The M446 `-474..-479` snapshot
cluster did not recur as a large spike: M447 snapshot was about `3..5 ms` over
`-474..-486`, with wall around `32..37 ms`.

Analyzer acceptance remained FAIL: 28/39 general gates and 9/12 stop gates
passed. `unfinished_visual` was selected as the analyzer's hole key and stayed
nonzero throughout; its `visual_holes_rate=1` is a readiness/work-debt signal,
not pixel evidence. Separate proxies recorded median/max visible-black focus
`0/18`, 36 transitions across 1,405 steady samples, and `near_focus_holes>0`
in 84 route periods (2 in the eye-proxy corridor). There was no framebuffer
capture and no operator visual verdict for M447. Do not describe these values
as proof that every frame or chunk looked blank.

The run report also records 42 seconds of visible-black proxy without pending
light focus, 12 seconds with near-zero relight drain during that proxy, 14
seconds of chain stall, 1,064 cumulative FIFO drops, and 72 false-clear events.
These are candidate lifecycle/ownership signals; the flight does not prove
that FIFO drops caused an image defect. Trace exact coordinates and source,
mesh-publication, and lighting states before changing queue policy.

The wrapper restores `world_data.json` and `users.json`, but does not clear or
restore terrain-column storage. The manifest's `cold_warm_mode=cold` is not
evidence that requested columns were generated rather than read from disk.
Source tracing, relight audit, visual-black tracing, dense pixel trace, GPU
profile, and framebuffer capture were all disabled; the runner did not use
pixel evidence.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m447_world164_suspend_aware_full_m335 --report bin/suite_reports/engine_refactor/m447_world164_suspend_aware_full_m335_20261007.json --process-timeout 7200
```

Artifacts: report
`bin/suite_reports/engine_refactor/m447_world164_suspend_aware_full_m335_20261007.json`,
raw perf `bin/logs/perf_20261007-121505_38948.jsonl`, and flight report
`bin/flight_sim_report.json`. Generated artifacts remain under `bin`; do not
stage them. Use a separate source-trace lane next; do not treat its wall-time
as an uninstrumented performance comparison.

## M448 — M335 column-source trace (2026-10-07)

M448 used the exact visible, fixed-day, no-teleport M335 settings on
`World_164` with only `CUBA_WORLD_COLUMN_SOURCE_TRACE=1`. Release EXE was from
`c7a4f76d`, SHA-256
`f9d9c84e14baf986c3ada43b445ffa39f6ba7b42e02b88140f248a92d1581098`. It
reached focus `7 -> -877`, traveled 14,144 blocks, and measured
`5.19653 blocks/s`; three predicted detours completed, with no blocked
substeps, ground contacts, or collision stop. Active and wall elapsed were
both `2845.79 s`, with no excluded clock gaps. The app returned 0, but the
14,300-block route-completion gate failed by 156 blocks; treat this as a
near-complete source diagnosis, not a full M335 acceptance run.

Source tracing added substantial synchronous logging. The analyzer found
1,405 periods (1,403 steady) and 354 spikes, compared with 63 on uninstrumented
M447. Median wall was `27.7143 ms`; it is not a clean performance comparison.
At focus `-832`, one frame reached `891.346 ms`, including
`async_chunk_io_drain_ms=677.923` and `async_chunk_io_save_drain_ms=822.449`
with five save completions processed. `WorldPersistence.cpp` emits each save
completion through INFO and stderr within the measured loop, so use these
values to identify the affected diagnostic path, not to characterize the
normal flight.

The trace recorded 8,061 disk columns queued and completed, zero unmatched or
out-of-range cancellations, and 9 procedural misses followed by commits near
the unexplored endpoint (`x=-881`, `z=-1..7`). For the X bin `[-836,-820)`
around focus `-832`, all 144 observed columns completed from disk; the earlier
proxy locations `-116`, `-310`, `-361`, and `-643` also fall in disk-loaded
bins. Thus this route mostly used persisted terrain and only generated a few
columns at the frontier. Disk reads were fast: median/p95/max `file_read_ms`
`1.0022/1.6625/38.9546`. Worker queue wait was
`1.413/59.0184/15335.4872 ms`. `result_wait_ms` sums waits across vertical
slices per column: median/p95/max `656.8746/1613.0976/48526.2604 ms`; the
single slowest slice's `result_wait_max_ms` was
`179.2811/393.2978/12132.3458 ms`. Do not add these nested timings.

The route also queued 7,962 unique column saves and wrote 32,543 terrain
slices. All saved columns had their expected slices; no terrain slice write
failure was recorded. Nine lighting metadata updates to `column_light.json`
failed replacement with `ERROR_ACCESS_DENIED` (`replace_failed: Access is
denied`), while the file was updated by the end of the process. No root cause
for those transient replace failures was proven. Windows replacement behavior
depends on ACLs and compatible sharing for an open target; inspect the exact
file access before changing persistence semantics. See [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)
and [Moving and Replacing Files](https://learn.microsoft.com/en-us/windows/win32/fileio/moving-and-replacing-files).

The code review found that the same four-worker FIFO in `UAsyncChunkIO`
currently accepts disk reads, terrain-slice saves, and disk-index warmup jobs.
M448's 8,061 disk columns and 7,962 saved columns make worker contention a
plausible contributor to read queue tails, separate from the completed-result
wait on the main thread. Next, keep the total ChunkIo worker budget at four,
reserve a load lane and a background save/index lane, and verify with a clean
M335 run. Do not raise the global worker count or the main-thread result-apply
budget at the same time.

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m448_world164_m335_column_source_trace --report bin/suite_reports/engine_refactor/m448_world164_m335_column_source_trace_20261007.json --process-timeout 7200
```

Artifacts: flight report
`bin/suite_reports/engine_refactor/m448_world164_m335_column_source_trace_20261007.json`,
source summary
`bin/suite_reports/engine_refactor/m448_world164_column_source_trace_z3_20261007.json`,
X-bin summary
`bin/suite_reports/engine_refactor/m448_world164_source_by_x_20261007.json`,
perf `bin/logs/perf_20261007-130758_32208.jsonl`, and INFO log
`bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-130753.32208`.
Generated logs and world data stay under `bin`; do not stage them.

## M449 — full M335 after chunk-I/O lane split (2026-10-07)

M449 is the full visible, no-teleport M335 measurement for commit `ab7d2d85`
(Release EXE SHA-256
`0d8212eb7abf240efbcfbe24096f44ee35c873b4a2b556d07a606bb880cfa3e6`). The
I/O code used three load workers plus one save/index worker. The fixed-day
wrapper and route exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`) and restored `world_data.json` and `users.json`.

The flight report records start focus `[7,3]`, end focus `[-886,3]`, travel of
14,288 blocks, and median movement speed `5.19653 blocks/s`. The route gate
required 14,300 blocks, so this run is near-complete but does not pass route
acceptance. It ran for 2,856.24 seconds wall and active time with
`excluded_clock_gap_sec=0` and `excluded_clock_gap_count=0`; there was no
hibernate/suspend pause inside this run. Four obstacle detours started and
completed with zero replans/failures; blocked movement, ground contacts,
collision stop, and heading deviation were all zero.

The report contains 1,406 periods (1,404 steady) and 158 spike events. Median
wall time was `30.42995 ms` (`30.3593 ms` in flight), median streaming phase
`15.57895 ms`, median render total `18.4793 ms`, and effective flight FPS
`32.94`. Analyzer `pass=false`: 25/39 general gates and 10/12 stop gates
passed; route completion missed its minimum by 12 blocks. Compared with M447
on matched M335 conditions, wall median increased from `26.7576 ms` to
`30.42995 ms`, streaming phase from `14.0182 ms` to `15.57895 ms`, and render
total from `11.0142 ms` to `18.4793 ms`. Spike count rose from 63 to 158.

The largest spike event was `676.322 ms` at focus `[-151,3]`, of which the
streaming phase was `658.644 ms` and `mesh_emerge_ms` was `521.637 ms` inside
`TickMeshEmerge`; `async_chunk_io_drain_ms` was `7.6225 ms`. A separate
`585.868 ms` event at focus `[-601,3]` included `411.537 ms` in
`update_streaming_ms`. The event timings identify the affected broad stages,
not a complete attribution for those very large stalls.

Appearance telemetry is mixed. Visible-black blink rate was `0.01853` (26
transitions, longest run 42 periods, max count 18), versus `0.02564` (36
transitions, longest run 27, same max count) on M447. Near-focus
hole-telemetry was positive in 110 periods, versus 84 on M447; the eye-proxy
stop line failed for stale visual debt and stale visual without hole counters.
The analyzer's `hole_key` is `unfinished_visual`, whose nonzero rate reports
readiness/work debt rather than a blank pixel. Framebuffer capture was
disabled, so these are not pixel findings. The post-stop convergence gate
remained false; M447 also failed it.

The black-proxy signal recurred at stable coordinates over saved terrain.
For `focus_cx` in `[-836,-820)`, M449 had black focus in 27/27 periods (median
5, max 9), versus 15/27 on M447 (median 3, max 9). Both had zero
`visible_black_fully_dark_no_ticket_n` in this bin. M448's source trace reports
all 144 columns in `[-836,-820)` as disk-complete; the overall trace's median
`file_read_ms` was about 1 ms. M449 does not support missing disk source as the
cause; active repair and post-load mesh/relight timing remain candidates.

Key artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m449_world164_m335_io_load_lane_20261007.json`
- Preserved app flight report: `bin/suite_reports/engine_refactor/m449_world164_m335_io_load_lane_flight_20261007.json`
- Perf log: `bin/logs/perf_20261007-141440_38260.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-141436.38260`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m449_world164_m335_io_load_lane --report bin/suite_reports/engine_refactor/m449_world164_m335_io_load_lane_20261007.json --process-timeout 7200
```

Raw reports and logs remain under `bin`; do not stage them. M449's full report
and comparison update are in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m449-checkpoint--split-chunk-io-lanes-2026-10-07).

## M450 — stage and framebuffer diagnostic M335 route (2026-10-07)

M450 used the Release executable from `b10fd8b2` (SHA-256
`0cdae339ef8d6a416146d864a21eb3c77ae5b83c99118c024ea766945d1a0df0`) on
`World_164`. It kept the established visible, no-teleport M335 inputs and
captured the framebuffer every 15 seconds. A low 75 ms stage-watchdog
threshold was also enabled, so frame-time values from this run are diagnostic
and should not serve as a clean performance acceptance result. The manifest
records render distance 4 and a distance-fog start ratio of 0.48. Its
`cold_warm_mode=cold` means no warm protocol was requested; with source tracing
off, it does not prove how each route column was populated.

The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`) and the wrapper restored `world_data.json` byte-for-byte
and restored `users.json`. The route gate missed by 316 blocks: focus `7 ->
-867`, 13,984 observed against 14,300 required. The 8,192-block far checkpoint
was reached. Median measured movement speed was 5.19653 blocks/s. No blocked
movement substeps or ground contacts were recorded. The analyzer returned
`pass=false` (25/39 general gates and 7/12 stop gates passed); its separate
route gate also failed.

The 189 captured PNGs are in
`bin/suite_reports/engine_refactor/m450_world164_frames_20261007/`. Frames
73–75 show faint, low-contrast distant shapes through blue haze above water;
frames 139–141 around `focus_cx=-649` show forest/coast transitioning to
water, without an obvious black patch. The user's observation is that slow
silhouette visibility changes have existed for a long time and are unrelated
to these edits. This flight moves between locations, so it cannot explain
whether one fixed distant object changes visibility. Record the silhouettes as
a separate unresolved fog/water/render behavior; these frames do not establish
missing or empty chunks.

The analyzer's `unfinished_visual` readiness signal was positive in all
periods (`holes_rate=1.0`), but its description explicitly says that this is
readiness debt rather than a blank framebuffer. `visible_black_focus_fly_med`
and the mid-corridor `near_focus_holes` median were both zero; symptom
reproduction failed for too little focus-missing/black signal. Whole-route
`near_focus_holes` was positive in 99 periods, while the corridor count was
zero. `draw_oracle_missing_resident_n` peaked at 84 near `-647`; it is still a
readiness census, not an independent pixel or terrain-occupancy test.

The run reported 1,400 periods (1,398 steady), median wall time 32.8237 ms
(32.8504 ms in flight), effective flight FPS 30.44, and 381 spikes. The
largest whole-frame sample was 895.535 ms near `-820`; measured streaming was
79.4 ms and mesh emerge 49.23 ms, with the remainder classified as
unattributed/`other`. The largest isolated async-I/O stall was at `-548`:
390.414 ms wall, 376.462 ms world streaming, 354.492 ms async chunk systems,
and 345.83 ms in the outer `TickAsyncChunkIo` interval. At that point the
load and background job queues and completed-load queue were empty. The
nested selection/apply/requeue timers were near zero, while light-flags save
took 10.887 ms; roughly 335 ms of the outer interval has no current
subphase attribution. Related async drain spikes were 165.918 ms at `-732`,
205.814 ms at `-782`, 180.401 ms at `-810`, and 113.009 ms at `-848`. The
largest `update_streaming` spikes were 169.749 ms at `-217`, and 157.865 ms at
`-806`. These timings are a concrete attribution target; they do not yet
identify file-read latency.

The actual I/O pool counts were 3 load workers plus 1 background worker.
Maximum snapshots were 6 pending/3 active load jobs, 10 pending/1 active
background jobs, 33 completed loads, and 7 completed saves; all corresponding
medians were zero. The relight FIFO drop counter reached 1,122. The cumulative
`RelightFalseClearN` count reached 70; source review shows it is incremented
when cleanup finds no remaining surface band requiring that light obligation,
so the name alone does not prove a correctness bug. One save of
`column_light.json` failed at revision 102102 with `Access is denied`; the
code kept the state dirty and scheduled a retry. This is an isolated metadata
write failure and does not show that terrain reads failed.

Key artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m450_world164_m335_stage_and_pixel_diagnostic_20261007.json`
- Perf log: `bin/logs/perf_20261007-154024_18688.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-154020.18688`
- Stage watchdog: `bin/logs/m450_world164_stage_watchdog_20261007.log`
- PNG frames: `bin/suite_reports/engine_refactor/m450_world164_frames_20261007/`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR='E:\Work\Home\Cubatarium\bin\suite_reports\engine_refactor\m450_world164_frames_20261007'
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH='E:\Work\Home\Cubatarium\bin\logs\m450_world164_stage_watchdog_20261007.log'
$env:CUBA_STAGE_WATCHDOG_THRESHOLD_MS='75'
$env:CUBA_STAGE_WATCHDOG_INTERVAL_MS='25'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m450_world164_m335_stage_and_pixel_diagnostic --report bin/suite_reports/engine_refactor/m450_world164_m335_stage_and_pixel_diagnostic_20261007.json --process-timeout 7200
```

All raw artifacts remain under ignored `bin` data and must not be staged. M450
is also documented in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m450-checkpoint--stage-and-framebuffer-diagnostic-2026-10-07).

## M451 — four load workers, clean M335 route (2026-10-07)

M451 is the capture-disabled comparator for M449. It ran the established
visible, no-teleport M335 route on `World_164`, with four load workers and one
background worker. The manifest matched M449's route hash, world metadata,
effective render config, user settings, fog-off setting, and speed scale. It
completed 14,304 blocks at median speed 5.19653 blocks/s. The app exited
normally and the route gate passed. The analyzer returned `pass=false` (27/39
general and 9/12 stop gates), so this is route completion, not product
acceptance.

Frame median was 30.96 ms versus 30.43 ms in M449; streaming median was 16.02
ms versus 15.58 ms. M451 reported 134 spikes, fewer than M449's 158, while its
largest whole-frame sample was 1,177.5 ms versus 676.32 ms. Its largest spike
was mostly outside measured subphases. Completed-result selection had a 6.91
ms p95 and 95.35 ms maximum, and requeue had a 12.82 ms p95 and 68.44 ms
maximum across period and spike rows. Those timers include mutex wait. Source
review found that partial drain/requeue can compact an unbounded queue despite
free ring slots; the next pass should measure queue lock wait separately and
verify that avoiding this compaction reduces the tails.

M451 recorded no framebuffer frames. The user reports that the world currently
looks good, without visible black or empty chunks. Intermittent readiness and
visible-black counters are therefore recorded as diagnostic signals only.
The slow distant silhouettes through fog or water are longstanding and
unexplained; M335 disables `FogPullIn`, so this run cannot establish their
cause.

Key artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m451_world164_m335_four_load_one_background_clean_20261007.json`
- Perf log: `bin/logs/perf_20261007-164247_23548.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-164240.23548`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m451_world164_m335_four_load_one_background_clean --report bin/suite_reports/engine_refactor/m451_world164_m335_four_load_one_background_clean_20261007.json --process-timeout 7200
```

Raw output remains under ignored `bin` data and must not be staged. M451 is
also documented in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m451-checkpoint--four-load-workers-clean-full-m335-route-2026-10-07).

## M452 — completed-result queue ring optimization, clean M335 route (2026-10-07)

M452 is the capture-disabled follow-up to M449 and M451. It ran on
`World_164` using the Release executable from `ed9201d9` (SHA-256
`029968c9e098bf889e567d456af509f637c7088c7fd9795867364040a2b56ae5`), with
four load workers and one background worker. Route/configuration inputs and
M335 protocol match the prior lane. It completed 14,304 blocks (`focus_cx=7
-> -887`) at median speed 5.19653 blocks/s; the route gate passed and the
process exited normally. The analyzer returned `pass=false` (27/39 general,
9/12 stop gates). No framebuffer capture was enabled. The user reports that
the current world looks normal, without visible black or empty chunks.

The bounded ranking and ring-slot reuse implementation improved the normal
path. Report median frame time was 26.61 ms and median streaming time was
13.46 ms, compared with 30.43/15.58 ms in M449 and 30.96/16.02 ms in M451.
On raw period/spike rows, M452 had 80 spike rows and 443.19 ms maximum wall
time; M449 had 158/676.32 ms and M451 134/1,177.5 ms. Result-selection p95
was 0.03 ms and requeue p95 0.01 ms, down from 6.91/12.82 ms in M451.
The analyzer's `unfinished_visual`, `near_focus_holes`, and visible-black
proxies remain telemetry only; this no-capture run is not pixel evidence.

Important residuals by focus band:

- At `focus_cx=-163`, outer `async_chunk_io_drain_ms` was 407.42 ms, but
  cancellation (0.001), selection (0.032), result processing (3.911), requeue
  (0), save drain (2.891), and light-flags save (0.465) account for only
  about 7.30 ms. Approximately 400 ms remains unassigned inside the call.
- At `-195`, outer drain was 90.50 ms; selection mutex wait was 60.51 ms and
  requeue mutex hold was 28.69 ms. A separate requeue at `-463` held the
  mutex 9.63 ms. The ring can still compact when producers refill freed slots
  before the consumer's requeued batch arrives.
- At `-193`, selection wait reached 41.03 ms. At `-687`, it reached 66.36
  ms while the measured selection critical section held the mutex only
  0.035 ms. This points to wait/descheduling or another queue-lock owner,
  rather than ranking cost alone. Producer push-lock timing is not yet
  measured.
- Other high drain rows were dominated by result processing at `-194`,
  `-531`, and `-258`, and save-result cleanup at `-200`. These are separate
  from the unassigned `-163` event and the queue lock symptoms.

The corresponding M449 hotspots were not repeated at the same scale in the
same distance regions: M452's `-505..-480` band had at most 2.50 ms world
apply and 2.72 ms result processing; `-555..-540` had at most 3 ms async-I/O
drain; `-625..-595` had 2.55 ms max async-I/O drain; `-744..-726` had 2.36
ms. Separate whole-frame stalls still appeared with low async-I/O cost, such
as `-729` and `-791..-772`; for example the latter included a 68.29 ms
`update_streaming_ms`. The result queue change is beneficial for common-case
cost but does not resolve every intermittent frame stall.

Artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m452_world164_m335_completed_result_queue_ring_20261007.json`
- Perf log: `bin/logs/perf_20261007-174613_4508.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-174609.4508`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m452_world164_m335_completed_result_queue_ring --report bin/suite_reports/engine_refactor/m452_world164_m335_completed_result_queue_ring_20261007.json --process-timeout 7200
```

Raw output remains under ignored `bin` data and must not be staged. M452 is
also documented in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m452-checkpoint--bounded-completed-result-queue-work-2026-10-07).

## M453 — queued chunk handles, clean full M335 route (2026-10-07)

M453 ran the established visible, no-teleport route on `World_164` using
Release commit `8482aed10db9c41c2de1a00b5898a4d23d5917d0` (executable
SHA-256 `eaabe85b7efa808a493295116f9df82506d8b31641f233a58cc646f318775aea`).
The route completed 14,304 blocks (`focus_cx=7 -> -887`) and its distance and
speed gates passed (`movement_speed_fly_med=5.19653`). The app exited
successfully (`process_rc=0`, `run_outcome=success`, `hang_killed=false`). The
flight wrapper returned code 1 because product acceptance failed 25/39
general gates and 8/12 stop gates; this is separate from app and route
completion. No frames were captured. The user reports that the world
currently looks normal. Analyzer hole/readiness and visible-black counters
are not framebuffer proof.

Report median frame time was 28.1772 ms, median streaming time 13.9223 ms,
and there were 82 spike rows with a 429.728 ms maximum. M452 had 26.61 ms,
13.46 ms, 80 spikes, and 443.19 ms max. The M453 queue instrumentation
showed small locks: across spike rows, selection wait p95/max was
0.0006/0.0008 ms, requeue wait max 0.0027 ms, and producer-push wait/hold
maxima were 0.0124/0.0234 ms. The M452 multi-tens-of-milliseconds queue
waits were not repeated. This supports keeping the ring and ranking
optimization, while moving the next fix to other work on the frame.

Notable per-frame spikes from `kind=spike` rows:

- `focus_cx=-275`: 233.1 ms wall, 147.55 ms streaming, 95.96 ms
  `update_streaming_ms`, including 82.70 ms `streamer_unload_ms`.
- `-86`: 163.64 ms wall and 62.94 ms `streamer_update_ms`.
- `-261`: 172.12 ms wall; async pre-scheduler was 68.54 ms while async I/O
  drain was 0.02 ms.
- `-80`: 229.13 ms wall; `light_flags_save_ms` was 29.71 ms even though the
  actual metadata serialization/file write runs on its own worker.
- `-237`: 155.48 ms wall; `streamer_update_ms` was 53.47 ms and I/O
  save-result drain was 26.74 ms.
- `-576`: 201.09 ms wall; one loaded chunk took 16.89 ms in world apply.
- `-73`: I/O drain was 48.28 ms; measured result application 1.64 ms, save
  result drain 15.79 ms, light-flags submission 5.99 ms, and an internal
  remainder of 24.84 ms. Queue locks were negligible.
- `-160`: largest whole frame, 429.73 ms, included 230.29 ms swap wait and
  199.37 ms simulation. Its 89.22 ms streaming phase explains only part of
  the event, so do not label the full stall as a streaming hitch.

Source inspection after the run found why the unload path can still stall:
`UChunkStreamer` calls the persistence save callback before removing a
column. That callback invokes `RequestAsyncTerrainColumnSave`, which queues
file writes but calls `UChunkStorageService::SerializeChunk` synchronously
for each slice before enqueueing the worker. It also performs completeness
and highest-slice scans and synchronously removes stale higher slices. The
next save refactor should copy an immutable `UChunk` snapshot on the world
thread, then serialize, write, and clean up on a worker. The worker must be
fenced before world/persistence teardown. This is consistent with
[Godot Voxel Tools' asynchronous block-stream save design](https://github.com/Zylann/godot_voxel/blob/master/doc/source/streams.md)
and its [threaded task/result phase](https://github.com/Zylann/godot_voxel/blob/master/engine/voxel_engine.cpp);
the application to our ownership model is an inference from those sources
and our current code.

The period rows average only selected session fields; new inner-tick fields
are last-frame values in `kind=period` rows. For direct attribution use
`kind=spike` rows, and do not compare a period-average outer drain to a
last-frame inner tick. Add matching accumulators before making period-level
claims from these new fields.

Artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m453_world164_m335_queued_buffer_handle_20261007.json`
- Perf log: `bin/logs/perf_20261007-201931_35124.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-201926.35124`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m453_world164_m335_queued_buffer_handle --report bin/suite_reports/engine_refactor/m453_world164_m335_queued_buffer_handle_20261007.json --process-timeout 7200
```

Raw output remains under ignored `bin` data and must not be staged. M453 is
also documented in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m453-checkpoint--queued-chunk-handles-and-full-m335-route-2026-10-07).

## M454 — immutable snapshots, worker serialization, clean full M335 route (2026-10-07)

M454 ran the same visible, no-teleport M335 route on `World_164`, with the
same route hash as M453 (`024a223f5827926f85b7ccfb030d9456d2515e1373f35faba620ce59e040c312`).
It used the Release executable from commit `776c48e1`
(`AD9DA6B91DCC8689DDC3CEE7CB806333E835F93BE4EBA41D8F0FBEF2CC439443`).
The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`); route completion passed at 14,320 blocks and
`focus_cx=7 -> -888`, above the 14,300-block minimum. The wrapper returned
1 because analyzer acceptance remained false (27/39 general gates, 9/12
stop gates; post-stop convergence failed). Those internal visual-readiness
proxies are not framebuffer evidence. M454 captured no frames; the operator
had reported that the world looked good.

The targeted code change copies each chunk to an immutable main-thread
snapshot, performs serialization and atomic file writes on the background
I/O pool, moves legacy-JSON cleanup to that worker, and updates the disk-slice
index on the main thread when the save result is consumed. Common-route
metrics improved relative to M453: median fly frame time was 25.674 ms vs
28.138 ms; median streaming phase was 12.186 ms vs 13.922 ms; spike rows
fell from 82 to 22. M454's `async_chunk_io_drain_ms` spike maximum was
5.08 ms vs 61.80 ms in M453, and save-result drain maximum was 0.145 ms vs
49.07 ms. The background queue briefly reached 21 pending tasks, but it
drained and did not remain continuously backlogged.

The former M453 unload hotspot near `cx=-275` (82.70 ms) did not repeat in
M454. The prior `cx=-576` world-apply stall also did not repeat. A later audit
cross-checking the linked M454 raw JSONL found that the previously reported
late-route values of 76.99/106.44 ms at `cx=-878/-883` do not occur in that
log. M454's route-wide `streamer_unload_ms` maximum was 12.758 ms at
`cx=-280`; its maximum `UpdateStreaming` was 26.582 ms at `cx=-150`. Thus this
run does not establish a new tail unload or streaming stall. The M455 trace
did independently confirm synchronous stale-path deletion as a source of
unload hitches (see below).

Other analyzer metrics changed in both directions: `dirty_med/max` was
109/395 vs 121/427 and `fly_visible_black_max` was 18 vs 25, while
`fly_void_near_max` increased from 1,084 to 1,201. These are internal
telemetry counters, not proof of rendered blank pixels. Keep the user's
visual observation separate from the product analyzer gates.

Artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m454_world164_m335_async_save_serialize_20261007.json`
- Perf log: `bin/logs/perf_20261007-212333_31428.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-212329.31428`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m454_world164_m335_async_save_serialize --report bin/suite_reports/engine_refactor/m454_world164_m335_async_save_serialize_20261007.json --process-timeout 7200
```

Raw output remains under ignored `bin` data and must not be staged. M454 is
also documented in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m454-checkpoint--immutable-save-snapshots-corrected-unload-measurements-2026-10-07).

## M455 — unload phase trace on the full M335 route (2026-10-07)

M455 ran the unchanged visible, no-teleport M335 route on `World_164` with
`CUBA_WORLD_COLUMN_SOURCE_TRACE=1`, using Release executable from commit
`cdbe58de` (SHA-256
`3efb4b6bf222ed68ac61e71a65fc106d58b90a5bb8c96501997e06e19b3d6f24`). The
route hash remained
`024a223f5827926f85b7ccfb030d9456d2515e1373f35faba620ce59e040c312`.
The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`) after 14,320 blocks (`focus_cx=7 -> -888`), above the
14,300-block minimum. The wrapper returned 1 because analyzer acceptance was
false (28/39 general gates and 9/12 stop gates); post-stop convergence did
not pass. No framebuffer capture was enabled. These visual-readiness and
visible-black values are telemetry proxies, not pixel evidence; the operator
had previously reported that the world currently looked good.

The phase trace identified a remaining synchronous filesystem operation in
the unload save callback. `RequestAsyncTerrainColumnSave` removes stale slice
files (`cy > highest_to_save`) on the main thread. Although completeness scan,
disk-index lookup, highest-nonair scan, materialization, and snapshot enqueue
were each below 0.1 ms in the cited cases, stale-path cleanup took:

| Column | Stale slices | Main-thread cleanup |
| --- | ---: | ---: |
| `(-6, 0, 4)` | 5 | 36.07 ms |
| `(-319, 0, 1)` | 5 | 26.30 ms |
| `(-417, 0, 0)` | 5 | 48.94 ms |
| `(-418, 0, 4)` | 5 | 47.78 ms |
| `(-436, 0, 4)` | 5 | 67.56 ms |

At `(-436, 0, 4)`, the full save callback took 68.25 ms, matching the 67.56
ms stale cleanup. The four other subphases together were under 0.14 ms. This
directly confirms filesystem deletion as a source of unload hitches. This is
independent evidence: the large aggregate unload tail previously attributed
to M454 is absent from that run's raw log. The incomplete-column
branch also performs a synchronous full-column deletion and must be included
in the same ownership-safe refactor.

With tracing enabled, diagnostic log writes themselves add variable callback
time: for example, the measured internal unload-column phases sum to 0.38 ms
at `(-634, 0, 1)` while the wrapped callback took 12.27 ms. Treat wrapper
durations from M455 as instrumented values; the timed filesystem-removal
subphase remains direct evidence. M455's analyzer reported 26 spike rows,
median fly frame 25.33 ms, median world-streaming phase 12.44 ms, and maximum
frame 295.57 ms. Those aggregate values are not a clean comparison because
the trace writes synchronously.

Artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m455_world164_m335_unload_phase_trace_20261007.json`
- Perf log: `bin/logs/perf_20261007-221940_24328.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-221936.24328`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m455_world164_m335_unload_phase_trace --report bin/suite_reports/engine_refactor/m455_world164_m335_unload_phase_trace_20261007.json --process-timeout 7200
```

Raw output remains under ignored `bin` data and must not be staged. The
result and the updated next step are recorded in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m455-checkpoint--synchronous-stale-slice-deletion-confirmed-2026-10-07).

## M456 — async stale-slice cleanup, full M335 verification (2026-10-08)

M456 ran the same visible, no-teleport M335 route on `World_164` with source
tracing and framebuffer capture disabled. The Release executable was built
from `58e9bc6b9709b6de1c8489669a8a2304f1c61961` (SHA-256
`2f6136a2068497f63840e59d355dcd6232c24a37ae997594588a0f06da8be6b6`). The
route hash remained
`024a223f5827926f85b7ccfb030d9456d2515e1373f35faba620ce59e040c312`.
It reached 14,320 blocks (`focus_cx=7 -> -888`), passed route adequacy, and
exited normally (`process_rc=0`, `run_outcome=success`, `hang_killed=false`).
The wrapper returned 1 because analyzer acceptance was false: 29/39 general
gates and 9/12 stop gates passed. There was no framebuffer capture; counters
are readiness proxies, not pixel evidence.

The M455 traced run had confirmed synchronous stale-path cleanup directly:
the slowest cleanup removed five stale paths in 67.56 ms, and the full save
callback took 68.25 ms. Its raw log's `streamer_unload_ms` maximum was
73.557 ms at `cx=-450`, but synchronous tracing inflated enclosing timings.
M456's raw log had `streamer_unload_ms` max 0.571 ms, consistent with the
cleanup fix removing that known main-thread work.

A raw-log audit corrected the earlier M456/M454 streaming outlier report.
M456 samples at `focus_cx=-763` were about 2.52 ms in `UpdateStreaming`, not
76.97 ms; its route-wide maximum was 27.474 ms at `cx=-594`. M454 samples
near `cx=-756` were about 3.39 ms, not 49.27 ms; its route-wide maximum was
26.582 ms at `cx=-150`. The claimed 101.328 ms M456 frame and related
cross-run comparison do not match the cited raw JSONL files. Code inspection
did find the unbudgeted readiness scans described below, but these runs do
not establish them as the cause of a large stall.

The broad timer does not identify the individual caller. Inspection of
`UpdateStreaming` found an unconditional moving catch-up readiness walk, an
R=4 enter-visibility walk every four fast frames, and an R=8 diagnostic walk
every eight frames. Focus changes invalidate the visual cache, so the first
walk can perform a complete cache rebuild. M456 did not have subphase timers
for these calls; this is a plausible attribution, not proof of how much each
call contributed.

The follow-up patch uses the prior async-cycle `PostLoadRingNotReady` sample
for the moving catch-up decision, retaining at most one frame of latency. It
limits the R=4 debt scan to active enter/burst/soft-force latch work, removes
the telemetry-only R=8 scan from the runtime loop, and records catch-up and
enter-debt call time plus sample-validity bits. This bounds recurring policy
work; M456 did not measure the alleged 76.97 ms tail, so M457 cannot be treated
as a validation of that specific stall's cause.

Run summary:

- Report-level `wall_ms_fly_med=24.778`; `world_streaming_phase_ms=12.1834`.
- The linked raw period log has wall median 24.806 ms and streaming-phase
  median 12.198 ms. Its largest wall sample was startup at 294.467 ms; its
  route-wide `UpdateStreaming` maximum was 27.474 ms at `focus_cx=-594`.
  The previously reported late-route rows at 100.492/101.328 ms do not match
  this raw log and are withdrawn.
- Analyzer gates: 29/39 general, 9/12 stop; post-stop convergence failed.
- The operator had reported that the world looked good. No screenshots were
  collected, so the run does not prove a pixel-level visual result.

Artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m456_world164_m335_async_stale_slice_cleanup_20261007.json`
- Perf log: `bin/logs/perf_20261007-232332_37432.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261007-232329.37432`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m456_world164_m335_async_stale_slice_cleanup --report bin/suite_reports/engine_refactor/m456_world164_m335_async_stale_slice_cleanup_20261007.json --process-timeout 7200
```

Raw output stays under ignored `bin` data and must not be staged. M456's
analysis and next action are also recorded in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m456-checkpoint--asynchronous-stale-slice-cleanup-verified-cruise-scans-bounded-2026-10-08).

## M457 — catch-up probe budget check on the full M335 route (2026-10-08)

M457 verifies commit `cc75e339` on the unchanged visible, no-teleport M335
route. The Release executable SHA256 was
`51A3BF1471B6B515DF7BE2A849451692E682A621C65B05C708FD2CB3432B803B`.
Tracing, screenshot capture, GPU process profiling, and stage watchdog were
disabled to preserve the established performance lane.

The 1,408-period run passed manifest acceptance and route adequacy, traveling
14,320 blocks from `focus_cx=7` to `-888` at median 5.19653 blocks/s and eye
height 70. The app exited normally (`process_rc=0`, `run_outcome=success`,
`hang_killed=false`). The wrapper returned 1 because analyzer acceptance was
false: 27/39 general gates and 9/12 stop gates passed. The route's measured
median frame was 26.411 ms (37.95 FPS); the streaming phase median was 12.735
ms. `streamer_update_ms` was 0.0104 ms median / 0.0284 ms p95 / 7.663 ms max;
`update_streaming_ms` was 1.8291 ms median / 4.1322 ms p95 / 26.4119 ms max.
The maximum wall frame was 301.013 ms at route entry, but its streaming phase
was 25.1715 ms and `UpdateStreaming` was 5.2865 ms. M457's
`update_streaming_ms` median/p95/max were 1.8291/4.1322/26.4119 ms, versus
2.170/6.265/27.474 ms in M456. Overall wall and streaming-phase medians were
higher in M457 (26.411 vs 24.806 ms and 12.735 vs 12.198 ms), so this is not
an overall performance win or causal proof. The earlier 121 ms core-streamer
and disk-slice-discovery attribution is not present in the linked M457 JSONL
and is withdrawn.

The analyzer names `unfinished_visual` as its hole signal and reports its
semantics explicitly: it is a visual-readiness/debt count, not framebuffer
evidence. It stayed nonzero in every steady sample (median 27, maximum 83),
which produces `effective_holes_rate=1.0` and a red generic holes gate. The
route-level symptom gate did not reproduce its required black/missing signal
(`focus_missing_mesh` median 0; visible-black median 0). In the covered
mid-corridor, `near_focus_holes`, `visual_holes`, and stale-dark-near counters
were zero. No screenshot or pixel/object-ID witness was captured.

The far endpoint has a narrower investigation lead: at `focus_cx=-877..-888`,
the ring-level `visible_black_stale_lit_n` was nonzero in 27 samples (1–7
columns). It remained 6 through the 12 sampled stop periods while oldest stale
vertex-light age rose to 623 frames. At `cx=-888`, the nearest-dark-face probe
found 70 void-light faces and 0 stale-light faces within its local radius;
those nearby legal-dark faces do not establish whether the separate
ring-level stale meshes entered the camera view. At stop end, dirty geometry
was about 140 and pending light about 1. Stop gates failed missing-zero,
readiness-hole-zero, and falling-dirty checks, while pending/not-ready counts
fell. Do not merge these internal counters with the user's long-standing
fog/water silhouette behavior without a fixed-camera visual witness.

Artifacts:

- Analyzer report: `bin/suite_reports/engine_refactor/m457_world164_m335_catchup_probe_budget_20261008.json`
- Perf log: `bin/logs/perf_20261008-004108_38488.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261008-004104.38488`

Exact invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m457_world164_m335_catchup_probe_budget --report bin/suite_reports/engine_refactor/m457_world164_m335_catchup_probe_budget_20261008.json --process-timeout 7200
```

Raw output remains under ignored `bin` data. M457 findings and the next
diagnostic pass are in
[`ENGINE_REMEDIATION_PLAN_2026-10-03.md`](ENGINE_REMEDIATION_PLAN_2026-10-03.md#m457-checkpoint--cruise-readiness-rescans-bounded-far-end-light-debt-remains-2026-10-08).

## M458 — capture-heavy endpoint attempt (2026-10-08)

M458 kept the M335 world/start/camera/speed/route, but enabled in-app frame
capture every 15 seconds. The app returned 0, yet covered only 14,080 blocks
(`focus_cx=7 -> -873`) against the required 14,300; do not compare this as a
completed long-route result. The extra capture load may have slowed the run,
but this single result does not prove causality. Report:
`bin/suite_reports/engine_refactor/m458_world164_m335_far_endpoint_frame_capture_20261008.json`.

## M459 — targeted far-end frames without capture-heavy flight (2026-10-08)

M459 reran the established visible, no-teleport M335 route with all in-app
capture and source/GPU tracing disabled. A few early manual captures were
discarded because they showed the desktop behind the game; the DPI-aware
window-capture tool was corrected and verified. The endpoint watcher then
captured two approach frames and one stationary frame without adding repeated
capture load to the route. Reusable tools are
[`capture_flight_sim_window.ps1`](../../tools/capture_flight_sim_window.ps1)
and [`watch_m335_endpoint_capture.ps1`](../../tools/watch_m335_endpoint_capture.ps1).

The run completed 14,320 blocks (`focus_cx=7 -> -888`), `process_rc=0`, with
no movement-blocked substeps. It did not pass analyzer acceptance or
post-stop-convergence. The moving frames show heavy blue haze/low-contrast
silhouettes; the stopped endpoint shows lit terrain and trees, without an
obvious black/empty chunk. The frames are at different camera positions and
cannot explain the user's longstanding slow silhouette toggling through
fog/water. The ring's stale-lit proxy reached nine at stop (oldest age 666
frames), but that does not prove those chunks were visible. A separate
nearest-dark-face probe found five stale-light faces at stop, but the capture
does not map them to projected coordinates. At `focus_cx=-198`, one spike row
had `UpdateStreaming=83.6144 ms`, `world_streaming_phase=90.5611 ms`, and
`wall=101.246 ms`, while `streamer_update=0.0099 ms` and
`streamer_unload=0.0025 ms`. The same period's `max_wall_ms=172.032` was a
different frame.

Artifacts:

- Analyzer: `bin/suite_reports/engine_refactor/m459_world164_m335_capture_disabled_full_route_20261008.json`
- Perf log: `bin/logs/perf_20261008-022251_13416.jsonl`
- Frames: `bin/suite_reports/engine_refactor/m459_world164_m335_far_endpoint_frames_20261008/`
- M459's raw artifacts are local ignored `bin` data and are not committed.

Exact route invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m459_world164_m335_capture_disabled_full_route --report bin/suite_reports/engine_refactor/m459_world164_m335_capture_disabled_full_route_20261008.json --process-timeout 7200
```

The endpoint watcher used the perf log above and saved to the `m459_*_frames`
directory. It captured at `focus_cx<=-877`, `<=-883`, then after stationary
samples at the route endpoint.

## M460 — altitude terrain-query timing (completed, 2026-10-08)

`UpdateStreaming` calls `FindTopSolidSurfaceY` for altitude-adaptive fog once
per update. Before M460 its synchronous downward scan from `MaxHeight` had no
dedicated timer. Commit `fe0a9718` adds per-sample `altitude_surface_query_ms`
and period `max_altitude_surface_query_ms` without changing the query or fog
behavior. The app was built with `cmake --build bin --config Release --target
Cubatarium --parallel 8`. M460 was the unchanged M335 route, capture disabled,
with only phase/report names changed. It completed with `process_rc=0`, reached
`focus_cx=-888`, and traveled 14,320 blocks against the 14,300 minimum. The
fixed-day wrapper restored `world_data.json` byte-for-byte and restored the
fog setting. The analyzer's overall result is FAIL because quality and
post-stop convergence gates did not pass; process and route completion passed.
Partial results through `focus_cx=-205` include 17
period records across `-195..-205`, with no spike rows. In the current period
format `update_streaming_ms` is a last-sample snapshot, not an average; these
snapshots range from 1.62 to 7.22 ms. Terrain-query period averages range from
0.017 to 0.025 ms, and the explicit per-period maximum reached 0.387 ms. All four
visible-black counters were zero in this interval. One earlier
9.03 ms query maximum near `focus_cx=-2` is still a single-window observation.
The near-peak interval makes the terrain query an implausible cause of
M459's 83.61 ms single-frame `UpdateStreaming` peak, but does not identify the
source of that isolated outlier. Raw log:
`bin/logs/perf_20261008-033019_35884.jsonl`.

Further M460 progress: one spike at `focus_cx=-207` measured
`wall_ms=131.654`, `async_chunk_pre_scheduler_ms=81.9294`, and
`prep_refresh_miss_ms=81.5154`; `update_streaming_ms` was 1.925 ms and
`streamer_update_ms` 0.0109 ms. `chunk_count=460`, `miss_horiz=4`, and all
visible-black counters were zero. The same miss block includes a full resident
chunk iteration in `HasMissingGreedyMeshInHorizontalRadius`, a screen-ray
repair probe, and an optional nearest-missing search, so the present log does
not isolate the call responsible. A separate spike at `focus_cx=-262` measured
`prep_refresh_facing_ms=27.0451` and `scene_transparent_ms=35.439`; its
`update_streaming_ms` was 0.817 ms. The current source now records separate
miss-radius, screen-ray, nearest-search, and residual timings. M460's Release
binary did not contain those additions; M461 will repeat the same route after
a Release-only rebuild.

M460 completed 1,408 periods (1,406 steady cruise periods) with six spike
frames. The route speed median was 5.197 blocks/s; median flight wall time was
25.96 ms and effective flight rate 38.53 FPS. The largest measured spikes were
292.6 ms during startup (`cx=7`), 144.7 ms at `cx=-175`, 131.7 ms at `cx=-207`,
104.0 ms at `cx=-262`, then 111.7/105.2 ms at `cx=-885/-887`. The `cx=-207`
and `cx=-262` stage attribution above remains the actionable timing result;
the two far-end spikes were dominated by mesh-emerge/streaming-phase work, not
`UpdateStreaming` itself (`1.35/1.43 ms`).

The analyzer reports `unfinished_visual_rate=1.0` and
`effective_holes_rate=1.0`, but its declared `hole_key` is
`unfinished_visual`, a readiness/debt proxy rather than framebuffer pixels.
`visible_black_focus_n` is also a ring census, not screen-ray visibility.
Across the flight, `visible_black_blink_rate` was 2.7%; this does not establish
that a dark patch was on screen. The analyzer found no focus-missing median
signal in the flight segment and marked symptom reproduction as failed for
too-low focus-missing and visible-black inputs. Post-stop convergence also
failed: `post_stop_missing_max=24`, `post_stop_not_ready_end=24`, and
`demand_stop_converged=false`, while black-sticky,
visible-black-no-ticket, and visible-black-stalled stop maxima were zero.
These internal readiness signals remain useful for follow-up, but are not
evidence of a visible hole by themselves.

The endpoint watcher saved three full-window frames:
`bin/suite_reports/engine_refactor/m460_world164_m335_altitude_surface_query_frames_20261008/approach_cx-877.png`,
`bin/suite_reports/engine_refactor/m460_world164_m335_altitude_surface_query_frames_20261008/approach_cx-883.png`,
and
`bin/suite_reports/engine_refactor/m460_world164_m335_altitude_surface_query_frames_20261008/stop_cx-888.png`.
The `-877` frame shows only distant terrain through fog. The `-883` frame has
terrain and trees plus some isolated thin dark marks whose source is not
identified. The stationary `-888` frame shows a coherent, well-lit sandy hill and vegetation through fog,
with no obvious chunk-sized black holes or empty patches. These samples
support the operator's report that the scene currently looks acceptable, but
do not explain the longstanding slow silhouette changes at the fog/water
boundary. The stop image was taken after two stationary periods, not after
full post-stop convergence.

M460's exact route invocation (same M335 settings as M459, with the added
terrain-query timing):

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m460_world164_m335_altitude_surface_query_timing --report bin/suite_reports/engine_refactor/m460_world164_m335_altitude_surface_query_timing_20261008.json --process-timeout 7200
```

## M461 — nested miss and `UpdateStreaming` timing (completed, 2026-10-08)

M461 rebuilt Release after commits `469cb5d2` and `90408242` added nested
miss-path timing and average/maximum `UpdateStreaming` phase fields. The
executable SHA-256 was
`7462ea77dba8c413187192d424a9438d83b127a1c81c7b30261fbb54f3c47add`; it ran
on desktop GL with AMD Radeon(TM) Graphics. M461 repeated the visible,
no-teleport M335 route with in-app capture and source/GPU tracing disabled. It
completed normally (`process_rc=0`, 14,320 blocks, `focus_cx=7 -> -888`, median
speed 5.1965 blocks/s). Route and manifest passed. Analyzer quality remained
red: 27/39 general gates passed, symptom reproduction was false, and stop
convergence was false. `unfinished_visual` / `effective_holes` remain
readiness-debt proxies, not pixel-level holes.

M461 did **not** reproduce M460's `cx=-207` miss-query hitch. Across 32 samples
at `focus_cx=-195..-215`, there were no >100 ms spike frames;
`prep_refresh_miss_ms` peaked at 0.865 ms (radius query 0.021 ms, screen ray
0.825 ms, nearest search 0.019 ms, residual 0.036 ms). `UpdateStreaming`
peaked at 32.15 ms in this interval, with a period wall maximum of 34.43 ms.
The per-segment pre/core/post maxima can come from different frames. The
81.5 ms miss query remains an unreproduced M460 outlier, not a sustained query
cost.

The six M461 spike frames exposed different costs:

- Startup at `cx=7`: 297.437 ms wall.
- `cx=-172`: 131.006 ms wall, including 31.114 ms pre-scheduler pressure
  refresh; `prep_refresh_unfinished_ms=28.647 ms`. The miss query was only
  0.417 ms.
- A second `cx=-172` frame: 103.109 ms wall, `mesh_emerge_ms=38.949 ms` and
  `world_streaming_phase_ms=46.340 ms`.
- `cx=-175`: 100.365 ms wall; pressure refresh took 30.630 ms, including
  29.343 ms in `prep_refresh_ring_resync_ms` and `prep_refresh_sticky_ms`.
  The same frame had 24.329 ms mesh emerge and 29.569 ms opaque GPU scene
  work; these simultaneous timings do not establish causality.
- `cx=-225`: 119.778 ms wall, while the recorded async pre-scheduler,
  `UpdateStreaming`, mesh-emerge, and opaque-GPU stages were small; most of
  this frame remains unattributed.
- `cx=-762`: 149.600 ms wall and 121.044 ms streaming phase; post-scheduler
  async IO was 105.462 ms, including 104.109 ms IO drain and 104.083 ms
  `ProcessColumnLightFlagSaveResults`. At the sample point load-result queue
  depth and load pending/active counts were zero; direct light-flag save work
  was 0.001 ms. Current metrics cannot distinguish result-queue mutex wait,
  time holding that mutex, result processing, or a scheduling pause. This is a
  persistence/IO timing lead, not evidence of terrain-load latency or a
  rendered hole.

The old M446 `cx=-474..-479` hotspot also did not recur: 11 M461 periods had
no >100 ms spike, maximum period wall 35.45 ms, and maximum per-frame
`UpdateStreaming` 17.30 ms. These repeats reduce confidence in both old
single-run hotspots as stable bottlenecks.

The endpoint watcher saved three full-window frames:

- `bin/suite_reports/engine_refactor/m461_world164_m335_miss_path_instrumentation_frames_20261008/approach_cx-877.png`
- `bin/suite_reports/engine_refactor/m461_world164_m335_miss_path_instrumentation_frames_20261008/approach_cx-883.png`
- `bin/suite_reports/engine_refactor/m461_world164_m335_miss_path_instrumentation_frames_20261008/stop_cx-888.png`

The approach images show blue fog and faint terrain; `cx=-883` also has a few
thin dark marks of unknown origin. The stationary endpoint is a coherent lit
hill and vegetation without an obvious chunk-sized hole. It was captured
before stop convergence passed, and these three views do not rule out defects
elsewhere on the route. Keep the user's longstanding slow fog/water silhouette
transitions in their separate, still-unexplained track.

Artifacts:

- Analyzer: `bin/suite_reports/engine_refactor/m461_world164_m335_miss_path_instrumentation_20261008.json`
- Perf log: `bin/logs/perf_20261008-043414_2668.jsonl`
- Frames directory: `bin/suite_reports/engine_refactor/m461_world164_m335_miss_path_instrumentation_frames_20261008/`
- Manifest source: commit `4a0c2a0c`, branch `codex_audit2`, clean diff; the
  fixed-day wrapper restored world data and fog.

Exact M461 route invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m461_world164_m335_miss_path_instrumentation --report bin/suite_reports/engine_refactor/m461_world164_m335_miss_path_instrumentation_20261008.json --process-timeout 7200
```

## M462 — detail trace timing with M335 (2026-10-08)

M462 used the same visible, no-teleport, fixed-day route as M461, with the
unfinished/ring-resync/light-flag detail trace enabled. It completed the flight
process normally and restored `world_data.json`, but traveled 14,288 blocks
against the 14,300 minimum (focus `7 -> -886`), so route completion failed by
12 blocks. Median flight speed was 5.1965 blocks/s; median flight wall time was
27.175 ms. The report passed 28/39 gates and stop convergence remained false.
The readiness-debt counters are not pixel-level evidence.

Do not use M462's light-flag IO timing as a benchmark. The executable was built
at `d0991e9e` before `8bc56125` reduced detail-log volume. Its INFO log contains
66,149 `light_flags_save_result` lines, which inflate the measured result-drain
and streaming times. The same run contains a 2,025.7 ms spike at `cx=-352`,
with 1,942.3 ms unaccounted, and a 1,540.3 ms spike at `cx=-343`, with 1,457.9
ms unaccounted. These are unclassified process/system pauses, with no blocked
movement substeps; do not attribute them to disk loading. The existing question
about sleep/lock during the pause was not answered.

M462's three endpoint PNGs are not valid game captures: they show a desktop and
only a narrow slice of the game window. Reject them for visual classification.
The process used AMD Radeon(TM) Graphics / desktop GL 3.3. The manifest says
HEAD `8bc56125`, while executable SHA-256
`21b5f32d4a36d89d31803f3e673329c69f90a107e9244cec2b3d04f633066d3a` is from the
Release build at `d0991e9e`; this is a source/binary timing mismatch, not a
reproducible binary identity.

Artifacts:

- Analyzer: `bin/suite_reports/engine_refactor/m462_world164_m335_detail_timing_20261008.json`
- Perf log: `bin/logs/perf_20261008-054424_36928.jsonl`
- INFO log: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261008-054420.36928`
- Captures (invalid): `bin/suite_reports/engine_refactor/m462_world164_m335_detail_timing_frames_20261008/`

Exact M462 invocation:

```powershell
$env:CUBA_VISUAL_BLACK_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBA_STREAMING_DETAIL_TRACE='1'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m462_world164_m335_detail_timing --report bin/suite_reports/engine_refactor/m462_world164_m335_detail_timing_20261008.json --process-timeout 7200
```

## M463 — M335 screen-ray readiness and scroll-map fix (2026-10-08)

M463 used the visible, no-teleport M335 route on `World_164` with the exact
established camera and distance settings. Release source was commit
`8a726f523096f58ff02caa13e94afc924b42de53`; the executable SHA-256 was
`246698E70D9AACD3DE04834F683A98C7DFCE0AD6C52E78144E1E93FC960A69B1`.
The manifest matched that source and binary, and the fixed-day wrapper restored
`world_data.json` to SHA-256
`0ade40413ad4172777a59c2573809ed415ac19dee2f30c8500c737ac5ec2d344`.

The process and flight runner completed normally. Focus X moved `7 -> -888`
(14,320 blocks), passing the 14,300-block route gate; median flight wall time
was 24.19 ms and median speed 5.2 blocks/s. Product/analyzer status was still
FAIL (30/39 gates), including stop convergence. Two endpoint periods are not a
full convergence window.

M463 enabled `CUBA_STREAMING_DETAIL_TRACE=1`. It also recorded sparse screen-ray
readiness witnesses: 92 ray-candidate period samples at 90 unique miss
coordinates, with 70 samples coinciding with near-focus holes. There were 75
near-focus samples and 75 unique focus positions. These witnesses are stronger
than generic `visible_black_focus` or `effective_holes` counters because an
opaque voxel ray intersects a resident solid column that meets the bounded
repair-candidate predicate. That predicate includes both a slice with no mesh
satisfying column readiness and a slice whose existing drawable mesh satisfies
readiness but has repairable geometry revision debt. Therefore the 92 samples
show repeated ray-confirmed geometry maintenance demand; they do not all mean
that published geometry is absent. Sampling covers only 5 of 20 horizontal
tiles and five vertical rows with rotating phases, so the counts do not
estimate full-frame hole coverage or pixel-area rate.

In the near-hole periods, median wall time was 30.12 ms versus 24.05 ms in clean
periods; streaming phase was 16.24 versus 11.87 ms and mesh emerge 9.80 versus
7.15 ms. The mesh pipeline backpressure flag was set in 74/75 near-hole
periods; median requested schedule was 16 with output headroom 9. This points
to meshing/publication pressure, but does not alone prove that GPU slot capacity
is saturated. No disk completion or generation commit coincided with those
near-hole samples. Across the run there were 785 load candidates, 119 async
queued, zero terrain stream loads/disk completions, two generation commits,
133 saves, and 546 unloads.

The run is not a clean logging-performance benchmark. The detail trace still
caused 380 per-result light-flag records; 104 failures all reported
`replace_failed: Access is denied`. Result-drain samples had 42.46 ms median,
134.28 ms p95, and 169.72 ms maximum; queue mutex wait/hold were negligible in
the worst sample. Synchronous per-result logging on the main-thread drain can
inflate streaming time. The transient Windows replace failure's external cause
is unknown; the file ACL and single-process check did not identify a persistent
permission problem.

The three endpoint captures are valid full-window game images. The approach
frames are fog-heavy and low contrast; the stationary endpoint shows populated
sandy terrain, a hill, and trees, with no obvious chunk-sized hole. These three
frames do not classify the whole route. Keep the user's longstanding slow
fog/water silhouette changes in the separate fog track.

Artifacts:

- Analyzer: `bin/suite_reports/engine_refactor/m463_world164_m335_scroll_alignment_fix_20261008.json`
- Perf: `bin/logs/perf_20261008-063939_38604.jsonl`
- INFO: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261008-063935.38604`
- Frames: `bin/suite_reports/engine_refactor/m463_world164_m335_scroll_alignment_fix_frames_20261008/`
- Source: `8a726f523096f58ff02caa13e94afc924b42de53`; Release executable SHA-256 as above.

Exact M463 invocation:

```powershell
$env:CUBA_STREAMING_DETAIL_TRACE='1'
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m463_world164_m335_scroll_alignment_fix --report bin/suite_reports/engine_refactor/m463_world164_m335_scroll_alignment_fix_20261008.json --process-timeout 7200
```

## M464 — M335 worker-side light-flag result logging (2026-10-08)

M464 repeated the exact visible/no-teleport M335 route on `World_164` after
moving light-flag result diagnostics off the main-thread result drain. The
Release source and executable matched commit `f706bb65a895e21bb29c6f129fb371fa8feec628`;
executable SHA-256 was
`DD75B30690A5D8F4A3D060495FD09E6DC02EBE1CD46191753033FEF3B46B0C35`. The
manifest passed (resolution remains untested). The route passed its 14,300
block adequacy gate: focus `7 -> -887`, 14,304 blocks, median speed 5.19653
blocks/s. The process and wrapper completed normally, and world settings were
restored. Product gates passed 28/39; post-stop convergence failed after 11
stop periods.

The run used `CUBA_STREAMING_DETAIL_TRACE=0`,
`CUBA_VISUAL_BLACK_TRACE=1`, `CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS=0`, and
disabled focus probes, source tracing, relight audit, GPU profiling, and frame
capture. Dense per-pixel logging was off, though the visual-black trace still
filled its four-row pixel-probe ring (32,768 probes). The resulting 85,003-row
trace contains 8,192 sparse screen-ray rows and 32,768 pixel probes; the
diagnostics materially perturb timing, so do not use this run as a clean
performance benchmark.

The screen-ray analyzer found 856 repair candidates at 321 distinct chunk
coordinates. Of these samples, 840 (98.1%) had a mesh currently satisfying
column readiness while also carrying repairable geometry revision debt; 16
samples had no satisfying mesh (10 with no geometry-debt flag and six with
repairable debt). This corrects the broad interpretation of candidate rows in
the M463 entry: most candidates do not represent a complete absence of a
drawable mesh. They are bounded maintenance targets. The analyzer also joined
101 candidate rows to the pixel probe from the same frame, column, and sampled
row. All 101 had pre-transparent depth below 1 and a valid opaque surface;
none had clear depth. In 89/101 the ray-mapped chunk also had a positive
visible-MDI index count. Of the matched samples, 64 were at or before the
36-block fog end and 37 were beyond it. This is direct evidence that those
sampled candidate locations had rendered opaque coverage, even though their
chunks carried repair debt. The join is sparse: 755 candidates had no same-frame
pixel probe, so it cannot establish full-screen coverage. Candidate ray distance was 3.48 /
31.11 / 74.57 / 95.61 blocks (min / p50 / p95 / max). Using the M335
distance-fog full-blend end of 36 blocks, 332/856 samples (38.8%) were beyond
that distance and therefore fog-dominated; 524 were at or before the fog end,
which still does not prove visible pixel coverage.

Captures at `cx=-636` show dense, connected forest and near terrain without an
obvious chunk-sized gap. Captures at `cx=-563`, `-710`, and the approach/end
are dominated by fog or water silhouettes and cannot classify distant
geometry. The stationary endpoint shows connected terrain and trees. These
images support the user's observation that the route currently looks good,
while leaving sparse geometry revision debt and stop convergence open. Keep the
longstanding slow fog/water silhouette transition in its separate track.

With detail tracing disabled, no `detail=light_flags_save_result` rows were
written to the main-thread INFO path. The per-frame
`async_chunk_io_light_flags_result_drain_ms` median was 0.0039 ms, p95 0.0075
ms, and max 7.13 ms across 1,815 perf rows. The broader
`async_chunk_io_drain_ms` remained distinct (median 0.85 ms, p95 1.79 ms, max
64.12 ms), so other result-drain work still merits separate profiling. The
worker emitted 25 rate-limited `outcome=light_flags_write_failed` messages;
the latest observed error remained `replace_failed: Access is denied`. The
underlying intermittent file-replace failure is unresolved and is not evidence
of a terrain-read or rendering failure.

The analyzer reports 28/39 gates, median flight wall time 25.34 ms, median
world-streaming phase 12.35 ms, and mesh-emerge median 7.18 ms. The report's
`hole_key` and `unfinished_key` are both `unfinished_visual`, a readiness/debt
count; all three reported hole rates were 1.0 because that proxy stayed
nonzero, not because pixel readback found blank chunks. The visual-black focus
median was zero. The route is complete, but stop convergence failed
(`post_stop_not_ready_end=31`, `post_stop_focus_dirty_end=135`).

Artifacts:

- Flight report: `bin/suite_reports/engine_refactor/m464_world164_m335_worker_log_isolation_20261008.json`
- Visual-trace summary: `bin/suite_reports/engine_refactor/m464_visual_coverage_trace_20261008.json`
- Perf: `bin/logs/perf_20261008-073618_31764.jsonl`
- INFO: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261008-073614.31764`
- Frames: `bin/suite_reports/engine_refactor/m464_world164_m335_worker_log_isolation_frames_20261008/`

Exact M464 invocation:

```powershell
$env:CUBA_STREAMING_DETAIL_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_VISUAL_BLACK_TRACE_FOCUS_PROBES='0'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m464_world164_m335_worker_log_isolation --report bin/suite_reports/engine_refactor/m464_world164_m335_worker_log_isolation_20261008.json --process-timeout 7200
```

## M465 — M335 screen-ray-aligned pixel/depth evidence (2026-10-08)

M465 enabled `CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_SCREEN_RAY=1` on the exact
visible/no-teleport M335 route. On screen-ray trace frames the renderer captured
the existing 20-column pixel/depth grid on all five exact ray rows; other
periodic probes retained the normal four-row grid. The 40,960-record pixel
trace ring filled and evicted some early samples, which the M466 follow-up
addresses. Dense pixels, focus-change probes, source trace, relight audit, GPU
profile, and frame captures stayed off. The instrumented timings are not a
clean performance comparison.

The Release executable matched clean commit `12dbdf2ca11e69eb90715137c1d6c780f5671f01`
and SHA-256
`5a6cbe203e1d75fd24b35b0ff6c10ed9e83f06b205f95b2c30789d39e4ad8dc2`. The
manifest passed (resolution remains untested). The app process exited 0 and
the route passed its 14,300-block adequacy gate: focus `7 -> -888`, 14,320
blocks, median speed 5.19653 blocks/s. The wrapper returned 1 because the
product dual-lane stop-line failed; post-stop convergence also failed after 11
periods. This is a gate result, not a failed flight.

There were 8,192 retained screen-ray samples and 787 candidate rows at 353
unique chunk coordinates. The analyzer matched 514 candidates to same-frame,
same-column, same-row pixel/depth samples. In 512/514 matched locations the
opaque pass wrote depth `<1` and reconstructed a valid opaque surface. The
other two samples had clear depth and no opaque surface; both candidate rays
hit geometry farther than the 36-block full-blend fog horizon (70.16 and
76.98 blocks). There were no clear-depth matched samples inside that horizon.
The matched samples therefore do not establish a visible gap.

The remaining 273 candidates had no matching pixel record because the ring
evicted older captures. There were 173 such candidates at or before the fog
horizon, so these must remain unclassified. This is a retention loss rather
than a pixel-grid mapping issue: the five screen-ray rows were present and the
ray/pixel key matched exactly for retained frames. Candidate distance was
4.03 / 30.64 / 76.78 / 95.64 blocks (min / p50 / p95 / max), with 505/787 at
or before the full-blend fog end. These rays still sample sparse locations,
not the whole framebuffer.

The report had 1,408 periods (1,406 steady), 54 spikes, 29/39 product gates,
median wall 25.25 ms, and a 458.08 ms maximum spike. `unfinished_visual` stayed
nonzero and its hole-rate aliases were 1.0; this is readiness/debt telemetry.
Visible-black focus median was 0. The dual-lane line failed on `unlit_max`,
eye-proxy failed on stale visual evidence/blink rate, and A24 found near-focus
mesh telemetry holes in 78 periods (one cruise-corridor period). These gates
remain distinct from the pixel/depth witnesses. No M465 screenshot was saved.

Artifacts:

- Flight report: `bin/suite_reports/engine_refactor/m465_world164_m335_pixel_ray_sync_20261008.json`
- Visual-trace summary: `bin/suite_reports/engine_refactor/m465_visual_coverage_trace_20261008.json`
- Perf: `bin/logs/perf_20261008-094118_26160.jsonl`
- INFO: `bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20261008-094115.26160`

Exact M465 invocation:

```powershell
$env:CUBA_STREAMING_DETAIL_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_VISUAL_BLACK_TRACE_FOCUS_PROBES='0'
$env:CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_SCREEN_RAY='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m465_world164_m335_pixel_ray_sync --report bin/suite_reports/engine_refactor/m465_world164_m335_pixel_ray_sync_20261008.json --process-timeout 7200
```

## M466 — M335 retained ray-correlated pixel/depth evidence (2026-10-08)

M466 repeated the exact visible/no-teleport M335 conditions and enabled the
same-frame renderer pixel probe on the screen-ray sampler's five vertical
scanlines. The pixel ring was 65,536 records. Dense pixels, focus-change
probes, source tracing, relight audit, GPU profile, and frame captures stayed
off. The user reported a heavy concurrent host task during part of the run,
approximately focus `-340..-365`; treat M466 latency and route completion as
host-load contaminated.

The Release executable matched clean commit
`c2e9b5f4b9f2ffd94351a5587a1241cd8f2e5c96`, SHA-256
`247c86aad30cc82a1890272ecd98bc907e4674cdc7cf97148c41e89e31827d6a`. The app
exited 0, `run_outcome=success`, and manifest acceptance passed. It traveled
13,744 blocks at median 5.19653 blocks/s; this missed the 14,300-block route
gate by 556 blocks, so the wrapper and product stop-lines reported FAIL. The
log has 1,395 periods, 1,393 steady periods, and 688 spikes. Median wall time
was 28.94 ms and effective FPS was 34.55; neither is a clean baseline because
of the concurrent host load and trace instrumentation.

At shutdown, the trace rings dumped 8,192 screen-ray rows and 65,536 pixel
rows, both at their configured caps. The retained ray history spans focus
chunks `-550..-836`, candidate block X `-8,797..-13,427`, 120 candidate
epochs, and 968 repair candidates. It covers the far part of M335, not the
entire route. Every retained candidate joined to the exact same-frame,
same-column, same-row pixel probe (968/968; zero unmatched). Of the 611
candidates at or before the 36-block full-blend fog horizon, all 611 had valid
opaque depth and an opaque surface; there were zero clear-depth candidates
within fog. Of 357 candidates beyond fog, 355 had opaque surfaces. The two
clear-depth samples were at 79.86 and 82.07 blocks and both were fog background
RGB `(117,163,233)`.

The renderer-pixel analyzer found 5,957 samples below luma 96 and 945 below
luma 32 among the 65,536 probes. Every dark sample still had a valid opaque
depth surface and visible MDI indices; 5,693 had settled light demand. The
transparent pass changed none of the dark samples' RGB values. These are dark
rendered surfaces, not blank pixel witnesses. The trace does not alone prove
their lighting or material color is correct.

`unfinished_visual` stayed nonzero throughout and its hole-rate alias was 1.0.
All 1,024 retained focus-column classifications were `MissingMesh` (enum value
5). `CountUnfinishedVisualNear` counts per-slice FirstMesh debt at column
level even though other ready slices can be drawn progressively, and
`draw_oracle_missing_resident_n` directly aliases that counter. The associated
readiness gates are not independent framebuffer evidence. Do not treat these
counters as proof of empty chunks without a pixel/depth witness.

The screen-ray history ring reached its 8,192-row cap, so earlier route
segments were evicted even though every retained candidate has pixel data.
M466 therefore improves the correlation evidence over M465 and resolves its
unmatched retained candidates, but does not prove whole-route or full-screen
coverage. The user-visible report that the world looked good is consistent
with these sampled points; no full-frame screenshots were saved.

Artifacts:

- Flight report: `bin/suite_reports/engine_refactor/m466_world164_m335_pixel_ray_sync_retention_20261008.json`
- Visual-coverage summary: `bin/suite_reports/engine_refactor/m466_visual_coverage_trace_20261008.json`
- Renderer-pixel summary: `bin/suite_reports/engine_refactor/m466_renderer_pixel_trace_20261008.json`
- Perf trace: `bin/logs/perf_20261008-104720_17708.jsonl`

Exact M466 invocation:

```powershell
$env:CUBA_STREAMING_DETAIL_TRACE='0'
$env:CUBA_VISUAL_BLACK_TRACE='1'
$env:CUBA_VISUAL_BLACK_TRACE_DENSE_PIXELS='0'
$env:CUBA_VISUAL_BLACK_TRACE_FOCUS_PROBES='0'
$env:CUBA_VISUAL_BLACK_TRACE_PIXEL_ON_SCREEN_RAY='1'
$env:CUBA_WORLD_COLUMN_SOURCE_TRACE='0'
$env:CUBATARIUM_RELIGHT_AUDIT='0'
$env:CUBA_FLIGHT_CAPTURE_DIR=''
$env:CUBA_GPU_PROCESS_PROFILE='0'
$env:CUBA_GPU_PROCESS_PROFILE_PATH=''
$env:CUBA_STAGE_WATCHDOG_PATH=''
python tools/flight_sim_fixed_day.py --world World_164 -- --scenario product-174657-far --visible --product-start-position 120 56 56 --cruise-eye-y 70 --yaw 180 --pitch -30 --fly-phase-sec 2800 --stop-phase-sec 20 --stop-after-blocked-sec 8 --minimum-travel-blocks 14300 --phase-id m466_world164_m335_pixel_ray_sync_retention --report bin/suite_reports/engine_refactor/m466_world164_m335_pixel_ray_sync_retention_20261008.json --process-timeout 7200
```
