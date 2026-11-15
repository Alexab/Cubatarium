"""Regression cases for geometry-preserving item QA rendering."""
import json
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
import numpy as np
from itemgen.export_gltf import write_gltf
from itemgen.pipeline import load_materials
from itemgen.raster import load_primitives, render

class RasterTests(unittest.TestCase):
    def test_budget_includes_later_meshes_and_nonindexed_triangles(self):
        from itemgen.quality import _triangle_budget
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)/'multi';folder.mkdir()
            gltf=dict(accessors=[dict(count=6),dict(count=9)],meshes=[
                dict(primitives=[dict(indices=0,attributes=dict(POSITION=0))]),
                dict(primitives=[dict(attributes=dict(POSITION=1))])])
            path=folder/'model.gltf';path.write_text(json.dumps(gltf))
            with patch('itemgen.quality.MODELS',Path(tmp)):
                self.assertEqual(_triangle_budget('multi'),5)
                gltf['meshes'][1]['primitives'][0]['mode']=1
                path.write_text(json.dumps(gltf))
                with self.assertRaisesRegex(ValueError,'triangle primitives'):
                    _triangle_budget('multi')

    def test_positive_camera_elevation_sees_top_surface(self):
        def face(y,color,reverse=False):
            positions=np.array([[-.2,y,-.2],[-.2,y,.2],[.2,y,.2],[.2,y,-.2]])
            if reverse:positions=positions[::-1]
            return dict(positions=positions,indices=np.array([[0,1,2],[0,2,3]]),uv=np.zeros((4,2)),texture=None,factor=np.array([*color,1]),double_sided=False,alpha_cutoff=.5)
        surfaces=[face(.1,[1,0,0]),face(-.1,[0,0,1],True)]
        above=render(surfaces,size=96,yaw=0,pitch=45).getpixel((48,48))
        below=render(surfaces,size=96,yaw=0,pitch=-45).getpixel((48,48))
        self.assertGreater(above[0],above[2])
        self.assertGreater(below[2],below[0])

    def test_grayscale_albedo_remains_separate_from_gray_material(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            write_gltf(folder,'gray',[dict(role='head',offset=[0,0,0],size=[.4,.4,.2])],load_materials()['iron'])
            parts=load_primitives(folder)
            before=np.asarray(render(parts,mode='grayscale',size=96))
            for part in parts:part['factor'][:3]*=.1
            self.assertFalse(np.array_equal(before,np.asarray(render(parts,mode='grayscale',size=96))))

    def test_gray_material_ignores_albedo_and_retains_shape_lighting(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            write_gltf(folder,'gray',[dict(role='head',offset=[0,0,0],size=[.4,.4,.2])],load_materials()['iron'])
            primitives=load_primitives(folder)
            before=np.asarray(render(primitives,mode='gray',size=96))
            for part in primitives:
                part['factor']=np.array([.01,0,0,1])
                part['texture'][:,:,:3]=[0,1,0]
            np.testing.assert_array_equal(before,np.asarray(render(primitives,mode='gray',size=96)))
            self.assertGreater(len(set(map(tuple,before.reshape(-1,4)))),2)

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
