"""Deterministic provenance for own generated resources; no reference assets."""
import hashlib
import json
import platform
from pathlib import Path

import PIL
from PIL import features
import yaml
from creaturegen.hashutil import file_hash,pixel_hash


def canonical_hash(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode()).hexdigest()


def source_context(root,recipe_id):
    paths=[root/'LICENSE',root/'tools/creature_recipes'/f'{recipe_id}.yaml',root/'tools/creature_palettes.yaml',
        root/'tools/requirements-creaturegen.txt',*sorted((root/'tools/creaturegen').glob('*.py'))]
    contracts=yaml.safe_load((root/'tools/creature_quality_contracts.yaml').read_text(encoding='utf8'))
    # Acceptance metadata is deliberately separate: hashing a review's bundle
    # hash into its own build inputs would create a circular dependency.
    functional={k:v for k,v in contracts['species'][recipe_id].items()
        if k not in {'review_state','r5_review_path','r6_state'}}
    return {'source_inputs':{p.relative_to(root).as_posix():file_hash(p) for p in paths},
        'toolchain':{'python':platform.python_version(),'implementation':platform.python_implementation(),
            'Pillow':PIL.__version__,'Pillow_zlib':features.version_codec('zlib'),'PyYAML':yaml.__version__},
        'functional_contract_sha256':canonical_hash(functional)}


def write_authorship(root,folder,resource_id,recipe_id):
    """Use the project's existing license for its newly authored outputs."""
    path=folder/'LICENSE.txt'
    path.write_text((root/'LICENSE').read_text(encoding='utf8').rstrip()+
        f'\n\nGenerated asset: {resource_id}\nAuthoring source: Cubatarium project recipes\n'
        f'Recipe: tools/creature_recipes/{recipe_id}.yaml\n'
        'This notice covers the project-authored generated files listed in the asset manifest.\n'
        'Reference pixels and meshes are not inputs to the production generator.\n',encoding='utf8')
    return path


def file_records(paths,folder,recipe):
    from PIL import Image
    result=[]
    for path in sorted(paths):
        record={'path':path.relative_to(folder).as_posix(),'role':path.suffix,'sha256':file_hash(path),
            'recipe_version':recipe['recipe_version'],'seed':recipe['seed'],
            'seed_hex':f"{recipe['seed']:016x}",'authoring_source':'cubatarium-recipe'}
        record['runtime_role']=('preview_icon' if path.stem=='icon' else 'albedo_atlas') if path.suffix=='.png' else {
            'model.gltf':'skinned_scene','model.bin':'mesh_and_animation_data',
            'geometry.geo.json':'bone_geometry','rigid_model.json':'rigid_parts',
            'skin.json':'skin_definition','LICENSE.txt':'license_and_authorship'}.get(path.name,'asset_data')
        if path.suffix=='.png':
            with Image.open(path) as image:
                image.load();record.update(pixel_sha256=pixel_hash(image),size=list(image.size))
        result.append(record)
    return result


def gltf_components(gltf,blob):
    """Separate mesh and clip records, including their actual binary inputs."""
    def data_hash(description,accessors):
        digest=hashlib.sha256(json.dumps(description,sort_keys=True,separators=(',',':')).encode())
        for index in sorted(set(accessors)):
            a=gltf['accessors'][index];view=gltf['bufferViews'][a['bufferView']]
            digest.update(json.dumps(a,sort_keys=True).encode())
            digest.update(blob[view.get('byteOffset',0):view.get('byteOffset',0)+view['byteLength']])
        return digest.hexdigest()
    meshes=[];clips=[]
    for i,mesh in enumerate(gltf['meshes']):
        indices=[value for p in mesh['primitives'] for value in p['attributes'].values()]
        indices.extend(p['indices'] for p in mesh['primitives'] if 'indices' in p)
        meshes.append({'index':i,'primitive_count':len(mesh['primitives']),'data_sha256':data_hash(mesh,indices)})
    for clip in gltf['animations']:
        indices=[s[key] for s in clip['samplers'] for key in ('input','output')]
        clips.append({'name':clip['name'],'channel_count':len(clip['channels']),'data_sha256':data_hash(clip,indices)})
    return {'meshes':meshes,'clips':clips}
