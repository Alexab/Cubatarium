# World smoke после корректировки cc7063ff

## Результат

**Не принят: CORRECTNESS_FAIL.** Локальные regression tests зелёные, но world streaming ещё не удовлетворяет исходному аудиту. Откат всей ветки не следует из этого результата: следующая работа — диагностировать сохранённый progress failure, не ослаблять gates.

## Условия и артефакты

- База: `cc7063ff7bcfb3d408185fb23e456e41fb80184a`, dirty build, `codex/world-streaming-audit-fix`.
- Release MSVC; SHA256 executable: `D9CE2C50C720AC65DDC31165C18D605FBD5B83667940D33276F67ED0F162EB8D`.
- Изолированная копия: `build/audit-runtime-20260911/worlds/World_164`, 43466 файлов, 372942654 bytes. WritableRoot — каталог скопированного executable, исходный мир не использовался как рабочий.
- Config SHA256: `4069879244DC10C85672B1452507DB74CD853A8FDB0287A78747A8239853E835`.
- World metadata SHA256: `B2F95091ACBBA531EF7A21A4695FF57D13CD8BAF9BBDE6FE89FD55B438DFEA80`. Хеши исходного config и world metadata после теста совпали с исходными.
- CLI: `--flight-sim --world World_164 --seconds 45 --idle 8 --fly --hold-forward --teleport-cruise --report smoke-report.json --perf-out smoke-perf.jsonl`.
- Hidden window 1280×720; штатный flight harness отключает autosave и включает minimal overlay. GPU real-driver fixture на этой машине: AMD Radeon(TM) Graphics.
- Не A/B: один диагностический маршрут с teleport; полного content/driver manifest и 3 cold/3 warm нет. Снимок стеков кратко приостанавливал процесс. FPS из этого прогона нельзя считать сравнительным benchmark.

Артефакты под `build/audit-runtime-20260911/`: `smoke-report.json`, `smoke-analysis.json`, `smoke-verdict.json`, `smoke-stacks.log`, `logs/perf_20260911-083748_26432.jsonl`, `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260911-083743.26432`, `logs/enter_lit_20260911-083840.jsonl`.

Первый sandbox-запуск не создал логов и был завершён адресно (проверены PID и executable path). Повторный запуск создал логи и завершился самостоятельно. Flight harness меняет CWD на корень проекта: созданные им `smoke-report.json` и `smoke-perf.jsonl` перенесены в каталог артефактов. Следующие запуски должны передавать абсолютные output paths.

## Наблюдения, не гипотезы

1. Flight report: exit 0, 2779 игровых кадров, 45 секунд, start `(-47,5)`, end `(-59,5)`, 12 чанков перемещения.
2. INFO: `force_ingame_no_uf`, `elapsed_ms=150111`, `visibility_debt=80`, `underfeet=0`. Успешный выход процесса не означает успешный прогрев.
3. В прогреве длительное плато `relight_inflight=32`, FIFO=1. В снимке стеков worker threads находились в condition-variable wait. Release binary не содержал доступных PDB: функции по адресам не атрибутированы, root cause ещё не доказан.
4. Анализатор: 23 periods, 21 steady; effective holes rate=1.0; медиана fly wall=29.6966 ms, max spike=609.424 ms; tail missing не закрывается. Это sampling/telemetry proxies, не pixel oracle.
5. Scorecard: CORRECTNESS_FAIL, в том числе focus_missing_frac=1, visual_holes_frac≈0.929, empty_backlog_max=120. Teleport дополнительно нарушает fidelity gate. Baked historical baseline в выводе оценщика НЕ является измеренным A/B текущей ветки.

## Исправления и следующий эксперимент после этого build

- В `PruneGhostDirty` найден перевёрнутый аргумент `!HasChunk`: вместо отсутствующих удалялись resident chunks. Дефект подтверждён в `cc7063ff`, исправлен; decor integration теперь проверяет оба случая и проходит. Политический unit test не ловил ошибку wiring.
- Prepared spawn больше не отключает CPU warmup drain при незавершённой работе, visibility debt или неготовой опоре. Проверены пять комбинаций policy; фактический выход мира из прогрева ещё требует нового replay.

