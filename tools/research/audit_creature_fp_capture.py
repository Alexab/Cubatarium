"""Check actual FP pixels, authored GPU UV buffers and unchanged animation roots."""
import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image

from audit_creature_resource_reload_probe import fnv64
from audit_creature_texture_mapping_probe import read, digest


def arm_source(root, species, left=False, shift_u=0):
    definition = read(root / 'creatures' / species / 'creature.json')
    folder = root / 'creatures' / species
    document = read(folder / definition['visual']['geometry_file'])
    geometries = [g for g in document.values() if isinstance(g, dict) and 'bones' in g]
    assert len(geometries) == 1
    geometry = geometries[0]
    bone = next(b for b in geometry['bones'] if b['name'] == ('leftArm' if left else 'rightArm'))
    assert len(bone['cubes']) == 1
    cube = bone['cubes'][0]
    assert not cube.get('inflate', 0) and not any(cube.get('rotation', [0, 0, 0]))
    w, h, d = cube['size']
    u, v = cube['uv']
    u += shift_u
    tw, th = geometry['texturewidth'], geometry['textureheight']
    rectangles = [(u+d+w+d, v+d, u+d+w+d+w, v+d+h), (u+d+w, v+d, u+d+w+d, v+d+h),
                  (u+d, v+d, u+d+w, v+d+h), (u, v+d, u+d, v+d+h),
                  (u+d, v, u+d+w, v+d), (u+d+w, v, u+d+w+w, v+d)]
    corners = [(0, 1, 2, 3), (0, 1, 2, 3), (1, 0, 3, 2), (3, 1, 2, 0), (0, 1, 2, 3), (0, 1, 3, 2)]
    header = (Path(__file__).resolve().parents[2] / 'src/Creatures/Visual/CreaturePartMeshData.h').read_text()
    body = re.search(r'kCreaturePartPositions\[\]\s*=\s*\{(.*?)\}', header, re.S)[1]
    positions = [float(x) for x in re.findall(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', body)]
    assert len(positions) == 72
    values = []
    for face, rect in enumerate(rectangles):
        x0, y0, x1, y1 = rect
        u0, v0, u1, v1 = (x0+.5)/tw, (y0+.5)/th, (x1-.5)/tw, (y1-.5)/th
        if face in (1, 3) and (bone.get('mirror', False) or cube.get('mirror', False)):
            u0, u1 = u1, u0
        uv = [(u0, v1), (u1, v1), (u0, v0), (u1, v0)]
        for vertex, corner in enumerate(corners[face]):
            index = (face * 4 + vertex) * 3
            values.extend(positions[index:index+3] + list(uv[corner]))
    assert len(values) == 120
    model = np.diag([.63*w/h, .63, .63*d/h, 1.])
    model[1, 3] = -.315
    if left:
        model = np.diag([-1., 1., 1., 1.]) @ model
    return fnv64(struct.pack('<120f', *values)), .63 / (h/16), model


def matrix(values):
    result = np.asarray(values, dtype=np.float64).reshape((4, 4), order='F')
    assert np.isfinite(result).all()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='fp_final')
    parser.add_argument('--native-dir', default='native_final')
    parser.add_argument('--before-label', default='fp_before')
    parser.add_argument('--before-dir', default='native_before')
    parser.add_argument('--before-out', type=Path, help='Historical legacy-right-arm FP evidence directory; keeps original context provenance. Authored-to-authored RGBA parity is a separate publication check.')
    parser.add_argument('--before-model-snapshot', type=Path, help='Archived models root for exact historical inputs that have since changed; current inputs still verified live')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    out = args.out.resolve()
    before_out = args.before_out.resolve() if args.before_out else out
    contexts = [read(before_out / f'context_{args.before_label}.json'), read(out / f'context_{args.label}.json')]
    context = contexts[1]
    assert context['native_fp_matrix'] and not context['qa_models_override']
    root = Path(context['models_root'])
    assert digest(repo / 'bin/Cubatarium.exe') == context['exe_sha256']
    archived_inputs = []
    for index, c in enumerate(contexts):
        for name, sha in c['files_sha256'].items():
            live = Path(c['models_root']) / name
            if live.is_file() and digest(live) == sha:
                continue
            assert index == 0 and args.before_model_snapshot, name
            archive_root = args.before_model_snapshot.resolve()
            archived = (archive_root / name).resolve()
            assert archived.is_relative_to(archive_root) and digest(archived) == sha, name
            archived_inputs.append({'name': name, 'path': str(archived), 'sha256': sha})
        for name, sha in (c['shaders_sha256'] | c['fp_shaders_sha256']).items():
            assert digest(repo / name) == sha, name
        for name, sha in c['equipment_files_sha256'].items():
            assert digest(repo / 'bin' / name) == sha, name
    folders = [before_out / args.before_dir / 'fp', out / args.native_dir / 'fp']
    before, current = [read(folder / 'manifest.json') for folder in folders]
    assert before['passed'] and current['passed']
    assert current['width'] == 768 and current['height'] == 432
    assert current['controlled_inventory_unchanged'] and current['user_selection_restored']
    expected = {'human': ('human', ''), 'bot': ('bot', ''), 'zombie': ('zombie', ''), 'skeleton': ('skeleton', ''),
                'human_adventurer': ('human', 'human_adventurer'), 'human_guard': ('human', 'human_guard'),
                'user_adventurer': ('human', 'human_adventurer'), 'user_guard': ('human', 'human_guard'),
                'legacy_guard': ('human', 'human_guard')}
    resources = {r['id']: r for r in current['resources']}
    originals = {r['id']: r for r in before['resources']}
    assert resources.keys() == originals.keys() == expected.keys()
    frame_keys = {(load, action, phase) for load in ('bare', 'main', 'off', 'dual')
                  for action, count in [('idle', 1), ('walk', 8), ('attack', 17)] for phase in range(count)}
    frame_rows = []
    before_rows = []
    max_root_error = max_model_error = 0.
    source_cache = {(species, left): arm_source(root, species, left) for species in ('human', 'bot', 'zombie', 'skeleton') for left in (False, True)}
    for rid, resource in resources.items():
        sid, skin = expected[rid]
        assert resource['species'] == sid and resource['effective_skin'] == skin and resource['appearance_resolved']
        if rid.startswith('user_'):
            assert resource['raw_skin'] == '' and resource['selected_skin'] == skin
        if rid == 'legacy_guard':
            assert resource['raw_skin'] == resource['selected_skin'] == '' and resource['legacy_selection'] == skin
        original = originals[rid]
        frames = {(f['loadout'], f['action'], f['phase']): f for f in resource['frames']}
        old = {(f['loadout'], f['action'], f['phase']): f for f in original['frames']}
        assert frames.keys() == old.keys() == frame_keys and len(resource['frames']) == len(original['frames']) == 104
        source_png = root / ('skins' if skin else 'creatures') / (skin or sid) / 'textures/diffuse.png'
        with Image.open(source_png) as image:
            colors = set(image.convert('RGBA').getdata())
        for key, frame in frames.items():
            baseline = old[key]
            assert frame['time'] == baseline['time'] and frame['preset'] == baseline['preset'] and frame['duration'] == baseline['duration']
            assert frame['passed'] and frame['skin_texture'] == resource['expected_texture'] > 0
            assert len(frame['arms']) == 2 and {a['left'] for a in frame['arms']} == {False, True}
            for arm in frame['arms']:
                left = arm['left']
                mesh_hash, scale, model = source_cache[(sid, left)]
                assert arm['source'] == ('bone/leftArm' if left else 'bone/rightArm')
                assert arm['mesh_hash'] == arm['gpu_vertex_hash'] == mesh_hash and arm['vertex_buffer'] > 0
                assert arm['vertex_float_count'] == 120 and abs(arm['source_scale'] - scale) < 1e-6
                assert arm['texture'] == frame['skin_texture']
                root_mvp = matrix(arm['root_mvp'])
                model_error = np.max(np.abs(matrix(arm['mvp']) - root_mvp @ model))
                assert model_error < 3e-6, (rid, key, 'authored model', model_error)
                max_model_error = max(max_model_error, float(model_error))
                legacy = next(a for a in baseline['arms'] if a['left'] == left)
                assert legacy['source'] == 'legacy_right_arm_atlas'
                sleeve = np.diag([.18, .52, .18, 1.]); sleeve[1, 3] = -.26
                reference_root = matrix(legacy['mvp']) @ np.linalg.inv(sleeve)
                error = np.max(np.abs(reference_root - root_mvp))
                assert error < 3e-6, (rid, key, 'root', error)
                max_root_error = max(max_root_error, float(error))
            path = folders[1] / frame['png']
            before_path = folders[0] / baseline['png']
            assert before_path.stat().st_mtime >= datetime.fromisoformat(contexts[0]['captured_at_utc']).timestamp()
            with Image.open(before_path) as image:
                assert image.size == (768, 432)
                assert int(np.count_nonzero(np.asarray(image.convert('RGBA'))[:, :, 3])) == baseline['coverage_pixels'] > 0
            before_rows.append({'path': baseline['png'], 'sha256': digest(before_path)})
            assert path.stat().st_mtime >= datetime.fromisoformat(context['captured_at_utc']).timestamp()
            with Image.open(path) as image:
                assert image.size == (768, 432)
                rgba = np.asarray(image.convert('RGBA'))
                assert int(np.count_nonzero(rgba[:, :, 3])) == frame['coverage_pixels'] > 0
                if frame['loadout'] == 'bare':
                    visible = rgba[rgba[:, :, 3] > 0]
                    assert all(tuple(v) in colors for v in np.unique(visible, axis=0)), (rid, key, 'foreign arm pixels')
            frame_rows.append({'path': frame['png'], 'sha256': digest(path)})
        for other in ('human_adventurer', 'human_guard'):
            if skin == other and rid != other:
                primary = {(f['loadout'], f['action'], f['phase']): f for f in resources[other]['frames']}
                for key, frame in frames.items():
                    with Image.open(folders[1] / frame['png']) as a, Image.open(folders[1] / primary[key]['png']) as b:
                        assert a.convert('RGBA').tobytes() == b.convert('RGBA').tobytes()
    assert len(frame_rows) == 936 and len(list(folders[1].glob('*.png'))) == 936
    retained = current['retained_cache']
    assert retained['passed']
    names = ['initial_fixture', 'file_changed_without_invalidation', 'explicit_clear', 'restored_fixture', 'species_switch', 'source_directory_return']
    assert [s['name'] for s in retained['stages']] == names
    fixture = folders[1] / 'cache_fixture'
    source = root / 'creatures/human' / read(root / 'creatures/human/creature.json')['visual']['geometry_file']
    assert Path(retained['source']) == source and Path(retained['fixture']).parent == fixture
    assert digest(Path(retained['fixture'])) == digest(source)
    changed = read(fixture / 'geometry.changed.json')
    original = read(source)
    for geometry in changed.values():
        if isinstance(geometry, dict) and 'bones' in geometry:
            next(b for b in geometry['bones'] if b['name'] == 'rightArm')['cubes'][0]['uv'][0] -= 1
    assert changed == original
    for stage in retained['stages']:
        sid = 'skeleton' if stage['name'] == 'species_switch' else 'human'
        assert stage['species'] == sid and len(stage['arms']) == 2
        for arm in stage['arms']:
            shift = 1 if stage['name'] == 'explicit_clear' and not arm['left'] else 0
            wanted, _, _ = arm_source(root, sid, arm['left'], shift)
            assert arm['cpu_hash'] == arm['gpu_hash'] == wanted and arm['passed']
    report = {'schema': 'creature-fp-native-evidence-audit.v1', 'passed': True,
              'scope': 'Actual offscreen FP renderer: six B resources, user/legacy fallback, GPU authored buffers, sampled roots/pixels; not full world camera/FP/save/all-state/performance acceptance',
              'context': str(out / f'context_{args.label}.json'), 'exe_sha256': context['exe_sha256'],
              'manifest_sha256': digest(folders[1] / 'manifest.json'), 'before_manifest_sha256': digest(folders[0] / 'manifest.json'),
              'frames': 936, 'gpu_authored_arm_draws': 1872, 'retained_renderer_cases': 6,
              'raw_user_legacy_selected_pixels_identical': True, 'bare_pixels_from_expected_skin_atlas': True,
              'max_animation_root_matrix_error': max_root_error, 'max_source_arm_model_matrix_error': max_model_error,
              'current_inputs_verified': True, 'historical_inputs_verified_from_snapshot': archived_inputs,
              'frame_sha256': frame_rows, 'before_frame_sha256': before_rows}
    (out / 'fp_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('frame_sha256', 'before_frame_sha256')}, indent=2))


if __name__ == '__main__':
    main()
