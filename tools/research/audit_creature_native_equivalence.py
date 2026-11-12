"""Exact current-renderer equivalence, not a complete visual acceptance matrix."""
from pathlib import Path
import argparse
import hashlib
import json
from datetime import datetime

from PIL import Image, ImageDraw


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--reference',default='reference_catalog')
    p.add_argument('--candidate',default='candidate_catalog')
    p.add_argument('--reference-dir',default='native_reference')
    p.add_argument('--candidate-dir',default='native_candidate')
    a=p.parse_args();out=a.out.resolve()
    contexts=[json.loads((out/f'context_{label}.json').read_text(encoding='utf8'))
        for label in (a.reference,a.candidate)]
    assert contexts[0]['exe_sha256']==contexts[1]['exe_sha256']
    assert contexts[0]['shaders_sha256']==contexts[1]['shaders_sha256']
    assert contexts[0]['preview_species']==contexts[1]['preview_species']
    ids=contexts[0]['preview_species'];rows=[]
    for context,folder in zip(contexts,(a.reference_dir,a.candidate_dir)):
        started=datetime.fromisoformat(context['captured_at_utc']).timestamp()
        for sid in ids:
            for frame in range(35 if sid=='human' else 29):
                path=out/folder/f'{sid}_{frame}.png'
                assert path.stat().st_mtime>=started,f'Stale capture: {path}'
        # Check current candidate payload, independently from the captured PNGs.
        # The reference may legitimately have been replaced after publication.
        if folder==a.candidate_dir:
            for relative,digest in context['files_sha256'].items():
                path=Path(context['models_root'])/relative
                assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,str(path)
    for sid in ids:
        for frame in range(35 if sid=='human' else 29):
            name=f'{sid}_{frame}.png'
            with Image.open(out/a.reference_dir/name) as ref,Image.open(out/a.candidate_dir/name) as new:
                assert ref.size==new.size and ref.convert('RGBA').tobytes()==new.convert('RGBA').tobytes(),name
            rows.append({'frame':name,'sha256':hashlib.sha256((out/a.candidate_dir/name).read_bytes()).hexdigest()})
    canvas=Image.new('RGB',(7*208,((len(ids)+6)//7)*232),(28,32,37));draw=ImageDraw.Draw(canvas)
    for i,sid in enumerate(ids):
        with Image.open(out/a.candidate_dir/f'{sid}_0.png') as im:
            tile=im.convert('RGBA');tile.thumbnail((200,200))
            canvas.paste(tile,(i%7*208+4,i//7*232+2),tile)
        draw.text((i%7*208+5,i//7*232+208),sid,fill=(240,240,240))
    canvas.save(out/f'{a.candidate}_overview.png')
    report={'schema':'creature-native-equivalence.v1','species_count':len(ids),'exact_rgba_frames':len(rows),
        'reference':a.reference,'candidate':a.candidate,'same_executable_and_shaders':True,
        'fresh_images_and_current_candidate_payload':True,'world_species':contexts[1]['world_species'],
        'scope':'native preview equivalence only; not full pose/habitat/equipment/performance acceptance','frames':rows}
    (out/f'{a.candidate}_parity.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(f'EXACT {len(rows)}/{len(rows)}; {len(ids)} species')


if __name__=='__main__':main()
