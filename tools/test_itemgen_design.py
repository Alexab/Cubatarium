"""Surface design export is deterministic, preserves sources and physical bounds."""
from pathlib import Path
import hashlib
import json
import shutil
import tempfile
import unittest
import yaml
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
        grip=g['extras']['presentation']['right_grip']
        self.assertTrue(np.all(np.array(grip)>=points.min(axis=0)-1e-6))
        self.assertTrue(np.all(np.array(grip)<=points.max(axis=0)+1e-6))
        authored=yaml.safe_load(source.read_text(encoding='utf-8'))
        self.assertEqual(g['extras']['presentation']['fp_euler_deg'],authored['presentation']['fp_euler_deg'])
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

    def test_authored_node_pivots_preserve_rest_geometry(self):
        path=build_design('bucket',root=self.root)
        before=np.concatenate([p['positions'] for p in load_primitives(path)])
        source=self.root/'tools/item_designs/bucket/design.yaml'
        design=yaml.safe_load(source.read_text(encoding='utf-8'))
        design['nodes']={'handle':{'pivot':[0,.12,0]}}
        for part in design['parts']:
            if part['name'].startswith('handle'): part['node']='handle'
        source.write_text(yaml.safe_dump(design,sort_keys=False),encoding='utf-8')
        path=build_design('bucket',root=self.root)
        after=np.concatenate([p['positions'] for p in load_primitives(path)])
        # Grouping changes vertex order, never the rest-pose surface positions.
        self.assertEqual({tuple(np.round(p,5)) for p in before}, {tuple(np.round(p,5)) for p in after})
        g=json.loads((path/'model.gltf').read_text())
        self.assertEqual(g['nodes'][1]['name'],'handle')
        self.assertEqual(g['nodes'][0]['children'],[1])

    def test_bad_overlay_and_source_destination_are_rejected(self):
        path=build_design('bucket',root=self.root)
        island=json.loads((path/'uv_layout.json').read_text())['islands'][0]
        source=self.root/'tools/item_designs/bucket/surfaces'; source.mkdir()
        Image.new('RGBA',(1,1)).save(source/(island['name']+'.png'))
        with self.assertRaisesRegex(ValueError,'size mismatch'): build_design('bucket',root=self.root)
        with self.assertRaisesRegex(ValueError,'overwrite'): build_design('bucket',root=self.root,output=source)


class PilotTests(unittest.TestCase):
    def test_all_twelve_pilot_sources_build_without_aliases(self):
        ids=['stone_pickaxe','iron_pickaxe','copper_axe','gold_sword','iron_head','leather_chest','wood_bow','bucket','shears','apple','bread','raw_iron']
        hashes=set()
        with tempfile.TemporaryDirectory() as tmp:
            for id in ids:
                path=build_design(id,output=Path(tmp)/id)
                g=json.loads((path/'model.gltf').read_text())
                recipe=json.loads((path/'design_build.json').read_text())
                self.assertLessEqual(recipe['triangles'],2000)
                value=hashlib.sha256((path/'model.bin').read_bytes()).hexdigest()
                self.assertNotIn(value,hashes)
                hashes.add(value)
                self.assertIn('right_grip',g['extras']['presentation'])
                load_primitives(path)

    def test_seeding_never_overwrites_an_author_design(self):
        from itemgen.pilot_designs import seed_missing
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); seed_missing(root)
            p=root/'tools/item_designs/apple/design.yaml'
            p.write_text('artist-authored sentinel',encoding='utf-8')
            self.assertEqual(seed_missing(root),[])
            self.assertEqual(p.read_text(),'artist-authored sentinel')


if __name__=='__main__': unittest.main()
