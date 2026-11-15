# Whole-frame performance checkpoint

This checkpoint evaluates staged item resources, not production promotion or
final artistic acceptance. The production catalog still uses the prior assets.

## Reproduction

```text
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/run_item_performance.py --skip-build --repeats 3 --trace-pass
```

The tool uses `tools/flight_sim_run.py` and the same Release executable for both
resource sets. It restores the same dedicated flat `ItemVisualPerfQA` snapshot
and configuration before every run. The baseline uses archived references when
available, otherwise current production resources; the candidate overlays all 70
validated staging dependencies. Both world and runtime resources are hashed and
restored afterwards. Unrelated worlds and author sources are not modified.

At 1920x1080 and 100% UI scale, vsync is explicitly disabled for this opt-in
benchmark. Each run warms up for at least eight seconds and waits for the local
terrain columns to be visually ready. Measurement then lasts at least eight
seconds and 120 frames. Recorded wall time includes the real window loop,
simulation, streaming, rendering, GUI and swap. The optional creature-pass trace
does not replace this whole-frame metric. GPU timestamp intervals are command
intervals, not an estimate of pure GPU busy time.

Scenes:

- FP with an actual selected iron pickaxe and normal viewmodel rendering.
- Twelve real saved human NPCs, both body forms and all four complete armor sets.
  The normal save loader is used because manual human spawning reserves the
  player species. Only the restored QA copy is seeded; its original snapshot is
  restored. All twelve must be drawn in every measured frame.
- Exactly 512 physical bodies distributed across all 70 item IDs; every measured
  frame verifies that none was silently removed.
  These are resting drops after warmup, not 512 simultaneous moving projectiles.
- A cold cache invalidation followed by completion of all 70 actual item icon
  textures through the normal queue, and then warm catalog frame measurement.
  Cold latency covers this complete item queue, not all blocks/creatures/skins.

Repeat order alternates baseline/candidate and candidate/baseline. The gate
compares median run p95 values; it fails if candidate p95 exceeds baseline by
more than 10%. No threshold was relaxed. Raw per-frame samples, cache-resolution
counters, texture dimensions and hardware identity are retained.

Texture memory means allocated uncompressed RGBA mip texels in the item texture
cache. It excludes texture object overhead, geometry, icon render targets and
driver VRAM allocation. The legacy loader also loads historical PNG siblings
that are not referenced by its active mesh. Those are real old-path allocations
and explain why total catalog texture allocations can decrease despite higher
resolution new albedos. This is not a universal claim about the size of every
new texture: the selected FP pickaxe and complete armor sets increase it.

## Defects found and corrected

- The new surface asset and albedo lookups repeatedly canonicalized filesystem
  paths during rendering. Spelling aliases now share canonical ownership and are
  invalidated on asset revision/reload. Unit/native checks cover the fast path;
  benchmark counters verify no canonical resolutions during warm rendering.
- Equipment path lookup now retains successful resolution until content revision.
- Consecutive primitives in one model/node avoid redundant model/MVP uploads and
  identical texture binds. Animation, material uniforms and node visibility remain
  evaluated normally.
- Tiny ground objects sampled a block using `floor(position)` although blocks are
  centered on integers. Sampling the ground's darkness made them black. Loose
  items now use the same rounded air-cell/solid fallback as low creatures; FP eye
  lighting uses the common world-to-block mapping.
- The original forest fixture directly edited terrain and measured unsettled
  geometry. Its diagnostic pair (approximately 299/423 ms p95) is not acceptance
  evidence. The new fixture does not edit terrain.
- Invalid FP/armor fixtures were rejected, corrected and rerun, not counted as
  successful measurements.
- CLI world creation destroyed the GL context before its core/world owners.
  Creation now releases them while the context is still current.
- Release crash analysis also found the immediate shutdown failure in the sound
  bank: `ma_sound_uninit` accessed an already destroyed engine node graph from
  `USoundBank::Shutdown`/`UAudioDirector` destruction. Both audio disable and
  worker termination now free the bank before the engine. A real flat-world CLI
  creation subsequently returned zero with 81 saved chunks. No Debug build was
  used for diagnosis.

## Results on AMD Radeon(TM) Graphics

Three pairs per scene, 19,149 measured frames in total. Every run passed scene
validation; world, config and all 210 overlaid runtime files were restored.
All twelve warm candidate runs recorded zero asset/texture canonical resolutions.

| Scene | Legacy median run p95, ms | Candidate median run p95, ms | Change |
|---|---:|---:|---:|
| FP | 10.267 | 10.328 | +0.6% |
| 12 armored NPCs | 11.083 | 12.119 | +9.3% |
| 512 resting drops | 23.715 | 14.390 | −39.3% |
| Warm item catalog | 10.623 | 10.577 | −0.4% |

The armor result passes narrowly and must be checked again after final
publication and on broader hardware. It is not a comfortable universal margin.
Cold completion of all 70 icon textures had median latency 883.7 ms legacy and
982.5 ms candidate (+11.2%). This is reported separately from the warm-frame gate.

| Resident item texture mip texels | Legacy bytes | Candidate bytes |
|---|---:|---:|
| Selected FP pickaxe | 32,768 | 349,524 |
| Four complete armor sets | 720,896 | 22,369,600 |
| All 70 item resources | 43,384,832 | 37,311,736 |

The complete candidate cache contains 15 textures at 128, 39 at 256 and 16 at
512 pixels, each with a full mip chain. `performance.json` identifies the exact
measured executable and original world hashes. Compressed raw reports/traces are
under `runs/`; `raw-artifacts.json` records their uncompressed SHA-256 hashes.
The later CLI/audio cleanup verification and functional regressions use a newer
Release executable and identify that hash separately. Final production acceptance
will repeat performance on its final executable/resources.

## Acceptance limits

Results apply to this static flat world, this hardware and these resource sets.
Day/night/interior art review, action transitions, broader hardware and final
production performance after publication remain separate acceptance work.
The overall redesign goal remains active.
