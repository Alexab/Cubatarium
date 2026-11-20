import tempfile
import unittest
from pathlib import Path
import subprocess
import json
from engine_build_provenance import file_hash, source_snapshot, validate_record, inspect_build


class BuildProvenanceTest(unittest.TestCase):
    def test_manifest_loader_rejects_source_and_executable_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            (root / "src").mkdir()
            source = root / "src/main.cpp"
            source.write_text("source")
            exe = root / "app.exe"
            exe.write_bytes(b"release")
            self.assertEqual(inspect_build(root, exe)["status"], "UNTESTED")
            record = dict(schema="engine-release-build.v1", config="Release",
                          source=source_snapshot(root), exe_sha256=file_hash(exe))
            path = root / "build/desktop-msvc/release-provenance.json"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(record), encoding="utf-8")
            self.assertEqual(inspect_build(root, exe)["status"], "PASS")
            source.write_text("changed")
            with self.assertRaisesRegex(ValueError, "Build inputs differ"):
                inspect_build(root, exe)
            source.write_text("source")
            exe.write_bytes(b"different")
            with self.assertRaisesRegex(ValueError, "executable differs"):
                inspect_build(root, exe)

    def test_wip_untracked_deleted_and_executable_mismatches(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            (root / "src").mkdir()
            source = root / "src/main.cpp"
            source.write_text("first")
            subprocess.run(["git", "add", "src/main.cpp"], cwd=root, check=True)
            exe = root / "app.exe"
            exe.write_bytes(b"release")
            snapshot = source_snapshot(root)
            record = dict(schema="engine-release-build.v1", config="Release",
                          source=snapshot, exe_sha256=file_hash(exe))
            validate_record(record, snapshot, exe)
            for mutate, restore in [
                (lambda: source.write_text("WIP"), lambda: source.write_text("first")),
                (lambda: (root / "src/new.cpp").write_text("new"), lambda: (root / "src/new.cpp").unlink()),
                (source.unlink, lambda: source.write_text("first")),
            ]:
                mutate()
                with self.assertRaisesRegex(ValueError, "Build inputs differ"):
                    validate_record(record, source_snapshot(root), exe)
                restore()
            exe.write_bytes(b"other executable")
            with self.assertRaisesRegex(ValueError, "executable differs"):
                validate_record(record, source_snapshot(root), exe)


if __name__ == "__main__":
    unittest.main()
