# Cubatarium AssetGen

`tools/assetgen.py` builds, audits, and previews the original texture and voxel
decor library in `resource_packs/cubatarium_worldgen_cc0/`, with an optional
64px texture-only overlay in `resource_packs/cubatarium_worldgen_photo64_cc0/`.
The game reads only generated PNG and JSON files; it does not need Python,
PyTorch, or model weights.

## Library target

The catalog targets the Minecraft Java Edition 26.3 inventory snapshot checked
on 2026-10-22: 1,286
block definitions and 1,405 files under `assets/minecraft/textures/block/`.
Those figures measure different things. The texture number counts paths/files,
not distinct pixel images. Cubatarium's own acceptance measure is at least
1,286 block definitions and 1,405 texture files, with pixel-identical images
reported as duplicates. The current generated pack contains 1,664 block
definitions and 2,224 unique textures: 1,396 procedurally catalogued material
blocks, 258 Cubatarium definitions previously bundled with the Minetest-reference
texture pack, and 10 geology definitions. The 2,224 files comprise 1,396 base
material textures, 313 new Minetest replacements, nine geology replacements,
and 506 role-specific top, bottom, grass, masonry, and wood-end textures.

The benchmark sources and snapshot date are recorded in `asset_catalog.yaml`
and each generated `asset_manifest.json`:

