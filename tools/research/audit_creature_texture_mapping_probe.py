"""Validate native B/G/R mapping/fallback evidence against source and GPU readback."""
import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path

from PIL import Image


def read(path):
    return json.loads(path.read_text(encoding='utf8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_stems(folder, visual):
    backend = visual['backend']
    if backend == 'bone_skeleton':
        geometry = read(folder / visual['geometry_file'])
        count = sum(len(bone.get('cubes', [])) for entry in geometry.values()
                    if isinstance(entry, dict) for bone in entry.get('bones', []))
        return [visual.get('texture') or 'diffuse'] * count
    if backend == 'gltf_skeleton':
        model = read(folder / visual['gltf']['model'])
        stems = []
        for mesh in model['meshes']:
            for primitive in mesh['primitives']:
                material = model['materials'][primitive['material']]
                tex = material['pbrMetallicRoughness']['baseColorTexture']['index']
                image = model['images'][model['textures'][tex]['source']]
                # Cubatarium's loader addresses its texture storage by the
                # material name. Own published G assets also keep image stems
                # identical; don't silently substitute the image URI oracle.
                stem = material.get('name', 'body')
                assert Path(image['uri']).stem == stem
                stems.append(stem)
        return stems
    parts = read(folder / visual['rigid_model'])['parts'] if visual.get('rigid_model') else visual.get('parts', [])
    return [part.get('texture', 'body') or visual.get('default_texture', '') for part in parts] or [visual.get('default_texture') or 'body']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='mapping_final_catalog')
    parser.add_argument('--native-dir', default='native_mapping_final')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    context_path = out / f'context_{args.label}.json'
    context = read(context_path)
    root = Path(context['models_root'])
    assert context['native_resource_cache_probe'] and not context['qa_models_override']
    assert digest(repo / 'bin/Cubatarium.exe') == context['exe_sha256']
    for file, sha in context['files_sha256'].items():
        assert digest(root / file) == sha, file
    for file, sha in context['shaders_sha256'].items():
        assert digest(repo / file) == sha, file
    definitions = {p.parent.name: read(p) for p in (root / 'creatures').glob('*/creature.json')}
    assert len(definitions) == 56 and set(context['preview_species']) == definitions.keys()
    native = out / args.native_dir
    probe_path = native / 'texture_mapping_probe.json'
    probe = read(probe_path)
    assert probe_path.stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp()
    fixture = native / 'texture_mapping_fixture'
    kinds = {'mapped', 'unmapped', 'missing_mapped', 'direct_key', 'missing_skin'}
    expected_cases = {(sid, kind) for sid, d in definitions.items()
                      for kind in kinds | ({'legacy_atlas'} if d['visual']['backend'] == 'bone_skeleton' else set())}
    rows = {(r['species'], r['kind']): r for r in probe['cases']}
    assert rows.keys() == expected_cases and len(rows) == len(probe['cases']) == 297
    registry = probe['texture_ids']
    assert all(t > 0 for t in registry.values()) and len(set(registry.values())) == len(registry)
    files = {}
    for (sid, kind), row in rows.items():
        visual = definitions[sid]['visual']
        assert row['backend'] == visual['backend']
        assert row['runtime_backend'] == ('sprite' if visual.get('sprite', {}).get('billboard') else visual['backend'])
        stems = source_stems(root / 'creatures' / sid, visual)
        if kind == 'unmapped' and visual['backend'] == 'bone_skeleton':
            stems = ['qa_unmapped'] * len(stems)
        assert row['stems'] == stems and row['first_stem'] == stems[0]
        skin_id = f'qa_texture_{sid}_{kind}'
        assert row['skin'] == skin_id
        folder = fixture / skin_id
        mapping = {s: 'variant' if kind == 'mapped' else 'absent' for s in stems} if kind in ('mapped', 'missing_mapped') else {'qa_unrelated': 'variant'} if kind == 'unmapped' else {}
        expected_skin = {'id': skin_id, 'creature_id': sid,
                         'visual': {'texture': stems[0] if kind == 'direct_key' else 'qa_unrelated', 'texture_map': mapping}}
        expected_files = {'textures/variant.png', 'textures/diffuse.png'}
        if kind != 'missing_skin':
            assert read(folder / 'skin.json') == expected_skin
            expected_files.add('skin.json')
        if kind == 'direct_key':
            expected_files.add(f'textures/{stems[0]}.png')
        assert {str(p.relative_to(folder)).replace('\\', '/') for p in folder.rglob('*') if p.is_file()} == expected_files
        for file in expected_files:
            source = folder / file
            files[str(source.relative_to(native))] = digest(source)
            if source.suffix == '.png':
                color = (43, 199, 71, 255) if kind == 'direct_key' and source.stem == stems[0] else (231, 37, 89, 255) if source.stem == 'variant' else (17, 91, 217, 255)
                with Image.open(source) as image:
                    assert image.size == (4, 4) and set(image.convert('RGBA').getdata()) == {color}
        expected_keys = []
        for stem in stems:
            if kind == 'mapped':
                key = f'skin/{skin_id}/variant'
            elif kind == 'legacy_atlas':
                key = f'skin/{skin_id}/diffuse'
            elif kind == 'direct_key' and stem == stems[0]:
                key = f'skin/{skin_id}/{stem}'
            elif visual['backend'] == 'rigid_voxels':
                key = f'{sid}/{stem or "body"}'
            else:
                candidates = [stem, 'diffuse', visual.get('default_texture', ''), 'body']
                key = next(f'{sid}/{s}' for s in candidates if s and (root / 'creatures' / sid / 'textures' / f'{s}.png').is_file())
            expected_keys.append(key)
        assert row['expected_keys'] == expected_keys
        ids = sorted(registry[key] for key in expected_keys)
        assert row['expected_textures'] == row['actual_textures'] == ids
        assert row['draws'] == row['base_draws'] == len(stems) > 0
        assert row['base_geometry_hashes'] == row['actual_geometry_hashes']
        assert len(row['base_geometry_hashes']) == len(stems)
        assert row['geometry_unchanged'] and row['passed']
    for key in registry:
        prefix, *tail = key.split('/')
        source = fixture / tail[0] / 'textures' / f'{tail[1]}.png' if prefix == 'skin' else root / 'creatures' / prefix / 'textures' / f'{tail[0]}.png'
        assert source.is_file()
        if prefix != 'skin':
            assert str(source.relative_to(root)) in context['files_sha256']
    assert len(probe['gpu_textures']) == 891
    gpu_keys = set()
    for row in probe['gpu_textures']:
        _, skin_id, stem = row['key'].split('/')
        assert row['texture'] == registry[row['key']] > 0
        with Image.open(fixture / skin_id / 'textures' / f'{stem}.png') as image:
            assert tuple(row['rgba']) == image.convert('RGBA').getpixel((0, 0))
        assert row['passed']
        gpu_keys.add(row['key'])
    assert {k for k in registry if k.startswith('skin/')} <= gpu_keys
    assert probe['passed']
    report = {'schema': 'creature-texture-mapping-evidence-audit.v1', 'passed': True,
              'scope': 'Synthetic 56-species B/G/R mapping/missing fallback and UV/pose invariance; diagnostic texture GPU RGBA only. Not art/FP/save/full reload acceptance.',
              'context': str(context_path), 'context_sha256': digest(context_path),
              'probe_sha256': digest(probe_path), 'exe_sha256': context['exe_sha256'],
              'cases': 297, 'by_kind': dict(Counter(r['kind'] for r in rows.values())),
              'runtime_resources': dict(Counter(next(r['runtime_backend'] for (s, _), r in rows.items() if s == sid) for sid in definitions)),
              'gpu_readbacks': 891, 'all_geometry_submissions_unchanged': True,
              'current_runtime_inputs_verified': True, 'fixture_sha256': files}
    (out / 'texture_mapping_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k != 'fixture_sha256'}, indent=2))


if __name__ == '__main__':
    main()
