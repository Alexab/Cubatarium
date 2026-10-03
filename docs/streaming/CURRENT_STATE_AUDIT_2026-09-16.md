# Повторный аудит мира: целостность изображения, стриминг и производительность

Дата: 16 сентября 2026. Ветка: `cursor_audit2_impl`. Проверенный HEAD: **`2474265c46edb43b811596cb8efd874da6b16156`**.

## 0. Главный вывод

**Сейчас первоочередная проблема — нарушение владения опубликованной GPU-геометрией, а не недостаточный бюджет relight.** Найден и исполнен контрпример на production allocator/publication: batch остаётся в draw-table, его диапазон освобождается, а затем выдаётся другой геометрии. Повторение пути создаёт пересекающиеся «живые» allocations. Это конкретная причина, способная давать подмену текстур/геометрии и дыры; привязка каждого наблюдаемого артефакта к ней требует захвата соответствующего кадра.

Последние исправления улучшили некоторые локальные контракты, но не создали единого владельца изображения. Приёмка дополнительно маскирует часть регрессий: постоянные дыры проходят eye-proxy, часть таймеров стала накопительной, а «середина автопролета» преимущественно проверяет остановку. Поэтому очередное снижение счётчика stale или stalled не доказывает исправление картинки.

Рекомендация: **не откатывать весь проект и не продолжать наращивать repair-эвристики**. Сохранить доказанные исправления frustum, полных координат cooldown, skylight install и fence polling. Перестроить публикацию вокруг versioned resident registry и явных replace/remove операций; затем сделать проверяемыми зависимости mesh/light и сквозное планирование работы.

Приоритеты:

1. Исправить повторное освобождение и защитить allocation ownership исполняемыми инвариантами.
2. Разделить resident geometry, видимый draw-list и смену CPU/packed backend. Пустая геометрия должна быть полноценным результатом, а не отсутствием информации.
3. Починить метрики и приёмку; ввести независимый geometric/material oracle.
4. Устранить неявные зависимости snapshot и ложные подтверждения mesh/light progress.
5. Перенести управление demand на один событийный owner с общими ресурсами и ограниченным временем кадра; после этого оптимизировать остаточную стоимость.

Подробный порядок работ и критерии выхода — в разделе 7. Это план изменений, **не утверждение об их выполнении**.

## 1. Область проверки и ограничения

Исследованы изменения после аудита [14 сентября](E:/Work/Home/Cubatarium/docs/streaming/CURRENT_STATE_AUDIT_2026-09-14.md): 40 коммитов после `420335e9`, текущие renderer/mesh/lighting/streaming paths, планы N01/N04, suite reports и шесть raw JSONL за 16 сентября. Уточнение пользователя: сейчас заметны **подмена текстур, дыры, возможно мигание**. Последний доступный raw log — `123828`; отдельного подтверждения, что каждое описанное наблюдение относится именно к нему, нет.

Обозначения доказательств: **E** — исполненный контрпример/тест; **C** — установленный путь в коде; **M** — сохранённая метрика; **H** — гипотеза. P0 здесь означает блокер достоверности renderer/визуального релиза, а не оценку информационной безопасности.

Что выполнено:

- Пересобраны 26 executable targets зарегистрированного streaming-набора и отдельно `mark_relit_characterization_test`.
- CTest: **26/27 PASS**. Падает `draw_oracle_gate_test`: `G1-P3: lit publish restarts age`. Real-driver pool smoke прошёл, не skipped. Отдельный MarkRelit characterization — exit 0.
- Изолированный C++ repro: **четыре нарушения**; exit 1 означает нарушение correctness. Использованы настоящие `GreedyVertexPool.cpp` и `GreedyGpuPublication.cpp`, GL storage/fence mock из существующего production test. Это не GPU screenshot test.
- Два отрицательных теста текущего eye-proxy: оба ошибочно возвращают PASS.
- Python Q9/Phase57 tests прошли. В `test_eye_proxy_stop_line.py` синтетические проверки прошли, но три исторические проверки `095545`, `102527`, `121131` **SKIP: отсутствуют raw logs**.
- Include rules: PASS с прежними **41 legacy exception и 31 reverse Render→World edge**. Это не доказательство чистых границ модулей.

Артефакты: [результаты тестов](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_16/test_results.txt), [C++ repro](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_16/PublicationRepro.cpp), [gate repro](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_16/GateRepro.py), [анализатор](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_16/analyze_flights.py), [расчёты и SHA-256 raw logs](E:/Work/Home/Cubatarium/docs/streaming/audit_2026_09_16/flight_analysis.json).

**Не выполнялись:** новый игровой полёт, редактирование сохранений, полный rebuild игрового executable, RenderDoc capture, GPU fault injection на настоящем мире, Android/GLES acceptance. Production-код не менялся. Тесты не заменяют эти проверки. Логи сняты ранее на разных промежуточных состояниях; временная последовательность и названия отчётов не заменяют верифицированную цепочку source→binary→world→run.

Raw `154921` от 14 сентября сейчас отсутствует: его прежние измерения сохранены в старом аудите, но здесь заново не пересчитаны. Не использовать его как свежий повторный прогон.

## 2. Что из предыдущего аудита реализовано правильно

| Направление | Текущее состояние | Решение |
|---|---|---|
| N02: GLM frustum planes | Используются строки `P*V`; translated-camera test проходит | Сохранить. Расширить property-тесты, не возвращать column extraction |
| N06: cooldown identity | Структурный ключ `(cx,cz,kind)` с полноценным equality | Сохранить; прежняя коллизия упаковки координат устранена |
| N01: атомарность нескольких материалов | Group staging при частичном OOM сохраняет предшественника; соответствующие tests PASS | Принцип сохранить. Реализацию публикации нельзя считать закрытой: R01–R04 |
| N01: полный набор материалов dirty-чанка | Inputs дополняются из `GreedyCache`; удаление одного из нескольких материалов покрыто | Полезная коррекция, но не покрывает удаление последнего материала и backend switch |
| N03: начало deadline | Перенесено перед `UpdateStreaming` и `TickAsyncChunkSystems` | Исправление верное; соблюдение единого бюджета ещё не обеспечено |
| N05: прогресс record | Повторный truth sample без смены опубликованного identity больше не всегда обновляет progress | Улучшение; aggregate age и источник GPU identity ещё ненадёжны |
| N07: dual lanes | FM/remesh разделены, K3/M3 fixtures получили реальный remesh demand и теперь проходят | Сохранить семантику lanes; убрать перекрывающие quota/floor decisions при новом admission |
| N08: fail-closed Q9/Phase57 | Старые грубые случаи неполных данных закрыты | Не переносить этот статус на новый eye-proxy, где есть новые контрпримеры |
| N04: LegalDark rollback | Отказались от blanket equal-rev→LegalDark | Правильно: очередь не определяет физическую освещённость |
| I3t: hold prior on stale result | Снижает риск замены существующей картинки устаревшим результатом | Допустимо как временная политика; не исправляет allocation ownership и не обеспечивает convergence |
| Fence retirement, skylight merge, immutable mesh catalog | Основные полезные изменения сохранены, локальные tests проходят | Не отменять ради восстановления старой картинки; расширить интеграционные проверки |

Возраст OpenGL/greedy сам по себе не объясняет эти дефекты. Современный renderer всё равно обязан доказать lifetime и идентичность ресурсов; переход на Vulkan/Nanite не исправит неправильный `Free`.[^nvrhi]

## 3. Что показывают пролёты

### 3.1. Корпус и несовпадение маршрутов

