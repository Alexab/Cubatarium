"""Read-only authored design sources -> staging. Publishing is a separate step."""
from pathlib import Path
import hashlib
import json
import math
import tempfile
import yaml

from itemgen.mesh import Face, bevel_box, blade, extrude, shell, tube, transform, revolve, ring
from itemgen.surface_export import write_asset
from itemgen.visual_bindings import prepare_bindings
from itemgen.wear_sections import prepare_sections
from itemgen.presentation import prepare_presentation

ROOT=Path(__file__).resolve().parents[2]


def _build_design(item_id, *, root=ROOT, output=None):
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
    if 'geometry_source' in recipe:
        from itemgen.manual import build_manual
        definition=json.loads((root/'content/items'/f'{item_id}.json').read_text(encoding='utf-8'))
        fit=build_manual(item_id,source,destination,recipe,definition)
        sources={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source.rglob('*')) if p.is_file()}
        (destination/'design_build.json').write_text(json.dumps(dict(id=item_id,version=3,triangles=fit['triangles'],sources=sources,physical_fit=fit),indent=2)+'\n',encoding='utf-8')
        return destination
    faces=[]; face_nodes={}; fit_faces=[]
    for part in recipe['parts']:
        kwargs=dict(role=part['material'],name=part['name'])
        kind=part['shape']
        if kind=='bevel_box': geometry=bevel_box(part['size'],part['bevel'],**kwargs)
        elif kind=='extrude': geometry=extrude(part['contour'],part['depth'],**kwargs)
        elif kind=='blade': geometry=blade(part['contour'],part['depth'],part['edge_depth'],part.get('bevel_fraction',.18),edge_role=part.get('edge_material'),**kwargs)
        elif kind=='shell': geometry=shell(part['size'],part['thickness'],**kwargs)
        elif kind=='tube': geometry=tube(part['size'],part['thickness'],**kwargs)
        elif kind=='revolve': geometry=revolve(part['profile'],sides=part.get('sides',8),**kwargs)
        elif kind=='ring': geometry=ring(part['outer_radius'],part['inner_radius'],part['depth'],sides=part.get('sides',8),**kwargs)
        else: raise ValueError(f'Unknown geometry shape: {kind}')
        shaped=transform(geometry,offset=part.get('offset',(0,0,0)),rotation=part.get('rotation',(0,0,0)),pivot=part.get('pivot',(0,0,0)),taper=part.get('taper',1),scale=part.get('scale',(1,1,1)))
        faces.extend(shaped)
        if part.get('fit_reference',True): fit_faces.extend(shaped)
        if part.get('node'):
            for face in geometry: face_nodes[face.name]=part['node']
    if not faces or not fit_faces: raise ValueError('Empty item design')
    definition=json.loads((root/'content/items'/f'{item_id}.json').read_text(encoding='utf-8'))
    # Keep the physical profile authoritative, independently of icon framing.
    low=[min(p[i] for f in fit_faces for p in f.points) for i in range(3)]
    high=[max(p[i] for f in fit_faces for p in f.points) for i in range(3)]
    physical=definition['physical']['size']
    scale=min(physical[i]/(high[i]-low[i]) for i in range(3))
    centre=[(low[i]+high[i])/2 for i in range(3)]
    faces=[Face(tuple(tuple((p[i]-centre[i])*scale for i in range(3)) for p in f.points),f.role,f.name) for f in faces]
    presentation=prepare_presentation(recipe.get('presentation',{}),scale,centre)
    nodes={}
    for name,node in recipe.get('nodes',{}).items():
        pivot=node['pivot']
        if not name or len(pivot)!=3 or not all(math.isfinite(v) for v in pivot): raise ValueError('Invalid authored node pivot')
        nodes[name]=dict(parent=node.get('parent',''),pivot=[(pivot[i]-centre[i])*scale for i in range(3)])
    bindings=prepare_bindings(recipe.get('visual_bindings',[]),nodes,scale,item_id)
    sections=prepare_sections(recipe.get('wear_sections',[]),nodes,scale,centre,definition.get('armor',{}).get('slots',[]))
    gltf=write_asset(destination,item_id,faces,recipe['materials'],size=recipe['atlas_size'],density=recipe['texels_per_metre'],overlays=source/'surfaces',presentation=presentation,nodes=nodes,face_nodes=face_nodes,bindings=bindings,wear_sections=sections)
    triangles=sum(gltf['accessors'][p['indices']]['count']//3 for mesh in gltf['meshes'] for p in mesh['primitives'])
    if triangles>recipe['triangle_budget']: raise ValueError('Item exceeds triangle budget')
    sources={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source.rglob('*')) if p.is_file()}
    (destination/'design_build.json').write_text(json.dumps(dict(id=item_id,version=3,triangles=triangles,sources=sources,physical_fit=dict(scale=scale,centre=centre)),indent=2)+'\n',encoding='utf-8')
    return destination


def build_design(item_id, *, root=ROOT, output=None):
    """Build/validate privately, then replace staging; failed work keeps last output."""
    from itemgen.artifact_contract import validate_model,digest
    from itemgen.filesystem import replace_path
    root=Path(root).resolve()
    if not item_id or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789_' for c in item_id):raise ValueError('Invalid item ID')
    destination=Path(output or root/'assetgen_output/items/redesign'/item_id).resolve()
    if root.is_relative_to(destination) or (destination.is_relative_to(root) and not destination.is_relative_to(root/'assetgen_output')):
        raise ValueError('Staging cannot overwrite author sources or runtime')
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.itemgen-build-',dir=destination.parent) as temp:
        staged=Path(temp)/'next'
        _build_design(item_id,root=root,output=staged)
        recipe=yaml.safe_load((root/'tools/item_designs'/item_id/'design.yaml').read_text(encoding='utf-8'))
        validated=validate_model(staged,budget=recipe['triangle_budget'])
        record=json.loads((staged/'design_build.json').read_text(encoding='utf-8'))
        definition=root/'content/items'/f'{item_id}.json'
        record['definition']={definition.relative_to(root).as_posix():digest(definition)}
        record['generator']={'version':3,'sha256':{p.relative_to(ROOT).as_posix():digest(p) for p in sorted([*(ROOT/'tools/itemgen').glob('*.py'),ROOT/'shaders/item_surface.vert',ROOT/'shaders/item_surface.frag'])}}
        provenance=root/'tools/item_designs/provenance.json'
        if provenance.is_file():
            entry=json.loads(provenance.read_text(encoding='utf-8'))['items'][item_id]
            record['sources'][provenance.relative_to(root).as_posix()]=digest(provenance)
            (staged/'ATTRIBUTION.json').write_text(json.dumps(entry,indent=2)+'\n',encoding='utf-8')
        license=root/'LICENSE'
        if license.is_file():
            record['sources']['LICENSE']=digest(license)
            (staged/'LICENSE.txt').write_bytes(license.read_bytes())
        record['outputs']={p.relative_to(staged).as_posix():digest(p) for p in sorted(staged.rglob('*')) if p.is_file() and p.name!='design_build.json'}
        record['validated_resources']=validated['resources']
        (staged/'design_build.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
        backup=Path(temp)/'previous'
        if destination.exists():replace_path(destination,backup)
        try:replace_path(staged,destination)
        except BaseException:
            if backup.exists():replace_path(backup,destination)
            raise
    return destination
