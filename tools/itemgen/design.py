"""Read-only authored design sources -> staging. Publishing is a separate step."""
from pathlib import Path
import hashlib
import json
import yaml

from itemgen.mesh import Face, bevel_box, extrude, shell, transform
from itemgen.surface_export import write_asset

ROOT=Path(__file__).resolve().parents[2]


def build_design(item_id, *, root=ROOT, output=None):
    if not item_id or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789_' for c in item_id):
        raise ValueError('Invalid item ID')
    source=root/'tools/item_designs'/item_id
    recipe=yaml.safe_load((source/'design.yaml').read_text(encoding='utf-8'))
    if recipe.get('version')!=3 or recipe.get('id')!=item_id:
        raise ValueError('Expected version 3 design for this ID')
    if not (root/'content/items'/f'{item_id}.json').is_file():
        raise ValueError('Design must refer to an existing gameplay item')
    destination=output or root/'assetgen_output/items/redesign'/item_id
    if any(destination.resolve().is_relative_to((root/name).resolve()) for name in ('tools','content','models','shaders','docs')):
        raise ValueError('Staging cannot overwrite author sources')
    faces=[]
    for part in recipe['parts']:
        kwargs=dict(role=part['material'],name=part['name'])
        kind=part['shape']
        if kind=='bevel_box': geometry=bevel_box(part['size'],part['bevel'],**kwargs)
        elif kind=='extrude': geometry=extrude(part['contour'],part['depth'],**kwargs)
        elif kind=='shell': geometry=shell(part['size'],part['thickness'],**kwargs)
        else: raise ValueError(f'Unknown geometry shape: {kind}')
        faces.extend(transform(geometry,offset=part.get('offset',(0,0,0)),rotation=part.get('rotation',(0,0,0)),pivot=part.get('pivot',(0,0,0)),taper=part.get('taper',1)))
    if not faces: raise ValueError('Empty item design')
    definition=json.loads((root/'content/items'/f'{item_id}.json').read_text(encoding='utf-8'))
    # Keep the physical profile authoritative, independently of icon framing.
    low=[min(p[i] for f in faces for p in f.points) for i in range(3)]
    high=[max(p[i] for f in faces for p in f.points) for i in range(3)]
    physical=definition['physical']['size']
    scale=min(physical[i]/(high[i]-low[i]) for i in range(3))
    centre=[(low[i]+high[i])/2 for i in range(3)]
    faces=[Face(tuple(tuple((p[i]-centre[i])*scale for i in range(3)) for p in f.points),f.role,f.name) for f in faces]
    gltf=write_asset(destination,item_id,faces,recipe['materials'],size=recipe['atlas_size'],density=recipe['texels_per_metre'],overlays=source/'surfaces')
    triangles=sum(gltf['accessors'][p['indices']]['count']//3 for p in gltf['meshes'][0]['primitives'])
    if triangles>recipe['triangle_budget']: raise ValueError('Item exceeds triangle budget')
    sources={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source.rglob('*')) if p.is_file()}
    (destination/'design_build.json').write_text(json.dumps(dict(id=item_id,version=3,triangles=triangles,sources=sources),indent=2)+'\n',encoding='utf-8')
    return destination
