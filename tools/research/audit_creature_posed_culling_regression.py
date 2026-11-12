"""Verify full-catalog/FP regression and real world actor evidence after clipping."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]


def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--fast',action='store_true',help='Audit final early-exit implementation and its fresh world runs')
    args=parser.parse_args();out=args.out.resolve()
    product=out/('fast_clip' if args.fast else 'production');before=out.parent/'motion_v5/production'
    context=read(product/'context_production_final.json');old_context=read(before/'context_production_final.json')
    assert context['exe_sha256']==sha(ROOT/'bin/Cubatarium.exe')
    assert context['files_sha256']==old_context['files_sha256']
    assert context['shaders_sha256']==old_context['shaders_sha256']
    for path,digest in old_context['equipment_files_sha256'].items():
        assert context['equipment_files_sha256'].get(path)==digest
    for path,digest in context['equipment_files_sha256'].items():
        assert sha(ROOT/'bin'/path)==digest
    body=read(product/'body_parity.json');fp=read(product/'fp_audit.json');save=read(product/'audit_production_final.json')
    assert body['exact_rgba_png_count']==5290 and not body['expected_changed_frames']
    assert fp['passed'] and fp['gpu_authored_arm_draws']==1872 and fp['retained_renderer_cases']==6
    assert save['passed'] and save['resources_checked']==128 and save['actual_loads']==256
    old_fp=before/'native_final/fp';new_fp=product/'native_final/fp'
    a,b=read(old_fp/'manifest.json'),read(new_fp/'manifest.json');count=0
    assert len(a['resources'])==len(b['resources'])==9
    for x,y in zip(a['resources'],b['resources']):
        assert x['id']==y['id'] and len(x['frames'])==len(y['frames'])==104
        for f,g in zip(x['frames'],y['frames']):
            assert f['png']==g['png']
            with Image.open(old_fp/f['png']) as i,Image.open(new_fp/g['png']) as j:
                assert i.size==j.size and i.convert('RGBA').tobytes()==j.convert('RGBA').tobytes()
            count+=1
    assert count==936
    posed_count=0
    if args.fast:
        first=read(out/'verified/native_posed/posed_culling/manifest.json')
        last=read(product/'native_final/posed_culling/manifest.json')
        assert first==last
        for a,b in zip(first['resources'],last['resources']):
            for x,y in zip(a['loadouts'],b['loadouts']):
                frames=list(x['frames'])+([x['edge']]if x['edge']else[])
                for f in frames:
                    name=f['native']['png']
                    with Image.open(out/'verified/native_posed/posed_culling'/name)as i,Image.open(product/'native_final/posed_culling'/name)as j:
                        assert i.size==j.size and i.convert('RGBA').tobytes()==j.convert('RGBA').tobytes()
                    posed_count+=1
        assert posed_count==1349
    worlds=[]
    world_cases=((product,'production_final'),(out/'fast_world','world_rigid_demo_walker'),(out/'fast_world','world_fire_spirit'))if args.fast else (
        (out/'verified','posed_culling'),(product,'production_final'),(out/'sprite_world','sprite_world'))
    for folder,label in world_cases:
        c=read(folder/f'context_{label}.json');flight=read(folder/f'report_{label}.json')
        assert c['exe_sha256']==context['exe_sha256'] and flight['process_success'] and not flight['hang_killed']
        relocation=(folder/f'relocation_{label}.txt').read_text(encoding='utf8')
        assert all(any(line.split()[0]==sid and 'src=3' in line for line in relocation.splitlines() if line.split())for sid in c['world_species'])
        png=folder/f'runtime_{label}/frame_002.png'
        worlds.append({'species':c['world_species'],'context_sha256':sha(folder/f'context_{label}.json'),
          'world_frame':str(png.relative_to(out)),'world_frame_sha256':sha(png)})
    source=('src/Creatures/Visual/CreatureDrawClipBounds.h','src/Creatures/Visual/CreatureDrawQueue.h',
      'src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonTypes.h','src/Render/Engine/CreatureDrawPass.cpp',
      'src/Creatures/Visual/CreatureVisual.h','src/App/Platform/CreatureQaPosedCullingProbe.h',
      'src/Test/CreatureDrawClipBoundsTest.cpp')
    result={'schema':'creature-posed-culling-regression.v1','passed':True,
      'scope':'All61 sampled preview/FP pixels and isolated persistence; three short idle/no-travel world process/spawn subsets, not full camera routes or matched performance acceptance',
      'exe_sha256':context['exe_sha256'],'body_rgba_exact_count':5290,'fp_rgba_exact_count':count,
      'fp_gpu_arm_draws':1872,'entity_cases':128,'actual_entity_restores':256,'world_subsets':worlds,
      'early_exit_implementation':args.fast,'posed_before_after_rgba_exact_count':posed_count,
      'additional_equipment_input_count':len(set(context['equipment_files_sha256'])-set(old_context['equipment_files_sha256'])),
      'source_sha256_at_audit':{p:sha(ROOT/p)for p in source},
      'input_report_sha256':{p:sha(product/p)for p in ('body_parity.json','fp_audit.json','audit_production_final.json')}}
    (out/('regression_fast.json'if args.fast else 'regression_audit.json')).write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print('5290 body/936 FP exact;1872 GPU arm draws;128 entity cases/256 restores;three real world spawn subsets verified')


if __name__=='__main__':main()
