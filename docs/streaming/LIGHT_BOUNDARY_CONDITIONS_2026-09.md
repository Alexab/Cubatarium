# Light boundary conditions (A21 P4.1)

## Channels

- **Sky** and **block** light are separate fields (S6).
- Zero in both channels is a **legal** cave result when `LightValidity` holds.

## Neighbor states (not interchangeable)

| State | Meaning for meshing / light |
|---|---|
| Unloaded | No voxel/light halo; provisional coverage or wait |
| Loaded, light unknown | Geometry may emit; light bake waits on field rev |
| Loaded, light known | Field revision advances; mesh may remesh |
| Drawable / hidden | Renderer visibility — **not** a light/geometry stamp key |

## Propagation

- Source/occluder change → propagate / remove-propagate on affected channels.
- Version **all** light fields actually read (including halo), not only center chunk.
- Matching center revision with stale halo → reject/rebuild (invalid).
- Matching center+halo with dark vertices → **valid**; skip remesh (A21-07).

## Seam

Temporary seam coverage uses a separate peer coverage generation; see
[ADR_SEAM_COVERAGE_CONTRACT_2026-09.md](ADR_SEAM_COVERAGE_CONTRACT_2026-09.md).