| Raw 16 сентября | Роль по сохранённым отчётам | Period / spike | Движущийся участок | Free slots в конце |
|---|---|---:|---|---:|
| `074859_20292` | Утренний ручной baseline с visual FAIL | 22 / 36 | `cx 7→−3`, затем `cz=4` | 297 718 |
| `111708_46784` | I1 counters | 36 / 142 | `cx 7→2`, затем stop | 332 906 |
| `114041_34044` | I3a Flow stalled remesh | 36 / 139 | `cx 7→2`, затем stop | 370 177 |
| `115808_26448` | I3t hold prior | 36 / 131 | `cx 7→2`, затем stop | 386 262 |
| `120154_22524` | I4 control | 36 / 135 | `cx 7→2`, затем stop | 375 543 |
| `123828_22764` | Последний доступный ручной маршрут | 24 / 46 | `cx 7→−3`, `cz=3` | 356 728 |

Во всех четырёх автопролетах `eye_proxy` выбирает 12 period rows средней трети с `cx∈[2,5]`; **8 из этих 12 имеют speed≤2**. Поэтому результат этой «mid-corridor» проверки нельзя считать проверкой мигания при непрерывном полёте. Автомаршрут также не покрывает западную часть `cx=1…−3`, где ручной полёт продолжает нагружать renderer. Стартовые speed 350–850 — артефакт установки позиции; фильтр только `speed>2` захватывает их.

### 3.2. Повторный расчёт ручных маршрутов

Диагностический фильтр: только `kind=period`, `2<movement_speed<20`, `−3≤focus_cx≤6`, `player_y≥54`. Он даёт 16 записей / 556 представленных кадров для `074859` и 16 / 527 для `123828`. Траектории/высоты и состояния мира не тождественны: **это описание состояния, не контролируемый A/B эффект коммита**.

Ниже — медианы period means, не per-frame p50/p99:

| Время, ms | `074859` | `123828` |
|---|---:|---:|
| Wall | 63.30 | 64.04 |
| Stream (`UpdateStreaming` + async systems) | 35.54 | 32.09 |
| Вся world streaming phase | 46.95 | 47.11 |
| Mesh-emerge | 10.08 | 14.53 |
| Scene | 9.63 | 10.64 |
| Opaque refresh | 5.45 | 5.47 |
| Spawn-ring readiness query | 5.86 | 9.11 |

Времена вложены: нельзя складывать world streaming phase со stream/emerge, а opaque refresh — ещё раз со scene. Главный видимый резерв остаётся на CPU/main thread. По `1000/median(wall)` порядок скорости — около 16 FPS; это не распределение FPS.

Другие наблюдения для `123828`:

- `near_focus_holes=0` **во всех 24 period samples**, хотя пользователь сообщает дыры; `unfinished_visual` достигает 14. Следовательно, существующая near-hole метрика не покрывает класс текущих визуальных дефектов.
- В диагностическом движущемся срезе VB median 90, FullyDark stalled 58.5, `stale_vl_rev=0`. Это census, не 90 подтверждённых чёрных объектов на экране.
- `publication_incomplete_material` и `publication_oom_retain` равны нулю; это не исключает dangling resident memory или ghost draw.
- `frame_deadline_remaining_ms=0` во всех выбранных записях. Наличие FM=1/remesh=2 по счётчикам schedule не означает завершение/публикацию именно нужных jobs.
- `pool_free_slot_n` растёт **110→356 728**; при этом `gpu_pool_used_mb` заканчивается на 7.95808, capacity — 128 MB. `UsedBytes()` — bump cursor/high-water, **не сумма всех уникальных live allocations**. Низкий UsedBytes не опровергает испорченный free-list.
- В конце `pass_mesh_rev_lag_max=752`. Это разница глобальных ревизий, а не возраст конкретной неправильной текстуры.
- В последнем period `mesh_gpu_kick_ms=783.916`, `mesh_gpu_finish_ms=1059.51` при wall=92.2179 ms. Это не доказательство GPU stall >1 s в таком кадре: таймеры накопились, см. R09.

### 3.3. Что нельзя заключать из сводок

В I4 report записан `wall_ms_fly_med≈55.73`. На raw `period` с явным `2<speed<20` получается median **77.60 ms**, 8 записей; на историческом фильтре с y≥54 — 86.69 ms, 7 записей. Разные селекторы измеряют разные наборы. Ни одно из чисел нельзя молча называть «FPS пролета» без определения сегмента.

Из малых `gpu_cull_exec_ms` следует только то, что измеренный cull dispatch не доминирует в своей области измерения. Время всего GPU frame и возможный перенос ожидания драйвером между GL-вызовами этим не измерены. Такая проблема атрибуции main-thread upload описана и в Voxel Tools.[^voxelperf]

## 4. Реестр проблем текущего решения

| ID | Приоритет / доказательство | Нарушение |
|---|---|---|
| R01 | P0 / E,C,M | Сохранённый draw batch освобождает свой диапазон; двойное освобождение и alias |
| R02 | P0 / E,C | Нет явной пустой замены/удаления; dirty и ghost geometry могут остаться навсегда |
| R03 | P1 / C,M | CPU-pool/packed имеют раздельных владельцев публикации; проверка overlap смотрит не на фактическую MDI-table |
| R04 | P1 / E,C | Epoch смешивает upload progress с identity таблицы; глобальный meshRevision блокируется чужим dirty |
| R05 | P1 / C,M | Stale acceptance/hold смешивает discard, commit и progress; ревизия света маркируется по current, не по источнику |
| R06 | P1 / C | Snapshot зависит от visual-neighbor state, которого нет в валидируемом ключе |
| R07 | P1 / C,M | FullyDark census управляет repair; наличие работы считается прогрессом, age не привязан к конкретному долгу |
| R08 | P1 / C,M | Deadline общий по старту, но не по расходу; ring cache неполон и не сокращает работу под debt |
| R09 | P1 / C,M | Накопительные/last-frame/averaged таймеры смешаны; названия счётчиков утверждают больше, чем измеряется |
| R10 | P1 / E,C,M | Eye-proxy fail-open и неполный маршрут; нет независимого материально-геометрического oracle |
| R11 | P2 / C | Buffer barrier и identity асинхронных cull stats неполны; frustum guards скрывают overdraw |
| R12 | P1/P2 / C,E | Credits не покрывают payload lifetime; tests/CI и границы модулей не защищают сквозные контракты |

### R01. Renderer сохраняет batch, но освобождает его память

Путь: [GreedyGpuPublication.cpp:336](E:/Work/Home/Cubatarium/src/Render/Engine/GreedyGpuPublication.cpp:336). Для координаты без inputs старый batch копируется в `staged`, но `retained[n]` остаётся false. Следующий цикл вызывает `ReleasePooledBatch` для исходного batch. В `staged` остаётся `pooled=true` и прежние offsets.

Контрпример использует **непустой** список видимых refs: сначала публикуются A и B, затем приходит только B. A сохраняется по заявленной политике untouched retention, но его allocation освобождён. После signaled fence новая C получает адрес A. Запись C меняет байты A, всё ещё находящегося в draw-table. Повторная публикация только B повторно освобождает A: два следующих allocations получают один диапазон.

Результат настоящего кода:

```text
retained_batch_aliases_new_allocation=1 retained_bytes_overwritten=1
repeated_omission_two_live_allocations_overlap=1
```

Ошибка введена в ветке N01 v2 retention (`234b77b6`). Старые group-OOM tests проверяют количество batches, blockId, dirty и epochs, но не ownership/непересечение диапазонов после нескольких кадров. Даже тест «untouched-only» подтверждает лишь метаданные. Сам fence allocator правильно ждёт старую работу, но не знает, что таблица продолжает **будущие** draws по освобождённому адресу. Дополнительный fence или задержка на N кадров этот контракт не исправит.

Связь с картинкой: draw сохраняет material/coord старого batch, но читает vertices/indices другого. Это достаточный механизм неверной геометрии и материала. Вклад в каждый конкретный артефакт `123828` пока не доказан frame capture. Растущий free-list во всех шести логах — сильное согласующееся наблюдение, не самостоятельное доказательство адресного overlap в записанном кадре.

