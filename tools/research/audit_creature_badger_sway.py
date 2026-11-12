"""Check a scratch badger proposal against own rig/texture/motion invariants."""
import argparse
import json
from pathlib import Path
import numpy as np
import yaml
from audit_creature_motion_v5 import ROOT, inspect, sha
from audit_creature_orc_sway import angle
from review_creature_redesign import accessor, sample_nodes
from analyze_creature_envelopes import measure
from audit_creature_anatomy_pilot import verified_context


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qa-name', default='badger_sway_v1')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(); out = args.out.resolve()
    qa = ROOT / 'assetgen_output/creatures' / args.qa_name
    source = ROOT / 'models/creatures/badger'; candidate = qa / 'models/creatures/badger'
    current_context = verified_context(out,'context_envelope_current.json')
    before_context = verified_context(out,'context_own_before.json')
    reference_context = verified_context(out,'context_envelope_original.json')
    assert Path(current_context['models_root']) == qa/'models'
    before_files = {name.replace('\\','/'):digest for name,digest in before_context['files_sha256'].items()}
    for name in ['model.gltf','model.bin','creature.json']:
        assert before_files['creatures/badger/'+name] == sha(source/name)
    reference = ROOT/'assetgen_output/creatures/review_2026_11_10/baseline/models/creatures/badger'
    diagnosis = json.loads((ROOT/'docs/research/creature-r6-v2/envelopes/motion_semantics_v2/diagnosis.json').read_text())
    for name,digest in diagnosis['resources']['badger']['reference']['files_sha256'].items():
        assert sha(reference/name) == digest
    g, b, stats = inspect(source); h, d, new_stats = inspect(candidate)
    assert stats == new_stats and g['nodes'] == h['nodes'] and g['skins'] == h['skins']
    assert g['materials'] == h['materials'] and g['images'] == h['images']
    assert (source/'creature.json').read_bytes() == (candidate/'creature.json').read_bytes()
    for x,y in zip(g['meshes'][0]['primitives'], h['meshes'][0]['primitives']):
        assert x['attributes'].keys() == y['attributes'].keys()
        for key in x['attributes']:
            assert np.array_equal(accessor(g,b,x['attributes'][key]), accessor(h,d,y['attributes'][key]))
        assert np.array_equal(accessor(g,b,x['indices']), accessor(h,d,y['indices']))
    for x,y in zip(g['skins'], h['skins']):
        assert np.array_equal(accessor(g,b,x['inverseBindMatrices']), accessor(h,d,y['inverseBindMatrices']))
    for p in (source/'textures').glob('*.png'):
        assert p.read_bytes() == (candidate/'textures'/p.name).read_bytes()
    old_recipe = yaml.safe_load((ROOT/'tools/creature_recipes/badger.yaml').read_text(encoding='utf8'))
    definition = json.loads((source/'creature.json').read_text())
    recipe = yaml.safe_load((qa/'recipes/badger.yaml').read_text(encoding='utf8'))
    assert old_recipe['anatomy'] == recipe['anatomy']
    root = next(i for i,n in enumerate(g['nodes']) if n.get('name') == 'body')
    rows = []; exact = 0
    for a,c in zip(g['animations'], h['animations']):
        assert a['name'] == c['name']; name = a['name']
        if name not in ('walk','run'):
            assert a['channels'] == c['channels']
            for x,y in zip(a['samplers'],c['samplers']):
                for key in ('input','output'):
                    assert np.array_equal(accessor(g,b,x[key]), accessor(h,d,y[key]))
                exact += 1
            continue
        old_duration = max(float(accessor(g,b,s['input'])[-1,0]) for s in a['samplers'])
        duration = max(float(accessor(h,d,s['input'])[-1,0]) for s in c['samplers'])
        cycles = recipe['motion']['clips'][name]['gait_cycles_per_loop']
        assert abs(duration/old_duration-cycles) < 1e-5
        speed = definition['visual']['animation']['clips'][name]['speed']
        assert recipe['motion']['clips'][name].get('runtime_speed') == old_recipe['motion']['clips'][name].get('runtime_speed')
        maximum = translation = seam = 0.
        for phase in np.linspace(0,1,cycles*256+1):
            old_phase = (float(phase)*duration % old_duration)/old_duration
            before = sample_nodes(g,b,name,old_phase); after = sample_nodes(h,d,name,float(phase))
            for i,(x,y) in enumerate(zip(before,after)):
                if i != root:
                    maximum = max(maximum,angle(x.get('rotation',[0,0,0,1]),y.get('rotation',[0,0,0,1])))
                delta = np.array(x.get('translation',[0,0,0]))-np.array(y.get('translation',[0,0,0]))
                translation = max(translation,float(np.abs(delta[[0,2]] if i==root else delta).max()))
        assert maximum < .001 and translation < 1e-6, (name,maximum,translation)
        first,last = sample_nodes(h,d,name,0.),sample_nodes(h,d,name,1.)
        for x,y in zip(first,last):
            assert angle(x.get('rotation',[0,0,0,1]),y.get('rotation',[0,0,0,1])) < .001
            seam = max(seam,float(np.max(np.abs(np.array(x.get('translation',[0,0,0]))-y.get('translation',[0,0,0])))))
        assert seam < 1e-5
        rows.append({'clip':name,'stride_cycles':cycles,'stride_period_seconds_at_unit_travel':old_duration/speed,
                     'sway_period_seconds_at_unit_travel':duration/speed,'nonroot_rotation_max_error_degrees':maximum,
                     'unchanged_translation_max_error_blocks':translation,'C0_translation_seam_blocks':seam})
    native = json.loads((out/'native_current/transition_probe.json').read_text())['resources']
    assert len(native) == 1 and native[0]['id'] == 'badger'
    frames = [f for c in native[0]['cases'] for f in c['frames']]
    floor = [f['low'][1] for c in native[0]['cases'] if c['supported'] for f in c['frames']]
    assert floor and min(floor) >= -.001 and all(f['visual_envelope_covers_geometry'] for f in frames)
    manifests = [json.loads((out/n/'envelopes/manifest.json').read_text())['resources'][0]
                 for n in ['native_before','native_current']]
    unchanged = 0
    for x,y in zip(manifests[0]['frames'],manifests[1]['frames']):
        assert (x['state'],x['phase'],x['view']) == (y['state'],y['phase'],y['view'])
        if x['state'] in ('Idle','Action'):
            from PIL import Image
            with Image.open(out/'native_before/envelopes'/x['native']['png']) as im, Image.open(out/'native_current/envelopes'/y['native']['png']) as jm:
                assert im.size == jm.size and im.convert('RGBA').tobytes() == jm.convert('RGBA').tobytes()
            unchanged += 1
    ordinary_floor = {}
    for label,folder in [('own_before',source),('candidate',candidate)]:
        dense = measure(folder,2049)
        ordinary_floor[label] = {}
        for name in ['walk','run']:
            heights = [s['low'][1] for s in dense['clips'][name]['samples']]
            ordinary_floor[label][name] = {'samples':len(heights),'minimum_mesh_y':min(heights),
                                           'maximum_mesh_floor_gap':max(heights)}
            assert min(heights) >= -.001, (label,name,min(heights))
            assert max(heights) <= .001, (label,name,'root contact resolution',max(heights))
    world_file = out/'world_motion_envelope_current.jsonl'
    world = [json.loads(line) for line in world_file.read_text().splitlines() if line]
    assert current_context['world_motion'] == 'land_walk_pause'
    assert world and {r['species'] for r in world} == {'badger'}
    assert all(r['on_ground'] and not r['in_fluid'] and not r['frozen'] and not r['body_blocked'] for r in world)
    walking = [r for r in world if r['state'].lower() == 'walk' and r['requested_move'] and r['horizontal_speed'] > .05]
    paused = [r for r in world if r['state'].lower() == 'idle' and not r['requested_move']]
    assert walking and paused and {r['requested_direction'] for r in walking} == {-1.,1.}
    result = {'passed':True,'state':'unapproved_proposal','scope':'Own geometry/rig/texture exact, equal-elapsed local limb channels, C0 seams and sampled native transitions; not art/stride/sliding/world/performance acceptance',
              'source_geometry':stats,'candidate_geometry':new_stats,'macroloops':rows,
              'unchanged_idle_action_samplers':exact,'unchanged_idle_action_native_envelope_png':unchanged,
              'transition_frames':len(frames),'supported_frames':len(floor),'supported_min_y':min(floor),
              'ordinary_sampled_full_mesh_floor':ordinary_floor,
              'world_route':{'scope':'One ordinary motor flat-ground QA Walk/pause route, not foot stance/sliding/AI/terrain acceptance',
                'log_sha256':sha(world_file),'records':len(world),'grounded_walk':len(walking),'idle_pause':len(paused),
                'frozen_frames':sum(r['frozen'] for r in world),'blocked_frames':sum(r['body_blocked'] for r in world),
                'world_x_range':[min(r['position'][0] for r in world),max(r['position'][0] for r in world)],
                'world_y_range':[min(r['position'][1] for r in world),max(r['position'][1] for r in world)]},
              'builder_sha256':sha(Path(__file__)),
              'source_hashes':{str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'tools/creature_recipes/badger.yaml',source/'model.gltf',source/'model.bin',source/'creature.json']},
              'candidate_hashes':{str(p.relative_to(candidate)):sha(p) for p in candidate.rglob('*') if p.is_file()}}
    (out/'motion_invariant_audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if not k.endswith('hashes')},indent=2))


if __name__ == '__main__':
    main()
