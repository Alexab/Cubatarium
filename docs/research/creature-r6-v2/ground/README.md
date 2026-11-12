# R6: наземная опора ранних волн

Hedgehog, kitten, panda, penguin, rat и warthog приняты для уточнения
опоры после прямого просмотра full native sheets, сравнений, иконок и мира.
Это не полная финальная R6 приёмка.

Новая `skinned_mesh` коррекция вычисляет минимум всего реально weighted
меша, включая shapes и вторые веса. Уплотняется только root height,
суставные ключи остаются исходными. Положение по X/Z и относительная
деформация сохраняются; independent-key regression проверяет это на
экспортированной геометрии. SLERP семплирует исходные вращения, линейная
интерполяция — translation/scale. Avatar geometry не переавторяется.

`payload_change_audit.json` подтверждает по всем шести видам побайтное
равенство POSITION/TEXCOORD_0/JOINTS_0/WEIGHTS_0 и PNG, всех не-корневых
named animation channels и всего creature.json. Penguin Fly не изменён,
включая root; это исходное водное состояние, а не наземный контакт.
Все обязательные clips сохранены, включая warthog die.

`metrics.json` содержит 1025 фаз каждого clip. Во всех исправленных
наземных clips общий диапазон −0.000047…+0.000831 блока. Обычно достаточно
257 root keys. У warthog Run первая версия дала +0.001561 блока, выше
порога +0.001: root keys увеличены до 513, итог −0.000014…+0.000362.
Порог не ослаблен. Исходные суставные keys остались прежними.
`root257_contact.json` сохраняет отклонённую промежуточную диагностику.

Финальные world proofs: runtime_final_<id>/frame_002.png, кроме warthog,
для которого принят runtime_dense_warthog/frame_002.png. Его прежний
runtime_final_warthog — версия с недостаточными 257 root keys и не является
финальным evidence. Context JSON фиксируют exact payload/exe/shader hashes.
Native sheets имеют по четыре rest-стороны и восемь фаз Walk/Run/Action;
имя native_approved не заменяет решение review.json. comparison_atlas.png
и icons_final.png — обзорные листы; отдельные исходные изображения сохранены.

В regression горизонтального выпада прежнее сравнение длины массивов
заменено проверкой самой непрерывной X/Z траектории: плотный корректирующий
канал закономерно содержит больше samples, но не меняет authored lunge.
Все 35 fidelity tests проходят (34 неизменённых в полном прогоне и
уточнённый trajectory test отдельно), 9 quality/9 C++ ранее прошли.
Проверки этой волны только Release через tools/flight_sim_run.py.

Review — Codex, не подпись пользователя. Art probes имеют отключённые
performance gates. Полная R6 матрица, stride/reference speed, travelScale,
переходы и habitats, equipment/FP/save-load, независимые formats, полный
61-resource current-generator rebuild и performance ещё не приняты.

Все шесть исправлений опубликованы и повторно загружены из обычного
bin/models без QA model override. Фактический spawn подтверждён свежими
контекстами/dumps. 174/174 native кадров пиксельно совпали с принятыми
кандидатами; production_worlds.png прямо просмотрен. Оба published payload
каталога сверены с точными review bundles. См. production_preview_parity.json.
