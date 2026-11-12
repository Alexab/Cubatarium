"""Verify real native matrix coverage and organize readable inspection sheets.

Never grants artistic acceptance. Empty projections, clipping and pose aliases
are reported for a reviewer instead of being hidden by an aggregate PASS.
"""
from pathlib import Path
import argparse
import hashlib
import json
from datetime import datetime

from PIL import Image, ImageDraw, ImageChops


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--label',required=True)
    parser.add_argument('--native-dir',required=True)
    parser.add_argument('--baseline-dir')
    parser.add_argument('--baseline-label')
    args=parser.parse_args();out=args.out.resolve();folder=out/args.native_dir/'matrix'
    context=json.loads((out/f'context_{args.label}.json').read_text(encoding='utf8'))
    if args.baseline_dir:
        assert args.baseline_label,'Comparisons require a baseline context'
        reference=json.loads((out/f'context_{args.baseline_label}.json').read_text(encoding='utf8'))
        assert reference['exe_sha256']==context['exe_sha256']
        assert reference['shaders_sha256']==context['shaders_sha256']
        assert reference['preview_species']==context['preview_species']
    assert context['native_matrix']
    started=datetime.fromisoformat(context['captured_at_utc']).timestamp()
    manifest=json.loads((folder/'manifest.json').read_text(encoding='utf8'))
    expected=set(context['preview_species'])
    root=Path(context['models_root'])
    for path in (root/'skins').glob('*/skin.json'):
        skin=json.loads(path.read_text(encoding='utf-8-sig'))
        if skin.get('creature',skin.get('creature_id')) in expected and skin.get('catalog',{}).get('equippable',True):expected.add(path.parent.name)
    ids=[r['id'] for r in manifest['resources']]
    # Engine-resolved primary skin ownership is also checked below via the
    # resource's species and actual current skin definition.
    for r in manifest['resources']:
        if r['skin']:
            skin=json.loads((root/'skins'/r['skin']/'skin.json').read_text(encoding='utf-8-sig'))
            owner=skin.get('creature',skin.get('creature_id'))
            assert owner==r['species'],(r['id'],owner)
    assert set(ids)==expected and len(ids)==len(expected),(set(ids)^expected)
    for relative,digest in context['files_sha256'].items():
        assert hashlib.sha256((root/relative).read_bytes()).hexdigest()==digest,relative
    sheets=out/f'sheets_{args.label}';sheets.mkdir(exist_ok=True)
    rows=[];empty=[];clipped=[];aliases=[];unresolved=[]
    for resource in manifest['resources']:
        rid=resource['id'];frames=resource['frames']
        if resource['backend']=='gltf_skeleton' and (not resource.get('primary_mapping_explicit',False) or not resource.get('primary_clip_present',False)):
            unresolved.append({'id':rid,'mapping_explicit':resource.get('primary_mapping_explicit'),
                'runtime_clip':resource['primary_clip'],'clip_present':resource.get('primary_clip_present')})
        assert len(frames)==30 and {(f['pose'],f['view']) for f in frames}=={(p,v) for p in range(3) for v in range(10)}
        baseline=(out/args.baseline_dir/'matrix') if args.baseline_dir else None
        per_pose=4 if baseline else 2
        sheet=Image.new('RGB',(10*160,3*per_pose*180+30),(35,39,44));draw=ImageDraw.Draw(sheet)
        draw.text((5,5),rid+' / '+resource['backend']+' / rest, '+resource['primary_state']+' 1/8 and 5/8',fill='white')
        frame_rows=[]
        for f in frames:
            pose,view=f['pose'],f['view']
            assert f['phase']==[0,.125,.625][pose]
            assert f['yaw_degrees']==(view*45 if view<8 else 0)
            assert f['pitch_degrees']==(15 if view<8 else 90 if view==8 else -90)
            paths=[folder/f['color'],folder/f['silhouette']]
            for path in paths:assert path.stat().st_mtime>=started,path
            with Image.open(paths[0]) as source,Image.open(paths[1]) as silhouette:
                color=source.convert('RGBA');sil=silhouette.convert('RGBA');alpha=color.getchannel('A')
                assert color.size==sil.size==(256,256)
                mask=alpha.point(lambda v:255 if v else 0)
                expected_sil=ImageChops.invert(mask)
                assert sil.getchannel('R').tobytes()==expected_sil.tobytes()
                assert all(sil.getchannel(c).tobytes()==expected_sil.tobytes() for c in ('G','B'))
                assert sil.getchannel('A').getextrema()==(255,255)
                bbox=alpha.getbbox()
                if bbox is None:empty.append({'id':rid,'pose':pose,'view':view})
                elif min(bbox[:2])==0 or max(bbox[2:])==256:clipped.append({'id':rid,'pose':pose,'view':view,'bbox':bbox})
                frame_rows.append({**f,'alpha_bbox':bbox,'color_sha256':hashlib.sha256(paths[0].read_bytes()).hexdigest(),
                    'silhouette_sha256':hashlib.sha256(paths[1].read_bytes()).hexdigest()})
            display=[]
            if baseline:display.append(('original color',baseline/f['color']))
            display.append(('current color',paths[0]))
            if baseline:display.append(('original silhouette',baseline/f['silhouette']))
            display.append(('current silhouette',paths[1]))
            for index,(title,path) in enumerate(display):
                tile=Image.new('RGBA',(256,256),(58,62,67,255))
                with Image.open(path) as image:tile.alpha_composite(image.convert('RGBA'))
                tile=tile.convert('RGB').resize((156,156),Image.Resampling.NEAREST)
                x=view*160;y=30+(pose*per_pose+index)*180
                sheet.paste(tile,(x+2,y))
                draw.text((x+2,y+157),title+' '+(str(view*45) if view<8 else 'top' if view==8 else 'bottom'),fill='white')
        for view in range(8):
            with Image.open(folder/rid/f'pose_1_view_{view}_color.png') as a,Image.open(folder/rid/f'pose_2_view_{view}_color.png') as b:
                if a.tobytes()==b.tobytes():aliases.append({'id':rid,'view':view})
        sheet.save(sheets/f'{rid}.png')
        rows.append({'id':rid,'backend':resource['backend'],'skin':resource['skin'],'frames':frame_rows})
    report={'schema':'creature-native-matrix-audit.v1','scope':'image/source integrity and coverage diagnostics only; human visual review still required',
        'resource_count':len(ids),'native_render_count':len(ids)*30,'png_count':len(ids)*60,
        'alpha_coverage_silhouettes_verified':True,'current_inputs_verified':True,
        'empty_projections':empty,'border_clipping':clipped,'identical_opposite_phase_views':aliases,'resources':rows}
    report['runtime_primary_fallbacks']=unresolved
    report['baseline_label']=args.baseline_label
    (out/f'audit_{args.label}.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k!='resources'},indent=2))


if __name__=='__main__':main()
