# S3.06: реальные CPU/GPU/fallback пути

Статус OPEN. Исходный объём не сокращается. Проверки renderer atlas не доказывают solver, host stubs не доказывают GPU.

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
