"""Offline comparison sheets for staged v3 designs; never native acceptance."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import numpy as np
from PIL import Image, ImageDraw
from itemgen.design import build_design, ROOT
from itemgen.raster import load_primitives, render


def presentation_parts(folder):
    parts=load_primitives(folder)
    g=json.loads((folder/'model.gltf').read_text(encoding='utf-8'))
    angles=g.get('extras',{}).get('presentation',{}).get('icon_euler_deg',[0,0,0])
    rotation=np.eye(3)
    for axis,degrees in enumerate(angles):
        c,s=math.cos(math.radians(degrees)),math.sin(math.radians(degrees))
        a,b=(axis+1)%3,(axis+2)%3
        r=np.eye(3);r[a,a]=r[b,b]=c;r[a,b]=-s;r[b,a]=s
        rotation=rotation@r
    for p in parts: p['positions']=p['positions']@rotation.T
    return parts


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--id')
    parser.add_argument('--read-staged',action='store_true',help='Read existing staging without rebuilding; reject changed author sources')
    args=parser.parse_args()
    ids=[args.id] if args.id else sorted(p.parent.name for p in (ROOT/'tools/item_designs').glob('*/design.yaml'))
    out=ROOT/'assetgen_output/items/redesign/previews';out.mkdir(exist_ok=True,parents=True)
    overview=Image.new('RGBA',(1280,220*((len(ids)+3)//4)),(24,24,28,255))
    draw=ImageDraw.Draw(overview)
    for i,id in enumerate(ids):
        if args.read_staged:
            folder=ROOT/'assetgen_output/items/redesign'/id
            from itemgen.design_publication import validate_design
            validate_design(id,staging=folder)
        else:
            folder=build_design(id)
        published=ROOT/'models/items'/id
        reference=published/'_itemgen_reference'
        old=presentation_parts(reference if (reference/'model.gltf').is_file() else published)
        new=presentation_parts(folder)
        sheet=Image.new('RGBA',(960,220),(24,24,28,255)); label=ImageDraw.Draw(sheet)
        modes=['silhouette','gray','color','color','color','color']
        for j,mode in enumerate(modes):
            yaw,pitch=(35,25) if j<3 else ((135,25),(35,65),(0,0))[j-3]
            sheet.paste(render(new,mode=mode,size=160,yaw=yaw,pitch=pitch),(j*160,26))
            label.text((j*160+5,5),mode+f' {yaw}/{pitch}',fill='white')
        label.text((5,200),id+' / offline surface geometry inspection',fill='white')
        sheet.save(out/(id+'.png'))
        x=i%4*320;y=i//4*220
        overview.paste(render(old,size=160,yaw=35,pitch=25),(x,y+25))
        overview.paste(render(new,size=160,yaw=35,pitch=25),(x+160,y+25))
        draw.text((x+5,y+4),id+' / old -> v3',fill='white')
    overview.save(out/'overview.png')
    print(out)


if __name__=='__main__': main()
