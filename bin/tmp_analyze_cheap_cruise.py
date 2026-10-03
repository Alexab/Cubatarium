#!/usr/bin/env python3
"""One-shot analysis: post-CheapRemesh cold cruise perf logs."""
from __future__ import annotations

import json
import statistics as stats
from collections import Counter, defaultdict
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")
FILES = [
    ("primary_220205", LOGS / "perf_20260821-220205_23988.jsonl"),
    ("cmp_214657", LOGS / "perf_20260821-214657_26228.jsonl"),
    ("baseline_195128", LOGS / "perf_20260821-195128_18004.jsonl"),
]


def spike_dominant_bucket(row: dict) -> str:
    candidates = {
        "fluid_map": float(row.get("fluid_map_cpu_ms") or 0),
        "world_extra": float(row.get("world_extra_ms") or 0),
        "stream": float(row.get("stream_ms") or 0),
        "relight": float(row.get("relight_drain_ms") or 0),
        "emerge": float(row.get("mesh_emerge_ms") or 0),
        "tick_env": float(row.get("tick_env_ms") or 0),
        "block_input": float(row.get("block_input_ms") or 0),
    }
    best_name, best_val = "other", 0.0
    for name, val in candidates.items():
        if val > best_val:
            best_val, best_name = val, name
    wall = float(row.get("wall_ms") or 0)
    if best_val <= 0.0 or (wall > 0 and best_val < 0.15 * wall):
        return "other"
    return best_name


def med(xs):
    xs = [x for x in xs if x is not None]
    return stats.median(xs) if xs else None


def p90(xs):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def flips(seq):
    n = 0
    prev = None
    for v in seq:
        if prev is not None and v != prev:
            n += 1
        prev = v
    return n


def corr(xs, ys):
    n = len(xs)
    if n < 3:
        return None
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    dx = sum((x - mx) ** 2 for x in xs) ** 0.5
    dy = sum((y - my) ** 2 for y in ys) ** 0.5
    if dx == 0 or dy == 0:
        return 0.0
    return num / (dx * dy)


