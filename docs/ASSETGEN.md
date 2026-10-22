# Cubatarium AssetGen

`tools/assetgen.py` builds, audits, and previews the original texture and voxel
decor library in `resource_packs/cubatarium_worldgen_cc0/`. The game reads only
the generated PNG and JSON files; it does not need Python, PyTorch, or model
weights.

## Library target

The catalog targets the Minecraft Java Edition 26.3 inventory snapshot checked
on 2026-10-22: 1,286
block definitions and 1,405 files under `assets/minecraft/textures/block/`.
Those figures measure different things. The texture number counts paths/files,
not distinct pixel images. Cubatarium's own acceptance measure is at least
1,286 block definitions and 1,405 texture files, with pixel-identical images
reported as duplicates. The current generated pack contains 1,396 block
definitions and 1,718 textures: 1,396 material variants, 313 replacements for
the Minetest PNGs, and replacements for the nine geology PNGs.

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
python tools/assetgen.py audit --minecraft-version 26.3
python tools/assetgen.py validate --pack cubatarium_worldgen_cc0
```

Generate the 1,396 catalog textures into a separate staging directory:

```sh
python tools/assetgen.py texture batch --catalog tools/asset_catalog.yaml --backend procedural
```

Generate one deterministic texture or a parametric voxel object:

```sh
python tools/assetgen.py texture --material basalt --variant fractured --backend procedural --seed 42 --output out/basalt.png
python tools/assetgen.py decor --preset tree:pine --seed 42 --output out/pine_tree.json
python tools/assetgen.py preview --object out/pine_tree.json
python tools/assetgen.py preview --compare-reference minetest_default_16
```

Texture commands also accept `--input reference.png`; the offline backend uses
only broad average color from the reference and creates a new image. Image or
model-derived outputs are marked `REVIEW_REQUIRED` in their sidecar manifest.
The generated object preview is saved beside its JSON file.

## Optional neural generation

The offline backend has no model dependency and is deterministic for the same
catalog, material parameters, and seed. Diffusers is optional. First install a
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
Git. The `auto` backend tries a named, locally cached model and falls back to
procedural generation; without `--model`, it picks the first cached user
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

The procedural backend uses the Minetest and geology file names only to select
material families. It records reference file hashes for QA but does not sample
the source pixels. `preview --compare-reference` creates temporary side-by-side
QA sheets under `assetgen_output/reference_qa/`, outside the distributable pack.
Its output is new pixel art, quantized to a compact palette,
16×16, alpha-aware, and edge-periodic. Minetest source textures remain licensed
under their original terms and are not copied into this CC0 pack.

Every generated texture records its seed, material, variant, backend, source
hash when applicable, model and revision when available, and license review
status. Only procedural assets with no third-party image/model input are
declared CC0 by this pack. Review model terms and input rights before moving any
neural or reference-derived result into a redistributable CC0 pack.

Godot Voxel Tools' blocky terrain guidance describes per-face texture tiles and
material reuse; Cubatarium uses a shared six-face image for most blocks and
overrides faces only where a source block needs a distinct mapping. Seamless
patch synthesis can use [Image Quilting](https://graphics.cs.cmu.edu/people/efros/research/quilting.html)
as a future optional backend. The current renderer consumes albedo and alpha,
not a full PBR material set; one photograph does not determine complete
SVBRDF properties ([single-image SVBRDF reconstruction study](https://diglib.eg.org/items/78c7597d-7e55-4365-90b2-4326f49d60fc)).

Further engine references: [Godot Voxel Tools blocky terrain](https://github.com/Zylann/godot_voxel/blob/master/doc/source/blocky_terrain.md),
[Diffusers memory optimization](https://huggingface.co/docs/diffusers/v0.37.1/en/optimization/memory),
and [TripoSR](https://github.com/VAST-AI-Research/TripoSR).
