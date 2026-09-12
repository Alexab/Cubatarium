#!/usr/bin/env python3
"""Unit tests for Phase57 scorecard fail-closed verdicts (M00 / A13 / C01 Q0)."""
from __future__ import annotations

import AnalyzePhase57Scorecard as s
from unittest.mock import Mock


def _full_period(**overrides):
    row = {
        "kind": "period",
        "wall_ms": 100.0,
        "movement_speed": 3.0,
        "unfinished_visual": 0,
        "visual_holes": 0,
        "visible_black_focus_n": 0,
        "mesh_apply_stale": 0,
        "focus_missing_mesh": 0,
    }
    row.update(overrides)
    return row


def test_missing_logs_are_invalid_not_pass():
    invalid = s.validate_run_inputs(
        {"periods": 1},
        None,
        None,
        require_manifest=False,
    )
    assert "perf_missing" in invalid
    assert "info_missing" in invalid
    assert "report.periods_without_perf" in invalid

    result = s.build_verdict(
        invalid=invalid,
        fidelity_fails=[],
        product_fails=[],
        hard_corr=[],
        hard_perf=[],
    )
    assert result["verdict"] == s.VERDICT_INVALID


def test_empty_evaluate_lists_do_not_imply_pass_without_validation():
    # Historical fail-open: empty lists looked like OK. validate_run_inputs must reject.
    assert s.evaluate_fidelity({"periods": 1}, None, None, None, require_info=False, require_perf=False) == []
    # With require flags, missing inputs are explicit fails (defense in depth).
    assert "perf_missing" in s.evaluate_product(None, None, require_info=True, require_perf=True)


def test_hard_gate_false_enters_verdict():
    corr, perf = s.evaluate_hard_gates(
        {
            "visual_holes_rate_le_0_10": False,
            "wall_ms_fly_le_16_6": False,
            "scene_ms_le_5": True,
        }
    )
    assert any("visual_holes" in x for x in corr)
    assert any("wall_ms_fly" in x for x in perf)
    result = s.build_verdict(
        invalid=[],
        fidelity_fails=[],
        product_fails=[],
        hard_corr=corr,
        hard_perf=perf,
    )
    assert result["verdict"] == s.VERDICT_CORRECTNESS


def test_null_hard_gate_evaluate_empty():
    corr, perf = s.evaluate_hard_gates({"visual_holes_rate_le_0_10": None})
    assert corr == []
    assert perf == []


def test_null_hard_gate_is_invalid():
    invalid = s.validate_run_inputs(
        {"gates": {"visual_holes_rate_le_0_10": None}},
        {"periods": 10, "cruise_n": 5, "schema_ok": True},
        {"settle_count": 1},
        require_manifest=False,
    )
    assert any("hard_gate:" in x and "null" in x for x in invalid)
    result = s.build_verdict(
        invalid=invalid,
        fidelity_fails=[],
        product_fails=[],
        hard_corr=[],
        hard_perf=[],
    )
    assert result["verdict"] == s.VERDICT_INVALID


def test_manifest_required_fields():
    invalid = s.validate_run_inputs(
        {"manifest": {"git_sha": "abc"}},
        {"periods": 10, "cruise_n": 5, "schema_ok": True},
        {"settle_count": 1},
        require_manifest=True,
    )
    assert any(x.startswith("manifest.") for x in invalid)


def test_manifest_required_by_default():
    invalid = s.validate_run_inputs(
        {},
        {"periods": 10, "cruise_n": 5, "schema_ok": True},
        {"settle_count": 1},
    )
    assert any(x.startswith("manifest.") for x in invalid)


def test_insufficient_cruise_is_invalid():
    invalid = s.validate_run_inputs(
        {},
        {"periods": 10, "cruise_n": 1, "schema_ok": True},
        {"settle_count": 1},
        require_manifest=False,
    )
    assert any("cruise_n" in x for x in invalid)


