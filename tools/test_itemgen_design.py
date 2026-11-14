"""Surface design export is deterministic, preserves sources and physical bounds."""
from pathlib import Path
import hashlib
import json
import shutil
import tempfile
import unittest
import numpy as np
from PIL import Image

from itemgen.design import build_design, ROOT
from itemgen.raster import load_primitives, render


class DesignTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        shutil.copytree(ROOT/'tools/item_designs/bucket',self.root/'tools/item_designs/bucket')
        (self.root/'content/items').mkdir(parents=True)
        shutil.copyfile(ROOT/'content/items/bucket.json',self.root/'content/items/bucket.json')

    def test_round_trip_normals_uvs_bounds_and_source_preservation(self):
        source=self.root/'tools/item_designs/bucket/design.yaml'
        before=source.read_bytes()
        path=build_design('bucket',root=self.root)
        g=json.loads((path/'model.gltf').read_text())
        blob=(path/'model.bin').read_bytes()
        for prim in g['meshes'][0]['primitives']:
            acc=g['accessors'][prim['attributes']['NORMAL']]
            view=g['bufferViews'][acc['bufferView']]
            normals=np.frombuffer(blob,dtype='<f4',count=acc['count']*3,offset=view['byteOffset']).reshape(-1,3)
            np.testing.assert_allclose(np.linalg.norm(normals,axis=1),1,atol=1e-6)
        points=np.concatenate([p['positions'] for p in load_primitives(path)])
        self.assertEqual(render(load_primitives(path),size=96).size,(96,96))
        physical=json.loads((self.root/'content/items/bucket.json').read_text())['physical']['size']
        self.assertTrue(np.all(np.ptp(points,axis=0)<=np.array(physical)+1e-6))
        with Image.open(path/'textures/diffuse.png') as image:
            self.assertEqual(image.size,(256,256))
        self.assertEqual(source.read_bytes(),before)
        files={p.relative_to(path):hashlib.sha256(p.read_bytes()).hexdigest() for p in path.rglob('*') if p.is_file()}
        build_design('bucket',root=self.root)
        self.assertEqual(files,{p.relative_to(path):hashlib.sha256(p.read_bytes()).hexdigest() for p in path.rglob('*') if p.is_file()})

    def test_author_overlay_is_read_only_and_applied(self):
        path=build_design('bucket',root=self.root)
        island=json.loads((path/'uv_layout.json').read_text())['islands'][0]
        x,y,w,h=island['rect']
        source=self.root/'tools/item_designs/bucket/surfaces'; source.mkdir()
        overlay=source/(island['name']+'.png')
        Image.new('RGBA',(w,h),(255,0,255,255)).save(overlay)
        before=overlay.read_bytes()
        build_design('bucket',root=self.root)
        with Image.open(path/'textures/diffuse.png') as image:
            self.assertEqual(image.getpixel((x,y)),(255,0,255,255))
            self.assertEqual(image.getpixel((x-1,y-1)),(255,0,255,255))
        self.assertEqual(overlay.read_bytes(),before)

    def test_bad_overlay_and_source_destination_are_rejected(self):
        path=build_design('bucket',root=self.root)
        island=json.loads((path/'uv_layout.json').read_text())['islands'][0]
        source=self.root/'tools/item_designs/bucket/surfaces'; source.mkdir()
        Image.new('RGBA',(1,1)).save(source/(island['name']+'.png'))
        with self.assertRaisesRegex(ValueError,'size mismatch'): build_design('bucket',root=self.root)
        with self.assertRaisesRegex(ValueError,'overwrite'): build_design('bucket',root=self.root,output=source)


if __name__=='__main__': unittest.main()
