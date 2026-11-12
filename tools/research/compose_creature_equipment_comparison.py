"""Compose native before/after equipment frames; does not grant art acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--candidate-root',default='shell')
    parser.add_argument('--native-dir',default='native_views')
    args=parser.parse_args();out=args.out.resolve()
    overview=Image.new('RGB',(1536,560),(31,35,40));draw=ImageDraw.Draw(overview)
    inputs=[]
    for column,material in enumerate(('copper','iron','leather')):
        for side,(folder,label) in enumerate((
                (out/material/'native_candidate/equipment','previous affine fit'),
                (out/args.candidate_root/material/args.native_dir/'equipment','current hollow armor'))):
            manifest=json.loads((folder/'manifest.json').read_text())
            for row,resource_id in enumerate(('human','skeleton')):
                resource=next(r for r in manifest['resources'] if r['id']==resource_id)
                frame=next(f for f in resource['frames'] if f['state']=='Run' and
                           abs(f['speed_ratio']-1.5)<.001 and f['worst_clearance_phase'] and f['loadout']==3)
                path=folder/frame['png'];x=(column*2+side)*256;y=row*280
                with Image.open(path) as im:overview.paste(im.convert('RGB'),(x,y+24))
                draw.text((x+3,y+3),f'{material} / {resource_id} / {label}',fill='white')
                inputs.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    overview.save(out/'comparison.png')
    (out/'comparison_inputs.json').write_text(json.dumps({'schema':'creature-equipment-comparison.v1',
        'images':inputs,'output_sha256':hashlib.sha256((out/'comparison.png').read_bytes()).hexdigest()},indent=2)+'\n')


if __name__=='__main__':main()
