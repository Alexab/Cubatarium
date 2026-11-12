"""Enable distance playback only on the unpublished IK badger catalog copy."""
import json
from pathlib import Path
import shutil
from audit_creature_motion_v5 import sha
from review_creature_redesign import accessor

ROOT=Path(__file__).resolve().parents[2]


def main():
    source=ROOT/'assetgen_output/creatures/badger_stance_ik_v2'
    out=ROOT/'assetgen_output/creatures/badger_distance_gait_v1';assert not out.exists()
    contract=json.loads((source/'support_contract.json').read_text())
    folder=source/'models/creatures/badger';g=json.loads((folder/'model.gltf').read_text())
    blobs=[(folder/b['uri']).read_bytes()for b in g['buffers']]
    walk=next(a for a in g['animations']if a['name']=='walk')
    duration=max(float(accessor(g,blobs,s['input'])[-1,0])for s in walk['samplers'])
    definition=json.loads((folder/'creature.json').read_text())
    distance=duration*contract['travel_model_per_clip_second']*definition['visual']['gltf']['model_scale']
    definition['visual']['animation']['clips']['walk']['ground_distance_per_cycle']=distance
    out.mkdir(parents=True);shutil.copytree(source/'models',out/'models')
    target=out/'models/creatures/badger/creature.json'
    target.write_text(json.dumps(definition,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    shutil.copy2(source/'support_contract.json',out/'support_contract.json')
    for p in (source/'models').rglob('*'):
        if p.is_file()and p.relative_to(source/'models')!=Path('creatures/badger/creature.json'):
            assert p.read_bytes()==(out/'models'/p.relative_to(source/'models')).read_bytes()
    report={'state':'unapproved_runtime_metadata_pilot','scope':'Only badger Walk ground_distance_per_cycle changed; no new geometry/textures/rig/keys. Generic generator not yet changed',
        'clip_duration_asset_seconds':duration,'world_distance_full_macroloop':distance,
        'gait_cycles':contract['gait_cycles'],'world_distance_single_stride':distance/contract['gait_cycles'],
        'source_definition_sha256':sha(folder/'creature.json'),'definition_sha256':sha(target),
        'model_bin_sha256':sha(out/'models/creatures/badger/model.bin'),'contract_sha256':sha(out/'support_contract.json'),
        'preparer_sha256':sha(Path(__file__))}
    (out/'proposal_report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
