"""Independent exported whale seams and blowhole exposure, not artistic acceptance."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
import yaml
from review_creature_redesign import ROOT, accessor, sample_nodes, world_matrices
from audit_creature_motion_v5 import sha


def load(folder, recipe):
    g = json.loads((folder / 'model.gltf').read_text())
    blob = [(folder / b['uri']).read_bytes() for b in g['buffers']]
    assert len(g['meshes']) == len(g['skins']) == 1 and len(g['meshes'][0]['primitives']) == 1
    attrs = g['meshes'][0]['primitives'][0]['attributes']
    p = accessor(g, blob, attrs['POSITION'])
    j = accessor(g, blob, attrs['JOINTS_0']).astype(int)
    w = accessor(g, blob, attrs['WEIGHTS_0'])
    inv = accessor(g, blob, g['skins'][0]['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    names = [c['name'] for b in recipe['anatomy']['bones'] for c in b.get('cubes', [])]
    assert len(p) == 24 * len(names) and len(set(names)) == len(names)
    cubes = {name: np.arange(i * 24, (i + 1) * 24) for i, name in enumerate(names)}
    def pose(clip, phase):
        world = world_matrices(sample_nodes(g, blob, clip, phase))
        palette = np.array([world[n] @ ib for n, ib in zip(g['skins'][0]['joints'], inv)])
        matrix = (palette[j] * w[:, :, None, None]).sum(axis=1)
        return np.einsum('nij,nj->ni', matrix, np.column_stack([p, np.ones(len(p))]))[:, :3] * recipe['model_scale']
    return g, p, cubes, pose


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    args = parser.parse_args(); out = args.out.resolve()
    qa = ROOT / 'assetgen_output/creatures' / args.qa_name
    pairs = [('rostrum', 'head', -20), ('head', 'trunk_front', -12),
             ('trunk_front', 'trunk_rear', -4), ('trunk_rear', 'rump', 4),
             ('rump', 'tail0', 10), ('tail0', 'tail1', 14)]
    rows = []
    for label, folder, recipe_path in [('before', args.source_root / 'models/creatures/whale', args.source_root / 'tools/creature_recipes/whale.yaml'),
                                      ('candidate', qa / 'models/creatures/whale', qa / 'recipes/whale.yaml')]:
        recipe = yaml.safe_load(recipe_path.read_text(encoding='utf-8'))
        g, p, cubes, pose = load(folder, recipe)
        seams = []
        for left, right, z in pairs:
            a = cubes[left][np.isclose(p[cubes[left], 2], z / 16)]
            b = cubes[right][np.isclose(p[cubes[right], 2], z / 16)]
            assert len(a) == len(b) == 12
            maximum = 0.
            for clip in [None] + [a['name'] for a in g['animations']]:
                for phase in ([0.] if clip is None else np.linspace(0, 1, 129)):
                    points = pose(clip, float(phase))
                    distances = np.linalg.norm(points[a, None] - points[b][None], axis=-1)
                    maximum = max(maximum, float(distances.min(0).max()), float(distances.min(1).max()))
            assert maximum < 1e-5, (label, left, right, maximum)
            seams.append({'parts': [left, right], 'z_texels': z, 'max_section_error_blocks': maximum})
        head = p[cubes['head']]; hole = p[cubes['blowhole']]
        za, zb = head[:, 2].min(), head[:, 2].max()
        ya = head[np.isclose(head[:, 2], za), 1].max()
        yb = head[np.isclose(head[:, 2], zb), 1].max()
        top, base = hole[:, 1].max(), hole[:, 1].min()
        cap_clearance, base_embed = [], []
        for z in [hole[:, 2].min(), hole[:, 2].max()]:
            surface = ya + (yb - ya) * (z - za) / (zb - za)
            cap_clearance.append(float((top - surface) * recipe['model_scale']))
            base_embed.append(float((surface - base) * recipe['model_scale']))
        assert min(cap_clearance) > .001 and min(base_embed) > .001, (label, cap_clearance, base_embed)
        rows.append({'version': label, 'seams': seams, 'blowhole_cap_clearance_blocks': cap_clearance,
                     'blowhole_base_embed_blocks': base_embed,
                     'files_sha256': {str(p): sha(p) for p in (folder / 'model.gltf', folder / 'model.bin', recipe_path)}})
    result = {'passed': True, 'scope': 'Six exported section matches across bind +129 phases/all3 clips per own version, static blowhole cap exposure/base embed; not all surfaces/intersections/animation art acceptance', 'resources': rows}
    (out / 'section_audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Six seams/all387 phases and exposed embedded blowhole PASS')


if __name__ == '__main__': main()
