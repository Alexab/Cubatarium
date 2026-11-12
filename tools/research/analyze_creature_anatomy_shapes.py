"""Read-only original/own shape diagnosis, never an automatic collision fix.

Geometry silhouettes are deliberately untextured/opaque: they measure submitted
triangles, not texture alpha, shaded appearance, enclosed volume or art quality.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from review_creature_redesign import ROOT, accessor, sample_nodes, world_matrices


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def geometry(folder):
    definition = json.loads((folder / 'creature.json').read_text(encoding='utf-8-sig'))
    g = json.loads((folder / 'model.gltf').read_text())
    blobs = [(folder / b['uri']).read_bytes() for b in g['buffers']]
    skin = g['skins'][0]
    inverse = accessor(g, blobs, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    positions, joints, weights, faces = [], [], [], []
    count = 0
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            assert primitive.get('mode', 4) == 4
            a = primitive['attributes']
            p = accessor(g, blobs, a['POSITION'])
            index = accessor(g, blobs, primitive['indices']).astype(int).reshape(-1, 3)
            faces.append(index + count)
            positions.append(p)
            joints.append(accessor(g, blobs, a['JOINTS_0']).astype(int))
            weights.append(accessor(g, blobs, a['WEIGHTS_0']))
            count += len(p)
    pos, joint, weight = map(np.concatenate, (positions, joints, weights))
    faces = np.concatenate(faces)
    hom = np.column_stack([pos, np.ones(len(pos))])
    cfg = definition['visual']['gltf']
    scale = cfg.get('model_scale', 1.)
    angle = math.radians(cfg.get('model_yaw_offset_deg', definition['visual'].get('model_yaw_offset_deg', 0)))
    rotation = np.array([[math.cos(angle), 0, math.sin(angle)], [0, 1, 0], [-math.sin(angle), 0, math.cos(angle)]])
    offset = cfg.get('model_offset_y', 0.) - float(pos[:, 1].min()) * scale

    def pose(clip=None, phase=0.):
        world = world_matrices(sample_nodes(g, blobs, clip, phase))
        palette = np.array([world[n] @ inv for n, inv in zip(skin['joints'], inverse)])
        matrices = (palette[joint] * weight[:, :, None, None]).sum(axis=1)
        points = np.einsum('nij,nj->ni', matrices, hom)[:, :3] * scale @ rotation.T
        points[:, 1] += offset
        return points

    bind = pose()
    clips = {}
    states = definition['visual']['animation']['state_map']
    available = {a['name'] for a in g.get('animations', [])}
    for state in ('Idle', 'Walk', 'Run', 'Fly', 'Swim', 'Action'):
        clip = states.get(state)
        if clip not in available:
            continue
        sampled = [pose(clip, float(t)) for t in np.linspace(0, 1, 65)]
        low = np.min([p.min(axis=0) for p in sampled], axis=0)
        high = np.max([p.max(axis=0) for p in sampled], axis=0)
        witnesses = []
        for axis in range(3):
            for sign in (-1, 1):
                frame = int(np.argmax([np.max(p[:, axis] * sign) for p in sampled]))
                vertex = int(np.argmax(sampled[frame][:, axis] * sign))
                influences = [{'node': skin['joints'][int(j)], 'name': g['nodes'][skin['joints'][int(j)]].get('name'), 'weight': float(w)}
                              for j, w in zip(joint[vertex], weight[vertex]) if w > 1e-6]
                witnesses.append({'axis': 'xyz'[axis], 'side': sign, 'phase': frame / 64,
                                  'vertex': vertex, 'position': sampled[frame][vertex].tolist(), 'influences': influences})
        clips[state] = {'clip': clip, 'low': low.tolist(), 'high': high.tolist(),
                        'union_extent': (high - low).tolist(), 'extreme_witnesses': witnesses}
    return {'bind_extent': np.ptp(bind, axis=0).tolist(), 'bind_low': bind.min(axis=0).tolist(),
            'bind_high': bind.max(axis=0).tolist(), 'states': clips, 'collision_rest': definition['bounds']['rest'],
            'source_sha256': {str(p.relative_to(folder)): sha(p) for p in [folder / 'creature.json', folder / 'model.gltf', *[folder / b['uri'] for b in g['buffers']]]}}, bind, faces


def silhouette(points, faces, axes, low, high, size=(260, 160)):
    uv = points[:, axes].copy()
    span = high - low
    factor = min((size[0] - 12) / max(span[0], 1e-6), (size[1] - 12) / max(span[1], 1e-6))
    uv = (uv - (low + high) / 2) * factor
    uv[:, 1] *= -1
    uv += np.array(size) / 2
    image = Image.new('1', size)
    draw = ImageDraw.Draw(image)
    for triangle in faces:
        draw.polygon([tuple(p) for p in uv[triangle]], fill=1)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    assert out.is_relative_to((ROOT / 'docs/research').resolve()) and not out.exists()
    out.mkdir(parents=True)
    species = json.loads((ROOT / 'docs/research/creature-r6-v2/save-roundtrip/bounds_diagnostic.json').read_text())['mismatching_species']
    original = ROOT / 'assetgen_output/creatures/review_2026_11_10/baseline/models/creatures'
    report = {'schema': 'creature-anatomy-shape-diagnosis.v1', 'scope': 'Read-only 17 discrepancies; bind opaque geometry silhouette and 65-phase state union; neither native/art acceptance nor physical volume',
              'builder_sha256': sha(Path(__file__)), 'resources': {}}
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 14)
    sheets = []
    for start in range(0, len(species), 6):
        group = species[start:start + 6]
        sheet = Image.new('RGB', (840, 44 + len(group) * 220), (25, 29, 33))
        draw = ImageDraw.Draw(sheet)
        draw.text((8, 6), 'Bind geometry | BLUE original | ORANGE own | WHITE overlap', font=font, fill='white')
        draw.text((8, 24), 'Shared world scale per pair; opaque triangles, no textures; not art acceptance', font=font, fill='white')
        for row, sid in enumerate(group):
            versions = [geometry(root / sid) for root in (original, ROOT / 'models/creatures')]
            resource = {name: version[0] for name, version in zip(('original', 'own'), versions)}
            resource['bind_extent_ratio_own_original'] = (np.array(versions[1][0]['bind_extent']) / versions[0][0]['bind_extent']).tolist()
            assert resource['original']['collision_rest'] == resource['own']['collision_rest']
            resource['projection_geometry'] = {}
            y = 44 + row * 220
            text = sid + '  bind XYZ original ' + ','.join(f'{v:.3f}' for v in versions[0][0]['bind_extent'])
            text += ' / own ' + ','.join(f'{v:.3f}' for v in versions[1][0]['bind_extent'])
            draw.text((8, y), text, font=font, fill='white')
            for col, (view, axes) in enumerate((('front', (0, 1)), ('side', (2, 1)), ('top', (0, 2)))):
                combined = np.concatenate([v[1][:, axes] for v in versions])
                low, high = combined.min(axis=0), combined.max(axis=0)
                masks = [silhouette(v[1], v[2], axes, low, high) for v in versions]
                arrays = [np.asarray(m, dtype=bool) for m in masks]
                color = np.full((160, 260, 3), (48, 53, 59), dtype=np.uint8)
                color[arrays[0]] = (69, 150, 235)
                color[arrays[1]] = (244, 159, 66)
                color[arrays[0] & arrays[1]] = (233, 233, 226)
                sheet.paste(Image.fromarray(color), (8 + col * 280, y + 40))
                draw.text((8 + col * 280, y + 21), view, font=font, fill='white')
                resource['projection_geometry'][view] = {'original_pixels': int(arrays[0].sum()), 'own_pixels': int(arrays[1].sum()),
                    'own_original_area_ratio': float(arrays[1].sum() / arrays[0].sum()), 'shared_pixels': int((arrays[0] & arrays[1]).sum())}
            report['resources'][sid] = resource
            print('MEASURED', sid, flush=True)
        path = out / f'bind_shapes_{start // 6 + 1}.png'
        sheet.save(path)
        sheets.append({'path': path.name, 'sha256': sha(path)})
    report['sheets'] = sheets
    (out / 'metrics.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
