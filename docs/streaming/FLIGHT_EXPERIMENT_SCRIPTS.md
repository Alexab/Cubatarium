# Experimental flight and analysis scripts

Обновлено: 2026-10-05. Цель каталога — сохранить и объяснить поддерживаемые flight tools и одноразовые анализаторы, которые накопились в рабочем дереве во время streaming/rendering расследований.

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
