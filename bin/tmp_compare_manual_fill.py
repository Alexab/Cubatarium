#!/usr/bin/env python3
import json
from pathlib import Path


def load(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return [r for r in rows if r.get("kind") == "period"]


def med(xs):
    if not xs:
        return None
    xs = sorted(xs)
    return xs[len(xs) // 2]


def summarize(label: str, path: Path) -> None:
    ps = load(path)
    if not ps:
        print(f"{label}: no periods")
        return
    dur = len(ps) * 2.0
    c0 = float(ps[0].get("chunk_count") or 0)
    c1 = float(ps[-1].get("chunk_count") or 0)
    op0 = float(ps[0].get("opaque_cmd_on") or 0)
    op1 = float(ps[-1].get("opaque_cmd_on") or 0)
    uf = [float(r.get("unfinished_visual") or 0) for r in ps]
    vb = [float(r.get("visible_black_focus_n") or 0) for r in ps]
    pl = [float(r.get("pending_light_focus") or 0) for r in ps]
    bs = [float(r.get("black_sticky") or 0) for r in ps]
    em = [float(r.get("mesh_emerge_ms") or 0) for r in ps]
    cap = [float(r.get("relight_capture_ms") or 0) for r in ps if float(r.get("relight_capture_ms") or 0) > 0]
    applyf = sum(int(r.get("relight_apply_final_n") or 0) for r in ps)
    applyp = sum(int(r.get("relight_apply_partial_n") or 0) for r in ps)
    fifo0 = float(ps[0].get("relight_fifo_dropped") or 0)
    fifo1 = float(ps[-1].get("relight_fifo_dropped") or 0)
    print(f"=== {label} ({path.name}) ===")
    print(
        f"  duration~{dur:.0f}s periods={len(ps)} "
        f"chunks {c0:.0f}->{c1:.0f} (+{c1 - c0:.0f}, {(c1 - c0) / max(1, dur):.1f}/s)"
    )
    print(f"  opaque_on {op0:.0f}->{op1:.0f} (+{op1 - op0:.0f})")
    print(
        f"  unfinished_visual med/max/end={med(uf):.0f}/{max(uf):.0f}/{uf[-1]:.0f}"
    )
    print(
        f"  visible_black med/max/end={med(vb):.0f}/{max(vb):.0f}/{vb[-1]:.0f}"
    )
    print(
        f"  pending_light_focus med/max/end={med(pl):.0f}/{max(pl):.0f}/{pl[-1]:.0f}"
    )
    print(f"  black_sticky med/max={med(bs):.0f}/{max(bs):.0f}")
    print(f"  mesh_emerge_ms med={med(em):.1f} capture_med={med(cap):.3f}")
    print(
        f"  apply final/partial={applyf}/{applyp} fifo_drop_delta={fifo1 - fifo0:.0f}"
    )
    print()


def main():
    root = Path(__file__).resolve().parent.parent / "bin" / "logs"
    targets = [
        ("NEW v2 112712", root / "perf_20260820-112712_30712.jsonl"),
        ("NEW v2 105912", root / "perf_20260820-105912_13768.jsonl"),
        ("OLD 090543", root / "perf_20260820-090543_26188.jsonl"),
        ("OLD 090824", root / "perf_20260820-090824_29024.jsonl"),
    ]
    for label, path in targets:
        summarize(label, path)


if __name__ == "__main__":
    main()
