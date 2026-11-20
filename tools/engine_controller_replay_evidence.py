"""Invoke the Release controller verifier; never infer replay from a pose trace."""
import hashlib
import json
from pathlib import Path
import subprocess


def evaluate(artifacts: Path, verifier: Path) -> dict:
    def result(status, reason, **values):
        return dict(schema="engine-controller-replay-evidence.v1", status=status,
                    reason=reason, scope="deterministic controller decisions from recorded observations/probes; not deterministic world simulation", **values)

    transcript = artifacts / "diagnostic_bot_replay.jsonl"
    if not transcript.is_file() or not verifier.is_file():
        return result("UNTESTED", "missing_replay_transcript_or_release_verifier")
    try:
        process = subprocess.run([str(verifier.resolve()), str(transcript.resolve())],
                                 capture_output=True, text=True, timeout=30)
        if process.returncode != 0:
            return result("FAIL", "controller_replay_rejected", exit_code=process.returncode,
                          diagnostic=process.stderr[-2000:])
        evidence = json.loads(process.stdout)
        if (evidence.get("status") != "PASS" or
                type(evidence.get("replayed_ticks")) is not int or evidence["replayed_ticks"] <= 0):
            return result("FAIL", "invalid_replay_verifier_result")
        sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        return result("PASS", "recorded_controller_decisions_reproduced",
                      replayed_ticks=evidence["replayed_ticks"],
                      transcript_sha256=sha(transcript), verifier_sha256=sha(verifier))
    except (OSError, ValueError, AttributeError, subprocess.TimeoutExpired) as error:
        return result("FAIL", "controller_replay_verification_failed", diagnostic=str(error))
