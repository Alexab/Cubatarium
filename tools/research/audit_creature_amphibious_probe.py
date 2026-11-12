"""Audit explicit amphibious transitions and actual world motor travel.

Produces evidence and full-turn sheets, never artistic acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
from collections import Counter
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--label', default='fluid_candidate')
    parser.add_argument('--native-dir', default='native_candidate')
    args = parser.parse_args()
    out = args.out.resolve()
    assert out.is_relative_to((ROOT / 'docs/research').resolve())
    context = read(out / ('context_' + args.label + '.json'))
    process = read(out / ('report_' + args.label + '.json'))
    assert process['process_success'] and process['process_rc'] == 0 and not process['hang_killed']
    assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
    for field, base in [('files_sha256', Path(context['models_root'])), ('shaders_sha256', ROOT)]:
        for name, digest in context[field].items():
            assert sha(base / name) == digest, name
    assert context['world_motion'] == 'water_pause' and context['native_fluid_transition_probe']
    native = out / args.native_dir
    fluid = read(native / 'fluid_transition_probe.json')
    expected = {('land_idle', 'fluid_idle'), ('fluid_idle', 'land_idle'),
                ('land_walk', 'free_swim'), ('free_swim', 'land_walk'),
                ('fluid_idle', 'free_swim'), ('free_swim', 'fluid_idle'),
                ('free_swim', 'bottom_tread'), ('bottom_tread', 'free_swim'),
                ('land_walk', 'bottom_tread'), ('bottom_tread', 'land_walk')}
    floors, count = [], 0
    assert {r['id'] for r in fluid['resources']} == set(context['preview_species'])
    for resource in fluid['resources']:
        cases = resource['cases']
        definition = read(Path(context['models_root']) / 'creatures' / resource['id'] / 'creature.json')
        assert definition['habitat'] == 'amphibious' and len(cases) == 160
        assert {(c['from'], c['to']) for c in cases} == expected
        for pair in expected:
            assert {c['outgoing_phase'] for c in cases if (c['from'], c['to']) == pair} == {i / 16 for i in range(16)}
        for c in cases:
            for side in ('from', 'to'):
                assert c[side + '_clip'] == definition['visual']['animation']['state_map'][c[side + '_state'].capitalize()]
            assert c['in_fluid'] == (c['to'] in ('fluid_idle', 'free_swim', 'bottom_tread'))
            assert c['supported'] == (c['to'] in ('land_idle', 'land_walk', 'bottom_tread'))
            assert c['on_fluid_bottom'] == (c['to'] == 'bottom_tread')
            assert len(c['frames']) == 21 and [f['step'] for f in c['frames']] == list(range(21))
            for f in c['frames']:
                low, high = np.array(f['low']), np.array(f['high'])
                assert np.isfinite(low).all() and np.isfinite(high).all() and (high >= low).all()
                assert f['visual_envelope_covers_geometry'] and f['draws'] > 0
                count += 1
                if c['supported']: floors.append(float(low[1]))
    assert floors and min(floors) >= -.001
    log = out / ('world_motion_' + args.label + '.jsonl')
    records = [json.loads(line) for line in log.read_text().splitlines() if line]
    assert records and {r['species'] for r in records} == set(context['world_species'])
    assert not any(r['frozen'] or r['body_blocked'] for r in records), 'motion route frozen or overlapping solids'
    assert max(r['elapsed'] for r in records) >= 4.75, 'return movement phase never reached'
    assert any(r['in_fluid'] and not r['requested_move'] for r in records)
    assert any(r['state'].lower() == 'swim' and r['in_fluid'] and r['requested_move'] for r in records)
    recovered = [r for r in records if not r['legacy_terrain_ready'] and (r['center_known_air'] or r['below_known_air'])]
    assert recovered, 'no native known-air freeze witness recovered'
    travel, directions = 0., Counter()
    for before, after in zip(records, records[1:]):
        assert after['elapsed'] >= before['elapsed']
        if before['id'] != after['id'] or not after['requested_move']: continue
        delta = np.array(after['position']) - before['position']
        distance = float(np.linalg.norm(delta[[0, 2]]))
        if distance > 1e-5 and after['in_fluid']:
            travel += distance
            directions[str(after['requested_direction'])] += 1
    assert travel > .5 and directions['1.0'] > 0 and directions['-1.0'] > 0, 'actual XZ travel in both directions not proven'
    matrix = read(native / 'matrix/manifest.json')
    sheets = []
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 14)
    for resource in matrix['resources']:
        frames = resource['frames']
        # Preserve manifest order and metadata; each group is one of the
        # actual native three poses, ten authored yaw/top/bottom views.
        assert len(frames) == 30
        for group in range(3):
            selected = frames[group * 10:(group + 1) * 10]
            sheet = Image.new('RGB', (800, 384), (27, 31, 36)); draw = ImageDraw.Draw(sheet)
            draw.text((8, 5), resource['id'] + ' | native full-turn pose group ' + str(group) + ' | unapproved', font=font, fill='white')
            for i, frame in enumerate(selected):
                with Image.open(native / 'matrix' / frame['color']) as source:
                    tile = source.convert('RGBA')
                    assert np.asarray(tile)[:, :, 3].any()
                    tile = tile.resize((152, 152), Image.Resampling.NEAREST)
                backdrop = Image.new('RGBA', tile.size, (224, 229, 232, 255)); backdrop.alpha_composite(tile)
                x, y = i % 5 * 160 + 4, i // 5 * 180 + 30
                sheet.paste(backdrop.convert('RGB'), (x, y + 18))
                draw.text((x, y), str(i) + ' ' + str(frame.get('state', '')), font=font, fill='white')
            path = out / (resource['id'] + '_fullturn_' + str(group) + '.png'); sheet.save(path)
            sheets.append({'path': path.name, 'sha256': sha(path)})
    sources = ['src/App/Platform/CreatureQaFluidTransitionProbe.h', 'src/Creatures/Diagnostics/CreatureQaWorldMotion.h',
               'src/World/Core/WorldViewBinding.cpp', 'src/World/Chunks/KnownAirTerrainSlice.h',
               'src/World/Chunks/KnownAirTerrainSlicePolicy.h', 'src/World/Raycast/OpaqueVoxelRayTrace.cpp']
    result = {'passed': True, 'scope': 'Explicit synthetic conditions and one short actual open-water input/motor route; not terrain/wet-dry travel/GPU fade/performance/art acceptance',
              'exe_sha256': context['exe_sha256'], 'transition_cases': 160 * len(fluid['resources']), 'cpu_transition_frames': count,
              'supported_frames': len(floors), 'supported_min_y': min(floors), 'world_samples': len(records),
              'world_simulated_seconds': records[-1]['elapsed'], 'world_xz_move_travel': travel,
              'world_move_direction_samples': dict(directions), 'world_state_counts': dict(Counter(r['state'] for r in records)),
              'known_air_legacy_freeze_recovered_samples': len(recovered), 'sheets': sheets,
              'input_sha256': {str(p.relative_to(out)): sha(p) for p in [log, native / 'fluid_transition_probe.json', out / ('context_' + args.label + '.json')]},
              'source_sha256': {p: sha(ROOT / p) for p in sources}}
    (out / 'audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('source_sha256', 'input_sha256', 'sheets')}, indent=2))


if __name__ == '__main__': main()
