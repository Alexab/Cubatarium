#!/usr/bin/env python3
"""Decision-matrix smoke for Classic vs Cubatarium block input (no GL)."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto


class Scheme(Enum):
    CLASSIC = auto()
    CUBATARIUM = auto()


class SlotKind(Enum):
    EMPTY = auto()
    BLOCK = auto()
    CREATURE = auto()


class Action(Enum):
    NONE = auto()
    BREAK_START = auto()
    BREAK_CANCEL = auto()
    USE_SLOT = auto()


@dataclass
class UiTiming:
    place_max: float = 0.20
    break_min: float = 0.50


def classic_lmb_release(hold: float, had_break: bool) -> Action:
    if had_break:
        return Action.BREAK_CANCEL
    return Action.NONE


def classic_lmb_press(looking_at_block: bool) -> Action:
    return Action.BREAK_START if looking_at_block else Action.NONE


def classic_rmb_release(slot: SlotKind) -> Action:
    if slot == SlotKind.EMPTY:
        return Action.NONE
    return Action.USE_SLOT


def cubatarium_lmb_release(hold: float, ui: UiTiming, had_break: bool) -> Action:
    if hold < ui.place_max:
        return Action.USE_SLOT
    if hold < ui.break_min:
        return Action.BREAK_CANCEL if had_break else Action.NONE
    return Action.BREAK_START if not had_break else Action.NONE


def cubatarium_lmb_tick_hold(hold: float, ui: UiTiming) -> Action:
    if hold >= ui.break_min:
        return Action.BREAK_START
    return Action.NONE


def cubatarium_rmb_release() -> Action:
    return Action.NONE


def check_classic_creature_slot() -> None:
    ui = UiTiming()
    assert classic_lmb_press(True) == Action.BREAK_START
    assert classic_lmb_release(0.8, True) == Action.BREAK_CANCEL
    assert classic_rmb_release(SlotKind.CREATURE) == Action.USE_SLOT
    assert classic_rmb_release(SlotKind.BLOCK) == Action.USE_SLOT
    # LMB must not use slot on release.
    assert classic_lmb_release(0.1, False) == Action.NONE
    print("OK Classic: LMB break + RMB use slot (creature/block independent)")


def check_cubatarium_creature_slot() -> None:
    ui = UiTiming()
    assert cubatarium_lmb_release(0.1, ui, False) == Action.USE_SLOT
    assert cubatarium_lmb_release(0.35, ui, False) == Action.NONE
    assert cubatarium_lmb_tick_hold(0.55, ui) == Action.BREAK_START
    assert cubatarium_rmb_release() == Action.NONE
    # Short tap must not start break.
    assert cubatarium_lmb_release(0.1, ui, False) != Action.BREAK_START
    print("OK Cubatarium: LMB tap use slot, hold break, RMB look only")


def check_hotbar_independent_break() -> None:
    """Breaking must not depend on hotbar slot kind."""
    for slot in (SlotKind.BLOCK, SlotKind.CREATURE, SlotKind.EMPTY):
        _ = slot
        assert classic_lmb_press(True) == Action.BREAK_START
        assert cubatarium_lmb_tick_hold(0.6, UiTiming()) == Action.BREAK_START
    print("OK break independent of hotbar slot kind")


def main() -> None:
    print("=== control scheme smoke ===")
    check_classic_creature_slot()
    check_cubatarium_creature_slot()
    check_hotbar_independent_break()
    print("=== all control scheme smoke checks passed ===")


if __name__ == "__main__":
    main()
