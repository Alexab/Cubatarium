# Foot observer v3: Release и регрессия

2026-11-12. `CreatureQaFootProbe.h` расширен с rigid lower joint на vertices,
целиком взвешенные к semantic lower-foot subtree. CPU transform использует
полный4-slot skin. В выводе есть joint membership/skin bindings; dedup учитывает
weights, поэтому coincident points разных привязок не теряются. Это QA observer,
по умолчанию выключен, не pose/physics/animation correction.

Release build exact command:
`cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8`.
Build/verify-windows-exe PASS. SHA
**30714e6226629f931ca35259bc440011ca1220a7c7544295847f44cc5f628e52**.
Предыдущий7e7f668f… и текущий EXE/header сохранены в
`assetgen_output/creatures/foot_probe_v3_runtime/`; hashes в capture contexts.

Enabled-path проверка:13-joint live и два19-joint own badger variants на обычном
world motor. Surface membership/bind weights независимо проверены по glTF,
включая claws/foot descendants. Это проверка известных pilot meshes, не любых
будущих blend layouts/terrain/GPU readback. [World evidence](../badger-stance-ik-v2/world_support.json).
Предыдущий v2 guard в ankle-control отчёте — исторический; v3 разрешает
известные13/17/19-joint rigs с четырьмя lower roots.

## Default-off production, полный текущий каталог

Flight через `flight_sim_run.py`, Release, `CreatureQaPerf_HintV1`,8s,
preview species взяты из предыдущего проверенного56-species context,
matrix+FP+save, observer/timing выключены.

- [5290/5290 body RGBA exact](production_valid/body_parity.json) к7e7f668f…,
 61resources включая5skins. Resources/shaders/metadata exact, разрешено только
 EXE change, нет exceptions для отдельных изображений.
- [936 authored FP RGBA exact](production_valid/fp_exact.json).
- [FP native audit](production_valid/fp_audit.json):936frames/1872authored GPU
 draws/6retained cases, user/legacy/material checks PASS. Не полный FP camera gate.
- [Save audit](production_valid/audit_production_final.json):128cases/256loads
 PASS. Не совместимость любых user/cooperative whole worlds.
- [Ledger](ledger_after.json):61/61R5 fresh, staged/published/bin payload exact.
 Все61R6 pending; известный bounds FAIL17 не исправлялся.

## Ошибочный QA ввод сохранён и исключён

Первый `production/` запуск был передан с вручную составленным неверным списком
species:11 отсутствующих IDs и10 пропущенных. Он не является проверкой полного
каталога, не используется для regression PASS. [Invalid capture](production/invalid_capture.json),
raw failure/context сохранены. После terminal exit1 выполнен новый fresh
`production_valid/` run со списком из authoritative reference context.

Wrapper теперь отклоняет отсутствующие species **до** flight; отдельная
[preflight negative check](catalog_guard.json) подтверждает rejection без запуска.
Это QA input validation, не изменение engine fallback behavior.

Ни default-off regression, ни observer evidence не принимают новый gait по art,
performance, terrain, compatibility или всем56+5/B/R/G. Цель active.
