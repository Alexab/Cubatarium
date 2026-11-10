# Cruise time-budget diagnosis (`083708`)

Manual inland −485/50→55 after cruise time-budget A–D
(physics debt drop, no moving Immediate, pin miss without pending,
center+edge Capture). Compare:

- `bin/logs/perf_20260817-215411_29916.jsonl` (manual pre time-budget)
- `bin/logs/perf_20260817-232819_19008.jsonl` (autofly after A–D)
- `bin/logs/perf_20260818-083708_13240.jsonl` (this manual)
- enter: `enter_lit_20260818-083727.jsonl` (3 rows, spawn freeze)

Filter: `chunk_count ≥ 80` (518 cruise samples; 437 spike + 81 period).
Corridor focus (−485,50)→(−485,55). **358 / 518 frames stay at cz=50**
(linger/hover) — 215411 had 47. Split linger vs moving vs late.

Do not “fix” with Unlit near, Imm-as-FirstMesh, `keep_h=2`, or `*FloorMs`.

## SLA vs `streaming_cruise_sot.md`

| Metric | Target | **215411** | **083708 all** | **083708 cz=51–54** |
|--------|--------|------------|----------------|---------------------|
| wall med / p90 | ≤130 / 220 | 198 / 355 | **138 / 167** | **104 / 121** |
| drain med / p90 | ≤8 / 25 | 32 / 79 | **12 / 15** | **9.7 / 12** |
| do_movement med | ≤16 | 56 | **26** | **25** |
| miss frame% | ≤47 | 98.7 | 99.6 | 100 |
| visible_black med | ≤25 | 75 | **95** | 74 |
| opaque_draw med | not collapse | 894 | 879 | 875 |
| late cz≥55 opaque | ≥200 | 111 | **95** | — |
| late packed / pool | not dump | 29 / 2.3 MB | **29 / 14.0 MB** | — |
| schedule_ok med | ≥8 | 4 | **7** | 4 |
| pending_light med | — | 14 | **8** | 12 |
| skip med | — | 71 | 65 | — |
| capture full_n | ≈span, not 9× | (3×3×5) | **2** (+9 light) | 2 |

Time SLA is **almost green on the whole log** and **green on the actual
moving corridor**. Miss% / visible_black / late opaque are **not**.

Enter hitch (first cruise row): wall **1073**, stream 264, drain 254 —
not the steady cruise.

## Three segments (the log is not one cruise)

| Segment | n | wall med/p90 | Imm sum | drain med | phys med | emerge med |
|---------|---|--------------|---------|-----------|----------|------------|
| cz=50 linger | 358 | 146 / 174 | **319** (~20 ms) | 13 | 26 | 40 |
| cz=51–54 moving | 50 | **104 / 121** | **4** | **9.7** | 25 | **16** |
| cz≥55 late | 89 | 112 / 128 | 73 | 6.5 | 27 | 38 |

A/B/D work on the moving slice. The all-sample Immediate sum 405 is
**idle underfeet Imm during the long cz=50 stand** (policy B allows
idle Imm). cz=51–54 Imm is 4 frames — cruise gate held.

Autofly `232819` could not show this: hidden-window phys ~82 ms and a
short fly. Manual is the SoT for A (`do_movement` 56→26).

## Wall composition (all cruise)

Median 138 ≈ stream 37 + emerge 38 + render 36 + phys 26.
`phase_budget_over` 487/518; `emerge_budget_ms` still **8** (stream ate
the 24 ms phase). Drain 12 sits **inside** stream, not extra.

| Step | 215411 med | 083708 med | Verdict |
|------|------------|------------|---------|
| Relight Capture unit | 32 (3×3×5 memcpy) | **12** (full_n=2, neigh_light=9) | D worked; leftover is Apply/FIFO, not memcpy |
| Locomotion | 56 (12 substeps) | **26** | A worked; residual is `RunLegacyPhysicsFrame`, not the spiral |
| Immediate | med 0; 61× ~62 ms | med **20**; **405× ~21 ms** | Idle linger, not moving cruise |
| mesh_emerge | 59 | 38 | Cheaper without 70 ms cruise Imm |
| render | 19 | **36** | Grew (GPU queue / packed) |

Drain>200: **2** (enter). Drain>600: 0. On miss, drain max **24**.

## Bottleneck 1 — FIFO overflow (new, industrial)

`relight_fifo_n` med **75** (min 57, never empty).  
`relight_fifo_dropped` cumulative **0→2859**, delta med **3 / frame**.  
`relight_completed_n==0` on **514/518** (ring occupancy lie, same as P0).  
`relight_apply_n` sum **545** (~1 apply/frame). Discarded on workers: **0**.

Cheap Capture (D) + `CaptureMovingBgCap=1` means: enqueue faster than
one Apply/frame → FIFO saturates and **drops columns**. Pending med 8
is better than 14, but the witness never cools (`pending_light≤2` on
only **21** frames). Skip med 65: RelightThenMesh still owns miss.

This is the throughput ceiling now that the unit is cheap. Raising
base `CaptureMovingBgCap` without Apply/drain budget still blows wall
(plan F). Need **Apply ≥ enqueue** or a FIFO that does not drop the
pinned miss column.

