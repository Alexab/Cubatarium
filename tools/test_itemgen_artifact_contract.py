"""Malformed/unsupported glTF cannot pass the versioned publication contract."""
from pathlib import Path
import copy
import json
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from itemgen.design import ROOT,build_design
from itemgen.artifact_contract import validate_model


class ContractTests(unittest.TestCase):
    def test_runtime_sync_rejects_unfinished_publication_without_touching_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'models';destination=Path(temp)/'runtime'
            (source/'items').mkdir(parents=True);destination.mkdir()
            (source/'items/PUBLICATION_TRANSACTION.json').write_text('{}')
            sentinel=destination/'sentinel.txt';sentinel.write_text('previous runtime')
            result=subprocess.run(['cmake',f'-DSYNC_SRC={source}',f'-DSYNC_DST={destination}','-P',str(ROOT/'cmake/sync_runtime_subdir.cmake')],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('Unfinished item publication',result.stderr)
            self.assertEqual(sentinel.read_text(),'previous runtime')
            self.assertEqual(list(destination.iterdir()),[sentinel])

    def test_transient_directory_rename_is_bounded_and_never_deletes_destination(self):
        from itemgen.filesystem import replace_path
        transient=PermissionError('busy');transient.winerror=5
        with patch('itemgen.filesystem.os.replace',side_effect=[transient,None]) as move,patch('itemgen.filesystem.time.sleep'):
            replace_path('source','destination');self.assertEqual(move.call_count,2)
        with patch('itemgen.filesystem.os.replace',side_effect=transient) as move,patch('itemgen.filesystem.time.sleep'):
            with self.assertRaises(PermissionError):replace_path('source','destination')
            self.assertEqual(move.call_count,10)
        with patch('itemgen.filesystem.os.replace',side_effect=OSError('disk failure')) as move:
            with self.assertRaises(OSError):replace_path('source','destination')
            self.assertEqual(move.call_count,1)

    def test_real_geometry_and_fail_closed_subset(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=build_design('bucket',output=Path(temp)/'bucket')
            path=folder/'model.gltf';original=json.loads(path.read_text());blob=(folder/'model.bin').read_bytes()
            report=validate_model(folder,budget=1800)
            self.assertGreater(report['triangles'],0);self.assertIn('textures/diffuse.png',report['resources'])
            def mutate(kind):
                g=copy.deepcopy(original);p=g['meshes'][0]['primitives'][0]
                if kind=='normal':del p['attributes']['NORMAL']
                elif kind=='mode':p['mode']=1
                elif kind=='normal_map':g['materials'][0]['normalTexture']={'index':0}
                elif kind=='metallic_map':g['materials'][0]['pbrMetallicRoughness']['metallicRoughnessTexture']={'index':0}
                elif kind=='alpha':g['materials'][0]['alphaMode']='MASK'
                elif kind=='extension':g['extensionsUsed']=['KHR_materials_unlit']
                elif kind=='animation':g['animations']=[]
                elif kind=='wrapping':g['samplers'][0]['wrapS']=10497
                elif kind=='filtering':g['samplers'][0]['minFilter']=9728
                elif kind=='empty_node':g['nodes'][0]['name']=''
                elif kind=='node_cycle':g['nodes'][0]['children']=[0]
                elif kind=='node_scale':g['nodes'][0]['scale']=[0,1,1]
                elif kind=='shared_node':g['nodes'][0]['children']=[1,1]
                elif kind=='quaternion':g['nodes'][0]['rotation']=[0,0,0,0]
                elif kind=='uri':g['images'][0]['uri']='../outside.png'
                elif kind=='negative_image':g['textures'][0]['source']=-1
                elif kind=='negative_accessor':p['attributes']['POSITION']=-1
                elif kind=='negative_material':p['material']=-1
                elif kind=='short_view':g['bufferViews'][g['accessors'][p['attributes']['NORMAL']]['bufferView']]['byteLength']=1
                elif kind=='zero_normal':
                    index=g['accessors'][p['attributes']['NORMAL']]['bufferView'];start=g['bufferViews'][index]['byteOffset']
                    data=bytearray(blob);struct.pack_into('<fff',data,start,0,0,0);(folder/'model.bin').write_bytes(data)
                elif kind=='support_missing':g.setdefault('extras',{}).setdefault('presentation',{})['support_grip_node']='missing'
                elif kind=='support_type':g.setdefault('extras',{}).setdefault('presentation',{})['support_grip_node']=3
                elif kind=='aim_vector':g.setdefault('extras',{}).setdefault('presentation',{})['fp_aim_euler_deg']=[1,2]
                elif kind=='nonfinite':g['materials'][0]['pbrMetallicRoughness']['metallicFactor']=float('nan')
                return g
            for kind in ['normal','mode','normal_map','metallic_map','alpha','extension','animation','wrapping','filtering','empty_node','node_cycle','node_scale','shared_node','quaternion','uri','negative_image','negative_accessor','negative_material','short_view','zero_normal','nonfinite','support_missing','support_type','aim_vector']:
                with self.subTest(kind=kind):
                    (folder/'model.bin').write_bytes(blob);path.write_text(json.dumps(mutate(kind)))
                    with self.assertRaises((ValueError,KeyError,IndexError)):validate_model(folder,budget=1800)
            path.write_text(json.dumps(original));(folder/'model.bin').write_bytes(blob)
            with self.assertRaisesRegex(ValueError,'budget'):validate_model(folder,budget=1)


if __name__=='__main__':unittest.main()
