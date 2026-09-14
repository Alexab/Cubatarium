#!/usr/bin/env python3
"""Unit tests for Q9 drawable_ok fail-closed (audit N08)."""
from __future__ import annotations

import q9_acceptance_suite as q9


def _run(**metric_overrides):
    m = {
        "mesh_apply_stale_med": 2.0,
        "unfinished_visual_med": 0.0,
        "visual_holes_med": 0.0,
    }
    m.update(metric_overrides)
    return {"metrics": m}


def test_drawable_ok_clean():
    assert q9.drawable_ok(_run()) is True


def test_drawable_ok_rejects_unbounded_stale():
    assert q9.drawable_ok(_run(mesh_apply_stale_med=999999)) is False


def test_drawable_ok_requires_unfinished_and_holes():
    assert q9.drawable_ok({"metrics": {"mesh_apply_stale_med": 1.0}}) is False
    assert (
        q9.drawable_ok(
            {
                "metrics": {
                    "mesh_apply_stale_med": 1.0,
                    "unfinished_visual_med": 0.0,
                }
            }
        )
        is False
    )


def test_drawable_ok_rejects_diagnostic():
    r = _run()
    r["diagnostic"] = True
    assert q9.drawable_ok(r) is False


if __name__ == "__main__":
    test_drawable_ok_clean()
    test_drawable_ok_rejects_unbounded_stale()
    test_drawable_ok_requires_unfinished_and_holes()
    test_drawable_ok_rejects_diagnostic()
    print("OK test_q9_acceptance_suite")
