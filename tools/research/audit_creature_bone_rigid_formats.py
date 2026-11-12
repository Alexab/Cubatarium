"""Independent published B/R/skin checks; imports no generator/validator code.

Checks Cubatarium's active paths and the declared legacy box-UV geometry
layout. This is structural evidence, never a visual or motion acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math

from PIL import Image

ROOT=Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def vec(value,n=3,positive=False):
    return isinstance(value,list) and len(value)==n and all(type(x) in (int,float) and math.isfinite(x) and (not positive or x>0) for x in value)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--models',type=Path,default=ROOT/'models')
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args();models=args.models.resolve();rows=[];bases={}
    def audit(sid,folder,kind):
        errors=[];inputs={}
        def require(ok,message):
            if not ok:errors.append(message)
        def load(relative):
            path=(folder/relative).resolve()
            if not path.is_relative_to(folder.resolve()):raise ValueError('Input escapes resource: '+relative)
            data=path.read_bytes();inputs[relative]=hashlib.sha256(data).hexdigest()
            return path
        def png(stem,size=None):
            require(isinstance(stem,str) and Path(stem).name==stem,'unsafe texture stem')
            with Image.open(load('textures/'+stem+'.png')) as im:
                im.load();require(im.format=='PNG','not a PNG: '+stem)
                require(im.convert('RGBA').getchannel('A').getbbox() is not None,'fully transparent texture: '+stem)
                if size:require(im.size==tuple(size),'texture dimensions mismatch: '+stem)
                return list(im.size)
        try:
            definition=read(load('skin.json' if kind=='skin' else 'creature.json'))
            require(definition.get('id')==sid,'definition id mismatch')
            visual=definition.get('visual',{})
            backend='skin' if kind=='skin' else visual.get('backend')
            row={'id':sid,'kind':kind,'backend':backend}
            if kind=='skin':
                base=definition['creature_id'];require(base in bases,'missing B base geometry')
                row['base_species']=base
                stems=set(visual.get('texture_map',{}).values())|{visual.get('texture','diffuse'),'diffuse'}
                for stem in sorted(stems):png(stem,bases.get(base,{}).get('texture_size'))
                require(all(visual.get('texture_map',{}).keys()),'empty texture-map key')
                row['texture_stems']=sorted(stems)
            elif backend=='bone_skeleton':
                identifier=visual['geometry'];geo=read(load(visual.get('geometry_file','geometry.geo.json')))
                # geometry.<id>/texturewidth/box uv is legacy 1.8, not the
                # minecraft:geometry[]/description layout declared by 1.12.
                require(geo.get('format_version')=='1.8.0','format_version_layout_mismatch: keyed legacy box-UV geometry requires 1.8.0')
                require(identifier in geo,'geometry id unresolved')
                block=geo[identifier];size=[block['texturewidth'],block['textureheight']]
                require(all(type(x)==int and x>0 for x in size),'invalid atlas dimensions')
                require(visual.get('texture_size',size)==size,'definition atlas dimensions mismatch')
                png(visual.get('texture','diffuse'),size)
                bones=block['bones'];names=[b['name'] for b in bones];parents={b['name']:b.get('parent') for b in bones}
                require(bool(names) and len(set(names))==len(names),'empty/duplicate bone ids')
                require(all(name for name in names),'empty bone name')
                rectangles=[];cube_count=0;overlaps=[]
                for bone in bones:
                    require(vec(bone.get('pivot',[0,0,0])),'invalid bone pivot: '+bone['name'])
                    for key in ('rotation','bind_pose_rotation'):
                        if key in bone:require(vec(bone[key]),'invalid rotation: '+bone['name'])
                    seen={bone['name']};parent=bone.get('parent')
                    while parent:
                        if parent not in parents:errors.append('missing parent: '+parent);break
                        if parent in seen:errors.append('hierarchy cycle: '+parent);break
                        seen.add(parent);parent=parents[parent]
                    for cube in bone.get('cubes',[]):
                        cube_count+=1;require(vec(cube['origin']) and vec(cube['size'],positive=True),'invalid cube dimensions')
                        require(vec(cube['uv'],2),'invalid box UV origin')
                        u,v=cube['uv'];w,h,d=cube['size'];rect=(u,v,u+2*(w+d),v+h+d)
                        require(rect[0]>=0 and rect[1]>=0 and rect[2]<=size[0] and rect[3]<=size[1],'box UV outside atlas: '+bone['name'])
                        for previous in rectangles:
                            if rect[0]<previous[2] and rect[2]>previous[0] and rect[1]<previous[3] and rect[3]>previous[1]:overlaps.append([rect,previous])
                        rectangles.append(rect)
                row.update(geometry_id=identifier,bones=len(bones),cubes=cube_count,texture_size=size,box_uv_overlaps=overlaps,
                    ignored_legacy_fallback_parts=len(visual.get('parts',[])))
                bases[sid]=row
            elif backend=='rigid_voxels':
                if visual.get('sprite',{}).get('billboard'):
                    row['active_render_path']='billboard';png(visual.get('default_texture','body'))
                    require(not (folder/'model.gltf').exists() and not (folder/'model.bin').exists(),'stale unused glTF behind billboard')
                else:
                    parts=read(load(visual['rigid_model']))['parts'];ids=[part['id'] for part in parts]
                    require(bool(ids) and all(ids) and len(set(ids))==len(ids),'empty/duplicate rigid part ids')
                    for part in parts:
                        require(vec(part['offset']) and vec(part['size'],positive=True),'invalid rigid size/offset: '+part['id'])
                        if 'pivot' in part:require(vec(part['pivot']),'invalid rigid pivot: '+part['id'])
                    stems=sorted({part.get('texture',visual.get('default_texture','body')) for part in parts})
                    for stem in stems:png(stem)
                    row.update(active_render_path='rigid_parts',parts=len(parts),texture_stems=stems)
            else:raise ValueError('Unexpected backend')
        except (KeyError,ValueError,TypeError,OSError) as exc:
            errors.append(str(exc));row={'id':sid,'kind':kind}
        row.update(errors=errors,files_sha256=inputs);return row
    for path in sorted((models/'creatures').glob('*/creature.json')):
        if read(path).get('visual',{}).get('backend') in ('bone_skeleton','rigid_voxels'):
            rows.append(audit(path.parent.name,path.parent,'species'))
    for path in sorted((models/'skins').glob('*/skin.json')):rows.append(audit(path.parent.name,path.parent,'skin'))
    summary={'bone_species':sum(r.get('backend')=='bone_skeleton' for r in rows),'rigid_species':sum(r.get('backend')=='rigid_voxels' for r in rows),
        'skins':sum(r['kind']=='skin' for r in rows),'resources':len(rows),'errors':sum(len(r['errors']) for r in rows),
        'resources_with_errors':[r['id'] for r in rows if r['errors']],
        'uv_overlap_resources':[r['id'] for r in rows if r.get('box_uv_overlaps')]}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps({'schema':'creature-independent-br-format-audit.v1','scope':'active structural paths; not art, actual contact, package or complete interchange conformance',
        'models_root':str(models),'summary':summary,'resources':rows},indent=2)+'\n',encoding='utf8')
    print(json.dumps(summary,indent=2));return bool(summary['errors'])


if __name__=='__main__':raise SystemExit(main())
