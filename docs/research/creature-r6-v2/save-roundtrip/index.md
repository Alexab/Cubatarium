# R6: entity save/load

Два Release процесса:256 scenarios/512 actual restores суммарно.
Состояние, gear, skin, sampled geometry/pose/material submissions сохраняются;
все9 JSON файлов повторного запуска побайтно идентичны.5290 body PNG точны.
Полная R6 pending; world/user/FP/cooperative save пока не приняты.

[Метод, ограничения и следующий этап](README.md).
[Первый независимый аудит](audit_save_first.json),
[аудит после рестарта](audit_save_restart.json),
[побайтная повторяемость](restart_parity.json),
[body RGBA parity](body_parity.json), [ledger61](ledger.json).

Общий CTest не зелёный:50 passed,141 unbuilt/Not Run,1 bounds audit Failed.
[17 расхождений размеров — gate pending](bounds_diagnostic.json),
[полный CTest](full_ctest.log), [профильные11/11](targeted_release_tests.log).

Прямо просмотренные world frames после диагностик:

![Волк после первого roundtrip](runtime_save_first/frame_002.png)

![Волк после повторного процесса](runtime_save_restart/frame_002.png)

Это world continuation observations; не raster frames загруженных fixture
actors и не performance/habitat приёмка. SHA/timing: visual_review.json.

Актуальный следующий world/user этап: [четыре cooperative save/load процесса](../user-world/index.md).
