"""Verify native attachment evidence and compose review sheets; never grants art approval."""
import argparse
from datetime import datetime
import hashlib
import itertools
import json
import struct
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--label',default='equipment_catalog')
    parser.add_argument('--native-dir',default='native_complete')
    parser.add_argument('--reference-label',default='anchors_catalog')
    parser.add_argument('--reference-dir',default='native_anchors')
    parser.add_argument('--skip-reference',action='store_true',help='Validate one new material capture without a restart comparison')
    args=parser.parse_args();out=args.out.resolve()
    context=json.loads((out/f'context_{args.label}.json').read_text())
    reference=None if args.skip_reference else json.loads((out/f'context_{args.reference_label}.json').read_text())
    assert context['native_equipment_matrix'] and context['equipment_files_sha256']
    if reference:
        assert context['exe_sha256']==reference['exe_sha256']
        assert context['shaders_sha256']==reference['shaders_sha256']
        assert context['files_sha256']==reference['files_sha256']
        assert context['equipment_files_sha256']==reference['equipment_files_sha256']
    for name,digest in context['files_sha256'].items():
        assert hashlib.sha256((Path(context['models_root'])/name).read_bytes()).hexdigest()==digest
    runtime=Path(__file__).resolve().parents[2]/'bin'
    assert hashlib.sha256((runtime/'Cubatarium.exe').read_bytes()).hexdigest()==context['exe_sha256']
    for name,digest in context['equipment_files_sha256'].items():
        assert hashlib.sha256((runtime/name).read_bytes()).hexdigest()==digest,name
    folder=out/args.native_dir/'equipment';before=out/args.reference_dir/'equipment'
    manifest=json.loads((folder/'manifest.json').read_text())
    texture_probe=None
    if context.get('native_equipment_texture_probe'):
        probe_path=folder/'texture_model_isolation.json'
        assert probe_path.stat().st_mtime>=datetime.fromisoformat(context['captured_at_utc']).timestamp()
        texture_probe=json.loads(probe_path.read_text())
        assert {r['item'] for r in texture_probe['models']}==set(
            m+'_'+s for m in ('copper','iron','leather') for s in ('chest','arms','legs'))
        assert len(texture_probe['models'])==9
        f32=lambda x:struct.unpack('<f',struct.pack('<f',x))[0]
        for model in texture_probe['models']:
            source=json.loads((runtime/'models/items'/model['item']/'model.gltf').read_text())
            index=source['meshes'][0]['primitives'][0].get('material',0)
            factor=source['materials'][index]['pbrMetallicRoughness']['baseColorFactor']
            expected=[int(f32(f32(c)*255.)) for c in (factor+[1.]*4)[:4]]
            assert model['source_rgba']==model['expected_source_rgba']==expected
            assert model['source_texture']>0 and model['worn_texture']>0
            assert model['source_texture']!=model['worn_texture'] and model['isolated_and_repeatable']
    if reference:assert manifest==json.loads((before/'manifest.json').read_text()),'Restart anchor/pose data differ'
    material=manifest.get('material','copper')
    assert material==context.get('equipment_material','copper')
    expected_ids={'human','bot','zombie','skeleton','human_adventurer','human_guard'}
    assert {r['id'] for r in manifest['resources']}==expected_ids
    expected_cases=set(itertools.product(('Walk','Run'),(.22,1.,1.5),range(17),range(4)))
    armor_bones=set()
    for iid in (material+'_'+slot for slot in ('head','chest','arms','hands','legs','feet')):
        armor_bones.update(json.loads((runtime/'models/items'/iid/'wear.json').read_text())['bones'])
    sheets=out/'sheets';sheets.mkdir(exist_ok=True)
    rows=[];count=0;clipped=[]
    for resource in manifest['resources']:
        frames=resource['frames']
        assert len(frames)==408
        assert {(f['state'],round(f['speed_ratio'],2),f['phase_index'],f['loadout']) for f in frames}==expected_cases
        positive=0;max_error=0.;frame_rows=[];rest_bounds={}
        for f in frames:
            expected=set(armor_bones) if f['loadout'] in (0,3) else set()
            if f['loadout'] in (1,3):expected.add('rightItem')
            if f['loadout'] in (2,3):expected.add('leftItem')
            assert set(f['anchors'])==expected,(resource['id'],expected,set(f['anchors']))
            if manifest.get('alpha_coverage'):
                assert set(f['fit_bounds'])==expected
                for name,bounds in f['fit_bounds'].items():
                    assert all(a<b for a,b in zip(bounds['low'],bounds['high']))
                    if name in rest_bounds:assert rest_bounds[name]==bounds
                    else:rest_bounds[name]=bounds
            assert all(len(m)==16 and all(abs(value)<1e6 for value in m) for m in f['anchors'].values())
            assert f['max_anchor_matrix_error']<=5e-6
            assert 0<=f['root_lift']<.1
            positive+=f['root_lift']>1e-4;max_error=max(max_error,f['max_anchor_matrix_error'])
            inputs=[(context,folder/f['png'])]
            if reference:inputs.append((reference,before/f['png']))
            for ctx,path in inputs:
                assert path.stat().st_mtime>=datetime.fromisoformat(ctx['captured_at_utc']).timestamp()
            if reference:assert (folder/f['png']).read_bytes()==(before/f['png']).read_bytes(),f['png']
            with Image.open(folder/f['png']) as im:
                im.load();assert im.size==(256,256)
                if manifest.get('alpha_coverage'):
                    bbox=im.getchannel('A').getbbox();assert bbox is not None
                    if min(bbox[:2])==0 or max(bbox[2:])==256:clipped.append(f['png'])
            frame_rows.append({**f,'sha256':hashlib.sha256((folder/f['png']).read_bytes()).hexdigest()})
        assert positive>0
        view_frames=[];view_sheet_records=[]
        if 'supplemental_view_yaws' in manifest:
            views=resource['views']
            assert {(v['phase_index'],v['loadout'],v['yaw']) for v in views}==set(
                itertools.product((2,10,16),range(4),range(0,360,45)))
            assert len(views)==96
            for v in views:
                assert v['state']=='Run' and abs(v['speed_ratio']-1.5)<1e-5
                path=folder/v['png']
                assert path.stat().st_mtime>=datetime.fromisoformat(context['captured_at_utc']).timestamp()
                if reference:assert path.read_bytes()==(before/v['png']).read_bytes(),v['png']
                with Image.open(path) as im:
                    im.load();assert im.size==(256,256)
                    bbox=im.getchannel('A').getbbox();assert bbox is not None
                    if min(bbox[:2])==0 or max(bbox[2:])==256:clipped.append(v['png'])
                view_frames.append({**v,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
            for loadout,label in enumerate(('armor','main','off','all')):
                sheet=Image.new('RGB',(8*192,3*216),(31,35,40));draw=ImageDraw.Draw(sheet)
                for v in views:
                    if v['loadout']!=loadout:continue
                    x=v['yaw']//45*192;y=(2,10,16).index(v['phase_index'])*216
                    with Image.open(folder/v['png']) as im:
                        sheet.paste(im.convert('RGB').resize((192,192),Image.Resampling.NEAREST),(x,y+24))
                    draw.text((x+2,y+3),f"yaw {v['yaw']} / p={v['phase']:.3f}",fill='white')
                name=f"{resource['id']}_views_{label}.png";sheet.save(sheets/name)
                view_sheet_records.append({'path':'sheets/'+name,'sha256':hashlib.sha256((sheets/name).read_bytes()).hexdigest()})
        sheet_records=[]
        detail=Image.new('RGB',(1024,6*280),(31,35,40));detail_draw=ImageDraw.Draw(detail)
        for group,(state,ratio) in enumerate(itertools.product(('Walk','Run'),(.22,1.,1.5))):
            sheet=Image.new('RGB',(17*128,4*152),(31,35,40));draw=ImageDraw.Draw(sheet)
            for f in frames:
                if f['state']!=state or round(f['speed_ratio'],2)!=ratio:continue
                with Image.open(folder/f['png']) as im:
                    sheet.paste(im.convert('RGB').resize((128,128),Image.Resampling.NEAREST),(f['phase_index']*128,f['loadout']*152+24))
                    if f['worst_clearance_phase']:detail.paste(im.convert('RGB'),(f['loadout']*256,group*280+24))
                loadout=('armor','main','off','all')[f['loadout']]
                draw.text((f['phase_index']*128+2,f['loadout']*152+2),f"{loadout} p={f['phase']:.3f}",fill='white')
                if f['worst_clearance_phase']:
                    detail_draw.text((f['loadout']*256+2,group*280+3),f"{state} {ratio} / {loadout} lift={f['root_lift']:.4f}",fill='white')
            name=f"{resource['id']}_{state}_{ratio}.png";sheet.save(sheets/name)
            sheet_records.append({'path':'sheets/'+name,'sha256':hashlib.sha256((sheets/name).read_bytes()).hexdigest()})
        name=f"{resource['id']}_deepest_clearance.png";detail.save(sheets/name)
        sheet_records.append({'path':'sheets/'+name,'sha256':hashlib.sha256((sheets/name).read_bytes()).hexdigest()})
        rows.append({'id':resource['id'],'frames':frame_rows,'positive_clearance_pngs':positive,
                     'max_anchor_matrix_error':max_error,'sheets':sheet_records,
                     'views':view_frames,'view_sheets':view_sheet_records});count+=len(frames)
    report={'schema':'creature-native-equipment-audit.v1','scope':'Preview anchor/image integrity; no FP/save/route or final R6 acceptance',
            'resource_count':len(rows),'png_count':count,'current_inputs_and_executable_verified':True,
            'supplemental_png_count':sum(len(r['views']) for r in rows),
            'material':material,'alpha_coverage':manifest.get('alpha_coverage',False),'border_clipping':clipped,
            'texture_model_isolation':texture_probe,
            'restart_exact_png_count':count if reference else None,'same_restart_anchor_manifest':bool(reference),
            'restart_exact_supplemental_png_count':sum(len(r['views']) for r in rows) if reference else None,
            'reference_world_spawn':reference['world_species'] if reference else None,
            'current_world_species':context['world_species'],'resources':rows}
    (out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('resources','texture_model_isolation')},indent=2))


if __name__=='__main__':main()
