"""Release/native manual-import + packaged Item Lab comparison on an isolated QA world.

Uses a temporary authored source project and a multi-image bucket. No production
resources, item definitions or player property are published or consumed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import yaml
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from itemgen.design import build_design
from itemgen.design_publication import validate_design,_hashes
from run_item_design_probes import runtime_designs
from qa_report_artifacts import reset_report


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--skip-build',action='store_true');parser.add_argument('--world',default='CarryAimQA');args=parser.parse_args()
    if not args.world.endswith('QA') or '/' in args.world or '\\' in args.world:parser.error('Use an existing isolated QA world')
    if not args.skip_build:subprocess.run(['cmake','--build','build/desktop-msvc','--config','Release','--target','Cubatarium','--parallel','8'],cwd=ROOT,check=True)
    for id in ['iron_pickaxe','iron_head','copper_legs','gold_feet']:
        if not (ROOT/'assetgen_output/items/redesign'/id/'model.gltf').is_file():build_design(id)
        validate_design(id)
    output=ROOT/'bin/items_acceptance/manual-import';output.mkdir(parents=True,exist_ok=True)
    lab=output/'lab.json';presentations=output/'presentations.json'
    for report in [lab,presentations]:reset_report(report,output)
    summary=output/'summary.json';summary.unlink(missing_ok=True)
    executable_hash=hashlib.sha256((ROOT/'bin/Cubatarium.exe').read_bytes()).hexdigest()
    def save_fixture(source,name):
        target=output/name
        if target.resolve().parent!=output.resolve() or target.is_symlink():raise ValueError('Unsafe QA evidence destination')
        if target.exists():shutil.rmtree(target)
        shutil.copytree(source,target)
        return _hashes(target)
    parent=ROOT/'assetgen_output/items/manual-qa';parent.mkdir(parents=True,exist_ok=True)
    runtime=ROOT/'bin/models/items/bucket';reference=runtime/'_itemgen_reference'
    if not runtime.resolve().is_relative_to((ROOT/'bin/models/items').resolve()):raise ValueError('Runtime bucket escapes the owned item directory')
    reference_owned=False
    before=_hashes(runtime)
    with tempfile.TemporaryDirectory(prefix='source-',dir=parent) as temporary:
        temp=Path(temporary);generated=build_design('bucket',output=temp/'generated');project=temp/'project';author=project/'tools/item_designs/bucket/authored'
        author.mkdir(parents=True)
        shutil.copytree(ROOT/'tools/itemgen',project/'tools/itemgen',ignore=shutil.ignore_patterns('__pycache__'))
        for name in ['LICENSE','shaders/item_surface.vert','shaders/item_surface.frag','content/items/bucket.json']:
            target=project/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
        for name in ['model.bin','textures/diffuse.png']:
            target=author/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(generated/name,target)
        g=json.loads((generated/'model.gltf').read_text(encoding='utf-8'));extras=g.pop('extras')
        g['nodes'][0].update(translation=[.2,.3,.4],scale=[2,2,2])
        with Image.open(author/'textures/diffuse.png') as image:
            red,green,blue,alpha=image.convert('RGBA').split()
            Image.merge('RGBA',(red.point(lambda v:min(255,v+25)),green,blue,alpha)).save(author/'textures/second texture.png')
        g['images'].append(dict(uri='textures/second%20texture.png'));g['textures'].append(dict(source=1,sampler=0));g['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']=1
        (author/'model.gltf').write_text(json.dumps(g,indent=2)+'\n',encoding='utf-8')
        profile=extras['presentation']
        for key in ['right_grip','left_grip','carry_right_grip','carry_left_grip']:
            if key in profile:profile[key]=[profile[key][i]*2+[.2,.3,.4][i] for i in range(3)]
        recipe=dict(version=3,id='bucket',triangle_budget=1800,geometry_source=dict(path='authored/model.gltf'),presentation=profile,visual_bindings=extras['visual_bindings'])
        (author.parent/'design.yaml').write_text(yaml.safe_dump(recipe,sort_keys=False),encoding='utf-8')
        provenance=json.loads((ROOT/'tools/item_designs/provenance.json').read_text(encoding='utf-8'));origin=provenance['items']['bucket']
        origin['geometry'].update(method='project-authored manual glTF QA fixture',authoring_format='manual-gltf-2.0',source='tools/item_designs/bucket/authored/model.gltf')
        (project/'tools/item_designs/provenance.json').write_text(json.dumps(dict(version=1,items={'bucket':origin})),encoding='utf-8')
        authored_before=_hashes(author.parent);stage=build_design('bucket',root=project);validate_design('bucket',root=project)
        manual_hashes=save_fixture(stage,'manual-asset')
        author_hashes=save_fixture(author.parent,'author-source')
        shutil.copyfile(project/'tools/item_designs/provenance.json',output/'source-provenance.json')
        shutil.copytree(runtime,temp/'baseline')
        staged=ROOT/'assetgen_output/items/redesign/bucket';hidden=temp/'staged-backup'
        if staged.exists():
            if not staged.resolve().is_relative_to((ROOT/'assetgen_output').resolve()):raise ValueError('QA staging target escapes generated output')
            staged.replace(hidden)
        try:
            if not reference.exists():
                reference.mkdir();reference_owned=True
                shutil.copytree(temp/'baseline',reference,dirs_exist_ok=True)
            reference_hashes=save_fixture(reference,'reference-asset')
            env=os.environ.copy()
            for key in ['CUBA_ITEMS_INVENTORY_QA','CUBA_ITEMS_SURVIVAL_LOOP_QA','CUBA_FLIGHT_QA_FP_ITEM','CUBA_ITEMS_CARRY_VISUAL_QA','CUBA_OWNERSHIP_CATALOG_VISUAL_QA']:env.pop(key,None)
            env.update(PYTHONIOENCODING='utf-8',CUBA_ITEM_LAB_QA=str(lab),CUBA_ITEM_PRESENTATION_QA=str(presentations),CUBA_ITEM_PRESENTATION_IDS='bucket')
            with runtime_designs({'bucket':stage}) as replaced:
                with (output/'native.log').open('w',encoding='utf-8') as log:
                    subprocess.run([sys.executable,'tools/flight_sim_run.py','--world',args.world,'--require-existing-world','--seconds','3','--process-timeout','240','--no-fly','--no-hold-forward','--idle-sec','0','--report',str(output/'flight.json')],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
                lab_data=json.loads(lab.read_text(encoding='utf-8'));draws=json.loads(presentations.read_text(encoding='utf-8'))
                if not lab_data.get('passed') or not draws.get('passed') or not lab_data.get('archived_reference') or lab_data.get('staged_candidate') is not False:raise RuntimeError('Manual import/packaged comparison failed native acceptance')
        finally:
            # Only this fixed QA-owned reference folder may be removed.
            if reference_owned and reference.exists():
                if reference.resolve().parent!=runtime.resolve() or reference.name!='_itemgen_reference':raise ValueError('Unsafe QA reference cleanup path')
                shutil.rmtree(reference)
            if hidden.exists():hidden.replace(staged)
        if _hashes(runtime)!=before or _hashes(author.parent)!=authored_before:raise RuntimeError('QA failed to conserve runtime or author files')
    summary.write_text(json.dumps(dict(passed=True,scope='manual glTF source and packaged Item Lab comparison',world=args.world,restored_runtime_files=replaced,
        lab_checks=len(lab_data['checks']),presentation_checks=len(draws['checks']),manual_images=2,source_files_preserved=True,runtime_restored=True,packaged_candidate_uses_production=True,executable_sha256=executable_hash,manual_asset_sha256=manual_hashes,author_source_sha256=author_hashes,reference_asset_sha256=reference_hashes),indent=2)+'\n',encoding='utf-8')
    print(summary.read_text(encoding='utf-8'))
    return 0


if __name__=='__main__':raise SystemExit(main())
