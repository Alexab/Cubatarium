"""Repeat native Release pilot probes. Produces evidence, never acceptance."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import shutil
import hashlib
import json
import time
from urllib.parse import unquote
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world',default='World_254')
    parser.add_argument('--profiles',default='human_adventurer,human_guard,stingray,rigid_demo_swimmer,air')
    parser.add_argument('--qa-name',help='generic isolated QA bundle under assetgen_output/creatures')
    parser.add_argument('--production',action='store_true',help='generic probe of ordinary bin/models, without QA model override')
    parser.add_argument('--species',default='')
    parser.add_argument('--preview-species',default='',help='UI/renderer probes only; actual world spawn gate still uses --species')
    parser.add_argument('--out',type=Path)
    parser.add_argument('--label',default='wave')
    parser.add_argument('--seconds',type=float,default=8.,help='World flight duration; longer motion routes allow buoyancy to settle after native exports')
    parser.add_argument('--capture-interval',type=float,default=3.,help='Actual world framebuffer capture interval in seconds')
    parser.add_argument('--water',action='store_true')
    parser.add_argument('--lava',action='store_true',help='contained lava fixture in disposable CreatureQaLava_* world only')
    parser.add_argument('--close',action='store_true',help='close follow camera; one species, optionally in real water')
    parser.add_argument('--primary-state',choices=['Walk','Swim','Fly','Hover','Glide'],help='Override ordinary preview frames4..11 only; does not force the actual world actor locomotion state')
    parser.add_argument('--native-dir',default='native_candidate')
    parser.add_argument('--skin',default='')
    parser.add_argument('--matrix',action='store_true',help='8 yaw + top/bottom, three poses, color/coverage silhouettes; includes compatible primary skins')
    parser.add_argument('--equipment-matrix',action='store_true',help='Native humanoid attachment matrix: both states, three speeds, 16 phases plus deepest-clearance phase, four loadouts')
    parser.add_argument('--equipment-material',choices=['copper','iron','leather'],default='copper')
    parser.add_argument('--world-gear',action='store_true',help='Equip the one actual humanoid QA actor through normal inventory APIs')
    parser.add_argument('--resource-cache-probe',action='store_true',help='Compare actual retained/fresh visual draws after mesh cache reset')
    parser.add_argument('--resource-reload-probe',action='store_true',help='Production56+5: retained texture/pack reload and missing model recovery')
    parser.add_argument('--fp-matrix',action='store_true',help='Actual first-person renderer: humanoid hands, loadouts and attack/walk phases')
    parser.add_argument('--save-probe',action='store_true',help='Real isolated entity legacy/v2 load-save-reload across production56+5 and aliases')
    parser.add_argument('--envelope-profile',type=Path,help='Research profile for native shared-scale original/current envelope captures')
    parser.add_argument('--envelope-variant',choices=('baseline','current'))
    parser.add_argument('--transition-probe',action='store_true',help='Actual retained glTF cold-entry clip transitions and submitted ground clearance')
    parser.add_argument('--fluid-transition-probe',action='store_true',help='Explicit amphibious land/free-fluid/bottom retained visual transitions')
    parser.add_argument('--world-motion',choices=['water_pause','water_swim_pause','land_walk_pause'],help='QA NPC input driver through ordinary motor/state derivation; land_walk_pause uses the same horizontal cycle on land')
    parser.add_argument('--world-speed',type=float,help='Positive ordinary-motor QA requested speed, at most4; default0.5')
    parser.add_argument('--pass-timing',action='store_true',help='Opt-in per-frame CPU pose/pass and asynchronous GPU draw interval; no complete performance verdict')
    parser.add_argument('--foot-probe',action='store_true',help='Observe actual QA badger submitted sole points; not a performance run or automatic footplant acceptance')
    parser.add_argument('--world-fluid-state',choices=['Swim','Tread'],default='Swim',help='Required derived moving fluid state; Tread selects a bottom-supported route without forcing actor facts')
    parser.add_argument('--posed-culling-probe',action='store_true',help='Actual B/R/sprite requests including actor inventory, native clip-edge witnesses')
    args=parser.parse_args()
    if not 0 < args.seconds <= 120 or not .1 <= args.capture_interval <= 60:parser.error('Invalid world duration/capture interval')
    if args.lava and (not args.world.startswith('CreatureQaLava_') or ',' in args.species):
        parser.error('--lava requires one species in a disposable CreatureQaLava_* world')
    generic=bool(args.qa_name or args.production)
    if args.world_motion and (not generic or ',' in args.species):parser.error('--world-motion requires one generic species')
    if args.world_speed is not None and (not args.world_motion or not 0<args.world_speed<=4):parser.error('--world-speed requires --world-motion and a finite speed in(0,4]')
    if args.world_motion and (args.water != (args.world_motion != 'land_walk_pause')):parser.error('--world-motion water/land fixture mismatch')
    if args.pass_timing and not generic:parser.error('--pass-timing requires a generic production/candidate probe')
    if args.foot_probe and (not generic or args.species!='badger' or args.world_motion!='land_walk_pause' or args.pass_timing):parser.error('--foot-probe requires one badger land_walk_pause, without --pass-timing')
    if args.world_gear and (not generic or ',' in args.species):parser.error('--world-gear requires one generic QA actor')
    if args.qa_name and args.production:parser.error('--qa-name and --production are mutually exclusive')
    if args.resource_reload_probe and not args.production:parser.error('--resource-reload-probe requires --production')
    if args.fp_matrix and not args.production:parser.error('--fp-matrix requires --production')
    if args.save_probe and not args.production:parser.error('--save-probe requires --production')
    if bool(args.envelope_profile) != bool(args.envelope_variant):parser.error('--envelope-profile and --envelope-variant are required together')
    if generic and (not args.species or args.out is None):
        parser.error('Generic probes require --species and --out')
    for name in ([args.label] if generic else args.profiles.split(',')):
        coat_skins={'sheep_wool_black':'sheep','sheep_wool_golden':'sheep','wolf_snow':'wolf'}
        coat=name=='coats' or name in coat_skins
        human=name.startswith('human_')
        aquatic=name in ('stingray','rigid_demo_swimmer','hermitcrab')
        if not generic and not human and not aquatic and not coat and name!='air':
            raise ValueError('Unknown probe '+name)
        out=ROOT/'docs/research'/('creature-humanoid-pilots-v2' if human else 'creature-aquatic-aerial-pilots-v2')
        qa='humanoid_v2' if human else 'aquatic_aerial_v2'
        if name=='hermitcrab':
            out=ROOT/'docs/research/creature-hermitcrab-stress-v2'
            qa='hermitcrab_v2'
        if coat:
            out=ROOT/'docs/research/creature-bone-coats-v2'
            qa='bone_coats_v2'
        env=os.environ.copy()
        env.pop('CUBA_CREATURE_QA_FOOT_PROBE_PATH',None)
        env.pop('CUBA_FLIGHT_QA_WORLD_USER',None)
        env.pop('CUBA_FLIGHT_QA_TRANSITION_PROBE',None)
        env.pop('CUBA_FLIGHT_QA_FLUID_TRANSITIONS',None)
        env.pop('CUBA_FLIGHT_QA_WORLD_MOTION',None)
        env.pop('CUBA_FLIGHT_QA_WORLD_MOVE_SPEED',None)
        if args.fluid_transition_probe:env['CUBA_FLIGHT_QA_FLUID_TRANSITIONS']='1'
        if args.world_motion:env['CUBA_FLIGHT_QA_WORLD_MOTION']=args.world_motion
        if args.world_motion:env['CUBA_FLIGHT_QA_WORLD_MOVE_SPEED']=str(args.world_speed if args.world_speed is not None else .5)
        env.pop('CUBA_FLIGHT_QA_POSED_CULLING',None)
        if args.posed_culling_probe:env['CUBA_FLIGHT_QA_POSED_CULLING']='1'
        if args.transition_probe:env['CUBA_FLIGHT_QA_TRANSITION_PROBE']='1'
        env.pop('CUBA_FLIGHT_QA_ENVELOPE_PROFILE',None)
        env.pop('CUBA_FLIGHT_QA_ENVELOPE_VARIANT',None)
        if args.envelope_profile:
            env['CUBA_FLIGHT_QA_ENVELOPE_PROFILE']=str(args.envelope_profile.resolve())
            env['CUBA_FLIGHT_QA_ENVELOPE_VARIANT']=args.envelope_variant
        env.pop('CUBA_FLIGHT_QA_RESOURCE_CACHE_PROBE',None)
        if args.resource_cache_probe:env['CUBA_FLIGHT_QA_RESOURCE_CACHE_PROBE']='1'
        env.pop('CUBA_FLIGHT_QA_RESOURCE_RELOAD_PROBE',None)
        if args.resource_reload_probe:env['CUBA_FLIGHT_QA_RESOURCE_RELOAD_PROBE']='1'
        env.pop('CUBA_FLIGHT_QA_FP_MATRIX',None)
        if args.fp_matrix:env['CUBA_FLIGHT_QA_FP_MATRIX']='1'
        env.pop('CUBA_FLIGHT_QA_SAVE_PROBE',None)
        if args.save_probe:env['CUBA_FLIGHT_QA_SAVE_PROBE']='1'
        for key in ('CUBA_FLIGHT_QA_WATER','CUBA_FLIGHT_QA_LAVA','CUBA_FLIGHT_QA_FP_SKIN','CUBA_FLIGHT_QA_PREVIEWS','CUBA_FLIGHT_QA_PREVIEW_SPECIES','CUBA_FLIGHT_QA_SKIN','CUBA_FLIGHT_QA_CLOSE','CUBA_FLIGHT_QA_PRIMARY_STATE','CUBA_FLIGHT_QA_MATRIX','CUBA_FLIGHT_QA_EQUIPMENT_MATRIX','CUBA_FLIGHT_QA_EQUIPMENT_MATERIAL','CUBA_FLIGHT_QA_WORLD_GEAR'):
            env.pop(key,None)
        env.update(CUBA_CREATURE_QA_MODELS=str(ROOT/'assetgen_output/creatures'/qa/'models'),
            CUBA_FLIGHT_QA_GRID='1',CUBA_FLIGHT_QA_RESET='1',
            CUBA_FLIGHT_QA_SPECIES='human,zombie' if human else name if aquatic else 'butterfly,rigid_demo_flyer,fire_spirit',
            CUBA_FLIGHT_CAPTURE_DIR=str(out/('runtime_'+name)),CUBA_FLIGHT_CAPTURE_INTERVAL_SEC=str(args.capture_interval))
        native_folder='native_baseline' if name=='human_baseline' else 'native_candidate'
        if coat:
            env['CUBA_FLIGHT_QA_SPECIES']=coat_skins.get(name,'pig,sheep,wolf,fox')
        if name in coat_skins:
            env['CUBA_FLIGHT_QA_SKIN']=name
            native_folder='native_'+name
        if name=='human_baseline':
            env['CUBA_CREATURE_QA_MODELS']=str(ROOT/'assetgen_output/creatures/review_2026_11_10/baseline/models')
        if name!='human_guard':
            env['CUBA_FLIGHT_QA_PREVIEWS']=str(out/native_folder)
        if human:env['CUBA_FLIGHT_QA_FP_SKIN']='human_adventurer' if name=='human_baseline' else name
        if aquatic:env['CUBA_FLIGHT_QA_WATER']='1'
        if generic:
            out=args.out.resolve()
            if not out.is_relative_to((ROOT/'docs/research').resolve()):
                raise ValueError('Research evidence must remain under docs/research')
            models=(ROOT/'bin/models' if args.production else ROOT/'assetgen_output/creatures'/args.qa_name/'models').resolve()
            if not args.production and not models.is_relative_to((ROOT/'assetgen_output/creatures').resolve()):
                raise ValueError('Candidate catalog outside QA scratch')
            env.update(CUBA_CREATURE_QA_MODELS=str(models),CUBA_FLIGHT_QA_SPECIES=args.species,
                CUBA_FLIGHT_QA_PREVIEWS=str(out/'native_candidate'),
                CUBA_FLIGHT_CAPTURE_DIR=str(out/('runtime_'+name)))
            if args.production:env.pop('CUBA_CREATURE_QA_MODELS',None)
            if args.water:env['CUBA_FLIGHT_QA_WATER']='1'
            if args.lava:env['CUBA_FLIGHT_QA_LAVA']='1'
            if args.close:
                if ',' in args.species:
                    raise ValueError('--close requires one species')
                env['CUBA_FLIGHT_QA_CLOSE']='1'
            if args.skin:env['CUBA_FLIGHT_QA_SKIN']=args.skin
            if Path(args.native_dir).name != args.native_dir or args.native_dir in ('.','..'):
                raise ValueError('Native evidence folder must be a single directory name')
            native_folder=args.native_dir
            env['CUBA_FLIGHT_QA_PREVIEWS']=str(out/native_folder)
            if args.primary_state:env['CUBA_FLIGHT_QA_PRIMARY_STATE']=args.primary_state
            if args.preview_species:env['CUBA_FLIGHT_QA_PREVIEW_SPECIES']=args.preview_species
            if args.matrix:env['CUBA_FLIGHT_QA_MATRIX']='1'
            if args.equipment_matrix:env.update(CUBA_FLIGHT_QA_EQUIPMENT_MATRIX='1',CUBA_FLIGHT_QA_EQUIPMENT_MATERIAL=args.equipment_material)
            if args.world_gear:env.update(CUBA_FLIGHT_QA_WORLD_GEAR='1',CUBA_FLIGHT_QA_EQUIPMENT_MATERIAL=args.equipment_material)
        models=Path(env.get('CUBA_CREATURE_QA_MODELS',str(ROOT/'bin/models')))
        if args.foot_probe:
            # Observer v3 follows the lower-foot subtree and records bindings.
            # Keep this pilot restricted to the reviewed badger rig variants.
            foot_gltf=json.loads((models/'creatures/badger/model.gltf').read_text())
            joints=foot_gltf['skins'][0]['joints']
            lower=[n for n in foot_gltf['nodes'] if n.get('name','').startswith('lower_')]
            if len(joints) not in (13,17,19) or len(lower)!=4:
                raise ValueError('--foot-probe v3 requires a known13/17/19-joint badger with four semantic lower-foot roots')
        files={}
        preview_ids=env.get('CUBA_FLIGHT_QA_PREVIEW_SPECIES',env['CUBA_FLIGHT_QA_SPECIES']).split(',')
        if generic:
            missing=[sid for sid in set(preview_ids)|set(env['CUBA_FLIGHT_QA_SPECIES'].split(','))
                     if not (models/'creatures'/sid/'creature.json').is_file()]
            if missing:raise ValueError('Species absent from selected QA catalog: '+','.join(sorted(missing)))
        folders=[models/'creatures'/sid for sid in sorted(set(preview_ids)|set(env['CUBA_FLIGHT_QA_SPECIES'].split(',')))]
        if args.save_probe:folders.extend(p.parent for p in (models/'creatures').glob('*/creature.json'))
        if human:folders.extend(models/'skins'/sid for sid in ('human_adventurer','human_guard'))
        if name in coat_skins:folders.append(models/'skins'/name)
        if args.skin:folders.append(models/'skins'/args.skin)
        if args.matrix or args.equipment_matrix or args.fp_matrix or args.save_probe or args.posed_culling_probe:folders.extend(p.parent for p in (models/'skins').glob('*/skin.json'))
        for folder in folders:
            for path in sorted(folder.rglob('*')):
                if path.is_file():files[str(path.relative_to(models))]=hashlib.sha256(path.read_bytes()).hexdigest()
        equipment_files={}
        if args.equipment_matrix or args.world_gear or args.fp_matrix or args.save_probe or args.posed_culling_probe:
            runtime=ROOT/'bin'
            def record(path):
                path=path.resolve()
                if not path.is_relative_to(runtime.resolve()):raise ValueError('Equipment asset outside runtime root')
                equipment_files[path.relative_to(runtime).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
            presets=runtime/'content/item_visual_presets.json'
            if presets.is_file():record(presets)
            equipment_ids=set(args.equipment_material+'_'+slot for slot in ('head','chest','legs','feet'))
            equipment_ids.update(('wood_sword','wood_torch'))
            if args.equipment_matrix or args.posed_culling_probe:
                equipment_ids.update(material+'_'+slot for material in ('copper','iron','leather') for slot in ('chest','legs'))
            if args.posed_culling_probe:
                equipment_ids.update(material+'_'+slot for material in ('copper','iron','leather') for slot in ('head','chest','legs','feet'))
            for iid in sorted(equipment_ids):
                definition=runtime/'content/items'/f'{iid}.json';record(definition)
                data=json.loads(definition.read_text(encoding='utf-8-sig'))
                model=runtime/data.get('model',f'models/items/{iid}/model.gltf');record(model)
                if model.suffix=='.gltf':
                    gltf=json.loads(model.read_text(encoding='utf-8-sig'))
                    for resource in gltf.get('buffers',[])+gltf.get('images',[]):
                        uri=resource.get('uri','')
                        if uri and not uri.startswith('data:'):record(model.parent/unquote(uri))
                wear=runtime/'models/items'/iid/'wear.json'
                if wear.is_file():
                    record(wear)
                    worn_model=json.loads(wear.read_text(encoding='utf-8-sig')).get('model')
                    if worn_model:
                        variant=wear.parent/worn_model;record(variant)
                        gltf=json.loads(variant.read_text(encoding='utf-8-sig'))
                        for resource in gltf.get('buffers',[])+gltf.get('images',[]):
                            uri=resource.get('uri','')
                            if uri and not uri.startswith('data:'):record(variant.parent/unquote(uri))
                grip=runtime/'models/items'/iid/'wield_grip.json'
                if grip.is_file():record(grip)
        if (args.resource_reload_probe or args.fp_matrix or args.save_probe or args.envelope_profile or args.transition_probe or args.fluid_transition_probe or args.world_motion or args.posed_culling_probe or args.pass_timing or args.foot_probe) and ((out/native_folder).exists() or (out/f'context_{name}.json').exists()):
            raise ValueError('Native reload/FP/save captures require fresh --label and --native-dir; existing evidence is immutable')
        out.mkdir(parents=True,exist_ok=True)
        env.pop('CUBA_CREATURE_PASS_TIMING_PATH',None)
        if args.pass_timing:env['CUBA_CREATURE_PASS_TIMING_PATH']=str(out/f'pass_timing_{name}.jsonl')
        if args.foot_probe:env['CUBA_CREATURE_QA_FOOT_PROBE_PATH']=str(out/f'foot_probe_{name}.jsonl')
        (out/('context_'+name+'.json')).write_text(json.dumps({
            'captured_at_utc':datetime.now(timezone.utc).isoformat(),
            'models_root':str(models),'files_sha256':files,
            'qa_models_override':bool(env.get('CUBA_CREATURE_QA_MODELS')),
            'exe_sha256':hashlib.sha256((ROOT/'bin/Cubatarium.exe').read_bytes()).hexdigest(),
            'shaders_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (ROOT/'bin/shaders').glob('*creature*.glsl')},
            'profile':name,'qa_camera':'close' if env.get('CUBA_FLIGHT_QA_CLOSE') else 'grid',
            'native_matrix':args.matrix,
            'native_equipment_matrix':args.equipment_matrix,
            'native_equipment_texture_probe':args.equipment_matrix,
            'native_resource_cache_probe':args.resource_cache_probe,
            'native_resource_reload_probe':args.resource_reload_probe,
            'native_fp_matrix':args.fp_matrix,
            'native_save_probe':args.save_probe,
            'native_envelope_variant':args.envelope_variant,
            'native_transition_probe':args.transition_probe,
            'native_fluid_transition_probe':args.fluid_transition_probe,
            'world_motion':args.world_motion,
            'world_requested_speed':(args.world_speed if args.world_speed is not None else .5)if args.world_motion else None,
            'world_motion_expected_fluid_state':args.world_fluid_state if args.world_motion else None,
            'world_seconds':args.seconds,'world_capture_interval_seconds':args.capture_interval,
            'native_posed_culling_probe':args.posed_culling_probe,
            'envelope_profile_sha256':hashlib.sha256(args.envelope_profile.read_bytes()).hexdigest() if args.envelope_profile else None,
            'fp_shaders_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (ROOT/'bin/shaders/vshader.glsl',ROOT/'bin/shaders/fshader.glsl')} if args.fp_matrix else {},
            'equipment_material':args.equipment_material,
            'world_gear_fixture':args.world_gear,
            'equipment_files_sha256':equipment_files,
            'world_species':env['CUBA_FLIGHT_QA_SPECIES'].split(','),'preview_species':preview_ids,
            'qa_water_placement':bool(env.get('CUBA_FLIGHT_QA_WATER')),
            'qa_lava_fixture':bool(env.get('CUBA_FLIGHT_QA_LAVA')),'world':args.world,
            'qa_primary_state':env.get('CUBA_FLIGHT_QA_PRIMARY_STATE'),
            'pass_timing':args.pass_timing,'foot_probe':args.foot_probe,'performance_gate':'disabled'},indent=2)+'\n',encoding='utf8')
        print('PROBE',name,flush=True)
        capture_started_ns=time.time_ns()
        subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/flight_sim_run.py'),
            '--world',args.world,'--require-existing-world','--seconds',str(args.seconds),'--no-fly',
            '--process-timeout','300' if args.resource_reload_probe or args.fp_matrix or args.save_probe or args.envelope_profile or args.transition_probe or args.fluid_transition_probe or args.posed_culling_probe else '240' if args.equipment_matrix else '180' if args.matrix else '90','--report',str(out/('report_'+name+'.json'))],
            cwd=ROOT,env=env,check=True)
        relocation=ROOT/'bin/qa_relocation.txt'
        if generic and (not relocation.is_file() or relocation.stat().st_mtime_ns<capture_started_ns):
            raise RuntimeError('QA relocation evidence missing or stale; frame 30 was not observed')
        if relocation.is_file():shutil.copy2(relocation,out/('relocation_'+name+'.txt'))
        if generic:
            observed={line.split()[0] for line in relocation.read_text(encoding='utf8').splitlines()
                      if 'src=3' in line}
            missing=set(args.species.split(','))-observed
            if missing:raise RuntimeError('QA species did not actually spawn: '+','.join(sorted(missing)))
        if args.world_motion:
            source=ROOT/'bin/qa_world_motion.jsonl'
            if not source.is_file() or source.stat().st_mtime_ns<capture_started_ns:raise RuntimeError('Actual world motion log missing or stale')
            shutil.copy2(source,out/('world_motion_'+name+'.jsonl'))
            records=[json.loads(line)for line in source.read_text().splitlines()if line]
            if not records or {r['species']for r in records}!=set(args.species.split(',')):raise RuntimeError('World motion species differ')
            if args.world_motion=='land_walk_pause':
                moving=[r for r in records if r['state'].lower()=='walk' and r['on_ground'] and not r['in_fluid'] and r['requested_move'] and r['horizontal_speed']>.05]
                if not moving or not any(not r['requested_move'] and not r['in_fluid'] for r in records):raise RuntimeError('Actual grounded Walk/pause not observed')
            else:
                moving=[r for r in records if r['state'].lower()==args.world_fluid_state.lower() and r['in_fluid'] and r['requested_move'] and r['horizontal_speed']>.05]
                if args.world_fluid_state=='Tread':moving=[r for r in moving if r['on_fluid_bottom']]
                if not moving:raise RuntimeError('Actual world '+args.world_fluid_state+' not observed')
                if not any(r['in_fluid'] and not r['requested_move']for r in records):raise RuntimeError('Actual fluid input pause not observed')
        if args.pass_timing:
            path=out/f'pass_timing_{name}.jsonl'
            if not path.is_file() or path.stat().st_mtime_ns<capture_started_ns:raise RuntimeError('Actual creature pass timing missing or stale')
            rows=[json.loads(line) for line in path.read_text().splitlines() if line]
            if rows[0].get('schema')!='creature-pass-timing.v1' or rows[-1].get('kind')!='end' or rows[-1]['dropped_frames']:
                raise RuntimeError('Incomplete creature timing capture')
            frames=[r for r in rows if r.get('kind')=='frame']
            if len(frames)<60 or len(frames)!=rows[-1]['frames'] or any(r['window_wall_ms']<0 for r in frames):
                raise RuntimeError('Creature timing frame coverage failed')
        if args.foot_probe:
            path=out/f'foot_probe_{name}.jsonl'
            if not path.is_file() or path.stat().st_mtime_ns<capture_started_ns:raise RuntimeError('Foot probe missing or stale')
            rows=[json.loads(line)for line in path.read_text().splitlines()if line]
            if len(rows)<60 or rows[-1]['elapsed_render']<5 or any(r['schema']!='creature-qa-foot-world.v3' or len(r['feet'])!=4 for r in rows):
                raise RuntimeError('Foot probe incomplete world coverage')
        if args.equipment_matrix:
            equipment=out/native_folder/'equipment'
            probe=equipment/'texture_model_isolation.json'
            if not probe.is_file() or probe.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Equipment texture isolation evidence missing or stale')
            isolation=json.loads(probe.read_text())
            if len(isolation['models'])!=9 or not all(m['isolated_and_repeatable'] for m in isolation['models']):
                raise RuntimeError('Equipment per-model texture cache isolation failed: '+str(isolation['models']))
            manifest=equipment/'manifest.json'
            if not manifest.is_file() or manifest.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Equipment matrix manifest missing or stale')
            matrix=json.loads(manifest.read_text())
            for resource in matrix['resources']:
                assert len(resource['frames'])==408 and len(resource['views'])==96
                for frame in resource['frames']+resource['views']:
                    path=equipment/frame['png']
                    if not path.is_file() or path.stat().st_mtime_ns<capture_started_ns:
                        raise RuntimeError('Equipment frame missing or stale: '+str(path))
        if args.resource_cache_probe:
            probe_path=out/native_folder/'resource_cache_probe.json'
            if not probe_path.is_file() or probe_path.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Resource cache probe missing or stale')
            probe=json.loads(probe_path.read_text())
            if {r['id'] for r in probe['resources']}!=set(preview_ids) or not probe['passed']:
                raise RuntimeError('Live resource cache probe failed')
            skin_path=out/native_folder/'appearance_skin_probe.json'
            if not skin_path.is_file() or skin_path.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Skin ownership probe missing or stale')
            skin_probe=json.loads(skin_path.read_text())
            if not skin_probe['passed'] or not skin_probe['cases'] or not all(c['passed'] for c in skin_probe['cases']):
                raise RuntimeError('Skin ownership or effective selection failed')
            mapping_path=out/native_folder/'texture_mapping_probe.json'
            if not mapping_path.is_file() or mapping_path.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Texture mapping probe missing or stale')
            mapping=json.loads(mapping_path.read_text())
            if not mapping['passed'] or {c['species'] for c in mapping['cases']}!=set(preview_ids) or not all(c['passed'] for c in mapping['cases']):
                raise RuntimeError('Texture mapping, fallback or UV geometry preservation failed')
            if len(mapping['gpu_textures'])!=3*len(mapping['cases']) or not all(c['passed'] for c in mapping['gpu_textures']):
                raise RuntimeError('Diagnostic texture GPU readback failed')
        if args.resource_reload_probe:
            reload_path=out/native_folder/'resource_reload_probe.json'
            if not reload_path.is_file() or reload_path.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Resource reload probe missing or stale')
            reload=json.loads(reload_path.read_text())
            if not reload['passed'] or len(reload['resources'])!=61 or len(reload['texture_stages'])!=11 or len(reload['mesh_stages'])!=5:
                raise RuntimeError('Resource reload, recovery or pack priority failed')
            if not all(s['passed'] for s in reload['texture_stages']+reload['mesh_stages']) or not all(r['passed'] for r in reload['rigid_recovery']):
                raise RuntimeError('Resource reload subcase failed')
        if args.envelope_profile:
            manifest=out/native_folder/'envelopes/manifest.json'
            if not manifest.is_file() or manifest.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Native envelope manifest missing or stale')
            captures=json.loads(manifest.read_text());profile=json.loads(args.envelope_profile.read_text())
            if captures['variant']!=args.envelope_variant or {r['id'] for r in captures['resources']}!=set(preview_ids):
                raise RuntimeError('Envelope resource/variant matrix differs')
            for resource in captures['resources']:
                if len(resource['frames'])!=len(profile['resources'][resource['id']]['frames']):
                    raise RuntimeError('Incomplete native envelope frames')
                for frame in resource['frames']:
                    image=out/native_folder/'envelopes'/frame['native']['png']
                    if frame['native']['alpha_pixels']<=0 or not image.is_file() or image.stat().st_mtime_ns<capture_started_ns:
                        raise RuntimeError('Envelope native image empty, missing or stale')
        if args.transition_probe:
            path=out/native_folder/'transition_probe.json'
            if not path.is_file() or path.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Native transition probe missing or stale')
            transitions=json.loads(path.read_text())
            if {r['id'] for r in transitions['resources']}!=set(preview_ids) or not all(r['cases'] for r in transitions['resources']):
                raise RuntimeError('Native transition resource/case matrix incomplete')
        if args.fluid_transition_probe:
            path=out/native_folder/'fluid_transition_probe.json'
            if not path.is_file() or path.stat().st_mtime_ns<capture_started_ns:raise RuntimeError('Fluid transition manifest missing or stale')
            fluid=json.loads(path.read_text())
            if {r['id']for r in fluid['resources']}!=set(preview_ids):raise RuntimeError('Fluid transition species differ')
            for resource in fluid['resources']:
                if len(resource['cases'])!=160 or any(len(c['frames'])!=21 for c in resource['cases']):raise RuntimeError('Fluid phase/frame matrix incomplete')
                if not all(f['visual_envelope_covers_geometry']for c in resource['cases']for f in c['frames']):raise RuntimeError('Fluid geometry escapes envelope')
        if args.posed_culling_probe:
            path=out/native_folder/'posed_culling/manifest.json'
            if not path.is_file() or path.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Native posed-culling probe missing or stale')
            posed=json.loads(path.read_text())
            if not posed['resources'] or not all(r['loadouts'] for r in posed['resources']):
                raise RuntimeError('Posed-culling resources empty')
            for resource in posed['resources']:
                for loadout in resource['loadouts']:
                    if len(loadout['frames'])!=len(resource['sampled_states'])*4:raise RuntimeError('Posed-culling phase matrix incomplete')
                    for frame in loadout['frames']:
                        image=path.parent/frame['native']['png']
                        if not image.is_file() or image.stat().st_mtime_ns<capture_started_ns or frame['native']['alpha_pixels']<=0:
                            raise RuntimeError('Posed-culling native frame empty, missing or stale')
        if args.save_probe:
            manifest=out/native_folder/'save/manifest.json'
            if not manifest.is_file() or manifest.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('Save roundtrip manifest missing or stale')
            save=json.loads(manifest.read_text())
            if not save['passed'] or not save['controlled_actor_unchanged'] or len(save['cases'])!=2:
                raise RuntimeError('Save roundtrip or controlled actor preservation failed')
            for case in save['cases']:
                if not case['passed'] or not case['canonical_save_stable'] or len(case['resources'])!=64 or not all(r['passed'] for r in case['resources']):
                    raise RuntimeError('Save roundtrip resource matrix failed')
        if args.fp_matrix:
            fp=out/native_folder/'fp';manifest=fp/'manifest.json'
            if not manifest.is_file() or manifest.stat().st_mtime_ns<capture_started_ns:
                raise RuntimeError('FP capture manifest missing or stale')
            matrix=json.loads(manifest.read_text())
            if not matrix['passed'] or {r['id'] for r in matrix['resources']}!={'human','bot','zombie','skeleton','human_adventurer','human_guard','user_adventurer','user_guard','legacy_guard','human_female','human_female_adventurer','human_female_guard'} or not matrix['controlled_inventory_unchanged'] or not matrix['user_selection_restored']:
                raise RuntimeError('FP world appearance/selection or inventory preservation failed')
            for resource in matrix['resources']:
                if len(resource['frames'])!=104 or not resource['appearance_resolved']:
                    raise RuntimeError('FP resource frame matrix or appearance resolution failed')
                for frame in resource['frames']:
                    path=fp/frame['png']
                    if not frame['passed'] or not path.is_file() or path.stat().st_mtime_ns<capture_started_ns:
                        raise RuntimeError('FP capture failed, missing or stale: '+str(path))
        if name!='human_guard':
            for sid in preview_ids:
                from PIL import Image
                for index in range(35 if sid=='human' else 29):
                    with Image.open(out/native_folder/f'{sid}_{index}.png') as im:
                        im.load()
                        assert min(im.size)>=64
        print('CAPTURED',name,'(performance gates disabled)',flush=True)


if __name__=='__main__':main()