Системное решение: allocation handle с generation и явным владельцем; `Free` разрешён единожды после отсоединения всех published references, затем fence retirement. Проверяемые множества Live/Retired/Free должны быть непересекающимися. NVRHI показывает именно сочетание resource references и GPU completion, а не замену владения одним fence.[^nvrhi]

Отдельная стоимость: [TryAllocateFromFreeList](E:/Work/Home/Cubatarium/src/Render/Engine/GreedyVertexPool.cpp:228) линейно ищет слот и делает `vector::erase`. При сотнях тысяч записей это ненужная CPU-работа. Сначала исправить дубликаты, затем измерить необходимость bins/coalescing; оптимизация поиска в испорченном free-list не является исправлением.

### R02. Отсутствие inputs не различает невидимость, удаление и смену backend

`PublishPassInputs` группирует только непустые `batch`. Поэтому dirty-координата с пустым authoritative pass set вообще не попадает в commit. Воспроизведено: «последний material удалён» оставляет старый batch, dirty и старую pass revision. Тест удаления material 1 при сохранённом material 0 этого случая не покрывает.

До backend доходят как минимум три разных события:

- Координата временно не видна: ресурс можно сохранить resident, но не обязано быть draw.
- Mesh стал пустым или chunk удалён: нужно опубликовать tombstone и освободить предшественника после последнего использования.
- CPU batches заменены packed geometry: старый pool backend обязан перестать рисовать этот pass после атомарного переключения.

Сейчас все три могут выглядеть как отсутствие input. `RemoveChunk/RemoveColumn` помечают geometry dirty, но `AppendGreedyPassBatchRefs` для отсутствующего/пустого cache ничего не возвращает. Полное расширение inputs решает неполный **непустой** набор материалов, а не эту неоднозначность.

Решение: typed mutation `Replace(complete payload) | Remove(reason)` с chunk instance, pass, source stamp и target version. Пустой Replace эквивалентен подтверждённому пустому результату, а не retry. Culling не может создавать/снимать mutation demand. Это локальная транзакционная модель ресурсов, согласующаяся с явными read/write/lifetime зависимостями render graph; не требуется внедрять целиком Filament.[^framegraph]

### R03. Переход CPU MDI ↔ packed не является единой публикацией

[CommitGpuMeshResult](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:3781) привязывает packed slot, устанавливает `GpuResident`, затем очищает CPU `batches`. Прежняя MDI publication живёт отдельно; R02 не гарантирует её удаления. При этом [packed filter](E:/Work/Home/Cubatarium/src/Render/Engine/GeometryEngine.cpp:2096) ищет координату в **`opaque_draw` текущих CPU refs**, а не в фактически сохранённой `GreedyGpuOpaque.batches`.

Отсюда существует путь: CPU refs уже нет → packed разрешён; старый batch всё ещё resident в MDI → оба backend могут рисовать один chunk/pass. Обратный переход также нуждается в единой точке переключения. Hold-prior только для stale результата не лечит переключение при корректном non-stale результате и не чинит освобождение старых MDI offsets.

Важно: `pass_mdi_stale_gpu_resident_n` на самом деле увеличивается на packed ref, не найденном в `opaque_draw`. Он **не проверяет stale GPU residency** и не доказывает dual-draw. В последних логах его median совпадает с `opaque_gpu_packed_n`; использовать сочетание этого счётчика с global lag как доказательство wrong-texture race нельзя. Гипотеза T1a имеет отдельное основание в коде R02/R03, не в названии этого счётчика.

Решение: `PublishedMesh` содержит выбранное представление **для каждого pass/допустимого непересекающегося range**, а backend storage не принимает самостоятельное решение о fallback draw. Новый complete payload получает ресурсы, затем один swap выбирает successor; прежние resources уходят в retirement. Opaque и transparent могут иметь разные backend, но один и тот же диапазон поверхности не должен одновременно принадлежать обоим. Immutable resource bindings и единое владение согласуются с NVRHI.[^nvrhi]

### R04. Identity таблицы подменена «в этом кадре загрузилось что-нибудь новое»

Контрпример: публикация `[A,B]`, затем reorder `[B,A]` без новых uploads меняет `cache.batches`, но не `publicationVersion`: bump разрешён только при `any_fresh`. Аналогично чистое удаление части списка без fresh upload может не менять version.

При этом `PendingGeometryDirty.empty()` — глобальное условие продвижения pass mesh/cull/sort revisions. Один пустой или другой застрявший dirty-чанк удерживает pass revision, хотя соседние чанки давно опубликованы. [MdiVertexPoolStore.cpp:776](E:/Work/Home/Cubatarium/src/Render/Engine/MdiVertexPoolStore.cpp:776) снова считает geometry refresh необходимым и перестраивает таблицы.

Не следует утверждать, что каждый reorder прямо использует старые indirect commands: текущий путь дополнительно сбрасывает compact flags и повышает `batchTableRevision` при rebuild. Эти защиты снижают риск, но превращают identity в набор перекрывающихся условий и добавляют работу. `pubver_changed_without_fresh_n` также увеличивается именно в ветке, где pubVer **не менялся**.

Решение: раздельные версии содержимого конкретного опубликованного chunk/pass, topology/состава resident table, порядка draw-list, bounds и visibility inputs. Частичный прогресс чанков допустим; aggregate «всё догнано» — диагностический статус, не ключ invalidation всех draw consumers. Кеш должен зависеть ровно от прочитанных данных; научная формализация корректности и минимальности incremental recomputation дана в Build Systems à la Carte. Здесь это перенос принципа, не предложение использовать build system внутри renderer.[^buildsystems]

### R05. Stale result: «принято», «вычислено» и «опубликовано» смешаны

После P1/P2 light-stale принимается, geom-stale допускается при drawable. После I3t [ApplyMeshResult](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:4719) при наличии prior фактически отбрасывает результат, оставляет старое изображение и возвращает ordinary Dirty. В GPU path `CommitGpuMeshResult` делает то же и возвращает `true`; вызывающие участки увеличивают `finished/stats.Completed`. Это не новое published image.

В результате меньше `mesh_apply_stale_visual` может означать переименование reject в accept/hold, а не меньше устаревших результатов. Количество «accepted» может расти даже для результата, который позднее не прошёл revision check. Совокупность counters не даёт end-to-end conservation.

Есть отдельное нарушение provenance света: CPU/GPU install записывают `MeshedLightRevision = chunk->GetLightFieldRevision()` **в момент установки** ([CPU:4964](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:4964), [GPU:3796](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:3796)). Если light-stale first mesh допускается без prior, изображение рассчитано по старому свету, а помечено текущим. Равенство revisions тогда не доказывает равенство baked data. Семь dependencies, включая соседей, вообще нельзя достоверно заменить одной текущей central revision.

Системное решение: typed outcome `PublishedExact / PublishedProvisional / RetainedPrior / Superseded / RetryableFailure / Removed`. Сохранить source stamp у опубликованного payload; не синтезировать его из current world. При изменении input в процессе работы owner coalesces latest demand и гарантирует одну последующую попытку. Нельзя лечить starvation разрешением произвольно старой геометрии. Luanti служит практическим примером coalescing queued mesh updates и исключения одновременной обработки одной области; конкретная temporal policy здесь должна быть собственной и тестируемой.[^luanti]

### R06. Один validated key может соответствовать разной геометрии

[ChunkMeshSnapshot.cpp:149](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshSnapshot.cpp:149): `neighbor_drawable` меняет shell occlusion через `ShellBlockForNeighborOcclusion`. [InputsStillValid:65](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshSnapshot.cpp:65) сознательно игнорирует этот callback и проверяет только geom/light stamps. Сосед может перейти drawable↔not-drawable без изменения блока; snapshot shell при новом capture будет другим, но key прежним.

