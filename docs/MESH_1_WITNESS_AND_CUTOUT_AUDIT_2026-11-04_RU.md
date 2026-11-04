# MESH-1: witness дыр и аудит Cutout internal faces

Дата: 2026-11-04.

## MESH-1.1: witness редактирования

Выполнен Release `break-stand`, 45 с, на отдельной копии `World_242_mesh_1_witness`; исходный мир пользователя не изменялся. Включены visibility audit, dense framebuffer pixels и screen-ray witness. Артефакты: `bin/suite_reports/mesh_1/break_witness/`, сводка `pixel_analysis.json`.

Результаты:

- 20 edit→publish: median 5,097 мс, p95 13,685 мс, max 56,187 мс, pending edits в конце 0;
- 5 `break_dark_face` и 18 low-luma pixel samples;
- все 18 low-luma samples не изменили RGB между pretransparent и final; 15 относятся к drawable opaque с unsettled light;
- pixel/ray evidence не содержит подтверждённого отсутствия exposed opaque surface; низкая яркость сама по себе не отличает тёмную текстуру/ориентацию от дефекта света;
- runner `pass=false`: сохраняются существующие readiness/visual gates; это не объявляется PASS визуального качества.

Следовательно, конкретный missing face/MDI range не получен. Правки `NeighborHidesFace`, MDI и публикации по этому witness не выполнялись. Для следующей итерации нужен frozen кадр с одним удалённым блоком и явная exposed-face/blank-pixel пара, включая все соседние chunk slices.

## Отставание за кольцом и свет

Предыдущие профили PERF-0.7/0.9 и текущий witness согласуются: worker start/build коротки, хвосты находятся в demand/light/publication и GPU output backpressure. В текущем прогоне mesh build median 0,635 мс, built→publish median 18,642 мс, demand→publish p95 6,379 с. Pending light в конце focus ring равен 0 при сохраняющемся missing mesh.

Это не доказывает единственную первопричину для всех сцен, но исключает обоснование оптимизации только mesh builder или вслепую увеличение FirstMesh cap данными этого этапа.

## MESH-1.2: Cutout

В CPU `NeighborHidesFace` пара Cutout–Cutout не скрывает общую грань. GPU packed mask использует occupancy-классы и может скрывать такие грани иначе; существующий `gpu_extract_mesher_parity_test` покрывает только opaque stone и не является Cutout parity gate.

Удаление общей грани одинакового Cutout ID уменьшает внутренние квады, но меняет изображение, видимое через alpha-просветы внешних блоков: внутренняя грань может служить видимым backing. Поэтому такая оптимизация не внесена без controlled alpha A/B и синхронного изменения CPU, CPU reference и всех GPU mask/extract путей.

Прежний изолированный PERF-0.3 leaf texture A/B не показал выигрыша FPS при изменении alpha-покрытия; он не является измерением internal-face removal, но и не даёт основания ожидать ускорение только по числу удалённых квадов.

## Статус

Этап завершён как исследовательская итерация с записанными положительными и отрицательными результатами. Дыры при break, визуальный lag, готовность света и Cutout optimization остаются открытыми; production mesh/scheduler код не изменялся.
