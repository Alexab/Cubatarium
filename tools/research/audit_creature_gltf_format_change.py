"""Prove an export cleanup changes only unused joints and portable materials."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from research.review_creature_redesign import accessor


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',type=Path,default=ROOT/'assetgen_output/creatures/pre_r6_format/models/creatures')
    parser.add_argument('--after',type=Path,default=ROOT/'assetgen_output/creatures/r6_format_v2/models/creatures')
    parser.add_argument('--out',type=Path,default=ROOT/'docs/research/creature-r6-v2/format/payload_audit.json')
    args=parser.parse_args();rows=[]
    for folder in sorted(args.after.iterdir()):
        if not folder.is_dir() or not (folder/'creature.json').is_file():continue
        definition=json.loads((folder/'creature.json').read_text(encoding='utf8'))
        if definition['visual']['backend']!='gltf_skeleton':continue
        previous=args.before/folder.name
        assert definition==json.loads((previous/'creature.json').read_text(encoding='utf8'))
        g0=json.loads((previous/'model.gltf').read_text());g1=json.loads((folder/'model.gltf').read_text())
        ignored={'materials','images','textures','samplers'}
        assert {k:v for k,v in g0.items() if k not in ignored}=={k:v for k,v in g1.items() if k not in ignored},folder.name
        assert [m['name'] for m in g0['materials']]==[m['name'] for m in g1['materials']]
        b0=[(previous/b['uri']).read_bytes() for b in g0['buffers']];b1=[(folder/b['uri']).read_bytes() for b in g1['buffers']]
        ignored_accessors=set();changed=0
        for primitive in g1['meshes'][0]['primitives']:
            a=primitive['attributes'];ji=a['JOINTS_0'];ignored_accessors.add(ji)
            j0=accessor(g0,b0,ji);j1=accessor(g1,b1,ji);w=accessor(g1,b1,a['WEIGHTS_0'])
            assert np.array_equal(j0[w>0],j1[w>0]),folder.name+': active joint changed'
            assert np.all(j1[w==0]==0),folder.name+': noncanonical unused slot'
            changed+=int(np.sum(j0!=j1))
        for i in range(len(g1['accessors'])):
            if i not in ignored_accessors:assert np.array_equal(accessor(g0,b0,i),accessor(g1,b1,i)),(folder.name,i)
        textures={}
        for image in (folder/'textures').glob('*.png'):
            assert image.read_bytes()==(previous/'textures'/image.name).read_bytes(),image
            textures[image.name]=hashlib.sha256(image.read_bytes()).hexdigest()
        rows.append({'id':folder.name,'runtime_definition_unchanged':True,'geometry_uv_weights_animation_bind_data_unchanged':True,
            'active_joint_indices_unchanged':True,'unused_joint_slots_reset':changed,'texture_sha256':textures,
            'material_names_unchanged':True,'portable_image_bindings':g1['images']})
    assert len(rows)==35
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps({'schema':'creature-gltf-format-equivalence.v1','count':len(rows),'resources':rows},indent=2)+'\n',encoding='utf8')
    print('PROVED',len(rows),'assets;',sum(r['unused_joint_slots_reset'] for r in rows),'unused joint slots reset')


if __name__=='__main__':main()
