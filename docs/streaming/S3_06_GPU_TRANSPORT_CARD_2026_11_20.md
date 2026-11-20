# S3.06: реальные CPU/GPU/fallback пути

Статус COMPLETE в пределах S3.06. Родительский S3 и исходный S0..S6 остаются OPEN до финальной матрицы. Проверки renderer atlas не заменяют solver, host stubs не являются GPU evidence. Ниже сохранена история реализации.

## Проверенная карта реализации

`UChunkRelightSnapshot::Compute` выполняет geometry-seeded sky/block flood на CPU; AsyncRelightBuilder вызывает его на worker. `UGpuFullLightingPipeline` делегирует Full CPU, BackendName=cpu_full_light. `TryGpuSeedSkylightColumns` возвращает false; section-local preview не является full-height sky. Старый GpuBlocklightFlood содержит optional dispatch без readback и CPU-authoritative результат; ни production snapshot, ни GPU adapter его не вызывают. Он не закрывает parity.

## Конечные шаги пакета

1. Реальный GPU sky/block transport kernel, bounded scratch, context/failure contract, readback, независимый oracle.
2. Immutable snapshot export/import: full-height geometry sky seeds, material properties, exact domains/write set/halo/read set. Подключить transport без повторного использования старых light bytes.
3. Main-thread GPU owner и фактический production dispatch; async CPU остаётся отдельным исполняемым путём. Не выполнять GL из произвольных worker threads.
4. Совместимые общие fixtures через CPU/GPU; проверить rooftops/water/seams/negative profile/AIR/source removal и все padded bytes.
5. Реальный no-compute/context/failure fallback: CPU result без ложных GPU counters/readiness; shader/fault negative controls, GL state/resource lifetime.
6. Связать доказательства с actual backend и persisted solver contract; не закрывать до integration/parity/fallback.

## H1: GPU transport primitive

Новый TryGpuTransportLight принимает только immutable dense seeds и transmission (opaque/transparent/liquid), shape и packed sky/block. GPU делает 15 Jacobi passes и один readback. Каждое ребро теряет >=1, поэтому положительное влияние ограничено 14 hops. Opaque emitter сохраняет собственный seed и освещает соседей. Полный верхний контекст готовится отдельно и остаётся обязательным шагом 2; этот primitive не выдаёт section certificate.

Scratch cap: 1,048,576 cells, четыре uint32 SSBO (16 MiB); проверяются размеры, типы материалов и hardware block limit. Временные GL resources удаляются до возврата; bindings/program и SSBO ranges восстанавливаются. Fence wait <=2 s; CTest timeout 30 s. Без current GL4.3 context или при ошибке output не меняется, CPU fallback не маскируется как GPU success.

Driver fixture: hidden GL4.3 context, asymmetric 35x39x33 domain, все шесть направлений, roof, water, barrier, opaque emitter emission14/6, source removal, nonmultiple dispatch groups. Четыре phases; каждый voxel сравнивается с независимым work-list CPU oracle, который не вызывает production solver или GPU recurrence. PASS требует 15 actual dispatches и 1 readback в каждой фазе, exact bytes, SSBO range restoration, no-context/invalid-input negative controls. Context unavailable возвращает skip77, не PASS.

Release target/CTest `gpu_light_transport_driver_test`, build/test logs `s3-gpu-transport-build-20261120.log`, `s3-gpu-transport-test-20261120.log`. После primitive PASS не расширять его тесты без нового изменения/непокрытого требования: перейти к snapshot integration.

H1 PASS: настоящий AMD Radeon (TM) Graphics, GL 4.3.0 Core Profile Context 22.20.27.09.230330. Четыре phases по 45,045 cells, суммарно 60 dispatch/4 readback; все bytes совпали с work-list oracle. Initial driver test 0.41 s. No-context и invalid-material controls не меняют output; indexed SSBO ranges восстановлены.

Для проверки независимого oracle временно изменён actual shader liquid sky loss 2→1. `s3-gpu-transport-negative-test-20261120.log`: FAIL на полном сравнении GPU cells, 0.31 s. Before/fault hashes и backup сохранены. Точные normal source bytes восстановлены с новой mtime, source recompiled; `s3-gpu-transport-restored-test-20261120.log`: 4 phases PASS, 0.30 s. Fault source/binary не оставлен.

