"""Independent geometry/rig/UV/contact checks for four isolated own proposals.

Never grants art acceptance or publishes recipes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
import yaml
from review_creature_redesign import ROOT, accessor


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def centreline(cube):
    o, s, shape = np.array(cube['origin']), np.array(cube['size']), cube['shape']
    q, heights = np.array(shape['planform']), np.array(shape['planform_y'])
    return np.array([[o[0] + s[0] * q[list(pair), 0].mean(),
                      o[1] + s[1] * heights[:, list(pair)].mean(),
                      o[2] + s[2] * q[list(pair), 1].mean()] for pair in ((0, 1), (2, 3))])


def inspect(folder):
    gltf = read(folder / 'model.gltf')
    blobs = [(folder / b['uri']).read_bytes() for b in gltf['buffers']]
    names = [gltf['nodes'][i]['name'] for i in gltf['skins'][0]['joints']]
    density, areas = [], []
    for mesh in gltf['meshes']:
        for primitive in mesh['primitives']:
            a = primitive['attributes']
            p, uv = accessor(gltf, blobs, a['POSITION']), accessor(gltf, blobs, a['TEXCOORD_0'])
            joints, weights = accessor(gltf, blobs, a['JOINTS_0']).astype(int), accessor(gltf, blobs, a['WEIGHTS_0'])
            assert np.isfinite(p).all() and np.isfinite(uv).all() and np.isfinite(weights).all()
            assert np.max(np.abs(weights.sum(axis=1) - 1)) < .000001 and (weights >= 0).all()
            assert joints.max() < len(names) and (uv >= 0).all() and (uv <= 1).all()
            texture_index = gltf['materials'][primitive['material']]['pbrMetallicRoughness']['baseColorTexture']['index']
            image = gltf['images'][gltf['textures'][texture_index]['source']]['uri']
            with Image.open(folder / image) as im:
                resolution = np.array(im.size)
            indices = accessor(gltf, blobs, primitive['indices']).astype(int).reshape(-1, 3)
            for tri in indices:
                v = p[tri]
                area = np.linalg.norm(np.cross(v[1] - v[0], v[2] - v[0])) / 2
                tex = uv[tri] * resolution
                tex_area = abs(np.linalg.det(np.array([tex[1] - tex[0], tex[2] - tex[0]]))) / 2
                areas.append(float(area))
                if area > 1e-9:
                    density.append(float(np.sqrt(tex_area / area)))
    assert min(areas) > 1e-10, 'degenerate model triangle'
    return gltf, blobs, {'triangles': len(areas), 'min_triangle_area': min(areas),
                        'constant_uv_triangles': sum(d == 0 for d in density),
                        'model_texel_density_p05': float(np.quantile(density, .05)),
                        'model_texel_density_median': float(np.median(density))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT, help='Project-shaped archived pre-publish inputs')
    args = parser.parse_args()
    out = args.out.resolve()
    catalog = ROOT / 'assetgen_output/creatures/envelope_motion_v5'
    report = {'schema': 'creature-motion-v5-invariant-audit.v1', 'passed': True,
              'scope': 'Rig nodes/IBM, branch centrelines, valid UV/skin/triangles, actual CPU transition floor/envelope; not art/stride/performance acceptance',
              'resources': [], 'native_transition_frames': 0}
    transitions = read(out / 'native_current/transition_probe.json')
    for sid in ('badger', 'treeman', 'lobster', 'seahorse'):
        source, candidate = args.source_root / 'models/creatures' / sid, catalog / 'models/creatures' / sid
        a, ab, old_stats = inspect(source)
        b, bb, new_stats = inspect(candidate)
        assert a['nodes'] == b['nodes'] and a['skins'] == b['skins']
        skin = a['skins'][0]['inverseBindMatrices']
        assert np.array_equal(accessor(a, ab, skin), accessor(b, bb, skin))
        assert (source / 'creature.json').read_bytes() == (candidate / 'creature.json').read_bytes()
        assert new_stats['constant_uv_triangles'] <= old_stats['constant_uv_triangles']
        row = {'id': sid, 'nodes_skin_ibm_definition_unchanged': True, 'source_mesh': old_stats, 'candidate_mesh': new_stats,
               'source_sha256': {p.name: sha(p) for p in (source / 'model.gltf', source / 'model.bin', source / 'creature.json')},
               'source_recipe_sha256': sha(args.source_root / 'tools/creature_recipes' / (sid + '.yaml')),
               'candidate_sha256': {p.name: sha(p) for p in (candidate / 'model.gltf', candidate / 'model.bin')},
               'textures_sha256': {p.name: sha(p) for p in (candidate / 'textures').glob('*.png')}}
        if sid == 'treeman':
            old_recipe = yaml.load((args.source_root / 'tools/creature_recipes/treeman.yaml').read_text(encoding='utf8'), Loader=yaml.CSafeLoader)
            new_recipe = yaml.load((catalog / 'recipes/treeman.yaml').read_text(encoding='utf8'), Loader=yaml.CSafeLoader)
            old_cubes = {c['name']: c for bone in old_recipe['anatomy']['bones'] for c in bone.get('cubes', [])}
            errors = []
            for bone in new_recipe['anatomy']['bones']:
                if bone['name'].startswith(('upper_arm_', 'forearm_', 'thigh_', 'shin_')):
                    for cube in bone.get('cubes', []):
                        if 'planform_y' in cube.get('shape', {}):
                            errors.append(float(np.max(np.abs(centreline(cube) - centreline(old_cubes[cube['name']])))))
            assert errors and max(errors) < 1e-10
            row.update(branch_sections_checked=len(errors) * 2, branch_centreline_max_error_px=max(errors))
        resource = next(r for r in transitions['resources'] if r['id'] == sid)
        floor = []
        for case in resource['cases']:
            for frame in case['frames']:
                assert frame['visual_envelope_covers_geometry']
                if case['supported']:
                    floor.append(frame['low'][1])
                report['native_transition_frames'] += 1
        if floor:
            assert min(floor) >= -.001
        row['supported_transition_min_y'] = min(floor) if floor else None
        report['resources'].append(row)
    assert report['native_transition_frames'] == 5456
    (out / 'invariant_audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print('Nodes/IBM/definitions,UV/weights/triangles,branch centrelines and5456 native transition frames pass')


if __name__ == '__main__':
    main()