Это фундаментальная неявная зависимость, не вопрос коэффициента soft-defer. Простое добавление всего visual state в общий stamp вернёт churn. Простое игнорирование оставляет stale seam geometry.

Предлагаемый контракт: постоянный mesh строится по voxel occupancy/material и явно выбранному boundary policy, **не по состоянию render queue соседа**. Если нужны временные закрывающие грани при недоставленном соседе, выделить их в отдельный boundary-overlay payload с собственным owner/version и убрать при появлении соседней published coverage. Альтернатива — полностью версионируемая boundary dependency; выбрать её только после измерения churn. В обоих вариантах равный ключ обязан давать равный output при immutable inputs.[^buildsystems]

Relight worker всё ещё вызывает `snapshot.Compute(*registryPtr)`, хотя удерживает `catalogKeep`. Pin защищает сравнение указателей при install, но не делает все live registry reads неизменными. Для hot reload нужен immutable lighting catalog либо запрет mutation до join/drain. Сейчас это **риск при изменяемом catalog**, не доказательство причины сегодняшней подмены текстур.

### R07. Repair-loop не знает физическую причину дефекта и реальный прогресс

`BatchesHaveFullyDarkFace` возвращает true при **одной нулевой вершине** не-bottom face; это не доказательство полностью чёрного чанка, даже не обязательно целой грани. GPU тоже хранит признак наличия dark face. `ClassifyVisibleBlackColumn` отличает LegalDark от stalled по ticket/progress/sticky, а не по независимому световому эталону. Из того, что relight не меняет поле, не следует ни «нужно бесконечно remesh», ни «это точно нормальная пещера».

В current code это не только диагностика: FullyDark направляет Flow repair, MarkRelit и remesh. Новая `RemeshTicketedFullyDarkStalledNearFocus` снова обходит колонны/высоту, выбирает до четырёх Dirty и помечает Meshing. Есть и локальный дефект: условие `if (dirty_n > 0)` после каждой колонки использует **общий**, а не per-column прирост; следующая колонка может получить Meshing без новой работы ([World.cpp:3265](E:/Work/Home/Cubatarium/src/World/Core/World.cpp:3265)).

`ColumnHasRepairProgress` возвращает true для PendingLight, Dirty, RAA или любой inflight работы в колонке. Это **наличие работы**, не изменение результата нужного чанка. `AdvanceOldestDebtAgeFrames` всё ещё сбрасывает oldest age при уменьшении общего количества долга; неизвестно, завершился ли старейший элемент. Нельзя восстановить per-key age из двух aggregate counts. Падающий CTest отражает конфликт старого age-контракта с частичной правкой; возвращать reset на любой GPU finish ради зелёного теста нельзя.

Решение: light demand рождается из изменения light dependencies/границ, а geometry demand — из geometry dependencies. Ввести `debt[key,target]` с временем создания, последнего **подтверждённого** изменения стадии и опубликованного version. No-op compute подтверждает отсутствие изменения только для своего точного input stamp. Для тёмной сцены без input changes система должна достигать fixed point, не производя новые tickets. Flood-fill с независимыми sky/block channels — нормальная практика; оптимизации propagation применять после доказанной правильности install и boundary dependencies.[^lighting]

### R08. Единый deadline пока не стал единым расходуемым ресурсом

[WorldViewBinding.cpp:1055](E:/Work/Home/Cubatarium/src/World/Core/WorldViewBinding.cpp:1055) теперь правильно запускает deadline до streaming. Но `ShouldDeferProducer(critical_progress=true)` безусловно разрешает работу; coordinator добавляет несколько независимых consume/kick budgets с floor `max(4.0, …)` ([ChunkEmergeCoordinator.cpp:5533](E:/Work/Home/Cubatarium/src/World/Streaming/ChunkEmergeCoordinator.cpp:5533)). Capture, schedule и finish всё ещё могут считать себя обязательным carve-out одновременно.

Spawn-ring cache [на строках 2112+](E:/Work/Home/Cubatarium/src/World/Streaming/ChunkEmergeCoordinator.cpp:2112) использует process-static state, x/z focus и enter edge. В нём нет world epoch, vertical band и полного dependency version readiness; `ring_dirty=NeedsSpawnRingCatchUp()` инвалидирует кеш каждый раз при сохраняющемся debt. Следовательно, под проблемной нагрузкой он часто возвращается к полному запросу. Это согласуется с измеренными 9.11 ms средней ring-query на ручном срезе. World switch/смена высоты при одинаковом x/z — отдельные непокрытые сценарии корректности кеша.

Решение: единый frame context, один admission ledger для времени и объёмов, заранее ограниченная emergency reserve, resumable scans с курсором и числом просмотренных элементов. End-to-end слот FM включает capture→compute→upload→publish, а не только счётчик schedule. Readiness хранится как инкрементальная проекция событий owner; slow full scan остаётся проверочным oracle. Общий timeout main-thread задач и ограниченный worker budget применяются в Voxel Tools; их конкретные численные настройки не переносить вслепую.[^voxelperf]

### R09. Измерения допускают неверную атрибуцию и ложный успех

1. `ProcessPendingGpuMeshes` делает `LastMeshGpuKickMs += local...` и `LastMeshGpuFinishMs += local...` ([4594](E:/Work/Home/Cubatarium/src/Render/Mesh/ChunkMeshCache.cpp:4594)). Сброс теперь только при `!skip_gpu_consume`, а основной coordinator вызывает rebuild с `skip_gpu_consume=true`. Перенос сохранения таймеров между стадиями превратился в сохранение между кадрами. Последние значения растут сотнями ms, но публикуются под видом времени текущей работы.
2. `AverageFromSession` сначала копирует `last`, затем усредняет только перечисленные поля ([FramePerfMonitor.cpp:2382](E:/Work/Home/Cubatarium/src/World/Diagnostics/FramePerfMonitor.cpp:2382)). Например, wall/stream/scene и ряд prep timings — means, а `mesh_emerge_prep_ms`, streamer/async breakdown и многие counters остаются last-frame. `prep>emerge` в period row не доказывает отрицательную стоимость остального этапа: агрегаты различны.
3. `mesh_apply_stale_visual` — накопительный отказ/классификация результата, не наличие мерцающих пикселей. Его абсолютная median зависит от длины сессии; новые accept/hold paths меняют смысл сравнений между версиями.
4. `pass_mdi_stale_gpu_resident_n` и `pubver_changed_without_fresh_n` названы не по выполняемой проверке, см. R03/R04. Global revision lag не является age в ms.
5. Spike rows селективны, blink повторяет состояние. Смешивание их с period rows не даёт распределение времени кадров. Производительность надо сравнивать на одинаковом capture mode; HUD включает readback и добавляет работу.

Решение: schema с `scope(frame/period/session)`, `aggregation(sum/mean/max/last)`, единицами, frame/pass/world identity и availability. Все frame timers reset один раз в начале кадра и суммируют несколько drains **только этого кадра**. Для latency хранить stage timestamps по job/chunk/target и histogram per frame. Разделить compute_completed, discarded, retained и published. Такой разбор task lifecycle/critical path соответствует Task Graph Insights.[^insights]

### R10. Eye-proxy не является тестом целостности изображения

В [compute_eye_proxy_stop_line](E:/Work/Home/Cubatarium/tools/flight_sim_run.py:325) постоянные `near_focus_holes=100`, `visual_holes=100` и stale=0 дают PASS: transitions=0, отдельного gate на ненулевое число holes нет. Второй контрпример без полей holes/publication также проходит. Это результат исполнения реальной функции, не ручная интерпретация условий.

Дополнительно:

