# Light boundary conditions (A21 P4)

Date: 2026-09-22  
Status: Working contract (reference flood still UNTESTED)

## Channels

- **Sky** and **block** light are independent channels in [0..15] (or float bake).
- Value `0` on either channel is a valid solution (caves, sealed rooms, night).
- **LightValidity** ≠ “has non-zero vertices”. Validity means the mesh bake
  matches the light field revisions actually read (center + halo).

## Neighbor states

| Neighbor | Sky BC | Block BC | Notes |
|---|---|---|---|
| Loaded solid | occludes / contributes per flood | same | Normal |
| Loaded AIR | transmits | transmits | Seam faces may emit |
| Unloaded | treat as unknown sky/block | do not invent full sun | SoftDefer / Unlit until Known |
| Hidden peer | no draw; light may still be Known | | SoftDefer≠Unknown (KEEP) |

## Propagation

- Place/remove light source or occluder → bump light field revision on touched
  chunks + required halo; remesh only when meshed_light_rev lags field rev
  (or explicit invalid).
- Dark-face census (`BatchesHaveFullyDarkFace`) is observational telemetry —
  not a sole remesh trigger.

## Acceptance

- Fully dark valid cave: finite jobs after stop; no endless remesh.
- Stale halo with matching center rev: reject/rebuild via dependency stamp,
  not via dark census alone.
