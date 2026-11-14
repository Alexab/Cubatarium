"""QA runtime substitution must restore resources even when a probe fails."""
from pathlib import Path
import tempfile
import unittest
from run_item_design_probes import runtime_designs


class RuntimeProbeTests(unittest.TestCase):
    def test_failed_probe_restores_every_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); runtime=root/'bin/models/items/bucket'; staged=root/'stage'
            for folder in (runtime,staged): (folder/'textures').mkdir(parents=True)
            names=('model.gltf','model.bin','textures/diffuse.png')
            for name in names:
                (runtime/name).write_bytes(b'original '+name.encode())
                (staged/name).write_bytes(b'candidate '+name.encode())
            with self.assertRaisesRegex(RuntimeError,'probe failure'):
                with runtime_designs({'bucket':staged},root=root) as count:
                    self.assertEqual(count,3)
                    for name in names: self.assertEqual((runtime/name).read_bytes(),b'candidate '+name.encode())
                    raise RuntimeError('probe failure')
            for name in names: self.assertEqual((runtime/name).read_bytes(),b'original '+name.encode())

    def test_incomplete_staging_restores_partial_substitution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); runtime=root/'bin/models/items/bucket'; staged=root/'stage'
            runtime.mkdir(parents=True);staged.mkdir()
            for name in ('model.gltf','model.bin'): (runtime/name).write_bytes(b'original')
            (staged/'model.gltf').write_bytes(b'candidate')
            with self.assertRaises(FileNotFoundError):
                with runtime_designs({'bucket':staged},root=root): self.fail('entered incomplete probe')
            self.assertEqual((runtime/'model.gltf').read_bytes(),b'original')


if __name__=='__main__': unittest.main()