- Средняя треть времени подменяет spatial route segment; fallback принимает другой участок вместо сообщения «маршрут не покрыт».
- Порог stale сначала был 6, затем поднят до 8 под известный thrash class. Без независимого пиксельного/геометрического ground truth это лишь диагностический порог, не product specification.
- `n01_v21_scorecard` допускает `operator_visual is None` в `merge_green`. Из отсутствия ручной оценки нельзя делать положительную оценку.
- Wrong-texture gate `lag>64 AND (geom_accept OR mdi_stale)` описан в N04 docs, но в проверенных `flight_sim_run.py`/`n01_v21_scorecard.py` не реализован как такой executable gate. Даже его реализация не превратила бы неверные исходные счётчики в oracle.
- `DrawOracle` для VB подставляет `has_gpu/in_commands/pixel_hit=true`; FalseNegCull синтезируется из census mismatch. Тест этой функции не проверяет rendering world.
- Adequacy требует VB не меньше заданного порога. Это допустимо для **воспроизведения старого дефектного workload**, но после настоящего исправления VB такой gate станет препятствием. Покрытие нагрузки должно определяться траекторией/доставленными колонками/событиями, а качество — отдельным результатом.

Решение: fail-closed schema и coverage; отдельные абсолютные hole/material/depth mismatch gates и temporal transitions; `UNTESTED` для отсутствующих данных/ручного наблюдения. Построить независимый reference для opaque voxel scenes: CPU ray traversal по snapshot мира определяет ближайший block/face/material/depth, GPU ID/depth pass возвращает реально нарисованное. Для block grid подходит DDA Amanatides–Woo; cutout, liquids, cross и сложные модели требуют собственной семантики и отдельных fixtures, а не ошибочного сравнения как solid cubes.[^dda]

Полезен RenderDoc для проверки draw calls, textures и buffer offsets проблемного кадра, но capture tool не заменяет автоматически определённый expected image.[^renderdoc]

### R11. GL visibility/barriers и cull stats остаются неоднозначными

[MdiVertexPoolStore.cpp:1110](E:/Work/Home/Cubatarium/src/Render/Engine/MdiVertexPoolStore.cpp:1110) после compute использует shader-storage/command barriers. Затем `ArmCullStatsAsyncSample` копирует shader-written stats через `glCopyBufferSubData`; для этой зависимости нужен соответствующий buffer-update barrier. Fence после copy доказывает завершение copy, но не заменяет visibility barrier **до** него.[^glbarrier]

Stats ring складывает результаты opaque/transparent в общие `StagedCullStatsVisible_/Valid_` без pass/sequence/age. Timestamp ring уже имеет pass IDs и sequence: это правильный образец, который не перенесён на stats ring. При этом stale sample используется для LastCullOpaqueOn и дальнейших диагностик.

Frustum rows теперь исправлены; broad distance admit и пропуск части planes остаются. Это overdraw/cull-efficiency долг, **не основание сейчас убирать все guards**: сначала coverage oracle, затем проверка CPU/GLSL planes для rotations, negative coords, large world positions, near/far и границ chunk. Математический эталон — clip inequalities/row planes.[^frustum]

### R12. Сквозной memory/owner контракт и защита CI незавершены

`MeshCaptureStore::CaptureAndStore` получает credits до capture, но guard умирает при return, а Store продолжает хранить snapshot. `TryGet` возвращает копию. В async mesh result bytes резервируются **после** построения output vectors. Это полезные локальные throttles, но не глобальная граница peak memory. Relight, mesh и другие pools также требуют общего CPU concurrency envelope, а не независимой настройки каждого в отрыве от renderer.[^voxelperf]

ColumnRecord по-прежнему восстанавливается из legacy world truth. Default cutover — EvictionOwner; `Decide*` возвращает record decision до comparison, поэтому mismatch=0 в этом режиме не доказывает parity. Generation-bearing published handle не заменён полноценным renderer-owned identity всех slices/backend. Работа со стадиями остаётся распределённой между Flow, Dirty, PendingLight, RAA, cache и callbacks.

Защита изменений:

- Workflow push pattern теперь `'cursor_audit_impl*'`, но **текущая ветка `cursor_audit2_impl` ему не соответствует**. Наличие ручного workflow_dispatch/PR пути не доказывает запуск push checks именно для неё. Семантика branch/path filters документирована GitHub.[^github]
- Штатный publication test подменяет `AppendGreedyPassBatchRefs` пустым stub и использует override sets; он не проверяет реальную цепочку cache→pass expansion→publication→backend switch.
- Существующий test retention пропускает memory ownership; новый контрпример демонстрирует этот пробел при PASS старого executable.
- Исторические raw fixtures могут отсутствовать, test возвращает SKIP; новые reports без них остаются, что затрудняет повторную проверку критериев.
- Runner имеет `kill_cubatarium_orphans`, завершающий процессы по имени. Для будущих автоматических экспериментов нужно ограничение собственным PID/process tree и отдельным world copy; аудит этот runner не запускал.

Решение: сквозные payload credits, per-world lifetime scope, unit + model-based state-sequence tests, узкий integration target без stub ключевого cache API, GPU oracle job и обязательное хранение identity/coverage артефактов. Сначала контракт, затем механическое разнесение монолитов по файлам.

## 5. Почему прежние итерации не дали устойчивого результата

Это не один «неудачный коэффициент» и не доказательство, что все прежние работы бесполезны. Сошлись четыре процесса:

1. **Локальный fix менял смысл соседнего контракта.** Batch→chunk atomicity потребовала отличить отсутствующий input от удаления. Retention добавили, но не перенесли ownership; позже version bump привязали к upload, а не к изменению таблицы.
2. **Управление лечит свой же census.** FullyDark→ticket→remesh→hold/dirty; затем изменение классификации или счётчика выглядит улучшением, хотя underlying pixels не измерены.
3. **Прогресс стадии не равен прогрессу пользователя.** Job finished или stale accepted может закончиться RetainedPrior; target изображения не достигнут.
4. **Сравниваются разные задачи.** Автополёт до `cx=2`, ручной до `−3`; moving и stop; period means и last-frame values; отсутствующие исходные логи. На такой базе невозможно надёжно выбирать architectural winner.

Поэтому «ещё один cap=4», повышение stale threshold, приравнивание darkness к LegalDark, бесконечное удержание prior или увеличение времени relight не являются системным закрытием. Каждый допустимый fallback должен иметь причину, owner, конечное условие завершения и тест convergence.

## 6. Целевая архитектура

### 6.1. Четыре независимых состояния

```text
World data + immutable catalogs
       │ change events / dependency versions
       ▼
DesiredWork (key, target, reason, age) ── admission ──► one job per key
       ▲                                                │
       │ newer target coalesces                         ▼
       └────────────────────────────── CandidateMesh(source stamp)
                                                        │ validate + stage
                                                        ▼
ResidentRegistry ── atomic Replace/Remove ──► PublishedMesh generation
                                                        │
                                  visibility query ─────┤
                                                        ▼
                                immutable DrawPacket / resource leases
                                                        │ last use complete
                                                        ▼
                                                   Retire → Free
```

`DesiredWork` не равно `JobInFlight`, `CandidateMesh` не равно `PublishedMesh`, `PublishedMesh` не равно `VisibleDraw`. Ни отсутствие refs, ни окончание worker, ни наличие ticket не подменяют другой слой.

Минимальные сущности (схема, не готовый patch):

```text
ChunkKey       = worldEpoch + full coord + incarnation
MeshInputKey   = center/boundary geom stamps + actual light stamps + catalog epoch
MeshTarget     = key + targetVersion + reasons
PublishedMesh  = key + sourceStamp + generation + quality + per-pass payload
GpuAllocation  = poolId + allocationId + generation + ranges + state
DrawPacket     = published generations + ordered commands + bounds + resource leases
```

Принципиальные инварианты:

