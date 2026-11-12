"""Independent exported-triangle cage proof; not visual/equipment acceptance."""
import hashlib
import json
from collections import Counter
from pathlib import Path

from build_creature_wear_variants import accessor


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/research/creature-r6-v2/equipment-fit'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    rows = []
    for material in ('copper', 'iron', 'leather'):
        previous = json.loads((OUT / material / 'context_candidate_catalog.json').read_text())
        preserved = []
        for rel, digest in previous['equipment_files_sha256'].items():
            if rel.endswith('/wear.json'):
                continue
            assert sha(ROOT / 'bin' / rel) == digest, rel
            preserved.append(rel)
        for slot in ('chest', 'arms', 'legs'):
            folder = ROOT / 'models/items' / f'{material}_{slot}'
            wear = json.loads((folder / 'wear.json').read_text())
            assert wear['fit_mode'] == 'bone_volume'
            assert wear['fit_bounds'] == {'low': [-1, -1, -1], 'high': [1, 1, 1]}
            doc = json.loads((folder / wear['model']).read_text())
            original = json.loads((folder / 'model.gltf').read_text())
            assert not original.get('images'), 'Source texture must not be replaced'
            assert doc['images'] == [{'uri': 'wear_texture.png'}]
            count = 0
            signed_volume = 0.
            edges = Counter()
            unique_positions = set()
            for mesh in doc['meshes']:
                for primitive in mesh['primitives']:
                    assert set(primitive['attributes']) == {'POSITION', 'TEXCOORD_0'}
                    positions = accessor(doc, folder, primitive['attributes']['POSITION'])
                    indices = [i[0] for i in accessor(doc, folder, primitive['indices'])]
                    assert len(indices) % 3 == 0
                    for k in range(0, len(indices), 3):
                        tri = [positions[i] for i in indices[k:k + 3]]
                        unique_positions.update(tri)
                        for a,b in zip(tri,tri[1:]+tri[:1]):
                            edges[tuple(sorted((a,b)))] += 1
                        a,b,c=tri
                        signed_volume += sum(a[i]*v for i,v in enumerate((
                            b[1]*c[2]-b[2]*c[1],b[2]*c[0]-b[0]*c[2],b[0]*c[1]-b[1]*c[0])))/6.
                        # A common separating plane proves exclusion for the
                        # WHOLE triangle; per-vertex exclusion is insufficient.
                        assert any(all(sign * p[axis] >= 1. - 2e-6 for p in tri)
                                   for axis in range(3) for sign in (-1, 1)), (folder, k)
                        count += 1
            assert count == 64 and len(unique_positions) == 32
            assert all(incidence == 2 for incidence in edges.values()), 'Non-manifold shell edge'
            assert signed_volume > 0., 'Shell orientation is inverted'
            files = {'wear.json', wear['model']}
            for resource in doc.get('buffers', []) + doc.get('images', []):
                if not resource['uri'].startswith('data:'):
                    files.add(resource['uri'])
            hashes = {name: sha(folder / name) for name in sorted(files)}
            for name, digest in hashes.items():
                assert sha(ROOT / 'bin/models/items' / folder.name / name) == digest
            rows.append({'item': folder.name, 'triangles': count,
                         'all_triangle_interiors_outside_declared_cage': True,
                         'watertight_two_manifold_edges': True,
                         'positive_oriented_shell_volume': signed_volume,
                         'source_inventory_materials_and_images_unchanged': True,
                         'worn_only_texture': 'wear_texture.png',
                         'source_bin_files_sha256': hashes,
                         'preserved_previous_non_wear_inputs': preserved})
    report = {'schema': 'creature-wear-cage-proof.v1',
              'scope': 'Exported local triangle/cage separation and preserved inventory/FP inputs only. '
                       'Not full body collision, articulation, visual, FP rendering or performance acceptance.',
              'items': rows}
    (OUT / 'cage_geometry_audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(rows)} cage meshes: triangle interiors separated; original item inputs/materials retained; bin exact')


if __name__ == '__main__':
    main()
