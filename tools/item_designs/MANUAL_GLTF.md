# Author-owned glTF source override

An item recipe may select a manually authored model instead of generated parts:

```yaml
version: 3
id: bucket
triangle_budget: 1800
geometry_source:
  path: authored/model.gltf
  # Optional named subtrees defining the physical-fit reference bounds:
  # fit_nodes: [body, handle]
presentation:
  right_grip: [0, 0.2, 0]
  left_grip: [0, 0.2, 0]
  fp_scale: 0.8
```

Place the glTF, BIN and PNG files inside the item's author directory. External
resource URIs must stay inside the glTF's folder, including after decoding
percent escapes (spaces and Unicode file names can be URI encoded). Network,
absolute, escaping, query and fragment references are rejected. `build-design`
reads these files but never rewrites them. Generation still writes staging;
validation and publication remain separate commands.

The supported [glTF 2.0 subset](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
is indexed triangles, float POSITION/NORMAL/TEXCOORD_0, unique named nodes and
materials, positive TRS or nonsingular orientation-preserving affine matrices,
opaque base-color PNGs and scalar color/metallic/roughness/emission properties.
PNG input must be RGB/RGBA. The atlas sampler must explicitly use clamp in both
axes, linear magnification and trilinear minification. Other maps, extensions,
skins, morphs, cameras and animation clips are rejected with a reason. This is
not a general-purpose scene importer or a conversion to a full PBR renderer.

## Units, pivots and presentation

The source node hierarchy, local rotations/scales, triangle indices, UVs,
authored normals and material properties are preserved. All source buffers are
merged at aligned offsets; new fitted POSITION accessors are appended without
changing shared/interleaved NORMAL or UV data. Images are copied byte-for-byte
to normalized runtime names. Multiple buffers and multiple material images are
supported. Existing data is never interpreted as a universal texture tile.

For reference bounds `[low, high]`, the importer uses centre `(low+high)/2` and
a uniform fit into the unchanged gameplay physical size. The world-space result
is `(source_position-centre)*scale`. Node translations and explicit visual-driver
lengths follow the same scale. The default fit covers all geometry; `fit_nodes`
can select named subtrees, for example excluding a nocked arrow. A fit selection
without geometry is rejected. Physical mass, collision sizes, IDs and recipes
are never written by import.

Presentation and visual bindings come from the item recipe, not imported extras.
Grip coordinates and wear reference bounds use source world units and follow
the physical fit. FP offsets/scales and icon framing keep their independent
presentation units. Driver translations and span anchor points use source local
units. Source node origins remain the rotation pivots. A source root may be named
after the item ID; otherwise an item-ID root is added above the existing roots.
An ID-named node elsewhere is rejected rather than silently renamed.

`nodes`, `parts`, generated `materials`, atlas packing and surface overlays belong
to the generated branch. A manual override uses the hierarchy, materials, UVs and
textures from the authored glTF. Keep or remove the old recipe parts as reference;
there is no implicit mixing or fallback when manual import fails. Hand-edit the
source textures externally, then rebuild.

## Provenance and publication

Update the item's explicit `provenance.json` geometry record to identify the
actual manual source and its origin; add `authoring_format: manual-gltf-2.0` and
`source: tools/item_designs/<id>/authored/model.gltf`. Review texture origin,
external inputs and license independently. The publisher rejects an old recipe
origin record for a manual model; it does not assign an originality stamp.
Generated geometry likewise must identify its active design recipe.

Build records hash every author file, dependency, gameplay definition and output.
Added textures or changed glTF/BIN invalidate staging until rebuilt. Failed imports
leave the previous stage intact. Publication preserves author sidecars and the
prior published comparison resource; see [publication](PUBLICATION.md).

The developer QA command is:

```text
python tools/research/run_item_manual_qa.py --world CarryAimQA
```

It builds Release, constructs an isolated multi-image/URI-encoded source fixture,
then runs `tools/flight_sim_run.py` with `bin/Cubatarium.exe`. During the probe it
hides the generated bucket stage to exercise packaged old/current comparison,
and restores all staged/runtime files on exit. It never publishes the fixture.