- Найден самостоятельный дефект `Completed.SetCapacity`: shrink терял результаты без уведомления owner. Теперь возвращаются evicted payloads; mesh/relight освобождают соответствующий InFlight и направляют demand в overflow retry. Добавлена регрессия. Связь с плато именно этого run пока не доказана.
- Scorecard отдельно отвергает `force_ingame_no_uf`, даже если поздние метрики улучшаются; regression проверяет debt 0 и 80.
- Добавлена opt-in диагностика `CUBATARIUM_RELIGHT_AUDIT`: реальное соотношение inflight/queued/running/completed и первая изменившаяся dependency при retry. Этот режим не использовать для performance acceptance.
- Следующий прогон: свежий executable, та же изолированная исходная save state, абсолютные output paths; сначала установить причину отсутствия progress. Затем повторить без диагностики и без teleport по acceptance route.

Исходный G1–G4 не закрыт. Этот smoke предшествует исправлению shrink и новой диагностике; не приписывать его результат последующим изменениям.

## Повторный диагностический прогон: smoke2, 13:52–13:54

Свежий Release после prepared-drain и исправления инверсии ghost prune. SHA256: `1F1DED2CB7641F23AE1542478FFB3E4133965F88FA2512B44A637D856AC4DCF7`. PID 8588 завершился самостоятельно; 871 игровых кадров, 45 секунд, 9 чанков перемещения. Использована прежняя изолированная копия, абсолютные output paths и `CUBATARIUM_RELIGHT_AUDIT=1`. Хеши config и world_data изолированной копии совпадают с указанными выше. Текущие файлы в `bin` уже имеют другие хеши и время 09:14; они не являются baseline этого replay и не перезаписывались.

Подтверждённый прогресс:

- На `gpu_warmup_draw`: 484 resident chunks, 484 greedy cache entries, 609 batches, 103368 vertices; mesh dirty/inflight = 0.
- Последняя запись warmup: elapsed 7500.94 ms, combined debt 0, underfeet 1, ring ready 1, visibility debt 0. `force_ingame_no_uf` отсутствует. Название записи `live_blockers` само по себе не является причиной отказа: значения долга нулевые.
- Исчезло прежнее плато с 32 relight jobs без обработки; в полёте есть running/completed/drain и 120 записей stale retry. Это не доказательство отсутствия всех stalls.

Оставшиеся проблемы:

- Scorecard **CORRECTNESS_FAIL**: focus missing fraction 0.9474, visible black focus median 60/max 87, empty backlog max 18. Visual holes fraction 0.2105. Это telemetry proxies, не pixel oracle.
- Анализатор: fly wall median 111.961 ms, streaming phase median 79.666 ms, mesh emerge median 34.196 ms; 132 stale mesh apply за cruise по scorecard. FPS не принят: включена диагностика, нет парного A/B, пройдено другое расстояние.
- Телепорт явно передан оценщику как `--teleport true`: raw flight report не содержит этот флаг, поэтому `auto` ошибочно не увидел бы ограничение fidelity. Manifest отсутствует; baked baseline оценщика не использовать для сравнения.

Артефакты: `smoke2-report.json`, `smoke2-analysis.json`, `smoke2-verdict.json`, `smoke2-perf.jsonl`, `logs/perf_20260911-135212_8588.jsonl`, `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260911-135208.8588` в том же каталоге runtime.

Следующий этап: приоритет M09/M10/M12–M14 — измерить конфликтующие relight read/write regions и stale mesh retries; разобрать стоимость emerge и общего streaming phase без диагностического логирования. Сначала обеспечить demand/retry progress и покрытие black chunks, затем ограничивать работу общим deadline и переносить capture с main thread. Не отключать validation и не публиковать stale mesh ради FPS. Повторить без teleport на фиксированном маршруте; только после correctness gates переходить к 3 cold/3 warm A/B.