- Allocation находится в одном ownership state; live draws не ссылаются на Free/Retired generation.
- Replace публикует весь требуемый chunk/pass payload или оставляет весь предшественник; соседние chunks прогрессируют независимо.
- Remove/empty являются положительным подтверждённым результатом. Невидимость не удаляет residency и не создаёт demand.
- На каждую поверхность в pass выбран один backend; overlay выделен отдельной ролью, не случайным fallback.
- Published source stamp отражает **прочитанные** inputs; equality current/source нельзя достигать переприсваиванием номера при install.
- Один owner target, одна активная попытка; повторные events обновляют target, а не плодят независимые repair chains.
- Dirty/no-op/retry никогда не выдаются за published progress. Без новых входных событий система сходится в quiescent state.
- Frame resources ограничены одновременно по времени, snapshot/result/upload bytes, GPU slots и worker concurrency. Memory credit живёт столько же, сколько payload.

### 6.2. Что переносить из best practices, а что не нужно

| Практика | Применение здесь | Что не переносить автоматически |
|---|---|---|
| Reference lifetime + GPU completion | Published generations и draw leases | Не внедрять целую RHI только ради одного бага |
| Explicit render dependencies | CPU/packed switch, barriers, derived draw cache | Не путать lifetime внутри frame graph с межкадровым world ownership |
| Incremental recomputation | Точный stamp, changed-set readiness, coalescing | Не расширять stamp всем изменяемым runtime state |
| Prioritized bounded workers | Один admission и конечный emergency budget | Не копировать число потоков/таймауты из чужого проекта |
| Independent reference rendering | Material/depth/coverage oracle | Не объявлять правильность pixels по наличию jobs |
| Binary greedy, meshlets, LOD | Только профильная оптимизация после correctness | Не менять алгоритм meshing как лечение dangling ranges |

Binary greedy — реальный пример ускорения meshing/уменьшения представления, но его upstream microbenchmark не обещает FPS в данном мире и не покрывает lighting/material/async contracts.[^binarygreedy] Предлагаемый порядок — применение устойчивых инженерных практик, а не утверждение, что существует универсальный SOTA voxel renderer для этого набора требований.

## 7. Детальный план системных доработок

План заменяет продолжение N04 набором локальных исключений. Работать от проверенного HEAD в отдельной ветке, например `codex/world-render-contracts`; создание ветки/implementation не выполнены этим аудитом. Исторический eye-safe baseline нужен для сравнения, но **не для переноса назад allocator/lighting fixes целиком**.

### S0. Зафиксировать baseline и исполняемые стоп-критерии

**Область:** tools/flight runner, CMake/CI, audit repro fixtures. Production behavior пока не менять.

1. Внести четыре publication counterexamples в постоянные tests; добавить cases nonempty peer refs, delayed/failed/null fence, повторный omit и reuse.
2. Добавить оба eye-proxy negative controls; отдельный expected-fail тест неправильного материала при нулевых hole counters.
3. Сохранить immutable run bundle: source SHA+dirty state, exe/shader/content hash, world snapshot hash, seed, tuning, backend/caps, GPU/driver, viewport, HUD, vsync, pose timeline, run ID, start/end, normal-shutdown status.
4. Восстановить full-west replay до `cx=−3`; зафиксировать `move`, `stop`, `edit`, `settle` как явные сегменты. Нет маршрута — `UNTESTED`, не fallback.
5. Исправить workflow coverage текущих implementation branches. Historical missing fixture — видимый незакрытый prerequisite acceptance, не безусловный success.

**Выход:** baseline воспроизводим; known defects красные; оригинальный пользовательский мир не используется для записи. Никакое изменение порога не входит в этот этап.

### S1. Восстановить GPU memory ownership

**Область:** `GreedyGpuPublication`, `GreedyVertexPool`, production pool tests.

1. Исправить передачу владения untouched batches; удостовериться, что predecessor не освобождается, пока остаётся в опубликованной таблице.
2. Ввести allocation ID/generation и state ledger. В debug/test каждый Free проверяет exact live allocation, а draw проверяет generation/ranges; double-free — немедленная диагностическая ошибка, не тихий decrement.
3. Rollback частично staged группы освобождает только её fresh allocations. Retained predecessors не меняют owner и stamp.
4. Проверять непересечение Live/Free/Retired и unique ranges после каждой операции model-based randomized sequence.
5. Отдельно исправить/уточнить byte accounting: live, staged, retired, reusable, fragmentation/high-water.

**Выход:** R01 repro exit 0, 10k+ воспроизводимых последовательностей с фиксированными seeds, existing OOM/growth/fence tests PASS; в soak free-list не растёт за счёт повторного освобождения. До этого не сравнивать performance альтернатив публикации: allocator state повреждён.

### S2. Единая typed publication и переключение backend

**Область:** `ChunkMeshCache`, `GreedyGpuBackend/Publication`, `MdiVertexPoolStore`, `GeometryEngine`, packed allocator interface.

1. Ввести `PublicationDelta` с `Replace / Remove / RepresentationSwitch`, полным key/stamp/target и complete pass payload, включая empty.
2. Сначала создать registry как adapter над текущими storage; затем переключить CPU и packed writers на одну точку commit. Не оставлять два активных writer на этапе миграции.
3. Отделить resident registry от visible refs. `RefreshPassRefs` становится построением/обновлением draw view, а не угадыванием существования mesh по frustum input.
4. Удаление chunk, последнего material, transparent-only↔opaque-only, CPU↔packed — явные операции с acknowledgement target.
5. Локальные publication generations продвигаются независимо; aggregate lag не блокирует пересборку/валидность соседних данных.
6. Draw packet order, bounds и command table получают собственные epochs; unchanged reuse проверяет их, а не `any_fresh`.

**Выход:** ноль ghost draws после Remove; ровно одно представление каждой поверхности; R02/R04 tests PASS; отдельный integration test вызывает настоящий `AppendGreedyPassBatchRefs`, без stub. Стабильная камера/мир не вызывает upload/table rebuild после завершения demand. Удаление только одного material недостаточно для закрытия.

### S3. Исправить telemetry и построить независимый oracle

S3 telemetry можно вести рядом с S1/S2 как независимый patch; конечная приёмка S2 требует oracle.

**Область:** `FramePerfMonitor`, `PhysicsTelemetry`, `DrawOracle`, renderer debug pass, flight scorecards.

1. Reset frame timings один раз до любых consumes; все stage calls добавляют в frame accumulator. Period schema перечисляет aggregation каждого поля.
2. Переименовать/разделить accepted/held/published, packed fallback/stale resident, upload/group progress/table epoch. Записывать age конкретного target в ms.
3. Fail-closed required fields, finite/range checks, immutable segment coverage, независимые absolute-hole и transition gates. `operator_visual=null` → `UNTESTED`, а не TRUE.
4. Сначала analytic scenes: solid plane, corner, tunnel, checkerboard materials, last-block delete, two material chunk, liquid boundary. Выключить fog/jitter/animation только в oracle fixture, не в product benchmark.
5. CPU snapshot ray reference + GPU integer object/chunk/material ID и depth outputs; сохранить disagreement coordinates и resource handles проблемных pixels. Для arbitrary models добавить отдельный geometry reference, не притворяться solid voxel.
6. Двухуровневая проверка: commands/resources соответствуют PublishedMesh; PublishedMesh в отведённый срок соответствует WorldTarget. Так отделяются allocator corruption, stale content и culling loss.
7. Temporal fixtures: фиксированная камера без world changes и повторяемый moving replay; wrong material, missing expected coverage, duplicate coverage, unexplained generation flips — отдельные ошибки.

**Выход:** синтетическая подмена texture/offset, пропуск command, двойной backend, постоянная дыра, переставленные timestamps и отсутствующая метрика гарантированно дают FAIL/UNTESTED. Oracle отключён в обычном gameplay; его overhead отдельно измерен.