Receipt `build/desktop-msvc/s3-gpu-transport-receipt-20261120.json` связывает driver, counts, source и logs. Primitive пока связан только с driver target; production snapshot и desktop adapter ещё используют CPU. Следующий шаг 2: optional immutable transport executor в snapshot, bounded dense export из ComputeChunks после full-height seed, checked import результата, затем main-thread production dispatch/fallback. Пакет S3.06 и вся цель остаются OPEN.

## H2: immutable snapshot transport integration

Compute получает explicit function-pointer executor; async caller без него остаётся CPU. После штатного full-height seed из immutable geometry экспортируется bounded dense domain (cap 1M cells). Отсутствующие sparse tiles и out-of-profile ячейки остаются opaque zero; old light fields не читаются. Только successful full-size readback с actual dispatch/readback work заменяет scratch; иначе CPU flood выполняется на исходных seeds. Read set, output sorting, padded extraction, private content certificate и install validation остаются общими.

Фиксированные targets (-1,-3,-1)/(0,-3,-1), profile -48..47, write band -48..-33; roof Y=32 вне compute band, water -35..-33, emitter (-1,-40,-8), hostile old bytes 0x97. Четыре phases: roof+water+emission14; roof removal; emission6+barrier; source/water removal. Все interiors и 8000 padded bytes на target сравниваются CPU/GPU; direct-sky samples ожидаются 0/12/12/15. GPU result должен валидироваться и устанавливаться с current certificate. Дополнительно sky-only/block-only, actual no-context CPU fallback и malformed partial-readback fallback. Partial callback не считается real GPU evidence.

Release target `gpu_relight_snapshot_driver_test`, CTest timeout 30 s. Build/test logs `s3-gpu-snapshot-build-20261120.log`, `s3-gpu-snapshot-test-20261120.log`. Actual desktop adapter/main-thread runtime owner ещё не подключён; H2 driver не закрывает шаг 3 или весь S3.06.

H2 initial PASS: real Radeon GL4.3, четыре phases по 16,000 padded cells; CPU/GPU exact, selected domains, actual no-context fallback и malformed readback fallback. Certificate/relight unit tests также PASS, 3/3 в 1.09 s. Перед переходом к runtime owner добавлены четыре конкретных edge controls экспортёра: field-only AIR без geometry, partial negative profile MinY=-45, sparse bounding box over cap до вызова backend, exception после partial backend output. Эти ветви не были покрыты первым H2 запуском; после PASS их повтор без изменения не нужен.

H2 edges и final source PASS: `s3-gpu-snapshot-edges-test-20261120.log`, затем final rebuild после explicit exception include: `s3-gpu-snapshot-final-build-20261120.log` / `s3-gpu-snapshot-final-test-20261120.log`, 3/3 PASS в 1.04 s. Snapshot driver 0.44 s. Все четыре primary phases, оба selected domains, field-only AIR, partial profile, over-budget и exception fallback прошли. Частичный GPU output не меняет CPU seeds; fallback bytes совпадают с CPU.

Полная app Release build PASS (`s3-gpu-snapshot-app-build-20261120.log`), provenance сохранён в `s3-gpu-snapshot-app-provenance-20261120.json`. Driver receipt `s3-gpu-snapshot-receipt-20261120.json` связывает final source hashes и проверки. Приложение пока не передаёт executor: actual runtime GPU dispatch ещё UNTESTED, async workers сохраняют default CPU путь. Следующий шаг 3 — main-thread desktop adapter owner и native dispatch evidence; GPU/CPU equivalence не означает, что GPU уже используется приложением.

## H3: desktop caller-thread owner

UGpuFullLightingPipeline устанавливает ScopedSyncRelightTransport только вокруг synchronous Relight*; ChunkLighting передаёт этот executor в immutable snapshot. RAII восстанавливает предыдущий выбор, thread_local не наследуется worker threads. AsyncRelightBuilder не читает scope и вызывает CPU Compute по умолчанию. Actual submitted dispatch counter отделён от backend success; sync audit пишет transport/dispatches/readbacks/fallback. Host-only tests явно отвергают GPU backend и не дают GPU evidence.

Runtime capability discovery: приложение создаёт GL3.3, native RenderBackendCaps сообщает compute=1/ssbo=1 через ARB extensions на Radeon. Поэтому transport поддерживает desktop core4.3 либо GL3.3 + ARB_compute_shader/ARB_shader_storage_buffer_object/ARB_shading_language_420pack с соответствующим GLSL prefix. Без возможностей остаётся CPU fallback. Нельзя менять окно на новый backend ради PASS.

