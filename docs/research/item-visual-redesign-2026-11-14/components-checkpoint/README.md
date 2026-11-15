# Shared arrow resource checkpoint

The bow's separately authored placeholder shaft/head/feathers have been removed.
It now composes the actual `arrow` item source, with its three feather vanes,
forged point, UVs and albedo. The `nock` source anchor follows the existing
string pull node. No gameplay item, recipe, mass, ammunition rule or projectile
physics was added or changed.

See [authoring contract](../../../../tools/itemgen/COMPONENTS.md).
This is a self-contained packed copy of a single authoritative author resource,
not shared GPU storage or an inventory instance. Buffers and PNG bytes are
identical to the separately built arrow. Material indices/node names are remapped
without changing UVs/normals/material values. The complete bow has 276 triangles.

The builder now supports static authored components, including manual glTF
sources. It fingerprints all transitive author files and dependent physical
definitions, validates embedded builds, rejects cycles/unknown fields/missing
anchors/nonpositive scales/namespace collisions, and preserves the last valid
staging output on failure. Sources with their own active visual bindings are
explicitly rejected; a component's host mount controls its state.

Publication refuses changing an item beside a retained stale embedded copy.
The source item and affected hosts must be rebuilt and published together.
The manifest also checks the transitive dependency list. This is tested in a
disposable repository, not by promoting the actual production catalog.

## Evidence

- Seven component tests cover byte-identical geometry/pixels/UVs, nock alignment,
  preserved physical length, deterministic rebuild, authored/manual source
  preservation, stale dependencies, publication and failed builds.
- Existing manual-import and transactional publication regressions pass in the
  full runner. The new suite is part of `run_items_acceptance.py`.
- Full staged Release native regression passes: 25 CTests, gameplay 59,
  ownership 57, ownership UI 18, player window 7, icons 209, Item Lab 283,
  presentations 3355. The extra native checks independently inspect the loaded
  nock/string positions and inherited visibility at rest, partial/full draw and
  cancellation. Maximum measured anchor gap is below 1e-8 metres in model space;
  this is a transform check, not a claim of physical simulation precision.
- All 212 overlaid runtime files were restored. `conservation.json` verifies
  unchanged bytes for 70 gameplay definitions, 70 recipes and 774 legacy resource
  files. The only production manifest change refreshes the checker-code hash in
  its 70 old records; provenance and all other fields remain unchanged.
- `summary.json` identifies exact executables and scopes. Fresh-checkout evidence
  is recorded separately after committing the changed generator.

```text
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/test_itemgen_components.py
python tools/run_item_design_probes.py --skip-build
```

## Remaining acceptance

The native images intentionally include the current poor bow hand poses. The
resource reuse and string attachment are correct; the character's FP/TP holding
orientation and support-hand contact are not artistically accepted. Fix them
before production promotion. Other all-item/world art acceptance, Item Lab
overlays and final performance remain in the main plan. Production v3 count is
still zero; this checkpoint does not complete the overall goal.
