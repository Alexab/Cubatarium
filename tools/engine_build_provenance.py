"""Content identity of compilation inputs and the Release executable."""
import hashlib
import json
from pathlib import Path
import subprocess

INPUT_PATHS = ["src", "cmake", "shaders", "tools", "CMakeLists.txt",
               "CMakePresets.json", "vcpkg.json", "vcpkg-configuration.json"]


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_snapshot(root):
    root = Path(root)
    names = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others",
         "--exclude-standard", "--", *INPUT_PATHS], cwd=root)
    files = {}
    for raw in sorted(set(names.split(b"\0")) - {b""}):
        name = raw.decode("utf-8")
        path = root / name
        if path.is_symlink():
            raise ValueError(f"Unresolved build input symlink: {name}")
        files[name] = file_hash(path) if path.is_file() else "MISSING"
    payload = json.dumps(files, sort_keys=True, separators=(",", ":")).encode()
    return {"sha256": hashlib.sha256(payload).hexdigest(), "files": files,
            "coverage": INPUT_PATHS}


def validate_record(record, snapshot, exe):
    if record.get("schema") != "engine-release-build.v1" or record.get("config") != "Release":
        raise ValueError("Missing or invalid Release build provenance")
    if record.get("source") != snapshot:
        raise ValueError("Build inputs differ from recorded Release sources")
    if record.get("exe_sha256") != file_hash(exe):
        raise ValueError("Release executable differs from build record")
    return record


def inspect_build(root, exe):
    root = Path(root)
    path = root / "build/desktop-msvc/release-provenance.json"
    if not path.is_file():
        return {"status": "UNTESTED", "reason": "missing_release_build_record"}
    record = json.loads(path.read_text(encoding="utf-8"))
    validate_record(record, source_snapshot(root), exe)
    return {"status": "PASS", "reason": "source_and_executable_match_build",
            "record_sha256": file_hash(path), "record": record,
            "scope": "Declared source inputs and executable identity; not dependency/compiler parity"}
