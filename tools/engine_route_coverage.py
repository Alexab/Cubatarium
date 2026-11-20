"""Reconcile retained native route evidence without rerunning the game."""
import argparse
import hashlib
import json
from pathlib import Path

from engine_route_path_evidence import evaluate as evaluate_path
from engine_round_trip_evidence import evaluate as evaluate_return
from engine_smoke_run import inspection_evidence
from engine_smoke_verdict import classify

KINDS = {"frontier", "out_and_back", "dry_cave", "wet_cave"}


def assess(kind: str, artifacts: Path) -> dict:
    paths = [artifacts/name for name in ("route.json", "bot-route.json", "evidence-report.json",
                                         "diagnostic_bot.jsonl", "smoke-verdict.json")]
    if kind in {"dry_cave", "wet_cave"}:
        paths.append(artifacts/"fixture.json")
    manifest, route, report = [json.loads(p.read_text(encoding="utf-8")) for p in paths[:3]]
    if hashlib.sha256(paths[1].read_bytes()).hexdigest() != manifest["bot_route_sha256"]:
        raise ValueError("authored route hash changed")
    scene = json.loads(paths[-1].read_text(encoding="utf-8")) if kind.endswith("cave") else None
    if scene is not None and scene.get("expected_eye_in_fluid") is not (kind == "wet_cave"):
        raise ValueError("wrong cave fixture")
    minimum = manifest["minimum_chunks"]
    if type(minimum) is not int or minimum <= 0:
        raise ValueError("invalid native route minimum")
    path = evaluate_path(paths[3], report, route, minimum_chunks=minimum, scene=scene)
    if kind == "out_and_back":
        lifecycle = evaluate_return(artifacts, report, route, minimum_chunks=minimum)
        paths.append(artifacts/"round-trip-residency.log")
    else:
        lifecycle = classify(report, None, minimum_chunks=minimum)["components"]["route"]
    inspection = inspection_evidence(artifacts, report)
    native = (type(report.get("exit_code")) is int and report["exit_code"] == 0 and
              type(report.get("ingame_frames")) is int and report["ingame_frames"] > 0 and
              not report.get("hang_killed"))
    complete = (type(inspection.get("requested")) is int and inspection["requested"] >= 24 and
                inspection.get("verified") == inspection["requested"])
    passed = native and complete and path["status"] == lifecycle["status"] == "PASS"
    return dict(kind=kind, status="PASS" if passed else "FAIL", artifacts=str(artifacts),
                recorded_exe_sha256=manifest["exe_sha256"], recorded_git_sha=manifest["git_sha"],
                native_process_pass=native, path=path, lifecycle=lifecycle, inspection=inspection,
                original_overall_status=json.loads((artifacts/"smoke-verdict.json").read_text(encoding="utf-8"))["status"],
                inputs=[dict(path=str(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in paths])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    matrix = json.loads(args.matrix.read_text(encoding="utf-8"))
    cases = matrix["cases"]
    if (matrix.get("schema") != "engine-route-coverage-input.v1" or len(cases) != 4 or
            {c["kind"] for c in cases} != KINDS):
        parser.error("exactly four distinct required route kinds are required")
    results = []
    for case in cases:
        try:
            results.append(assess(case["kind"], Path(case["artifacts"])))
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            results.append(dict(kind=case["kind"], status="FAIL", reason=str(error)))
    result = dict(schema="engine-route-coverage.v1",
                  status="PASS" if all(r["status"] == "PASS" for r in results) else "FAIL",
                  scope="Retained native route coverage for each recorded executable; not current-EXE matrix or original overall verdict acceptance",
                  matrix_sha256=hashlib.sha256(args.matrix.read_bytes()).hexdigest(), cases=results)
    with args.output.open("x", encoding="utf-8") as output:
        output.write(json.dumps(result, indent=2)+"\n")
    print(json.dumps(dict(status=result["status"], cases=[dict(kind=r["kind"], status=r["status"]) for r in results])))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
