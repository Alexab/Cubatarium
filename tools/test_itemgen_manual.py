"""Manual topology/UV/material import must preserve source files and property."""
from pathlib import Path
import copy
import json
import shutil
import struct
import tempfile
import unittest
import yaml
import numpy as np
from PIL import Image
from itemgen.design import ROOT,build_design
from itemgen.raster import load_primitives
from itemgen.design_publication import _hashes,validate_design,publish_designs
from itemgen.manifest import validate_manifest


def attribute(folder,g,index):
    a=g['accessors'][index];v=g['bufferViews'][a['bufferView']];data=(folder/g['buffers'][v['buffer']]['uri']).read_bytes()
    width={'VEC3':3,'VEC2':2}[a['type']]
    return np.ndarray((a['count'],width),dtype='<f4',buffer=data,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',width*4),4)).copy()


class ManualTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        base=build_design('bucket',output=self.root/'base')
        self.source=self.root/'tools/item_designs/bucket';self.source.mkdir(parents=True)
        self.author=self.source/'authored';self.author.mkdir()
        self.g=json.loads((base/'model.gltf').read_text());original_extras=self.g.pop('extras')
        for name in ['model.bin','textures/diffuse.png']:
            target=self.author/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/name,target)
        self.g['nodes'][0]['translation']=[2.,3.,4.];self.g['nodes'][0]['scale']=[100.,100.,100.]
        self.recipe=dict(version=3,id='bucket',triangle_budget=1800,geometry_source=dict(path='authored/model.gltf'),
                         presentation=dict(right_grip=[2.,3.,4.],left_grip=[2.,3.,4.],fp_scale=.8),visual_bindings=original_extras['visual_bindings'])
        (self.root/'content/items').mkdir(parents=True);shutil.copyfile(ROOT/'content/items/bucket.json',self.root/'content/items/bucket.json')
        shutil.copytree(ROOT/'tools/itemgen',self.root/'tools/itemgen',ignore=shutil.ignore_patterns('__pycache__'))
        for name in ['LICENSE','shaders/item_surface.vert','shaders/item_surface.frag']:
            target=self.root/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
        provenance=json.loads((ROOT/'tools/item_designs/provenance.json').read_text());self.provenance=provenance['items']['bucket']
        self.provenance['geometry'].update(authoring_format='manual-gltf-2.0',source='tools/item_designs/bucket/authored/model.gltf',method='test-authored manual glTF')
        self.save()

    def save(self):
        (self.author/'model.gltf').write_text(json.dumps(self.g))
        (self.source/'design.yaml').write_text(yaml.safe_dump(self.recipe,sort_keys=False))
        (self.root/'tools/item_designs/provenance.json').write_text(json.dumps(dict(version=1,items={'bucket':self.provenance})))

    def assert_round_trip(self):
        before=_hashes(self.source);definition=(self.root/'content/items/bucket.json').read_bytes()
        original=load_primitives(self.author);stage=build_design('bucket',root=self.root);g=json.loads((stage/'model.gltf').read_text());record=json.loads((stage/'design_build.json').read_text())
        output=load_primitives(stage);fit=record['physical_fit'];scale=fit['scale'];centre=np.array(fit['centre'])
        self.assertEqual(len(output),len(original))
        for old,new in zip(original,output):
            np.testing.assert_allclose(new['positions'],(old['positions']-centre)*scale,rtol=1e-5,atol=2e-6)
            np.testing.assert_array_equal(new['uv'],old['uv']);np.testing.assert_array_equal(new['indices'],old['indices'])
            np.testing.assert_array_equal(new['texture'],old['texture']);np.testing.assert_array_equal(new['factor'],old['factor'])
        for old,new in zip([p for m in self.g['meshes'] for p in m['primitives']],[p for m in g['meshes'] for p in m['primitives']]):
            np.testing.assert_array_equal(attribute(self.author,self.g,old['attributes']['NORMAL']),attribute(stage,g,new['attributes']['NORMAL']))
        points=np.concatenate([p['positions'] for p in output]);size=json.loads(definition)['physical']['size']
        self.assertTrue(np.all(np.ptp(points,axis=0)<=np.array(size)+2e-6))
        np.testing.assert_allclose(g['extras']['presentation']['right_grip'],(np.array(self.recipe['presentation']['right_grip'])-centre)*scale)
        self.assertEqual(g['extras']['presentation']['fp_scale'],.8)
        if self.recipe['visual_bindings']:self.assertEqual(g['extras']['visual_bindings'][0]['node'],self.recipe['visual_bindings'][0]['node'])
        self.assertEqual(_hashes(self.source),before);self.assertEqual((self.root/'content/items/bucket.json').read_bytes(),definition)
        outputs=_hashes(stage);build_design('bucket',root=self.root);self.assertEqual(_hashes(stage),outputs)
        validate_design('bucket',root=self.root)
        return stage,g

    def test_trs_geometry_uv_normals_state_and_determinism(self):
        stage,g=self.assert_round_trip()
        publish_designs(['bucket'],root=self.root)
        self.assertEqual(validate_manifest(self.root),[])
        self.assertEqual((self.root/'models/items/bucket/model.bin').read_bytes(),(stage/'model.bin').read_bytes())

    def test_matrix_multiple_buffers_textures_and_named_root_wrapper(self):
        node=self.g['nodes'][0];node.pop('translation');node.pop('scale');node['name']='handcrafted_root'
        matrix=np.array([[80.,0,25.,2.],[0,100.,0,3.],[-25.,0,80.,4.],[0,0,0,1.]])
        node['matrix']=matrix.T.reshape(-1).tolist()
        blob=(self.author/'model.bin').read_bytes();cut=self.g['bufferViews'][len(self.g['bufferViews'])//2]['byteOffset']
        (self.author/'first.bin').write_bytes(blob[:cut]);(self.author/'second.bin').write_bytes(blob[cut:])
        self.g['buffers']=[dict(uri='first.bin',byteLength=cut),dict(uri='second.bin',byteLength=len(blob)-cut)]
        for view in self.g['bufferViews']:
            if view.get('byteOffset',0)>=cut:view['byteOffset']-=cut;view['buffer']=1
        image=Image.open(self.author/'textures/diffuse.png').copy();image.putpixel((0,0),(255,0,0,255));image.save(self.author/'textures/second image.png')
        self.g['images'].append(dict(uri='textures/second%20image.png'));self.g['textures'].append(dict(source=1,sampler=0))
        self.g['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']=1
        self.save();stage,g=self.assert_round_trip()
        self.assertEqual(len(g['buffers']),1);self.assertEqual(len(g['images']),2)
        self.assertEqual(g['nodes'][g['scenes'][0]['nodes'][0]]['name'],'bucket')
        self.assertIn('handcrafted_root',[n['name'] for n in g['nodes']])
        self.assertEqual((stage/'model.bin').read_bytes()[:len(blob)],blob)

    def test_shared_position_normal_accessor_is_not_overwritten_by_fit(self):
        material=copy.deepcopy(self.g['materials'][0])
        blob=struct.pack('<15f3H',1,0,0,0,1,0,0,0,1,0,0,1,0,0,1,0,1,2)
        (self.author/'model.bin').write_bytes(blob)
        self.g=dict(asset=dict(version='2.0'),buffers=[dict(uri='model.bin',byteLength=len(blob))],
            bufferViews=[dict(buffer=0,byteOffset=0,byteLength=36),dict(buffer=0,byteOffset=36,byteLength=24),dict(buffer=0,byteOffset=60,byteLength=6)],
            accessors=[dict(bufferView=0,componentType=5126,count=3,type='VEC3'),dict(bufferView=1,componentType=5126,count=3,type='VEC2'),dict(bufferView=2,componentType=5123,count=3,type='SCALAR')],
            images=[dict(uri='textures/diffuse.png')],textures=[dict(source=0,sampler=0)],samplers=[dict(magFilter=9729,minFilter=9987,wrapS=33071,wrapT=33071)],materials=[material],
            meshes=[dict(primitives=[dict(attributes=dict(POSITION=0,NORMAL=0,TEXCOORD_0=1),indices=2,material=0)])],nodes=[dict(name='bucket',mesh=0)],scenes=[dict(nodes=[0])],scene=0)
        self.g['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']=0
        self.recipe['visual_bindings']=[];self.save();stage,g=self.assert_round_trip()
        self.assertEqual((stage/'model.bin').read_bytes()[:len(blob)],blob)
        primitive=g['meshes'][0]['primitives'][0]
        self.assertNotEqual(primitive['attributes']['POSITION'],primitive['attributes']['NORMAL'])

    def test_explicit_fit_nodes_and_invalid_selection(self):
        self.recipe['geometry_source']['fit_nodes']=['water_surface'];self.save()
        points=np.concatenate([p['positions'] for p in load_primitives(self.author) if p['node_name']=='water_surface'])
        stage=build_design('bucket',root=self.root);fit=json.loads((stage/'design_build.json').read_text())['physical_fit']
        np.testing.assert_allclose(fit['centre'],(points.min(axis=0)+points.max(axis=0))*.5)
        previous=_hashes(stage);self.recipe['geometry_source']['fit_nodes']=['missing'];self.save()
        with self.assertRaisesRegex(ValueError,'fit_nodes'):build_design('bucket',root=self.root)
        self.assertEqual(_hashes(stage),previous)

    def test_rejections_keep_previous_output_and_author_files(self):
        stage=build_design('bucket',root=self.root);previous=_hashes(stage);source_before=_hashes(self.source)
        original=copy.deepcopy(self.g)
        for kind in ['animation','sampler','reflection','normal','uri','name']:
            with self.subTest(kind=kind):
                self.g=copy.deepcopy(original)
                if kind=='animation':self.g['animations']=[]
                elif kind=='sampler':self.g['samplers'][0]['wrapS']=10497
                elif kind=='reflection':self.g['nodes'][0]['scale'][0]=-1
                elif kind=='normal':del self.g['meshes'][0]['primitives'][0]['attributes']['NORMAL']
                elif kind=='uri':self.g['images'][0]['uri']='../../outside.png'
                elif kind=='name':self.g['nodes'][0]['name']=''
                self.save();authored=_hashes(self.source)
                with self.assertRaises((ValueError,KeyError)):build_design('bucket',root=self.root)
                self.assertEqual(_hashes(stage),previous);self.assertEqual(_hashes(self.source),authored)
        self.g=original;self.save();self.assertEqual(_hashes(self.source),source_before)
        self.provenance['geometry'].pop('authoring_format');self.save();build_design('bucket',root=self.root)
        with self.assertRaisesRegex(ValueError,'provenance'):validate_design('bucket',root=self.root)


if __name__=='__main__':unittest.main()
