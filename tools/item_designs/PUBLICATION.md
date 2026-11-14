# Versioned item build, validation and publication

Generation writes staging only. `build` / `build-all` retain legacy recipe support,
but no longer publish by default; `--staging-only` remains a compatible spelling.
`build-design` reads the authored v3 recipe and source layers, builds privately,
checks actual geometry and promotes the complete staged directory. A failed
build retains the previous stage. It never writes author sources or gameplay.

```text
python tools/itemgen.py build-design --id bucket
python tools/itemgen.py validate-design --id bucket
python tools/itemgen.py validate-design --all
python tools/itemgen.py publish-design --id bucket
python tools/itemgen.py publish-design --all
python tools/itemgen.py recover-publication
```

Publication is an explicit operation, separate from generation. The production
catalog is still legacy while artistic/integration/performance acceptance is
unfinished; availability of the command is not evidence of final acceptance.

## Frozen inputs and resource contract

`design_build.json` records SHA-256 for the unchanged gameplay definition, every
item source/layer, the explicit shared provenance registry, repository license,
all generator modules, both item shaders and all generated output. Added source
files, changed code/definition, tampered staging or incomplete metadata require
a rebuild. Every selected design is checked again after preparing the transaction.

The glTF subset requires indexed triangles, float POSITION/NORMAL/TEXCOORD_0,
unit normals, consistent winding, valid nonnegative indices and buffer ranges,
finite transforms, positive TRS scales, unique named scene nodes, external PNG
albedo, opaque scalar metallic/roughness/emission, and explicit linear/trilinear
clamp sampling. UVs stay within the atlas. Skins, animation clips, morph targets,
extra maps, extensions and unsupported samplers fail with a reason. Visual
bindings and anatomical wear references are checked against actual named nodes.
The declared triangle budget is enforced. These checks do not assess beauty.
Manual external glTF source authoring/import is still an open implementation step.

`provenance.json` has an explicit record for every current item: coordinate-recipe
origin, texture-layer process, external mesh/pixel inputs, design card, license
source and separate review statements. No automatic `original` certification is
added. Current construction uses project-authored recipes/painting; the previous
published lineage and final visual-independence review remain separate evidence.

## Transaction and recovery

An OS writer lock excludes another publisher and is released after process death.
All previous item directories and the previous manifest are copied before the
journal is written. New directories preserve authored grip/wear/other sidecars;
only prior explicitly owned generated files can be removed. The writer installs
all selected directories, then replaces the manifest generation as commit marker.
A normal exception restores the previous byte contents. A process interrupted
before the marker is rolled back by `recover-publication` (also run automatically
at the start of the next publication); after the marker, the complete generation
is retained. Missing/corrupt backups stop recovery without further mutation.
Temporary Windows rename locks have bounded retries; destinations are never
removed to force a rename past a lock.

The first legacy replacement also retains that item's full prior directory under
`_itemgen_reference`, with its original textures, grips, wear and attribution.
The v4 manifest records `reference_model` and hashes these files. Subsequent
publications preserve this comparison source. Item Lab must still be wired to
this packaged reference after final publication instead of comparing production
with itself. The reference copy is part of the same transaction.

The runtime-sync CMake script refuses a source tree containing an unfinished
publication journal. Recover first, then build Release. This prevents a later
ordinary build from packaging an interrupted mixed generation. Run publication
and source/runtime synchronization sequentially. This protocol is tested against
process interruption, not a claim of filesystem/power-loss durability.

Legacy recipe publication and historical pack import reject a destination marked
as authored v3. The historical import CLI's `--list` is read-only. Old manifest
refresh preserves v3 entries; only validated versioned publication can update
their content hashes. This keeps ordinary old commands from replacing new work.
