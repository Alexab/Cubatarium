# Shared authored item components

`components` assembles an existing static item into a named host node. It uses
the item's current authored sources, including manual glTF and painted layers;
it never imports geometry from an unrelated pack or reads stale production output.

```yaml
components:
- name: loaded_arrow
  item: arrow
  parent: nocked_arrow
  source_anchor: nock
  rotation_deg: [-90, 0, 0]
```

The source is built privately with its ordinary recipe and physical fit. Its
buffers, UVs, normals, images and material values are copied byte-for-byte into
`components/<name>/`. The host glTF remaps indices to those resources and mounts
the imported hierarchy at `parent`. It is a self-contained glTF bundle, not a new
runtime ownership object or a second separately authored approximation. A packed
copy costs an additional texture allocation if that component is also displayed
as a standalone item; the author source remains single and authoritative.

The source anchor's rest-world position maps onto the host mount origin. Source
geometry is already in physical metres: `scale` is an optional positive
dimensionless XYZ multiplier, `rotation_deg` uses X/Y/Z Euler order, and optional
`translation` uses host authoring units converted by its existing fit scale.
The component does not participate in fitting the host's collision envelope.
The host mount controls animation and visibility. Animated source components are
explicitly rejected; silently discarding their gameplay bindings is not allowed.

For the bow, the existing `pull`/`nocked_arrow` hierarchy moves the real arrow's
`nock` anchor with the string. Without ammunition or after cancel/release, the
mount hides the entire imported hierarchy. This changes no ammunition quantity,
projectile mechanics, physical mass, damage or item identity.

Assembly uses ordinary glTF buffers, materials and node transforms; lineage is
stored in `extras`, as permitted by the
[glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
Import/export runtime restrictions remain those of `artifact_contract.py`.

Every transitive author file and dependent gameplay definition is fingerprinted.
Changing an arrow source or physical fit invalidates the bow's staged build.
Publication verifies each embedded build and rejects publishing a changed source
beside a retained stale component copy; rebuild and publish affected hosts
together. Cycles, unknown fields/anchors, animated sources, path traversal and
namespace collisions fail without replacing prior valid staging.

```text
python tools/itemgen.py build-design --id wood_bow
python tools/itemgen.py validate-design --id wood_bow
python tools/test_itemgen_components.py
```

The native presentation QA independently checks the loaded nock/string positions
and visibility at rest, partial/full charge and cancellation. Geometric alignment
alone does not approve the character's hand pose or final artistic appearance.

For a two-handed bow, `presentation.support_grip_node` names the animated node
the drawing palm must contact (the bow uses `pull`). This is a contact node,
not a second inventory slot. `fp_aim_euler_deg` and `tp_aim_euler_deg` define
the model orientation relative to the camera/rendered-head aiming frame;
ordinary `fp_euler_deg` remains its relaxed FP orientation. The human TP solver
keeps both palms within their mesh-derived reach, without scaling the arms.
The support node must exist and be named; invalid nodes/vectors fail validation
before publication. These values do not change the physical profile or ownership.