Контрольная точка тестов после исправления ghost prune: **17/17 CTest PASS**, scorecard regression PASS, include rules PASS (40 legacy allowlist), `git diff --check` без ошибок whitespace. Windows CI не запускался. G1–G4 остаются открытыми.

## Smoke3 после исправления установки света и overflow recovery

База `893fb9bf`, ветка `cursor_audit_impl`, незакоммиченные доработки. SHA256 Release executable `09A097AE5C9E71E705869FECEF688901AB23B1A89251FF0146969B4B5A5372F3`. PID 4776, запуск 18:49:54, самостоятельное завершение. Та же изолированная копия World_164; config/world_data hashes остались прежними. Команда: `--flight-sim --world World_164 --seconds 45 --idle 8 --fly --hold-forward` с абсолютными report/perf-out. Без `--teleport-cruise` и без `CUBATARIUM_RELIGHT_AUDIT`.

Изменения этого build:

- `DrainAsyncRelightResults` устанавливает рассчитанный skylight, не заменяя его повторным vertical seed. Боковой свет сохраняется; установка одного домена сохраняет другой, no-op не меняет light revision. Регрессия использует настоящий Capture/Compute и сцену с навесом.
- Overflow mesh replacement не отбрасывается при dirty_admit_budget=0; orphan ownership снимается, demand возвращается в очередь. Decor regression воспроизвёл отказ до исправления и подтвердил eventual replacement после освобождения result budget.

Фактический результат: 1301 игровых кадров, 45 секунд, `(-3,3) → (-14,3)`, 11 чанков. Прогрев: 8224.47 ms, combined debt 0, visibility debt 0, underfeet 1, ring ready 1; forced enter отсутствует. **CORRECTNESS_FAIL**: focus_missing_frac≈0.737 и visible_black_focus median 78 по scorecard. Manifest/парного baseline нет, отсутствие fidelity flags само по себе не делает прогон приёмочным.

Анализатор: fly wall median 70.6836 ms, streaming phase median 62.01675 ms, mesh emerge median 28.6665 ms, effective holes rate 0.7. Медианы по всем period samples отдельно: relight_apply_light 0.05 ms, relight_apply 0.13 ms, mesh_emerge_prep 18.88 ms; крупнейший измеренный prep segment — `prep_schedule_policy_ms` 17.60 ms (max 27.56). Эти выборки отличаются от cruise-only scorecard; не смешивать их. Mesh stale delta median 1/max 5, накопленный mesh_apply_stale max 35. Visible black остаётся открытой проблемой; это telemetry proxy, визуальный oracle ещё нужен.

Прямое сравнение FPS с smoke2 недопустимо: без teleport стартовый участок изменился, диагностика отключена. Новая точка профиля задаёт следующий приоритет M14: разделить стоимость внутри schedule_policy и убрать ненужные дорогие проверки готовности spawn вне enter gate; затем общий deadline. В частности, вызов `ShouldSuppressRelightSeamDirtyForEnterGate` сейчас eagerly вычисляет `IsSpawnMeshRingReady()` даже при неактивном enter gate. Его отдельный вклад пока не измерен — 17.60 ms относится ко всему сегменту, не к этому вызову.

Артефакты в `build/audit-runtime-20260911/`: `smoke3-report.json`, `smoke3-analysis.json`, `smoke3-verdict.json`, `smoke3-perf.jsonl`, `logs/perf_20260911-184958_4776.jsonl`, `logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260911-184954.4776`. Анализаторы возвратили 1 и 2 соответственно; scorecard запущен с явным `--teleport false`.

Release сборка завершена, **18/18 CTest PASS** после финальной пересборки; scorecard regression/include audit PASS. G1–G4 не закрыты. Эти доработки не являются заявлением об устранении всех чёрных чанков или о достижении FPS SLA.
