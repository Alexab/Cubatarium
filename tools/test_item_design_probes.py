"""QA runtime substitution must restore resources even when a probe fails."""
from pathlib import Path
import tempfile
import json
import unittest
from run_item_design_probes import runtime_designs
from qa_report_artifacts import reset_report


class RuntimeProbeTests(unittest.TestCase):
    def test_report_reset_removes_stale_frames_but_preserves_other_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);report=root/'native.json';frames=root/'native.json.frames'
            frames.mkdir();(frames/'stale.png').write_bytes(b'stale')
            report.write_text('{}');other=root/'other.json';other.write_text('keep')
            reset_report(report,root)
            self.assertFalse(report.exists());self.assertFalse(frames.exists())
            self.assertEqual(other.read_text(),'keep')
            reset_report(report,root)

    def test_report_reset_refuses_paths_outside_the_owned_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);output=root/'qa';output.mkdir()
            report=root/'outside.json';report.write_text('keep')
            with self.assertRaises(ValueError):reset_report(report,output)
            self.assertEqual(report.read_text(),'keep')

    def test_failed_probe_restores_every_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); runtime=root/'bin/models/items/bucket'; staged=root/'stage'
            for folder in (runtime,staged): (folder/'textures').mkdir(parents=True)
            names=('model.gltf','model.bin','textures/diffuse.png')
            for name in names:
                (runtime/name).write_bytes(b'original '+name.encode())
                (staged/name).write_bytes(json.dumps(dict(buffers=[dict(uri='model.bin')],images=[dict(uri='textures/diffuse.png')])).encode() if name=='model.gltf' else b'candidate '+name.encode())
            with self.assertRaisesRegex(RuntimeError,'probe failure'):
                with runtime_designs({'bucket':staged},root=root) as count:
                    self.assertEqual(count,3)
                    for name in names:self.assertEqual((runtime/name).read_bytes(),(staged/name).read_bytes())
                    raise RuntimeError('probe failure')
            for name in names: self.assertEqual((runtime/name).read_bytes(),b'original '+name.encode())

    def test_multiple_manual_images_are_removed_after_failed_probe(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);runtime=root/'bin/models/items/bucket';stage=root/'stage';stage.mkdir();runtime.mkdir(parents=True)
            (runtime/'model.gltf').write_bytes(b'original model');(runtime/'model.bin').write_bytes(b'original buffer')
            (stage/'model.gltf').write_text(json.dumps(dict(buffers=[dict(uri='model.bin')],images=[dict(uri='manual/a.png'),dict(uri='manual/b.png')])))
            (stage/'model.bin').write_bytes(b'new');(stage/'manual').mkdir()
            for name in ['a.png','b.png']:(stage/'manual'/name).write_bytes(b'image')
            with self.assertRaisesRegex(RuntimeError,'interrupted'):
                with runtime_designs({'bucket':stage},root=root) as count:
                    self.assertEqual(count,4);self.assertTrue((runtime/'manual/b.png').is_file())
                    raise RuntimeError('interrupted')
            self.assertEqual((runtime/'model.gltf').read_bytes(),b'original model')
            self.assertEqual((runtime/'model.bin').read_bytes(),b'original buffer')
            self.assertFalse((runtime/'manual').exists())

    def test_incomplete_staging_is_rejected_before_substitution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); runtime=root/'bin/models/items/bucket'; staged=root/'stage'
            runtime.mkdir(parents=True);staged.mkdir()
            for name in ('model.gltf','model.bin'): (runtime/name).write_bytes(b'original')
            (staged/'model.gltf').write_text(json.dumps(dict(buffers=[dict(uri='model.bin')],images=[])))
            with self.assertRaisesRegex(ValueError,'Missing or escaping'):
                with runtime_designs({'bucket':staged},root=root): self.fail('entered incomplete probe')
            self.assertEqual((runtime/'model.gltf').read_bytes(),b'original')


if __name__=='__main__': unittest.main()