### S4. Immutable dependencies и корректная freshness mesh/light

**Область:** `ChunkMeshSnapshot`, `MeshCaptureStore`, `AsyncMeshBuilder`, `AsyncRelightBuilder`, `MeshApplyPolicy`, cache apply paths.

1. Убрать readiness соседа из permanent geometry либо выделить versioned boundary overlay; записать ADR с выбранным вариантом и сравнением churn.
2. Разделить central incarnation/content mismatch, boundary geometry mismatch и light mismatch; ни один из них не называется просто «visual stale».
3. Published payload сохраняет полную source provenance. Не записывать current light revision поверх старого bake.
4. Explicit provisional first mesh допустим только по согласованной политике с bounded age/next target; никогда не выдавать его за exact render-ready. Старый mesh можно удерживать до безопасной замены, но edit acknowledgment требует отдельного ограничения latency.
5. Per-key coalescing: изменения во время compute обновляют latest target; завершение старой попытки не стирает новый demand. Удаление/recreate coord проверяет incarnation.
6. Relight читает immutable metadata с тем же lifetime, что и snapshot. Hot-reload/world-switch проходит cancel→drain/join→release.

**Выход:** равный input key даёт равный geometry output; neighbor-ready flip не изменяет permanent mesh; invalid incarnation никогда не публикуется; roof/side-light и torch add/remove parity; после остановки edits каждое retained/provisional состояние сходится к exact target. Изменение counters не считается convergence.

### S5. Один owner demand и отказ от census-driven repair

**Область:** `ColumnRecordCoordinator`, `ColumnFlowExecutor/Scheduler`, `World`, `MarkRelitInstall`, `ChunkDirtySet`.

1. Record содержит authoritative target/job/publication identities; события chunk load/unload, content/light changes, job completion, GPU commit меняют его явно.
2. Legacy maps сначала сравниваются в shadow **до** early return; после parity остаются read-only projections и затем удаляются из решений.
3. Dirty/Pending/RAA не считаются progress сами по себе. Хранить per-key ages и event причин непрохождения.
4. FullyDark census только диагностирует. Demand задаётся mismatch dependencies/недостающим coverage; no-op relight/mesh не создаёт следующий идентичный job.
5. Удалить Flow/H2 fallback duplicate writers после тестов единого owner, а не одновременно с его вводом. Исправить per-column Meshing bookkeeping.

**Выход:** зафиксированный свет/мир достигает нулевого нового repair work; один stale key не теряет age при завершении другого; очередь не теряется при overflow/cancel/OOM; progress conservation проверяется по IDs. LegalDark fixture не потребляет бесконечный remesh budget.

### S6. Общие resource credits и end-to-end fairness

**Область:** `PipelineAdmission`, capture store/worker, async pools, GPU pending/commit, `MeshWorkAdmission`.

1. Credit принадлежит payload до уничтожения/передачи ownership. Shared immutable snapshot исключает неучтённые копии Store→result.
2. Result memory резервировать до/во время роста output, а не после полной генерации. Определить верхнюю оценку/поэтапное резервирование с безопасным retry.
3. Один concurrency budget на mesh/relight/gen, с резервом render/main thread; отдельно I/O concurrency и GPU slots.
4. FM/remesh получают fairness на каждом необходимом ресурсе, не только при schedule. Bound на jobs/bytes/age, причины отказа и oldest-target priority.
5. Повторные запросы одного target coalesce; admission denial не теряет demand. Large job не должен навсегда блокироваться очередью small jobs.

**Выход:** stress с заполненной result/GPU/capture очередью остаётся в memory cap; обе lanes имеют измеренную bounded latency; cancellation/shutdown возвращают credits; нет незарегистрированных payloads. Значения cap выбирать после измерения worst-case payload, не по median.

### S7. Сократить main-thread critical path

**Область:** `WorldViewBinding`, `WorldStreaming`, `ChunkEmergeCoordinator`, readiness queries.

1. Снять CPU trace с nested scopes, work IDs, scanned counts, queue delay и driver calls на уже исправленном allocator. Разобрать отдельно `UpdateStreaming`, async systems, spawn ring и opaque refresh.
2. World-scoped readiness projection с ключом world epoch + focus/band + dependency epoch. Инкрементальные counters и changed sets вместо многократных полных обходов.
3. Для неизбежных scans — resumable cursor и deadline; full recompute периодически сверяет projection, не управляет каждым кадром.
4. Один ledger времени; reserve для критической progress выделяется **внутри** общего budget. Допустимый non-preemptible overrun ограничен одной измеренной единицей работы и виден в trace.
5. Исключить независимые second/extra 4 ms floors; вызывающие этапы расходуют один оставшийся budget. `critical=true` не означает бесконечное разрешение.
6. Dirty expansion/table rebuild O(changed chunks), packed exclusion — по registry key set, не вложенный поиск по всему visible list. Free-list bins вводить только если после S1 он всё ещё дорог.

**Выход:** stationary stable world не делает O(world/height) работу каждый кадр; readiness cache эквивалентен full reference; end-to-end FM latency не ухудшается ради wall. Минимальная промежуточная цель — управляемый streaming slice; целевой FPS принимается только по S9, не по одной быстрой стадии.

### S8. Renderer efficiency и GL contracts

**Область:** MDI store/cull, frustum, upload paths, geometry draw.

1. Исправить shader-write→copy barrier; stats samples получают pass/frame/sequence и unavailable/age. Поздний sample не перетирает новый.
2. CPU↔GPU cull parity на generated AABB/camera fixtures и near/far boundaries. Отдельно проверить opaque/cutout/transparent.
3. После oracle постепенно сужать broad distance/plane bypass. Не ослаблять guards и менять publication одновременно.
4. Профилировать full GPU frame, driver waits и upload/copy объёмы; выбирать CPU greedy/packed по полной latency target→published, а не скорости extract.
5. Light-only payload/update, binary greedy или LOD — отдельные ADR/experiments. Если greedy merge зависит от света, нельзя просто обновить четыре corner attributes и считать shading эквивалентным: требуется корректное resampling/разделённый light field либо remesh.

**Выход:** ноль новых coverage/material regressions, асинхронная telemetry без свежего blocking readback; доказанное снижение измеренного bottleneck. Новый backend/алгоритм не нужен, если bottleneck устраняется lifetime/ownership и incremental work.

### S9. Формальная приёмка и удаление переходных механизмов

1. Full west **до −3**, reverse/east и north, разные высоты/negative z; ручная оценка проблемного участка и автоматический oracle.
2. Cold world snapshot и warm cache — явно разные режимы; минимум три независимых повтора каждого кандидата и A/A для оценки шума. Чередование A/B уменьшает дрейф состояния системы; число повторов увеличивать, если разброс перекрывает эффект.[^benchmark]
3. Edit burst во время полёта, delete last block/material, CPU↔packed switch, transparent transitions, unload/reload same coord, world switch, normal shutdown.
4. 10–30 min soak: stable live/resident memory, отсутствие линейного роста free-list/dirty/age, дрейфа command count и накопительных frame timers. Это начальный тестовый протокол, не гарантия отсутствия всех долгих утечек.
5. Безусловные gates: live/free overlap=0, invalid allocation generation=0, ghost draw после Remove=0, unexplained material/coverage mismatch=0 в analytic fixtures; никакого `null→PASS`.
6. Для product profile зафиксировать hardware/viewport/render distance/скорость и **до эксперимента** выбрать SLA. Предложение для первого согласования: целевой frame budget 16.7 ms (60 FPS) или явно выбранный 33.3 ms профиль; streaming slice около 5 ms, edit→visible p95≤100 ms и max≤250 ms, обычный FirstMesh в защищённой зоне max≤500 ms. Это предлагаемые требования, **не измеренные достижения и не универсальные индустриальные нормативы**. Teleport/enter получают отдельный loading SLA, а не растягивают cruise.
7. При нарушении quality correctness perf-result отклоняется независимо от FPS. Не убирать fog/детализацию или замедлять камеру ради прохождения незаявленного профиля.
8. Удалить старые fallback writers/устаревшие flags/counters после passing parity; обновить документы одним closure matrix с ссылками на immutable runs.

