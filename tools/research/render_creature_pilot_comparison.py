"""Reproducible baseline / pre-correction / candidate pilot sheets."""
from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from review_creature_redesign import ROOT, faces_for, panel, extent, font


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species",default="cow,badger,rigid_demo_walker")
    parser.add_argument("--qa-models",type=Path,default=ROOT/"assetgen_output/creatures/pilots_v2/models")
    parser.add_argument("--pre-models",type=Path,default=ROOT/"assetgen_output/creatures/pre_fidelity/models")
    parser.add_argument("--out",type=Path,default=ROOT/"docs/research/creature-fidelity-pilots-v2")
    parser.add_argument("--native-dir",default="native_approved")
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    base=ROOT/"assetgen_output/creatures/review_2026_11_10/baseline/models/creatures"
    html='<meta charset="utf-8"><title>Fidelity pilots</title><style>body{font-family:sans-serif;background:#ddd}img{max-width:100%}</style><h1>Baseline / pre-correction / candidate</h1><p>Equal world scale per species; offline geometry is diagnostic. Native sheets below use the real engine renderer and controllers.</p>'
    for sid in args.species.split(','):
        before=args.pre_models/"creatures" if (args.pre_models/"creatures"/sid).is_dir() else ROOT/"models/creatures"
        versions=[faces_for(p/sid) for p in [base,before,args.qa_models/"creatures"]]
        span=max(np.linalg.norm(extent(f)[1]-extent(f)[0]) for f in versions)
        sheet=Image.new('RGB',(1080,660),(248,249,250));draw=ImageDraw.Draw(sheet)
        for row,(label,faces) in enumerate(zip(['baseline b35f00a75','published before correction','fidelity candidate v2'],versions)):
            draw.text((8,row*220+2),sid+' / '+label,font=font(),fill=(25,29,30))
            for col,(yaw,pitch,sil) in enumerate([(35,-15,False),(90,0,False),(180,0,False),(270,0,False),(0,-90,False),(35,-15,True)]):
                sheet.paste(panel(faces,yaw,pitch,span=span,silhouette=sil),(col*180,row*220+24))
        path=args.out/(sid+'.png');sheet.save(path)
        html+=f'<h2>{sid}</h2><img src="{path.name}">'
        native=args.out/args.native_dir
        if (native/f'{sid}_28.png').is_file():
            native_sheet=Image.new('RGB',(1024,1912),(242,242,239));draw=ImageDraw.Draw(native_sheet)
            groups=[('rest',list(range(4))),('locomotion',list(range(4,12))),('run',list(range(13,21))),('action',list(range(21,29)))]
            row=0
            for label,indices in groups:
                for k,index in enumerate(indices):
                    x=(k%4)*256;y=(row+k//4)*272
                    with Image.open(native/f'{sid}_{index}.png') as im:native_sheet.paste(im.convert('RGB'),(x,y+22))
                    caption=sid if label=='rest' and k==0 else f'{label} / frame {k}'
                    draw.text((x+4,y+2),caption,fill=(20,20,20),font=font())
                row+=(len(indices)+3)//4
                if label!='rest':
                    frames=[Image.open(native/f'{sid}_{i}.png').convert('RGB') for i in indices]
                    frames[0].save(args.out/f'{sid}_{label}.gif',save_all=True,append_images=frames[1:],duration=180 if label=='action' else 125,loop=0)
            native_path=args.out/f'native_approved_{sid}.png';native_sheet.save(native_path)
            html+=f'<h3>Native frames</h3><img src="{native_path.name}">'
        death_path=args.out/(sid+'_die_asset.png')
        if death_path.is_file():
            html+=f'<h3>Preserved die asset — sampled skinning, not a runtime Death state</h3><img src="{death_path.name}">'
        for mode in ('swim','glide'):
            source=args.out/('native_'+mode)
            if not (source/f'{sid}_11.png').is_file():continue
            supplement=Image.new('RGB',(1024,544),(242,242,239));draw=ImageDraw.Draw(supplement)
            for phase in range(8):
                x=phase%4*256;y=phase//4*272
                with Image.open(source/f'{sid}_{phase+4}.png') as im:supplement.paste(im.convert('RGB'),(x,y+22))
                draw.text((x+4,y+2),f'{sid} / {mode} / {phase}',font=font(),fill=(20,20,20))
            name=f'native_{mode}_{sid}.png';supplement.save(args.out/name)
            html+=f'<h3>Native {mode} phases</h3><img src="{name}">'
    (args.out/'index.html').write_text(html,encoding='utf-8')


if __name__=='__main__':main()