- [Minecraft Java Edition 26.3 release notes](https://www.minecraft.net/en-us/article/minecraft-java-edition-26-3)
- [26.3 block inventory](https://gofedako.com/mc/java/26.3/)
- [26.3 block texture file listing](https://mcasset.cloud/26.3/assets/minecraft/textures/block/)

The listing is an external inventory, so re-count a local 26.3 extraction when
available. `benchmark-minecraft` counts PNG files recursively and reads a
generated `reports/blocks.json`, `blocks.json`, or registry JSON when present.
The client jar alone may not include a block registry report.

## Install and use

Install the small offline tool dependencies:

```sh
python -m pip install -r requirements-assetgen.txt
```

Generate the checked-in resource pack, including contact sheets, object JSON,
stable block IDs, reference overrides, and an asset provenance manifest:

```sh
python tools/assetgen.py build-pack
python tools/assetgen.py build-pack --profile photo64
python tools/assetgen.py audit --minecraft-version 26.3
python tools/assetgen.py validate --pack cubatarium_worldgen_cc0
python tools/assetgen.py validate --pack cubatarium_worldgen_photo64_cc0
```

`voxel16` is the default world pack. Select `cubatarium_worldgen_photo64_cc0`
as a secondary pack when creating a world to use the 64px face overrides.
16×16 is the target resolution for the default pack and its pixel-art palette.
The optional 64px overlay remains a separate preview profile. Texture dimensions
are checked against each pack's declared resolution.

The 16px generator preserves each material's large-scale pattern before adding
periodic detail: rock families include mineral grains and strata, masonry uses
different layouts for brick, cobble, tile, and cut stone, and wood uses grain or
end-grain recipes. Grass top maps to a fully vegetated turf texture, while grass
sides use a thin irregular turf layer above soil; dry grass, snow, and leaf litter
have separate top-face treatments. Furnace, dispenser, workbench,
bookshelf, anvil, and chest faces receive functional pixel motifs. A block's
`render.texture_variation` gives ordinary cubes a deterministic, per-cell UV
phase so repeated blocks do not line up as one identical large pattern; semantic
machine faces keep their UV phase fixed. Alpha-cutout plant and fire sprites keep
their transparent silhouettes and skip surface-tile seam blending. Their blocks
use tuft, cluster, reed, or flame geometry, and opaque ground faces remain
visible through the clear pixels.

Generate the 1,396 catalog textures into a separate staging directory:

```sh
python tools/assetgen.py texture batch --catalog tools/asset_catalog.yaml --backend procedural --profile voxel16 --reference-mode qa-only
```

Generate one deterministic texture or a parametric voxel object:

```sh
python tools/assetgen.py texture --material basalt --variant fractured --backend procedural --seed 42 --output out/basalt.png
python tools/assetgen.py texture --material loam --role grass_side --backend procedural --profile voxel16 --seed 42 --output out/loam_side.png
python tools/assetgen.py texture --material basalt --input out/cc0_basalt_photo.png --source-license CC0-1.0 --backend quilting --profile photo64 --seed 42 --output out/basalt_photo64.png
python tools/assetgen.py decor --preset tree:pine --seed 42 --output out/pine_tree.json
python tools/assetgen.py preview --object out/pine_tree.json
python tools/assetgen.py preview --blocks resource_packs/cubatarium_worldgen_cc0
python tools/assetgen.py compare --reference-pack minetest_default_16 --reference-mode qa-only --pack resource_packs/cubatarium_worldgen_cc0
python tools/assetgen.py calibrate --reference-pack minetest_default_16
```

`--reference-mode qa-only` uses Minetest only for comparison and visual QA.
`aggregate-calibration` computes per-family medians and per-texture color,
luminance spread, neighbor contrast, and alpha coverage. It feeds these numeric
summaries into generation; it never uses reference patches or copies source
pixels. For a matching reference texture, its color centroid has a stronger
influence on the generated material, while catalog textures use the family
median. Since
the reference pack is CC BY-SA 3.0, aggregate-calibrated pack builds go to
`assetgen_output/staging/` and are marked `REVIEW_REQUIRED` until their
publication status is reviewed. The `compare` command writes contact sheets and
broad diagnostics; block-face previews show front/right/back/left/top/bottom.

`quilting` builds a new tile by selecting and blending small patches from an
explicit input image. Use only photographs you can publish under CC0 for
CC0-bound outputs, and set `--source-license CC0-1.0`; other or unknown licenses
are retained in provenance and marked for review. Generated and photo-derived
textures emit albedo/alpha only, which is what the current block renderer
consumes.

## Optional neural generation

The procedural and quilting backends have no model dependency and are
deterministic for the same catalog, material parameters, input, and seed.
Diffusers is optional. First install a
PyTorch wheel appropriate for the machine (CPU is supported), then install the
additional Python packages. The ML extra pins the tested Diffusers 0.37.x and
Transformers 4.x compatibility range because the model pipeline API and Hub
requirements change between major releases:

```sh
python -m pip install -r requirements-assetgen-ml.txt
python tools/assetgen.py models install <model-id> --revision <revision>
python tools/assetgen.py texture --material basalt --input reference.png --backend diffusers --model <model-id> --device cpu --resource-profile cpu --seed 42 --output out/basalt.png
```

Weights are stored in the operating system's Cubatarium AssetGen cache, outside
Git. The `auto` backend tries a named, locally cached model and otherwise uses
quilting when an input image is supplied, or procedural generation without one.
Without `--model`, it picks the first cached user
Diffusers pipeline in deterministic name order (Hub `hf-internal-testing`
fixtures are ignored). It does not download unless
`--allow-download --revision <revision>` are set. `--device cpu` always selects CPU inference. The
`cpu` resource profile also forces CPU; `low-vram` uses sequential CPU offload
when CUDA memory is tight and host RAM can hold the model, otherwise auto mode
falls back to CPU. An explicit `offline` resource profile disables neural
inference.

Photo-to-voxel decor uses an external TripoSR checkout and that project’s own
environment/dependencies. Install the model revision explicitly, then point
`--triposr-dir` at the checkout and `--triposr-python` at the Python interpreter
inside that environment. `CUBATARIUM_TRIPOSR_PYTHON` can provide the same path
as a default. The worker runs in a child process, so it can use its own PyTorch
and native extensions without mixing them into the main assetgen environment.
Use `--backend triposr --device auto` for device detection, or `--device cpu`
to force CPU inference. The adapter removes machine-level `PYTHONHOME` and
`PYTHONPATH` from the child process because either can override an isolated
Python environment. Alternatively, use `--allow-download --revision <revision>`
to pin the model download for that run.
The adapter passes the local model path to TripoSR, voxelizes the resulting
mesh, and maps reconstructed vertex/texture colors to the nearest catalog
materials. Use `--palette-mode single --block-type <block>` to keep one block
type, or use `--palette-mode material` to enable color matching. Check the
three-view preview because a single photo cannot recover unseen sides.
`--chunk-size` and `--mc-resolution` pass through to TripoSR for controlling
renderer memory and mesh extraction detail. On Windows, CPU-only PyTorch plus
a CPU build of `torchmcubes` avoids the CUDA compiler/toolkit dependency; the
TripoSR stack should remain in its own Python 3.10 environment, with NumPy
below 2 for compatibility with the tested PyTorch 2.2.2 CPU wheel.

### Windows CPU configuration verified on 2026-10-22

The full photo-to-voxel command was run on Windows with two isolated Python
3.10 environments. The AssetGen host used NumPy 1.26.4, SciPy 1.15.3, and
trimesh 5.1.1, with no PyTorch installed. The TripoSR worker used PyTorch
2.2.2+cpu (`torch.cuda.is_available() == False`), NumPy 1.26.4, Transformers
4.35.0, trimesh 4.0.5, rembg 2.0.69, and ONNX Runtime 1.17.3. `torchmcubes`
was built CPU-only from commit `a9ac95aa329957ba646e7bb057f825f5dfa77f38`
using CMake 3.31.10 and Visual Studio 2022 C++ Build Tools. TripoSR source was
pinned to `107cefdc244c39106fa830359024f6a2f1c78871`; model weights were pinned
to `5b521936b01fbe1890f6f9baed0254ab6351c04a` and stored in the external
AssetGen cache. No Diffusers install or GPU was needed for this TripoSR path.

On the tested machine, a system `PYTHONHOME` points to Python 3.13 and breaks
Python 3.10 even inside a venv. Clear Python override variables in the current
PowerShell session before launching either interpreter; this does not change
the machine-wide setting:

```powershell
'PYTHONHOME','PYTHON_HOME','PYTHONPATH','PYTHON_MINOR_VERSION','PYTHON_MAJOR_VERSION' |
  ForEach-Object { Remove-Item "Env:$_" -ErrorAction SilentlyContinue }
$assetgenRoot = Join-Path $env:LOCALAPPDATA 'Cubatarium\AssetGen'
$env:CUBATARIUM_ASSETGEN_HOME = $assetgenRoot
$hostPython = Join-Path $assetgenRoot 'envs\assetgen-host-py310\Scripts\python.exe'
$triposrPython = Join-Path $assetgenRoot 'envs\triposr-cpu-py310-v2\Scripts\python.exe'
$triposrSource = Join-Path $assetgenRoot 'sources\TripoSR'
& $hostPython tools/assetgen.py decor `
  --backend triposr `
  --input (Join-Path $triposrSource 'examples\chair.png') `
  --output assetgen_output/triposr_cpu/chair.json `
  --triposr-dir $triposrSource `
  --triposr-python $triposrPython `
  --device cpu `
  --model stabilityai/TripoSR `
  --revision 5b521936b01fbe1890f6f9baed0254ab6351c04a `
  --palette-mode material `
  --size 16 `
  --chunk-size 2048 `
  --mc-resolution 128
```

That run produced 441 unique voxels in a 9×9×16 bound, mapped to 35 catalog
materials. Model inference took about 75 seconds and mesh extraction about 33
seconds on CPU after background removal and model initialization. The first
run also downloads the TripoSR checkpoint (about 1.68 GB) and rembg's U2Net
weights; both are cached outside the repository. If `torchmcubes` is rebuilt,
install PyTorch first, then CMake, `scikit-build-core`, and `pybind11`, and run
`pip install --no-build-isolation .` from its checkout in a Visual Studio x64
developer shell without `nvcc` on `PATH`; its CMake configuration selects the
CPU implementation when no CUDA compiler is found.

## Asset and license rules

The default `qa-only` generator uses Minetest names only to select material
families and roles. It records source hashes for QA but does not sample source
pixels. `aggregate-calibration` extracts per-family and per-texture numeric summaries
and uses them to tune color and detail parameters. It does not sample patches
or copy pixels; outputs remain in a review staging folder because the reference
pack carries CC BY-SA 3.0. `compare` and `preview --compare-reference` write
QA sheets outside the distributable pack. The 16px profile uses a compact
palette and periodic noise; the 64px profile retains continuous color and
detail. Layer recipes assign grass-over-soil sides, soil top/bottom, and bark
versus endgrain. Animated liquids use vertical strips sized to their profile.
The Minetest source pixels remain under their original license and are not
copied into the CC0 pack. The block JSON
formerly distributed with `minetest_default_16` is generated from Cubatarium's
own mappings and gameplay metadata; those authored definitions are included
with replacement texture stems. The pack also integrates the CC0 geology
definitions.

Every generated texture records its seed, material, variant, backend, source
hash when applicable, model and revision when available, and license review
status. Only procedural assets with no third-party image/model input are
declared CC0 by this pack. Review model terms and input rights before moving any
neural or reference-derived result into a redistributable CC0 pack.

Godot Voxel Tools' blocky terrain guidance describes per-face texture tiles and
material reuse. Cubatarium stores a canonical face order and generates distinct
tiles only where a material recipe requires them. The `quilting` backend uses
patch selection and blending for CC0 photo inputs; optional diffusion can
provide a learned image-to-image path. [Image Quilting](https://graphics.cs.cmu.edu/people/efros/research/quilting.html)
is the source method for patch synthesis. The current renderer consumes albedo and alpha,
not a full PBR material set; one photograph does not determine complete
SVBRDF properties ([single-image SVBRDF reconstruction study](https://diglib.eg.org/items/78c7597d-7e55-4365-90b2-4326f49d60fc)).

Further engine references: [Godot Voxel Tools blocky terrain](https://github.com/Zylann/godot_voxel/blob/master/doc/source/blocky_terrain.md),
[Diffusers memory optimization](https://huggingface.co/docs/diffusers/v0.37.1/en/optimization/memory),
and [TripoSR](https://github.com/VAST-AI-Research/TripoSR).
