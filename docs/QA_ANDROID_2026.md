# QA Android 2026

Manual + automated QA matrix for Android UX blockers (`TD-AUD-028..031`).

## Automated coverage links

- Layout breakpoints: [`DockedOverlayLayoutTest.cpp`](../src/Test/DockedOverlayLayoutTest.cpp)
- Touch pointer lifecycle: [`TouchInputBridgeLifecycleTest.cpp`](../src/Test/TouchInputBridgeLifecycleTest.cpp)
- Icon cache invalidation: [`InventoryIconFingerprintTest.cpp`](../src/Test/InventoryIconFingerprintTest.cpp)
- Back routing entry point: `AKEYCODE_BACK -> GLFW_KEY_ESCAPE` in [`AndroidPlatformWindow.cpp`](../src/App/Platform/AndroidPlatformWindow.cpp), state machine in [`InputRouter.cpp`](../src/App/Input/InputRouter.cpp)

## Device profile matrix

| Profile | Resolution | DPI | Notes |
|---------|------------|-----|-------|
| A — phone narrow | 720×1280 portrait | ~320 | Single-pane inventory expected |
| B — phone wide | 1080×2400 portrait | ~420 | Controls must stay above preview |
| C — tablet | 1600×2560 portrait | ~320 | Dual-pane allowed when tall enough |
| D — low RAM | 720×1280 | ~280 | Cold start / world load ANR watch |
| E — landscape | 1280×720 landscape | ~320 | Joystick + look pad multitouch |

## Manual QA — inventory layout (TD-AUD-028)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-01 | Open creative palette on profile A | [ ] | [ ] | Close/menu buttons visible and tappable |
| AND-02 | Open inventory on profile B | [ ] | [ ] | No overlap with top-right HUD controls |
| AND-03 | Open worldgen palette on profile C | [ ] | [ ] | Preview hidden or non-overlapping on narrow width |
| AND-04 | Rotate to landscape (profile E) | [ ] | [ ] | Overlay remains operable |

## Manual QA — Back flow (TD-AUD-029)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-05 | Back in inventory | [ ] | [ ] | Closes inventory overlay |
| AND-06 | Back in-world (no overlays) | [ ] | [ ] | Opens main menu |
| AND-07 | Back in main menu | [ ] | [ ] | Shows quit confirmation |
| AND-08 | Back again on quit dialog | [ ] | [ ] | Dismisses confirmation |

## Manual QA — joystick lifecycle (TD-AUD-030)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-09 | Drag joystick, release inside zone | [ ] | [ ] | Movement stops, knob recenters |
| AND-10 | Drag joystick, release outside zone | [ ] | [ ] | No sticky movement |
| AND-11 | Joystick + second finger on look pad | [ ] | [ ] | Independent pointers |
| AND-12 | System gesture cancel / focus loss | [ ] | [ ] | Joystick resets to neutral |
| AND-16 | Joystick + Jump simultaneous | [ ] | [ ] | Movement continues while jumping; auto: `touch_input_bridge_lifecycle_test` |

## Manual QA — fluid surface (TD-FL-034 Android)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-17 | Sea surface visible from shore | [ ] | [ ] | Water film over ocean; requires EGL stencil |

## Manual QA — startup / load (TD-AUD-031)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-13 | Cold start to main menu | [ ] | [ ] | No transient ANR dialog |
| AND-14 | Load existing world | [ ] | [ ] | First interactive frame < 5s on profile D |
| AND-15 | Create new world | [ ] | [ ] | No freeze dialog near generation end |

## Sign-off gate

Close `TD-AUD-028..031` only when:

1. All automated tests above pass in CI/desktop-linux build.
2. Manual matrix has no FAIL on profiles A–E.
3. Evidence recorded in `docs/TECH_DEBT_AUDIT.md` execution progress section.
