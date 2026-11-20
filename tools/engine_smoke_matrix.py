"""Sequential, non-overwriting execution of a versioned engine smoke matrix."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from engine_smoke_run import run


def write_index(path, value):
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def execute(matrix, output, world, runner=run):
    if matrix.get("schema") != "engine-smoke-matrix.v1":
        raise ValueError("Invalid matrix schema")
    cases = matrix.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError("Matrix needs cases")
    names = set()
    for case in cases:
        name = case.get("id", "")
        if (not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,63}", name) or
                name in names or not isinstance(case.get("options"), dict)):
            raise ValueError("Invalid or duplicate matrix case")
        names.add(name)
        if {"world", "artifacts"} & case["options"].keys():
            raise ValueError("Matrix cannot override isolated artifact ownership")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    payload = json.dumps(matrix, sort_keys=True).encode()
    index = dict(schema="engine-smoke-matrix-index.v1", status="IN_PROGRESS",
                 matrix_sha256=hashlib.sha256(payload).hexdigest(), world=world,
                 scope=matrix.get("scope"), expected_cases=len(cases), cases=[])
    write_index(output / "matrix.json", matrix)
    write_index(output / "index.json", index)
    try:
        for case in cases:
            destination = output / case["id"]
            try:
                verdict = runner(world, destination, **case["options"])
                path = destination / "smoke-verdict.json"
                if not path.is_file() or json.loads(path.read_text(encoding="utf-8")) != verdict:
                    raise ValueError("Missing or mismatched persisted verdict")
                if verdict.get("status") not in ("PASS", "FAIL", "UNTESTED"):
                    raise ValueError("Invalid case verdict")
                row = dict(id=case["id"], status=verdict["status"],
                           verdict=str(path.resolve()),
                           sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            except Exception as error:
                row = dict(id=case["id"], status="FAIL", error=str(error))
            index["cases"].append(row)
            write_index(output / "index.json", index)
    finally:
        statuses = {row["status"] for row in index["cases"]}
        index["status"] = ("FAIL" if "FAIL" in statuses else "UNTESTED"
                           if len(index["cases"]) != len(cases) or "UNTESTED" in statuses else "PASS")
        write_index(output / "index.json", index)
    return index


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--world", required=True)
    args = parser.parse_args()
    from engine_build_provenance import inspect_build
    root = Path(__file__).resolve().parents[1]
    provenance = inspect_build(root, root / "bin/Cubatarium.exe")
    if provenance["status"] != "PASS":
        parser.error("Matrix requires a matching Release build record; run tools/release_build_with_provenance.py")
    result = execute(json.loads(args.matrix.read_text(encoding="utf-8")),
                     args.artifacts.resolve(), args.world)
    print(json.dumps(result))
    raise SystemExit(0 if result["status"] == "PASS" else 1 if result["status"] == "FAIL" else 2)
