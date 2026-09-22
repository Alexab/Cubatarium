# ADR: Seam coverage contract — target variant 1 (A21 P2)

Date: 2026-09-22  
Status: Accepted (target; full seam extract lands with P4)  
Context: A21-02 / A21-03 — temporary BoundaryOverlay and column FaceDebt conflate
coverage, readiness, and drawable residency.

## Decision — target variant 1

**Permanent mesh is independent of drawable residency.** Seam/coverage debt is a
**separate** obligation keyed by chunkXYZ / face / peer generation, not by
“column Ready because one Y-slice published.”

### Rules

1. **Permanent mesh ≠ drawable.** A chunk may hold a valid mesh artifact while
   pending successor work exists; Retain does not imply demand cleared.
2. **Seam is separate.** Face/seam coverage is not encoded as temporary opaque
   stub geometry that substitutes for the permanent mesh. Overlay may exist as a
   provisional diagnostic/heal aid only until P4 extract.
3. **`provisional ≠ Ready`.** Boundary overlay / SoftDefer / Unlit emit must not
   flip column or chunk Ready. Ready requires published demand satisfaction for
   required slices, not “at least one lit drawable in the column.”
4. **No cyclic drawable wait.** Chunk A must not wait for B’s drawable while B
   waits for A’s drawable to admit mesh. Dependencies are peer **coverage /
   light generation**, not mutual drawable presence.
5. **`water ≠ opaque stub`.** Water/fluid surface is not an opaque mesh stub used
   to fake seam coverage. Fluid remains its own subsystem (P6); seam contract
   must not treat water quads as permanent opaque face closure.

### Interim (P2)

- Shadow `NoteFaceDebtSatisfied(chunkXYZ, face_mask)` clears debt for the
  **publisher chunk only**.
- Legacy `ClearFaceDebt(column)` remains until cutover (TODO), and must not be
  read as proof that other Y-slices are debt-free.

## Related

- `ADR_PER_CHUNK_RENDER_DEMAND_2026-09.md`
- `ADR_BOUNDARY_OVERLAY_2026-09-16.md` (interim overlay; not Ready)
- Engine remediation plan P2 / P4