def test_c01_sparse_periods_are_invalid():
    periods = [{"kind": "period", "movement_speed": 3} for _ in range(3)]
    perf = s.analyze_perf_periods(periods)
    assert perf["schema_ok"] is False
    assert perf["schema_errors"]
    invalid = s.validate_run_inputs(
        {},
        perf,
        {"settle_count": 1},
        require_manifest=False,
    )
    assert any("schema" in x for x in invalid)
    result = s.build_verdict(
        invalid=invalid,
        fidelity_fails=[],
        product_fails=[],
        hard_corr=[],
        hard_perf=[],
    )
    assert result["verdict"] == s.VERDICT_INVALID
    assert result["verdict"] != s.VERDICT_PASS


def test_pass_when_complete_and_clean():
    result = s.build_verdict(
        invalid=[],
        fidelity_fails=[],
        product_fails=[],
        hard_corr=[],
        hard_perf=[],
    )
    assert result["verdict"] == s.VERDICT_PASS


def test_stale_storm_is_product_fail_not_wall_greenwash():
    fails = s.evaluate_product(
        {
            "settle_count": 1,
            "soft_force_with_debt": [],
            "bad_live_soft_vis_debt": [],
            "empty_batch_event_n": 0,
        },
        {
            "focus_missing_frac": 0.1,
            "phase_abort_heavy_frac": 0.1,
            "visual_holes_frac": 0.1,
            "empty_backlog_max": 1,
            "mesh_apply_stale_med": 676,
            "wall_med": 48,
        },
        {"wall_med": 141, "mesh_apply_stale_med": 9},
        require_info=True,
        require_perf=True,
    )
    assert any("mesh_apply_stale" in f for f in fails)


def test_forced_enter_cannot_pass_after_debt_clears():
    for debt in (0, 80):
        log = Mock()
        log.read_text.return_value = (
            f"settle_reason=force_ingame_no_uf elapsed_ms=150111 "
            f"ring_ready=1 visibility_debt={debt} underfeet=0\n"
        )
        info = s.analyze_info(log)
        fails = s.evaluate_product(info, {"focus_missing_frac": 0})
        assert any("forced_incomplete_settle" in f for f in fails)
        verdict = s.build_verdict(invalid=[], fidelity_fails=[],
                                  product_fails=fails, hard_corr=[], hard_perf=[])
        assert verdict["verdict"] == s.VERDICT_CORRECTNESS


def test_finite_number_rejects_nan():
    assert s.finite_number(float("nan")) is None
    assert s.finite_number(float("inf")) is None
    assert s.finite_number(42) == 42.0


def test_g_returns_none_not_zero():
    assert s.g({}, "missing_key") is None
    assert s.g({"x": None}, "x") is None
    assert s.g({"x": 0}, "x") == 0


def test_valid_period_schema_passes():
    periods = [_full_period() for _ in range(3)]
    errors = s.validate_period_schema(periods)
    assert errors == []
    perf = s.analyze_perf_periods(periods)
    assert perf["schema_ok"] is True
    assert perf["visual_holes_frac"] == 0.0


if __name__ == "__main__":
    test_missing_logs_are_invalid_not_pass()
    test_empty_evaluate_lists_do_not_imply_pass_without_validation()
    test_hard_gate_false_enters_verdict()
    test_null_hard_gate_evaluate_empty()
    test_null_hard_gate_is_invalid()
    test_manifest_required_fields()
    test_manifest_required_by_default()
    test_insufficient_cruise_is_invalid()
    test_c01_sparse_periods_are_invalid()
    test_pass_when_complete_and_clean()
    test_stale_storm_is_product_fail_not_wall_greenwash()
    test_forced_enter_cannot_pass_after_debt_clears()
    test_finite_number_rejects_nan()
    test_g_returns_none_not_zero()
    test_valid_period_schema_passes()
    print("OK test_AnalyzePhase57Scorecard")
