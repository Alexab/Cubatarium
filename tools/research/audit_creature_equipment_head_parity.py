"""Bind head-anchor correction to exact prior sampled art; compose comparison.

Mechanical checks only. These do not perform artistic or runtime acceptance.
"""
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'docs/research/creature-r6-v2/equipment-fit'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text())


def main():
    materials = ('copper', 'iron', 'leather')
    report = {'schema': 'creature-equipment-head-parity.v1',
              'scope': 'Exact image/input comparison only; not a new art decision', 'materials': []}
    overview = Image.new('RGB', (1536, 560), (31, 35, 40))
    draw = ImageDraw.Draw(overview)
    for column, material in enumerate(materials):
        folder = OUT / 'textured' / material
        before = load(folder / 'context_final_catalog.json')
        after = load(folder / 'context_head_catalog.json')
        assert before['exe_sha256'] == after['exe_sha256']
        assert before['shaders_sha256'] == after['shaders_sha256']
        assert before['files_sha256'] == after['files_sha256']
        old_inputs, inputs = before['equipment_files_sha256'], after['equipment_files_sha256']
        assert old_inputs.keys() == inputs.keys()
        changed_inputs = [p for p in inputs if inputs[p] != old_inputs[p]]
        assert changed_inputs == [f'models/items/{material}_head/wear.json'], changed_inputs
        for path, digest in inputs.items():
            assert sha(ROOT / 'bin' / path) == digest, path
        previous = load(folder / 'native_final/equipment/manifest.json')
        current = load(folder / 'native_head/equipment/manifest.json')
        exact, changed = [], []
        for old, new in zip(previous['resources'], current['resources']):
            assert old['id'] == new['id'] and len(old['frames']) == len(new['frames']) == 408
            for old_frame, new_frame in zip(old['frames'], new['frames']):
                assert old_frame['png'] == new_frame['png']
                name = new_frame['png']
                old_path = folder / 'native_final/equipment' / name
                new_path = folder / 'native_head/equipment' / name
                row = {'path': name, 'sha256': sha(new_path)}
                expected_change = new['id'] == 'bot' and new_frame['loadout'] in (0, 3)
                assert (old_path.read_bytes() != new_path.read_bytes()) == expected_change, name
                (changed if expected_change else exact).append(row)
        assert len(exact) == 2244 and len(changed) == 204
        report['materials'].append({'material': material, 'only_changed_input': changed_inputs[0],
                                    'exact_png_count': len(exact), 'changed_bot_helmet_png_count': len(changed),
                                    'exact_frames': exact, 'changed_frames': changed})
        # Historical affine volume fit versus current surface-cage artwork.
        affine_dir = OUT / material / 'native_candidate/equipment'
        affine = load(affine_dir / 'manifest.json')
        for row_index, resource_id in enumerate(('human', 'skeleton')):
            for side, (directory, manifest, label) in enumerate((
                    (affine_dir, affine, 'previous affine fit'),
                    (folder / 'native_head/equipment', current, 'current fitted armor'))):
                resource = next(r for r in manifest['resources'] if r['id'] == resource_id)
                frame = next(f for f in resource['frames'] if f['state'] == 'Run'
                             and abs(f['speed_ratio'] - 1.5) < .001
                             and f['worst_clearance_phase'] and f['loadout'] == 3)
                x, y = (column * 2 + side) * 256, row_index * 280
                with Image.open(directory / frame['png']) as im:
                    overview.paste(im.convert('RGB'), (x, y + 24))
                draw.text((x + 4, y + 3), f'{material} / {resource_id} / {label}', fill='white')
    overview.save(OUT / 'comparison.png')
    report['comparison_sha256'] = sha(OUT / 'comparison.png')
    (OUT / 'head_parity.json').write_text(json.dumps(report, indent=2) + '\n')
    print('6732 exact PNG; only 612 bot armor/combined helmet frames changed; comparison composed')


if __name__ == '__main__':
    main()
