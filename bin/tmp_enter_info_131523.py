#!/usr/bin/env python3
from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(r"E:/Work/Home/Cubatarium/bin/logs")
INFO = ROOT / "Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260912-131521.29204"


def med(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    xs = sorted(xs)
    n = len(xs)
    return xs[n // 2] if n % 2 else 0.5 * (xs[n // 2 - 1] + xs[n // 2])


def cruise_periods(path: Path):
    rows = [json.loads(l) for l in path.open(encoding="utf-8") if l.strip()]
    per = [r for r in rows if r.get("kind") == "period"]
    cru = [r for r in per if float(r.get("movement_speed") or 0) > 0]
    return per, cru


def timeline(tag: str, path: Path, limit_print: bool = False):
    per, cru = cruise_periods(path)
    print(f"=== mesh_apply_stale timeline {tag} cruise n={len(cru)} ===")
    deltas = []
    for i, r in enumerate(cru):
        d = r.get("mesh_apply_stale_delta")
        if d is not None:
            deltas.append(float(d))
        line = (
            f"[{i:02d}] spd={float(r.get('movement_speed') or 0):7.1f} "
            f"stale={r.get('mesh_apply_stale')} delta={d} "
            f"unfin={r.get('unfinished_visual')} empty={r.get('empty_backlog_n')} "
            f"ready={r.get('column_job_render_ready_n')} mesh={r.get('column_job_meshing_n')} "
            f"vb={r.get('visible_black_focus_n')} holes={r.get('visual_holes')} "
            f"wall={float(r.get('wall_ms') or 0):.1f} emerge={float(r.get('mesh_emerge_ms') or 0):.2f} "
            f"pol={float(r.get('prep_schedule_policy_ms') or 0):.3f} "
            f"ddl={r.get('frame_deadline_remaining_ms')} "
            f"cull={r.get('cull_stats_sync_read_n')} pub={r.get('publication_progress_unit_n')} "
            f"repair={r.get('visible_black_fully_dark_repair_n')} "
            f"census={r.get('visible_black_census_mismatch')}"
        )
        if not limit_print or i < 8 or i >= len(cru) - 4:
            print(line)
    print(
        f"  delta med={med(deltas)} max={max(deltas) if deltas else None} "
        f"stale med={med([float(r.get('mesh_apply_stale') or 0) for r in cru])}"
    )


def enter_summary(name: str):
    rows = [json.loads(l) for l in (ROOT / name).open(encoding="utf-8") if l.strip()]
    reasons = Counter((r.get("settle_reason") or "(empty)") for r in rows)
    ends = Counter(r.get("gate_end") for r in rows)
    last = rows[-1]
    max_elapsed = max(float(r.get("elapsed_ms") or 0) for r in rows)
    max_gate = max(float(r.get("gate_elapsed_ms") or 0) for r in rows)
    fp = [float(r.get("first_presentable_ms") or -1) for r in rows]
    fp_ok = [x for x in fp if x >= 0]
    print(f"=== ENTER {name} ===")
    print(f"  n={len(rows)} max_elapsed={max_elapsed:.1f} max_gate={max_gate:.1f}")
    print(f"  settle_reason={reasons.most_common(8)}")
    print(f"  gate_end={dict(ends)}")
    print(f"  first_presentable set={bool(fp_ok)} min={min(fp_ok) if fp_ok else None}")
    print(
        "  last:",
        {
            "kind": last.get("kind") or last.get("event"),
            "gate_end": last.get("gate_end"),
            "settle_reason": last.get("settle_reason"),
            "mesh_missing_greedy": last.get("mesh_missing_greedy"),
            "visibility_debt": last.get("visibility_debt"),
            "gate_done_n": last.get("gate_done_n"),
            "ring_not_ready": last.get("ring_not_ready"),
            "ring_blocker": last.get("ring_blocker"),
            "streaming_frozen": last.get("streaming_frozen"),
            "spawn_mesh_ring_ready": last.get("spawn_mesh_ring_ready"),
            "ttf_correct_proxy_ms": last.get("ttf_correct_proxy_ms"),
            "first_presentable_ms": last.get("first_presentable_ms"),
        },
    )
    print("  gate_miss:", {k: last.get(k) for k in last if k.startswith("gate_miss")})


def scan_info():
    pats = {
        "settle_reason": re.compile(r"settle_reason", re.I),
        "force_ingame": re.compile(r"force_ingame", re.I),
        "enter_lit": re.compile(r"enter_lit|EnterWarmup|EnterLit", re.I),
        "timeout": re.compile(r"timeout", re.I),
        "fence": re.compile(r"fence", re.I),
        "CORRECTNESS": re.compile(r"CORRECTNESS", re.I),
        "ERROR": re.compile(r"\bE\d{8}| ERROR |\[ERROR\]|Error:", re.I),
        "FATAL": re.compile(r"FATAL|F\d{8}", re.I),
        "census": re.compile(r"census", re.I),
    }
    counts = Counter()
    samples = {k: [] for k in pats}
    # glog lines often start with I/W/E/F
    severity = Counter()
    with INFO.open(encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f, 1):
            if line[:1] in "IWEF":
                severity[line[0]] += 1
            for name, rx in pats.items():
                if rx.search(line):
                    counts[name] += 1
                    if len(samples[name]) < 8:
                        samples[name].append((i, line.rstrip()[:240]))
    print("=== INFO severity counts (first char) ===", dict(severity))
    print("=== INFO keyword hits ===", dict(counts))
    for name, rows in samples.items():
        if not rows:
            continue
        print(f"-- samples {name} --")
        for ln, text in rows:
            print(f"  L{ln}: {text}")


def find_reports():
    hits = []
    for base in [ROOT, Path(r"E:/Work/Home/Cubatarium/bin/suite_reports")]:
        if not base.exists():
            continue
        for f in base.rglob("*"):
            if f.is_file() and ("131523" in f.name or "29204" in f.name):
                hits.append(str(f))
    print("=== report hits ===", hits)


def delta_medians():
    print("=== mesh_apply_stale_delta medians ===")
    for tag, name in [
        ("131523", "perf_20260912-131523_29204.jsonl"),
        ("090306", "perf_20260912-090306_32784.jsonl"),
        ("141350", "perf_20260910-141350_27632.jsonl"),
        ("174657", "perf_20260910-174657_26996.jsonl"),
    ]:
        _, cru = cruise_periods(ROOT / name)
        vals = [float(r["mesh_apply_stale_delta"]) for r in cru if r.get("mesh_apply_stale_delta") is not None]
        stale = [float(r["mesh_apply_stale"]) for r in cru if r.get("mesh_apply_stale") is not None]
        print(
            f"{tag}: delta_n={len(vals)} delta_med={med(vals)} delta_max={max(vals) if vals else None} "
            f"stale_med={med(stale)} stale_max={max(stale) if stale else None}"
        )


if __name__ == "__main__":
    timeline("131523", ROOT / "perf_20260912-131523_29204.jsonl")
    timeline("090306", ROOT / "perf_20260912-090306_32784.jsonl", limit_print=True)
    delta_medians()
    enter_summary("enter_lit_20260912-132402.jsonl")
    enter_summary("enter_lit_20260912-132132.jsonl")
    scan_info()
    find_reports()
