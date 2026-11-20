"""Run the required desktop Release build and record its source/EXE identity."""
import json
from pathlib import Path
import subprocess
from engine_build_provenance import file_hash, source_snapshot


def main():
    root = Path(__file__).resolve().parents[1]
    before = source_snapshot(root)
    command = ["cmake", "--build", "build/desktop-msvc", "--config", "Release",
               "--target", "Cubatarium", "--parallel", "8"]
    result = subprocess.run(command, cwd=root)
    if result.returncode:
        return result.returncode
    after = source_snapshot(root)
    if after != before:
        raise RuntimeError("Build inputs changed during Release build; provenance not issued")
    record = {"schema": "engine-release-build.v1", "config": "Release",
              "command": command, "source": after,
              "git_sha": subprocess.check_output(
                  ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
              "exe_sha256": file_hash(root / "bin/Cubatarium.exe")}
    target = root / "build/desktop-msvc/release-provenance.json"
    temporary = target.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    temporary.replace(target)
    print(f"Release provenance: {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