def load(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            o = json.loads(line)
        except json.JSONDecodeError:
            continue
        if o.get("kind") in ("spike", "period"):
            rows.append(o)
    return rows


def f3(v, nd=1):
    if v is None:
        return "-"
    return f"{v:.{nd}f}"


def analyze(label: str, path: Path):
    rows = load(path)
    spikes = [r for r in rows if r.get("kind") == "spike"]
    periods = [r for r in rows if r.get("kind") == "period"]
    src = spikes if spikes else periods
    kind = "spike" if src is spikes else "period"

    def col(key, rs=None):
        rs = rs if rs is not None else src
        return [float(r.get(key) or 0) for r in rs]

    timing = {
        "wall": "wall_ms",
        "sim": "sim_ms",
        "stream": "stream_ms",
        "emerge": "mesh_emerge_ms",
        "apply": "relight_apply_ms",
        "capture": "relight_capture_ms",
        "dirty_tick": "mesh_dirty_tick_ms",
    }
    counts = {
        "dirty_n": "dirty_n",
        "revisit": "dirty_revisit_same_n",
        "PL": "pending_light_focus",
        "VB": "visible_black_focus_n",
        "unlit_h": "chunk_meshed_unlit_hidden",
        "apply_n": "relight_apply_n",
    }

    out = {
        "label": label,
        "file": path.name,
        "n_spike": len(spikes),
        "n_period": len(periods),
        "used": kind,
    }
    for name, key in timing.items():
        xs = col(key)
        out[f"{name}_med"] = med(xs)
        out[f"{name}_p90"] = p90(xs)
        out[f"{name}_max"] = max(xs) if xs else None
    for name, key in counts.items():
        xs = col(key)
        out[f"{name}_med"] = med(xs)
        out[f"{name}_p90"] = p90(xs)
        out[f"{name}_max"] = max(xs) if xs else None

    uf = [int(r.get("underfeet_opaque_present") or 0) for r in periods]
    out["uf_flips"] = flips(uf)
    out["uf_rate"] = flips(uf) / max(1, len(periods) - 1) if periods else 0.0

    buckets = Counter(spike_dominant_bucket(r) for r in spikes)
    out["dom_class"] = buckets.most_common(1)[0][0] if buckets else None
    out["class_counts"] = dict(buckets.most_common())

    half = max(1, len(src) // 2)
    for half_name, hs in (("early", src[:half]), ("late", src[half:])):
        for name, key in (
            ("wall", "wall_ms"),
            ("stream", "stream_ms"),
            ("PL", "pending_light_focus"),
        ):
            xs = [float(r.get(key) or 0) for r in hs]
            out[f"{half_name}_{name}_med"] = med(xs)
            out[f"{half_name}_{name}_p90"] = p90(xs)

    stream = col("stream_ms", spikes)
    dirty = col("dirty_n", spikes)
    pl = col("pending_light_focus", spikes)
    apply = col("relight_apply_ms", spikes)
    emerge = col("mesh_emerge_ms", spikes)
    wall = col("wall_ms", spikes)
    out["corr_stream_dirty"] = corr(stream, dirty)
    out["corr_stream_PL"] = corr(stream, pl)
    out["corr_stream_apply"] = corr(stream, apply)
    out["corr_stream_emerge"] = corr(stream, emerge)
    out["corr_stream_wall"] = corr(stream, wall)

    # High-stream cohort vs rest
    if stream:
        thr = p90(stream)
        hi = [r for r in spikes if float(r.get("stream_ms") or 0) >= thr]
        lo = [r for r in spikes if float(r.get("stream_ms") or 0) < thr]
        out["hi_stream_n"] = len(hi)
        out["hi_stream_thr"] = thr
        for tag, rs in (("hi", hi), ("lo", lo)):
            if not rs:
                continue
            out[f"{tag}_dirty_med"] = med([float(r.get("dirty_n") or 0) for r in rs])
            out[f"{tag}_PL_med"] = med(
                [float(r.get("pending_light_focus") or 0) for r in rs]
            )
            out[f"{tag}_apply_med"] = med(
                [float(r.get("relight_apply_ms") or 0) for r in rs]
            )
            out[f"{tag}_wall_med"] = med([float(r.get("wall_ms") or 0) for r in rs])

    contribs = defaultdict(list)
    part_keys = [
        "mesh_emerge_ms",
        "stream_ms",
        "relight_apply_ms",
        "relight_capture_ms",
        "fluid_map_cpu_ms",
        "prepare_frame_ms",
        "world_streaming_phase_ms",
        "mesh_dirty_tick_ms",
        "relight_drain_ms",
        "app_update_ms",
        "world_extra_ms",
        "render_total_ms",
        "mesh_emerge_prep_other_ms",
    ]
    for r in spikes:
        w = float(r.get("wall_ms") or 0)
        if w < 50:
            continue
        for k in part_keys:
            v = float(r.get(k) or 0)
            contribs[k].append((v, w, v / w if w else 0))
    ranked = []
    for k, vals in contribs.items():
        ms = [v[0] for v in vals]
        fr = [v[2] for v in vals]
        ranked.append((med(ms) or 0, p90(ms) or 0, med(fr) or 0, k))
    ranked.sort(reverse=True)
    out["top_drivers"] = ranked[:8]

    wall_by_class = defaultdict(float)
    for r in spikes:
        b = spike_dominant_bucket(r)
        wall_by_class[b] += float(r.get("wall_ms") or 0)
    out["wall_sum_by_class"] = dict(
        sorted(wall_by_class.items(), key=lambda kv: -kv[1])
    )

    # Sample keys peek on first spike
    if spikes:
        s = spikes[0]
        needles = (
            "mesh",
            "stream",
            "softdefer",
            "emerge",
            "dirty",
            "apply",
            "capture",
            "wall",
            "sim",
        )
        sample_keys = sorted(
            k for k in s if any(n in k.lower() for n in needles)
        )
        out["sample_key_count"] = len(sample_keys)
        out["sample_keys_preview"] = sample_keys[:60]
        out["sample_values"] = {
            k: s.get(k)
            for k in (
                "wall_ms",
                "sim_ms",
                "stream_ms",
                "mesh_emerge_ms",
                "mesh_emerge_prep_ms",
                "mesh_emerge_prep_other_ms",
                "mesh_dirty_tick_ms",
                "relight_apply_ms",
                "relight_capture_ms",
                "dirty_n",
                "dirty_revisit_same_n",
                "pending_light_focus",
                "visible_black_focus_n",
                "chunk_meshed_unlit_hidden",
                "softdefer_held_n",
                "softdefer_capture_budget",
                "prep_softdefer_setup_ms",
                "stream_loads",
                "stream_gen_commit_n",
                "world_streaming_phase_ms",
            )
            if k in s
        }

    return out


def main():
    results = [analyze(label, path) for label, path in FILES]

    print("=== TABLE (spike rows preferred) ===")
    hdr = (
        "run | n_sp | wall med/p90 | stream med/p90 | emerge med/p90 | "
        "apply med/p90 | capture med/p90 | dirty_tick med/p90 | "
        "dirty_n med/p90 | revisit med | PL med/p90 | VB med/max | "
        "unlit_h med | uf_flips | dom_class"
    )
    print(hdr)
    for o in results:
        print(
            f"{o['label']} | {o['n_spike']} | "
            f"{f3(o['wall_med'])}/{f3(o['wall_p90'])} | "
            f"{f3(o['stream_med'])}/{f3(o['stream_p90'])} | "
            f"{f3(o['emerge_med'])}/{f3(o['emerge_p90'])} | "
            f"{f3(o['apply_med'],2)}/{f3(o['apply_p90'],2)} | "
            f"{f3(o['capture_med'],3)}/{f3(o['capture_p90'],3)} | "
            f"{f3(o['dirty_tick_med'],3)}/{f3(o['dirty_tick_p90'],3)} | "
            f"{f3(o['dirty_n_med'],0)}/{f3(o['dirty_n_p90'],0)} | "
            f"{f3(o['revisit_med'],0)} | "
            f"{f3(o['PL_med'],0)}/{f3(o['PL_p90'],0)} | "
            f"{f3(o['VB_med'],0)}/{f3(o['VB_max'],0)} | "
            f"{f3(o['unlit_h_med'],0)} | "
            f"{o['uf_flips']} | {o['dom_class']}"
        )

    print("\n=== SIM med/p90 ===")
    for o in results:
        print(f"  {o['label']}: {f3(o['sim_med'])}/{f3(o['sim_p90'])}")

    print("\n=== EARLY vs LATE (spike half) wall/stream/PL med ===")
    for o in results:
        print(
            f"  {o['label']}: early {f3(o['early_wall_med'])}/{f3(o['early_stream_med'])}/{f3(o['early_PL_med'],0)}"
            f"  late {f3(o['late_wall_med'])}/{f3(o['late_stream_med'])}/{f3(o['late_PL_med'],0)}"
        )

    print("\n=== CLASS COUNTS / WALL SUM ===")
    for o in results:
        print(f"  {o['label']}: counts={o['class_counts']}")
        print(f"           wall_sum={o['wall_sum_by_class']}")

    print("\n=== CORR stream vs dirty/PL/apply/emerge/wall ===")
    for o in results:
        print(
            f"  {o['label']}: "
            f"dirty={f3(o['corr_stream_dirty'],2)} "
            f"PL={f3(o['corr_stream_PL'],2)} "
            f"apply={f3(o['corr_stream_apply'],2)} "
            f"emerge={f3(o['corr_stream_emerge'],2)} "
            f"wall={f3(o['corr_stream_wall'],2)}"
        )
        if "hi_stream_thr" in o:
            print(
                f"    hi_stream(>=p90={f3(o['hi_stream_thr'])}, n={o.get('hi_stream_n')}): "
                f"dirty={f3(o.get('hi_dirty_med'),0)} PL={f3(o.get('hi_PL_med'),0)} "
                f"apply={f3(o.get('hi_apply_med'),2)} wall={f3(o.get('hi_wall_med'))} | "
                f"lo: dirty={f3(o.get('lo_dirty_med'),0)} PL={f3(o.get('lo_PL_med'),0)} "
                f"apply={f3(o.get('lo_apply_med'),2)} wall={f3(o.get('lo_wall_med'))}"
            )

    print("\n=== TOP DRIVERS (primary) ===")
    for o in results:
        if o["label"] != "primary_220205":
            continue
        for med_ms, p90_ms, frac, k in o["top_drivers"][:6]:
            print(f"  {k}: med={med_ms:.1f} p90={p90_ms:.1f} frac_of_wall={frac:.2f}")

    print("\n=== SAMPLE SPIKE KEYS (mesh/stream/softdefer) ===")
    o0 = results[0]
    print(f"  count={o0.get('sample_key_count')} preview={o0.get('sample_keys_preview')}")
    print("  values:")
    for k, v in (o0.get("sample_values") or {}).items():
        print(f"    {k}={v}")


if __name__ == "__main__":
    raise SystemExit(main())
