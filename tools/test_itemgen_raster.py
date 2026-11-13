"""Regression cases for geometry-preserving item QA rendering."""
import json
import tempfile
import unittest
from pathlib import Path
import numpy as np
from itemgen.export_gltf import write_gltf
from itemgen.pipeline import load_materials
from itemgen.raster import load_primitives, render

class RasterTests(unittest.TestCase):
    def test_disconnected_boxes_in_one_primitive_keep_gap(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            write_gltf(folder,'gap',[
                dict(role='head',offset=[-.3,0,0],size=[.2,.4,.2]),
                dict(role='head',offset=[.3,0,0],size=[.2,.4,.2]),
            ],load_materials()['iron'])
            primitives=load_primitives(folder)
            self.assertEqual(len(primitives),1,'fixture must share a material primitive')
            result=render(primitives,mode='silhouette',size=96,yaw=0,pitch=0)
            self.assertEqual(result.getpixel((48,48)),(255,255,255,255),'no AABB bridge')
            self.assertGreater(np.sum(np.asarray(result)[:,:,0]==0),100)

    def test_actual_uv_texture_details_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            write_gltf(folder,'textured',[dict(role='head',offset=[0,0,0],size=[.4,.4,.2])],load_materials()['iron'])
            result=render(load_primitives(folder),size=96,yaw=0,pitch=0)
            colors=set(result.getdata())-{(24,24,28,255)}
            self.assertGreaterEqual(len(colors),4,'flat center-color sampling must not pass')

    def test_node_transform_is_applied(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            write_gltf(folder,'node',[dict(role='head',offset=[0,0,0],size=[.4,.2,.2])],load_materials()['iron'])
            path=folder/'model.gltf';gltf=json.loads(path.read_text())
            before=load_primitives(folder)[0]['positions'].copy()
            gltf['nodes'][0]['translation']=[3,2,1]
            path.write_text(json.dumps(gltf))
            after=load_primitives(folder)[0]['positions']
            np.testing.assert_allclose(after-before,np.tile([3,2,1],(len(before),1)),atol=1e-6)

    def test_nonzero_accessor_offset_is_respected(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            write_gltf(folder,'offset',[dict(role='head',offset=[0,0,0],size=[.4,.2,.2])],load_materials()['iron'])
            path=folder/'model.gltf';gltf=json.loads(path.read_text())
            before=load_primitives(folder)[0]['positions'].copy()
            for accessor in gltf['accessors']:
                accessor['byteOffset']=accessor.get('byteOffset',0)+4
            # Insert one pad word at every bufferView start, leaving accessor data intact.
            data=(folder/'model.bin').read_bytes();new=bytearray()
            for view in gltf['bufferViews']:
                original=view.get('byteOffset',0)
                segment=data[original:original+view['byteLength']]
                view['byteOffset']=len(new);view['byteLength']+=4
                new.extend(b'\0'*4);new.extend(segment)
            gltf['buffers'][0]['byteLength']=len(new)
            (folder/'model.bin').write_bytes(new);path.write_text(json.dumps(gltf))
            after=load_primitives(folder)[0]['positions']
            np.testing.assert_array_equal(before,after)

if __name__=='__main__':unittest.main()