## Bottleneck 2 — pin arms, still hops

Hold policy C **does arm**: nh≤2 hold Counter is `{1: 337, 2: 8, …}`
not `{0: 92}` as on 215411. Max consecutive same miss xz **7** (was 2).
Moving slice hold max **13**. Capture horiz **matches miss horiz 348/348**.

Still not sticky enough:

- `softdefer_witness_retarget` end **961** (215411: 208; longer flight,
  but rate stays high after cz=50)
- hold **med 1** — increments then resets; FindNearest still hops
- top keys: (−485,50)×116, (−485,49)×99, (−485,51)×58… linger geometry
- miss_cy: **0×227, 1×219, 2×66** (215411 was cy=2×78) — nearer surface
- miss_horiz: nh=1×319, nh=0×156 — underfeet/near, not rim

Apply 1/frame + FIFO drop 3/frame ⇒ the pinned column’s job can be
**dropped** before MarkRelit. Pin without FIFO-protect is a leak.

## Bottleneck 3 — late draw cliff (P4, residency not CPU)

cz=54: opaque **873**, packed **228**, pool **13.7 MB**, uf7 **4/4**.  
cz=55: opaque **95**, packed **29**, pool **14.0 MB**, uf7 **51/89**.

Unlike 215411, **GPU pool does not dump to 2 MB**. Keep-until-bind holds
memory; **draw SoT still collapses** (packed 29). Late opaque p90 293
says a minority of cz=55 frames still have packed 226 — bimodal: some
stand frames recover, most do not.

`pending_gpu_queued` linger med **70** (p90 141) vs 215411 **10**. Idle
Imm + remesh (dirty_remesh 97 vs 40) floods the GPU apply queue, then
the last +Z step has no BindCommitted underfeet replacement.

uf underfeet: GpuInFlight(5)×207, MissingMesh(4)×104, None(0)×152,
NotReady(7)×55. NotLoaded(6) still gone.

## Bottleneck 4 — idle Immediate tax (linger only)

cz=50: Imm **319/358**, ~20 ms greedy, uf mostly GpuInFlight(5).  
Budget `immediate_ms_used()<8` is checked **before** the 20 ms greedy.  
visible_black **95** (counter cap) during linger — heal SoT worse while
Imm bakes whatever is underfeet, not the hopping miss column.

Moving corridor does not need Imm removed further. Idle Imm is the
wrong healer for a 358-frame stand with FIFO 75 and GPU q 70.

## What A–D did (gates)

| Phase | Gate | 083708 |
|-------|------|--------|
| A phys debt | do_movement med ≤16 | **26** (56→26; spiral gone, wrapper remains) |
| B no cruise Imm | moving Imm sum 0 | **cz=51–54: 4**; all-log 405 is idle linger |
| C pin miss | hold grows; retarget not 208 | hold arms (max 13); **retarget still climbs** |
| D center+edge | drain ≤8/25; full≈span | **12/15**; full_n=**2**; p90 **hit** |
| F bg_cap | keep base 1 | still 1; FIFO drop says Apply is the next lever |
| E COW | only if Capture still >8 | **not needed** — memcpy is 2 chunks |

Autofly `232819` phys 82 / wall 192 was **not** the manual truth.

## Root-cause statement

1. **CPU unit of Relight is no longer the cruise killer.** Capture is
   2 full chunks + neighbor light; drain p90 15. Wall p90 167 < 220.
2. **Throughput is Apply/FIFO.** One apply/frame vs 3 FIFO drops/frame
   loses the witness even when pin and Capture horiz are correct.
3. **Idle linger Imm** (~20 ms × 300 frames at cz=50) is the remaining
   wall tax on this log; the moving corridor already meets 130/220.
4. **Late opaque cliff is draw/bind, not pool.** Pool stays ~14 MB;
   packed/opaque dump at the last +Z step — same P4 tell, keep held
   memory without a drawn replacement.
5. Miss 99% is still RelightThenMesh skip + hopping near hole, not
   “need more Capture quota.”

## Next (architecture only)

Do **not**: Unlit near, cruise Imm, prune `keep_h=2`, `*FloorMs`, raise
`CaptureMovingBgCap` / `first_mesh_schedule` while FIFO drops the pin.

Do:

1. **FIFO must not drop the pinned nh≤2 miss column** (protect front /
   re-enqueue pin on overflow). Cheap Capture is wasted if the witness
   job is the one discarded (2859 drops).
2. **Apply ≥ 1 cheap job without a 12 ms hitch** — drain med 12 is Apply
   + queue, not memcpy. Then dynamic bg_cap=2 (plan F) can fire.
3. **Idle Imm**: keep allowed, but do not start a 20 ms greedy when
   `pending_gpu_queued` is already 70+ or FIFO is soft-capped. That is
   not Unlit and not cruise Imm.
4. Late keep: do not Free/skip **draw** of underfeet until incoming
   column is BindCommitted (pool already survives; packed does not).

Re-run inland −485/50. Restore spawn `(−7752, 96, 808)` yaw 90 pitch 0.
If measuring cruise SLA, do not stand 350 frames at cz=50 — or split
the audit (linger vs cz=51–54 vs cz≥55).