Driver H3: actual desktop adapter (не direct snapshot injection) должен отправить 15 dispatch, установить padded field/certificate, записать transport=gpu и восстановить scope. До GLFW context тот же adapter должен дать CPU fallback и 0 dispatch; отдельный thread не наследует выбор. Запускаются GL4.3 и GL3.3 contexts на одинаковых fixtures; CTest timeout 30 s.

Native H3: opt-in API case desktop_gpu_relight_owner в реальном app frame, отдельный UWorld profile -48..47, root (-1,-3,-1), roof Y32, emitter14. Factory-selected pipeline должен отправить ровно 15 dispatch, получить exact CPU-reference padded bytes и current certificate. Scope после вызова nullptr. Release runner startup-only, World_261 copy, rd4, --probe-terrain-save-failure, process timeout 600 s, каталог `s3-desktop-gpu-owner-20261120-native`. Это не вся visual/performance matrix. После FAIL сначала точный require/compile/fallback reason.

H3 driver PASS: s3-gpu-owner-configured-test-20261120.log, GL4.3 и app-compatible GL3.3 на Radeon; snapshot phases/edges, desktop owner GPU, no-context CPU fallback, scope/thread isolation и CPU propagation: 3/3 PASS, 1.38 s. Auto ChunkLightingTestStubs сначала конфликтовал с driver logger; явный frame epoch stub и повторный cmake configure исключили auto dependency. Link failures/Not Run сохранены отдельно и не считаются GPU-результатами.

## Итог H3 и приёмка S3.06

Native `s3-desktop-gpu-owner-20261120-native`: desktop_gpu_relight_owner PASS, ровно 15 actual dispatch, exact CPU-reference padded bytes, current certificate, восстановленный scope. Полный API probe 19/19 PASS; native exit 0, startup/build provenance PASS. Source World_261/config сохранены, lease освобождён. Оригинальный общий smoke UNTESTED не переписан: renderer/route/performance и отсутствующие component readers остаются исходными обязательствами.

Дополнительный codec control в driver: результат actual desktop GPU и actual no-context CPU fallback экспортирован через CaptureSectionLight, Encode/Decode и RestoreSectionLight под новым catalog/field identity с exact padded bytes и current certificate. `s3-gpu-owner-certificate-test-20261120.log`: GL4.3/GL3.3 owners и CPU propagation, 3/3 PASS, 1.02 s. Disk writer/reader этого же opaque receipt доказан S3.05; транспортный backend не записывается как runtime identity в cache. Семантика solver не менялась: GPU bytes совпадают с CPU, версия light-v6 сохраняется.

Final Release build/provenance PASS (`s3-gpu-owner-final-app-build-20261120.log`, `s3-gpu-owner-final-provenance-20261120.json`). EXE hash побайтно совпал с native verified EXE. Единственная source difference после native — добавленный codec control в test file; production sources не менялись, новый native повтор не нужен. Shader literal body побайтно совпадает с body реального shader-loss negative control; GL3.3 prefix не меняет проверенную арифметику.

| Шаг пакета | Доказательство |
|---|---|
| Реальный GPU transport и независимый oracle | Primitive receipt, 180180 packed cells, actual shader fault обнаружен |
| Immutable export/import, full-height context и halo | Snapshot driver, все interiors/padded bytes, roof Y32, water, negative/partial profiles, AIR, domains/removal |
| Production owner | Desktop adapter scope, GL4.3/GL3.3 driver и actual app factory dispatch |
| CPU/GPU parity | Общие geometry inputs, exact fields, literal roof/water/source checks; CPU independent dense evidence S3.02 переиспользовано |
| CPU fallback и lifetime | Actual no-context backend, dense-cap, partial/error/exception controls, scope/thread isolation, GL bindings/fence cleanup |
| Persisted contract | GPU/CPU receipt codec restore с fresh identities + реальный disk path S3.05 |

Итоговый `s3-gpu-backend-acceptance-20261120.json` связывает артефакты, hashes и границы scope. Desktop путь является hybrid: CPU готовит full-height geometry seeds, GPU выполняет authoritative sky/block transport и readback. Async workers остаются CPU; GLES renderer и draw consumers проверяются отдельно в S5. No-context/failure не считается GPU success. Legacy no-readback blocklight shim не вызывается production snapshot/adapter и не входит в GPU evidence.

После достаточного PASS пакет завершён; следующие повторы только при изменении зависимого кода. S4.01 теперь активен; остальные queues/atomic publication, S5 consumers и S6 visual/performance остаются открытыми.
