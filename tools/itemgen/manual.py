"""Read-only limited glTF source import; retain authored topology, UV and materials.

Original buffers are retained verbatim at aligned offsets. Fitted POSITION
accessors are appended separately, so interleaved or shared NORMAL/UV data is
never accidentally rewritten. Named-node translations use the same uniform fit.
"""
from pathlib import Path
import copy
import json
import shutil
import numpy as np
from itemgen.artifact_contract import relative_file,validate_model,digest
from itemgen.resource_uri import resolve_resource
from itemgen.raster import load_primitives
from itemgen.presentation import prepare_presentation
from itemgen.visual_bindings import prepare_bindings
from itemgen.wear_sections import prepare_sections


def build_manual(item_id,source,destination,recipe,definition):
    spec=recipe['geometry_source']
    if not isinstance(spec,dict) or set(spec)-{'path','fit_nodes'}:raise ValueError('Manual geometry_source accepts path and optional fit_nodes')
    path=relative_file(source,spec.get('path'))
    if path.suffix.lower()!='.gltf':raise ValueError('Manual source must be external glTF 2.0, not GLB')
    original=json.loads(path.read_text(encoding='utf-8'));g=copy.deepcopy(original)
    destination.mkdir(parents=True,exist_ok=True)
    # Normalize file names before using the same strict contract as publication.
    buffers=[];base=[];blob=bytearray()
    for row in g['buffers']:
        data=resolve_resource(path.parent,row['uri']).read_bytes()
        if isinstance(row['byteLength'],bool) or not isinstance(row['byteLength'],int) or len(data)!=row['byteLength']:raise ValueError('Manual buffer byteLength mismatch')
        while len(blob)%4:blob.append(0)
        base.append(len(blob));buffers.append(data);blob.extend(data)
    for view in g['bufferViews']:
        index=view['buffer']
        if isinstance(index,bool) or not isinstance(index,int) or not 0<=index<len(base):raise ValueError('Invalid manual buffer index')
        offset=view.get('byteOffset',0)
        if isinstance(offset,bool) or not isinstance(offset,int) or offset<0:raise ValueError('Invalid manual buffer offset')
        view['byteOffset']=offset+base[index];view['buffer']=0
    g['buffers']=[dict(uri='model.bin',byteLength=len(blob))]
    for index,image in enumerate(g['images']):
        if 'uri' not in image:raise ValueError('Manual images must be external PNGs')
        old=resolve_resource(path.parent,image['uri'])
        if old.suffix.lower()!='.png':raise ValueError('Manual albedo must be PNG')
        name='textures/diffuse.png' if len(g['images'])==1 else f'textures/albedo_{index}.png'
        target=destination/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(old,target);image['uri']=name
    (destination/'model.bin').write_bytes(blob)
    model=destination/'model.gltf';model.write_text(json.dumps(g,indent=2)+'\n',encoding='utf-8')
    inspected=validate_model(destination,budget=recipe['triangle_budget'])
    parts=load_primitives(destination)
    selected=spec.get('fit_nodes')
    if selected is not None:
        if not isinstance(selected,list) or not selected or len(set(selected))!=len(selected) or any(not isinstance(n,str) or n not in inspected['nodes'] for n in selected):raise ValueError('Invalid manual fit_nodes')
        nodes=g['nodes'];allowed=set()
        def include(i):
            allowed.add(nodes[i]['name'])
            for child in nodes[i].get('children',[]):include(child)
        for i,node in enumerate(nodes):
            if node['name'] in selected:include(i)
        parts=[p for p in parts if p['node_name'] in allowed]
    if not parts:raise ValueError('Manual fit selection has no geometry')
    points=np.concatenate([p['positions'] for p in parts]);low=points.min(axis=0);high=points.max(axis=0);extent=high-low
    size=np.asarray(definition['physical']['size'],dtype=float)
    if size.shape!=(3,) or not np.isfinite(size).all() or np.any(size<=0):raise ValueError('Invalid gameplay physical size')
    scale=float(min(size[i]/extent[i] for i in range(3) if extent[i]>1e-9));centre=(low+high)*.5
    positions={}
    for mesh in g['meshes']:
        for primitive in mesh['primitives']:
            old=primitive['attributes']['POSITION']
            if old not in positions:
                a=g['accessors'][old];view=g['bufferViews'][a['bufferView']]
                values=np.ndarray((a['count'],3),dtype='<f4',buffer=blob,offset=view.get('byteOffset',0)+a.get('byteOffset',0),strides=(view.get('byteStride',12),4)).copy()*scale
                while len(blob)%4:blob.append(0)
                start=len(blob);blob.extend(values.astype('<f4').tobytes())
                new_view=len(g['bufferViews']);g['bufferViews'].append(dict(buffer=0,byteOffset=start,byteLength=len(blob)-start))
                new_a=dict(bufferView=new_view,componentType=5126,count=len(values),type='VEC3',min=values.min(axis=0).tolist(),max=values.max(axis=0).tolist())
                positions[old]=len(g['accessors']);g['accessors'].append(new_a)
            primitive['attributes']['POSITION']=positions[old]
    for node in g['nodes']:
        if 'matrix' in node:
            node['matrix'][12:15]=[v*scale for v in node['matrix'][12:15]]
        elif 'translation' in node:node['translation']=[v*scale for v in node['translation']]
    roots=g['scenes'][g.get('scene',0)]['nodes']
    matching=[i for i,n in enumerate(g['nodes']) if n['name']==item_id]
    if matching:
        if roots!=matching:raise ValueError('Item ID node must be the single scene root; rename conflicting source nodes')
        root=g['nodes'][matching[0]]
        if 'matrix' in root:root['matrix'][12:15]=[root['matrix'][12+i]-centre[i]*scale for i in range(3)]
        else:root['translation']=[root.get('translation',[0,0,0])[i]-centre[i]*scale for i in range(3)]
    else:
        root=len(g['nodes']);g['nodes'].append(dict(name=item_id,translation=(-centre*scale).tolist(),children=roots))
        g['scenes']=[dict(nodes=[root])];g['scene']=0
    parents={child:node['name'] for node in g['nodes'] for child in node.get('children',[])}
    nodes={node['name']:{'parent':parents.get(i,'') if parents.get(i,'')!=item_id else ''} for i,node in enumerate(g['nodes']) if node['name']!=item_id}
    g['asset']['generator']='cubatarium-itemgen-v3-manual'
    g['extras']=dict(authoring_source='cubatarium-manual-design',design_version=3,
        presentation=prepare_presentation(recipe.get('presentation',{}),scale,centre),
        visual_bindings=prepare_bindings(recipe.get('visual_bindings',[]),nodes,scale,item_id),
        wear_sections=prepare_sections(recipe.get('wear_sections',[]),nodes,scale,centre,definition.get('armor',{}).get('slots',[])))
    g['buffers'][0]['byteLength']=len(blob)
    (destination/'model.bin').write_bytes(blob);model.write_text(json.dumps(g,indent=2)+'\n',encoding='utf-8')
    result=validate_model(destination,budget=recipe['triangle_budget'])
    return dict(scale=scale,centre=centre.tolist(),source=path.relative_to(source).as_posix(),source_gltf_sha256=digest(path),triangles=result['triangles'])
