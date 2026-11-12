"""Audit actual B/R/sprite native clip captures and compose diagnostic sheets."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--label', default='posed_culling')
    parser.add_argument('--native-dir', default='native_posed')
    args = parser.parse_args()
    out = args.out.resolve()
    context_path = out / ('context_' + args.label + '.json')
    context = read(context_path)
    assert context['native_posed_culling_probe'] and not context['qa_models_override']
    assert context['exe_sha256'] == sha(ROOT / 'bin/Cubatarium.exe')
    assert context['performance_gate'] == 'disabled'
    for rel, digest in context['files_sha256'].items():
        assert sha(Path(context['models_root']) / rel) == digest
    for rel, digest in context['equipment_files_sha256'].items():
        assert sha(ROOT / 'bin' / rel) == digest
    for rel, digest in context['shaders_sha256'].items():
        assert sha(ROOT / rel) == digest
    flight = read(out / ('report_' + args.label + '.json'))
    assert flight['process_success'] and not flight['hang_killed']
    relocation = (out / ('relocation_' + args.label + '.txt')).read_text(encoding='utf8')
    assert all(any(line.split()[0] == sid and 'src=3' in line for line in relocation.splitlines() if line.split())
               for sid in context['world_species'])
    folder = out / args.native_dir / 'posed_culling'
    manifest = read(folder / 'manifest.json')
    root = Path(context['models_root'])
    definitions = {sid: read(root / 'creatures' / sid / 'creature.json') for sid in context['preview_species']}
    expected = {sid: sid for sid, d in definitions.items() if d['visual']['backend'] != 'gltf_skeleton'}
    for p in (root / 'skins').glob('*/skin.json'):
        skin = read(p)
        if skin['creature_id'] in expected:
            expected[p.parent.name] = skin['creature_id']
    assert len(expected) == 26
    resources = {r['id']: r for r in manifest['resources']}
    assert resources.keys() == expected.keys()
    captures, witnesses, loadout_count, equipment_rows = [], [], 0, 0
    started = datetime.fromisoformat(context['captured_at_utc']).timestamp()
    for rid, resource in resources.items():
        sid = expected[rid]
        assert resource['species'] == sid
        definition = definitions[sid]
        humanoid = definition['visual'].get('animation_profile') == 'humanoid'
        loadouts = {l['material']: l for l in resource['loadouts']}
        assert loadouts.keys() == ({'bare', 'copper', 'iron', 'leather'} if humanoid else {'bare'})
        states = {s.lower() for s in resource['sampled_states']}
        assert {'idle', 'walk', 'run', 'crouch', 'jump', 'fall', 'action'} <= states
        habitat = definition.get('habitat')
        if habitat == 'aerial':
            assert {'fly', 'hover', 'glide'} <= states
        if habitat in ('aquatic', 'amphibious'):
            assert {'swim', 'tread'} <= states
        bare = {(f['state'], f['phase']): f for f in loadouts['bare']['frames']}
        for material, loadout in loadouts.items():
            loadout_count += 1
            assert len(loadout['frames']) == 4 * len(states)
            seen = set()
            for frame in loadout['frames']:
                key = (frame['state'], frame['phase'])
                assert key not in seen
                seen.add(key)
                assert frame['observed']['new_clip_accepts'] and frame['observed']['draws'] > 0
                if material != 'bare':
                    assert frame['observed']['draws'] >= bare[key]['observed']['draws'] + 12
                    equipment_rows += 1
                path = folder / frame['native']['png']
                assert path.stat().st_mtime >= started
                with Image.open(path) as im:
                    assert im.size == (128, 128)
                    count = int(np.count_nonzero(np.asarray(im.convert('RGBA'))[:, :, 3]))
                assert count == frame['native']['alpha_pixels'] > 0
                captures.append({'id': rid, 'material': material, 'file': path.relative_to(out).as_posix(), 'sha256': sha(path)})
            edge = loadout['edge']
            if edge:
                path = folder / edge['native']['png']
                with Image.open(path) as im:
                    count = int(np.count_nonzero(np.asarray(im.convert('RGBA'))[:, :, 3]))
                assert count == edge['visible_pixels'] == edge['native']['alpha_pixels']
                vp = np.array(edge['observed']['view_projection']).reshape(4, 4).T
                x, y, z = resource['collision_max']
                corners = np.array([[a*x/2, b*y, c*z/2, 1.] for a in (-1, 1) for b in (0, 1) for c in (-1, 1)])
                planes = [vp[3] + sign * vp[axis] for axis in range(3) for sign in (-1, 1)]
                rejected = any(np.max(corners @ plane) < 0 for plane in planes)
                assert rejected == edge['old_collision_rejects']
                assert not count or edge['observed']['new_clip_accepts']
                assert edge['recovered_false_cull'] == (rejected and count > 0 and edge['observed']['new_clip_accepts'])
                witnesses.append({'id': rid, 'material': material, 'recovered': edge['recovered_false_cull'],
                                  'visible_pixels': count, 'file': path.relative_to(out).as_posix(), 'sha256': sha(path)})
    assert loadout_count == 44
    examples = [('bee','bare'),('spider','bare'),('cow','bare'),('human','copper'),
                ('human_guard','iron'),('rigid_demo_flyer','bare'),('rigid_demo_swimmer','bare'),('fire_spirit','bare')]
    font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 14)
    sheet = Image.new('RGB', (768, len(examples) * 240 + 40), '#171b20')
    draw = ImageDraw.Draw(sheet)
    draw.text((8, 8), 'Actual actor requests + inventory; culling diagnostic, not art acceptance', fill='white', font=font)
    for row, (rid, material) in enumerate(examples):
        case = next(l for l in resources[rid]['loadouts'] if l['material'] == material)
        for col, state in enumerate(('idle','run','fly' if rid in ('bee','rigid_demo_flyer','fire_spirit') else 'swim' if rid=='rigid_demo_swimmer' else 'action','edge')):
            phase=.25 if state in ('run','fly','swim') else .5 if state=='action' else 0.
            item = case['edge'] if state == 'edge' and case['edge'] else next((f for f in case['frames'] if f['state'].lower()==state and f['phase']==phase), case['frames'][0])
            y = 40 + row*240
            label='edge: none' if state=='edge' and not case['edge'] else state
            draw.text((col*192+4,y+2),rid,fill='white',font=font)
            draw.text((col*192+4,y+19),material+' | '+label+((' '+str(phase)) if state!='edge' else ''),fill='white',font=font)
            with Image.open(folder/item['native']['png']) as im:
                bg=Image.new('RGBA',im.size,(220,224,229,255));bg.alpha_composite(im.convert('RGBA'))
                sheet.paste(bg.convert('RGB').resize((192,192),Image.Resampling.NEAREST),(col*192,y+42))
    sheet.save(out/'posed_examples.png')
    report={'schema':'creature-posed-culling-audit.v1','passed':True,
            'scope':manifest['scope'],'exe_sha256':context['exe_sha256'],'resources':len(resources),
            'loadout_cases':loadout_count,'native_main_images':len(captures),'equipped_native_frames':equipment_rows,
            'edge_images':len(witnesses),'recovered_false_culls':sum(w['recovered'] for w in witnesses),
            'context_sha256':sha(context_path),'manifest_sha256':sha(folder/'manifest.json'),
            'sheet_sha256':sha(out/'posed_examples.png'),'witnesses':witnesses,'native_frames':captures}
    (out/'audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('witnesses','native_frames')},indent=2))


if __name__=='__main__':main()
