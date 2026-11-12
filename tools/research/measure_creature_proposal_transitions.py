"""Independent sampled local-TRS clip blends; diagnostic, not native/terrain acceptance."""
import json
import math
from pathlib import Path
import numpy as np
from review_creature_redesign import ROOT, accessor, sample_nodes, world_matrices


def slerp(a, b, u):
    a, b = np.array(a, float), np.array(b, float)
    dot = float(a @ b)
    if dot < 0:
        b, dot = -b, -dot
    if dot > .9995:
        q = a * (1 - u) + b * u
    else:
        angle = math.acos(np.clip(dot, -1, 1))
        q = (a * math.sin((1 - u) * angle) + b * math.sin(u * angle)) / math.sin(angle)
    return (q / np.linalg.norm(q)).tolist()


def measure(folder):
    definition = json.loads((folder / 'creature.json').read_text())
    g = json.loads((folder / 'model.gltf').read_text())
    blobs = [(folder / b['uri']).read_bytes() for b in g['buffers']]
    skin = g['skins'][0]
    inverse = accessor(g, blobs, skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    p, j, w = [], [], []
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            a = primitive['attributes']
            p.append(accessor(g, blobs, a['POSITION']))
            j.append(accessor(g, blobs, a['JOINTS_0']).astype(int))
            w.append(accessor(g, blobs, a['WEIGHTS_0']))
    p, j, w = np.concatenate(p), np.concatenate(j), np.concatenate(w)
    vertices = np.column_stack([p, np.ones(len(p))])
    scale = definition['visual']['gltf'].get('model_scale', 1.)
    offset = definition['visual']['gltf'].get('model_offset_y', 0.) - float(p[:, 1].min()) * scale
    durations = {a['name']: max(float(accessor(g, blobs, s['input'])[-1, 0]) for s in a['samplers']) for a in g['animations']}
    names = list(dict.fromkeys(definition['visual']['animation']['state_map'].get(s, 'idle') for s in ('Idle', 'Walk', 'Run', 'Action')))
    names = [name for name in names if name in durations]
    speed = definition['visual']['animation']['clips']
    result = []
    for leaving in names:
        for incoming in names:
            if leaving == incoming:
                continue
            worst = {'min_y': float('inf')}
            for phase in np.arange(16) / 16:
                a = sample_nodes(g, blobs, leaving, float(phase))
                for step in range(11):
                    u = step / 10
                    time = .15 * u
                    entered = time * speed.get(incoming, {}).get('speed', 1.) / durations[incoming]
                    b = sample_nodes(g, blobs, incoming, min(entered, 1.))
                    nodes = []
                    for av, bv in zip(a, b):
                        node = dict(bv)
                        for key, default in (('translation', [0., 0., 0.]), ('scale', [1., 1., 1.])):
                            node[key] = (np.array(av.get(key, default)) * (1 - u) + np.array(bv.get(key, default)) * u).tolist()
                        node['rotation'] = slerp(av.get('rotation', [0., 0., 0., 1.]), bv.get('rotation', [0., 0., 0., 1.]), u)
                        nodes.append(node)
                    world = world_matrices(nodes)
                    palette = np.array([world[node] @ ibm for node, ibm in zip(skin['joints'], inverse)])
                    matrices = (palette[j] * w[:, :, None, None]).sum(axis=1)
                    posed = np.einsum('nij,nj->ni', matrices, vertices)
                    low = float((posed[:, 1] * scale + offset).min())
                    if low < worst['min_y']:
                        worst = {'min_y': low, 'leaving_phase': float(phase), 'blend_progress': u}
            result.append({'leaving': leaving, 'incoming': incoming, **worst})
    return result


def main():
    out = ROOT / 'docs/research/creature-r6-v2/envelopes/motion_v4'
    out.mkdir(exist_ok=True)
    result = {}
    for sid in ('badger', 'treeman', 'lobster'):
        result[sid] = {}
        for version, root in [('published', ROOT / 'models/creatures'),
                              ('proposal', ROOT / 'assetgen_output/creatures/envelope_motion_v4/models/creatures')]:
            result[sid][version] = measure(root / sid)
            print(sid, version, min(row['min_y'] for row in result[sid][version]), flush=True)
        (out / 'transition_diagnostic.json').write_text(json.dumps({
            'schema': 'creature-proposal-transition-diagnostic.v1',
            'scope': '16 outgoing phases,11 local-TRS blend steps/pair, fresh incoming clip; independent sampled math, not actual retained renderer/terrain or all clip history',
            'resources': result}, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
