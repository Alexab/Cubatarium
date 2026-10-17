#!/usr/bin/env python3
import glob
import json
import os
from collections import Counter
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")


def load(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


def med(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    s = sorted(xs)
    return s[len(s) // 2]


def p90(xs):
    xs = sorted(xs)
    if not xs:
        return None
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def flips(xs):
    n = 0
    prev = None
    for v in xs:
        if prev is not None and v != prev:
            n += 1
        prev = v
    return n


def analyze(label: str, path: Path):
    rows = load(path)
    spikes = [r for r in rows if r.get("kind") == "spike"]
    periods = [r for r in rows if r.get("kind") == "period"]
    use = spikes if spikes else periods
    n = len(use)
    dur = n * 2.0
    c0 = float(use[0].get("chunk_count") or 0)
    c1 = float(use[-1].get("chunk_count") or 0)

    def col(k, cast=float):
        out = []
        for r in use:
            if k not in r:
                continue
            try:
                out.append(cast(r[k]))
            except (TypeError, ValueError):
                pass
        return out

    pl = col("pending_light_focus", int)
    dirty = col("dirty_n", int)
    apply_n = col("relight_apply_n", int)
    apply_ms = col("relight_apply_ms")
    emerge = col("mesh_emerge_ms")
    wall = col("wall_ms")
    stream = col("stream_ms")
    unlit = col("chunk_meshed_unlit", int)
    hidden = col("chunk_meshed_unlit_hidden", int)
    vb = col("visible_black_focus_n", int)
    uf = col("underfeet_opaque_present", int)
    unfinished = col("unfinished_visual", int)
    fifo = col("relight_fifo_dropped", int)
    prep = col("mesh_emerge_prep_ms")
    dirty_tick = col("mesh_dirty_drain_ms")
    sched = col("mesh_dirty_schedule_ms")
    gpu = col("mesh_dirty_gpu_ms")
    opaque = col("opaque_cmd_on", int)
    soft_empty = col("soft_defer_empty_n", int)
    if not soft_empty:
        soft_empty = col("softdefer_empty_n", int)

    print(f"=== {label} ({path.name}) ===")
    print(f"  spikes={len(spikes)} periods={len(periods)} ~{dur:.0f}s")
    print(
        f"  chunks {c0:.0f}->{c1:.0f} (+{c1 - c0:.0f}, {(c1 - c0) / max(1.0, dur):.2f}/s)"
    )
    if opaque:
        print(f"  opaque_on {opaque[0]}->{opaque[-1]} (delta {opaque[-1] - opaque[0]})")
    print(
        f"  PL med/p90/max/end={med(pl)}/{p90(pl)}/{max(pl) if pl else None}/{pl[-1] if pl else None}"
    )
    print(f"  dirty med/p90/max={med(dirty)}/{p90(dirty)}/{max(dirty) if dirty else None}")
    print(
        f"  apply_n med/p90/max={med(apply_n)}/{p90(apply_n)}/{max(apply_n) if apply_n else None}"
    )
    if apply_ms:
        print(f"  apply_ms med/p90={med(apply_ms):.2f}/{p90(apply_ms):.2f}")
    if emerge:
        print(f"  emerge_ms med/p90={med(emerge):.1f}/{p90(emerge):.1f}")
    if prep:
        print(
            f"  emerge prep/drain/sched/gpu med="
            f"{med(prep):.1f}/{med(dirty_tick) if dirty_tick else None}/"
            f"{med(sched) if sched else None}/{med(gpu) if gpu else None}"
        )
    if wall:
        print(
            f"  wall_ms med/p90={med(wall):.1f}/{p90(wall):.1f} stream_med={med(stream):.1f}"
        )
    print(
        f"  unlit/hidden med={med(unlit)}/{med(hidden)} "
        f"VB med/max={med(vb)}/{max(vb) if vb else None}"
    )
    print(
        f"  unfinished med/max={med(unfinished)}/{max(unfinished) if unfinished else None}"
    )
    print(
        f"  uf_flips={flips(uf)}/{n} ({flips(uf) / max(1, n):.3f}/row) present_med={med(uf)}"
    )
    if fifo:
        print(f"  fifo_drop_delta={fifo[-1] - fifo[0]}")
    if soft_empty:
        print(f"  soft_defer_empty med/max={med(soft_empty)}/{max(soft_empty)}")

    reasons = Counter(int(r.get("underfeet_reason") or -1) for r in spikes)
    print(f"  underfeet_reason {dict(sorted(reasons.items()))}")
    print(
        f"  underfeet_draw_ok {dict(Counter(int(r.get('underfeet_draw_ok') or -1) for r in spikes))}"
    )
    print(
        f"  underfeet_has_mesh {dict(Counter(int(r.get('underfeet_has_mesh') or -1) for r in spikes))}"
    )
    print(
        f"  underfeet_pending_light "
        f"{dict(Counter(int(r.get('underfeet_pending_light') or -1) for r in spikes))}"
    )
    print()


def timeline(path: Path):
    rows = load(path)
    spikes = [r for r in rows if r.get("kind") == "spike"]
    print("NEW timeline every 12th spike:")
    print("  idx PL an am emerge wall u/h uf op chunks dirty")
    for i in range(0, len(spikes), 12):
        r = spikes[i]
        print(
            f"  {i:3d} PL={r.get('pending_light_focus')} "
            f"an={r.get('relight_apply_n')} "
            f"am={float(r.get('relight_apply_ms') or 0):.1f} "
            f"em={float(r.get('mesh_emerge_ms') or 0):.0f} "
            f"w={float(r.get('wall_ms') or 0):.0f} "
            f"u/h={r.get('chunk_meshed_unlit')}/{r.get('chunk_meshed_unlit_hidden')} "
            f"uf={r.get('underfeet_opaque_present')} "
            f"op={r.get('opaque_cmd_on')} "
            f"ch={r.get('chunk_count')} "
            f"d={r.get('dirty_n')}"
        )
    print()


def main():
    targets = [
        ("NEW manual 123613", LOGS / "perf_20260821-123613_28584.jsonl"),
        ("pre-Flicker 104125", LOGS / "perf_20260821-104125_4072.jsonl"),
    ]
    for name in sorted(glob.glob(str(LOGS / "perf_20260821-093*.jsonl"))):
        targets.append((f"093 {os.path.basename(name)}", Path(name)))
    for label, path in targets:
        if path.exists():
            analyze(label, path)
        else:
            print("MISSING", path)
    timeline(LOGS / "perf_20260821-123613_28584.jsonl")


if __name__ == "__main__":
    main()