**Выход:** состояние соответствует исходной цели пользователя — правильное изображение, своевременная доставка и согласованная скорость на заданном профиле, а не только нулевые выбранные census counters.

## 8. Карта проверок перед закрытием

| Проверка | Что должно ловиться | Что сейчас |
|---|---|---|
| A omitted, B visible, allocate C после fence | Ссылка draw на уже free range | Новый repro FAIL |
| Повторный omit, два allocations | Double-free / overlap | Новый repro FAIL |
| Empty Replace / Remove last material | Ghost и вечный dirty | Новый repro FAIL |
| Reorder без upload | Неизменённый identity изменённой таблицы | Новый repro FAIL; текущие flags частично компенсируют |
| CPU↔packed, exact и provisional | Двойное представление, stale binding | Нужен настоящий integration/GL test |
| Neighbor readiness flip, неизменные voxel inputs | Скрытая геометрическая dependency | Путь в коде подтверждён, нужен geometry parity test |
| Light update во время mesh build | Ложное current revision / бесконечный hold | Нужен source-stamp + convergence test |
| Permanent holes / отсутствующие поля | Ложный eye PASS | Два новых negative controls FAIL |
| Stop без input changes | Бесконечные no-op tickets/растущие allocations | Логи показывают рост free-list; per-key trace отсутствует |
| World switch / same xz, новая высота | Stale static readiness, incarnation reuse | Не проверено world acceptance |
| Frame timers / multiple consume | Накопление между кадрами | Код и raw подтверждают дефект |
| CPU/GLSL cull + delayed stats | Visibility false negative / чужой pass | Row fix PASS; сквозное покрытие отсутствует |

## 9. Воспроизведение материалов аудита

Для настроенного Windows/MSVC workspace с существующим static vcpkg:

```powershell
cmake -S docs/streaming/audit_2026_09_16 -B build/audit-20260916 -G "Visual Studio 17 2022" -A x64
cmake --build build/audit-20260916 --config Release --parallel 3
build/audit-20260916/Release/publication_audit.exe
python -X utf8 docs/streaming/audit_2026_09_16/GateRepro.py
ctest --test-dir build/desktop-msvc -C Release --output-on-failure
```

Диагностические repro на проверенном HEAD возвращают **1**, потому что проверяемые контракты нарушены. После исправления ожидается 0, а не сохранение «успешного воспроизведения бага».

```powershell
python -X utf8 docs/streaming/audit_2026_09_16/analyze_flights.py bin/logs/perf_20260916-074859_20292.jsonl bin/logs/perf_20260916-111708_46784.jsonl bin/logs/perf_20260916-114041_34044.jsonl bin/logs/perf_20260916-115808_26448.jsonl bin/logs/perf_20260916-120154_22524.jsonl bin/logs/perf_20260916-123828_22764.jsonl --output docs/streaming/audit_2026_09_16/flight_analysis.json
```

Статистический скрипт ничего не запускает в мире. В JSON сохранены source hashes, типы строк, размеры выборок, явно определённые сегменты и значения median/min/max доступных полей. Его таблицы — диагностические; они сознательно не выпускают `merge_green`.

## 10. Изученные первоисточники и применимость

Ссылки проверены 16 сентября 2026. Научные работы и документация обосновывают принципы; конкретные defects установлены по коду/контрпримерам этого репозитория. Наличие статьи не подтверждает гипотезу о причине конкретного кадра.

[^nvrhi]: Alexey Panteleev, NVIDIA, 2021, [Writing Portable Rendering Code with NVRHI](https://developer.nvidia.com/blog/writing-portable-rendering-code-with-nvrhi/). Изучены resource references, command-list lifetime и immutable bindings. Применение: R01/R03; требуется сочетание CPU ownership и завершения GPU, не только fence.
[^framegraph]: Google Filament, [FrameGraph](https://google.github.io/filament/notes/framegraph.html). Read/write resource dependencies и lifetime; применение R02 и S2. Межкадровые world resources требуют отдельного владельца поверх frame graph.
[^buildsystems]: Mokhov, Mitchell, Peyton Jones, ICFP 2018, [Build Systems à la Carte](https://www.microsoft.com/en-us/research/wp-content/uploads/2018/03/build-systems-final.pdf). Изучены dependency graph, correctness/minimality и separation scheduling/rebuilding. Применение R04/R06 и инкрементальных проекций; перенос модели, не готовый voxel scheduler.
[^luanti]: Luanti, [mesh_generator_thread.cpp](https://raw.githubusercontent.com/luanti-org/luanti/master/src/client/mesh_generator_thread.cpp). Просмотрены queue coalescing, inflight exclusion, block lifetime и neighbor update paths. Применение R05/S5; не предлагается копирование кода или универсальная гарантия этой реализации.
[^voxelperf]: Voxel Tools, [Performance](https://voxel-tools.readthedocs.io/en/latest/performance/). Общий лимит потоков, main-thread timeout, snapshot copies/locks и GL timing caveats. Применение R08/R12 и интерпретации профиля. Чужие численные defaults не являются SLA Cubatarium.
[^lighting]: Mikola Lysenko, 2018, [Voxel lighting](https://0fps.net/2018/02/21/voxel-lighting/). Flood-fill и разделение sky/block channels, word-level propagation optimizations. Применение R07/S4: сохранить физическую модель данных и проверять её независимость от repair queue; новые lighting features сейчас не нужны.
[^insights]: Epic Games, [Task Graph Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/task-graph-insights-in-unreal-engine-5). Task transitions и critical path; применение R09/S7. Нужна аналогичная trace-модель, не миграция движка на Unreal.
[^dda]: Amanatides, Woo, 1987, [A Fast Voxel Traversal Algorithm for Ray Tracing](https://physique.cmaisonneuve.qc.ca/svezina/projet/ray_tracer/download/A_Fast_Voxel_Traversal_Algorythm_For_Ray_Tracing.pdf). Оригинальная статья, доступная копия PDF. Упорядоченный обход voxel grid — основа независимого opaque oracle, не предложение заменить production renderer ray tracing.
[^renderdoc]: RenderDoc, [Python API documentation](https://raw.githubusercontent.com/baldurk/renderdoc/v1.x/docs/python_api/index.rst). Основание автоматизации capture/replay inspection в S3; expected world image формируется отдельно.
[^glbarrier]: Khronos, [glMemoryBarrier reference](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glMemoryBarrier.xml). Buffer-update visibility для API reads/writes после shader writes; применение R11. Completion fence не заменяет этот ordering/visibility контракт.
[^frustum]: Fabian Giesen, 2012, [Frustum planes from the projection matrix](https://fgiesen.wordpress.com/2012/08/31/frustum-planes-from-the-projection-matrix/). Clip inequalities и строки матрицы — reference N02/R11. Это фундаментальный алгоритм, не новая SOTA оптимизация.
[^github]: GitHub, [Workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax). Проверены branch/path filter semantics для R12.
[^benchmark]: Google Benchmark, [User Guide](https://github.com/google/benchmark/blob/main/docs/user_guide.md). Warmup, repetitions, reporting и random interleaving; применение S9. Whole-world runs дополнительно требуют неизменного snapshot и route coverage.
[^binarygreedy]: cgerikj, [binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing). Рассмотрено как условная meshing optimization после S1–S7. Нельзя переносить upstream throughput на полный pipeline без проверки эквивалентности материалов/света.
