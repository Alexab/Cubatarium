# Final performance acceptance

Status: **PASS**, all seven gates in one complete uninstrumented series.
[Final summary](final-no-capture/summary.json) retains 30 runs: three AB/BA pairs
for each of FP, armor, 512 resting bodies, catalog and 512 moving bodies. Every
window lasts at least 30 seconds. The rule remains median run p95 <= reference
median run p95 * 1.10. Cold-catalog and moving-only subsets are additional gates,
not replacements for whole-scene frames. No outlier frames or failed runs were
removed from this accepted series.

| Scene | Reference p95 ms | New p95 ms | Change | Gate |
|---|---:|---:|---:|---|
| fp | 10.202 | 10.315 | +1.11% | PASS |
| armor | 11.690 | 12.060 | +3.17% | PASS |
| drops | 23.802 | 14.699 | -38.25% | PASS |
| catalog | 10.617 | 10.443 | -1.64% | PASS |
| moving | 26.891 | 14.040 | -47.79% | PASS |
| cold catalog loading frames | 44.020 | 47.713 | +8.39% | PASS |
| moving-only frames | 27.286 | 14.867 | -45.51% | PASS |

## Method and scope

The same Release executable is used for both asset sets. Its SHA256 is
`9eb54f488ea8a309ec49ea61283ee5064b0107d88028dcfbd1608ebdc6e40099`.
Baseline assets come from the preserved old production generation. The
[baseline image closure](baseline-image-closure.json) checks that all 70 old
active atlases are 64x64, and the 18 extra root-texture images eagerly loaded by
the legacy cache are unchanged reference files, not leaked new item textures.

Independent flight QA screenshot capture is disabled during timing. This avoids
adding a capture observer to a short moving-body phase. Actual production icon
rendering, GPU readback and PNG disk-cache work remain enabled in the catalog
scene. Visual screenshots are collected in separate acceptance runs. Earlier
capture-enabled measurements are retained; a causal explanation of their frame
variance is not claimed.

The final series checks 1623 hashes across src, shaders, item definitions,
authored item designs and the performance/flight runners. They remain unchanged
during all 30 runs. All tests mute audio; config, QA world and temporary runtime
resources are restored. Each moving run reaches 512 flying bodies including 256
arrows, retains all 512 bodies and supplies at least 30 moving-only frames.
All raw frame reports and flight metadata are retained next to the summary;
`hashes.json` signs these archived bytes.

Memory values count uncompressed RGBA item-texture mip texels, not driver VRAM,
and exclude creature, GUI and framebuffer textures. The full-catalog values are
43,384,832 reference bytes versus 37,311,736 new bytes; legacy eager loading makes
this compatible with the higher resolution of active new atlases.

## Verified optimizations

- Pack unchanged local vertices and rebased indices into one VAO/VBO/EBO per
  asset: 954 -> 210 GL geometry objects for one cache containing all 70 items.
  Primitive order, node transforms, geometry and texture resolution are intact.
- Cache static wear selections and default bounds; reuse material/world uniforms
  within an explicitly scoped consecutive item batch.
- Queue cold icon lookups, use a soft 4 ms budget per warmup call and nonblocking
  PBO/fence polling for item PNG persistence. Never block on an unfinished fence.
- Keep textured GUI quads static and supply their rectangle by uniform; retain
  legacy/GLES pixel-position shader compatibility. Orphan colored quad streams.
  Native tests compare both paths against independent old shaders pixel-for-pixel.

The matching [native record](static-ui/native-release/summary.json) passes 3753
presentation checks, including 57 item renderer equivalence checks and 24 GUI
RGBA comparisons across six resolution/scale combinations. The final source
verification in the [consolidated audit](../final-audit/README.md) also records
subsequent restoration of accidentally re-encoded comments/log messages. Those
edits change no rendering, timing, geometry or gameplay logic; the performance
claim remains tied to the exact executable and source snapshot above.

## Retained rejected and diagnostic runs

- `latest-before-packing.json`, `final-long-four-scene`, and
  `catalog-five-pairs-before-packing`: rejected warm-catalog gates, including
  the five-pair +13.11% result.
- `packed-geometry/catalog-first-series`: packing alone did not pass the warm gate.
- `ui-streaming/catalog-first-series`: warm passed but cold failed.
- `static-ui-repeat-with-capture`: all whole-scene gates passed, but the
  moving-only subset failed (+17.98%); aggregate remains false.
- `static-ui-interrupted`: the flight wrapper exited 0xC0020043 while Cubatarium
  itself exited 0 and produced a complete successful report. Root cause is not
  established; this incomplete aggregate is not accepted or silently ignored.
- `fp-long-cached-bounds`: earlier rejected FP series.
- `fp-timing-diagnostic`: opt-in CPU/GPU timestamps, explicitly not acceptance.
- `accepted/fp-five-pairs` and `accepted/static-ui-catalog`: intermediate successes
  are historical, superseded for final performance by the single complete series.

No threshold is waived. These results cover the recorded AMD system and isolated
repeatable scenes; broader hardware and long inhabited-world soaks are backlog.
