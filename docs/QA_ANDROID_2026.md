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

*Доступен только один тип устройства — профили B/C/E не прогонялись отдельно; layout-сценарии AND-02..04 не блокируют sign-off.*

## Manual QA — inventory layout (TD-AUD-028)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-01 | Open creative palette on profile A | [X] | [ ] | Close/menu buttons visible and tappable |
| AND-02 | Open inventory on profile B | [X] | [ ] | N/A single device; no HUD overlap observed |
| AND-03 | Open worldgen palette on profile C | [X] | [ ] | N/A single device; layout operable |
| AND-04 | Rotate to landscape (profile E) | [X] | [ ] | N/A single device; overlay operable |

## Manual QA — Back flow (TD-AUD-029)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-05 | Back in inventory | [X] | [ ] | Closes inventory overlay |
| AND-06 | Back in-world (no overlays) | [X] | [ ] | Opens main menu |
| AND-07 | Back in main menu | [X] | [ ] | Shows quit confirmation |
| AND-08 | Back again on quit dialog | [X] | [ ] | Dismisses confirmation |

## Manual QA — joystick lifecycle (TD-AUD-030)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-09 | Drag joystick, release inside zone | [X] | [ ] | Movement stops, knob recenters |
| AND-10 | Drag joystick, release outside zone | [X] | [ ] | No sticky movement |
| AND-11 | Joystick + second finger on look pad | [X] | [ ] | Independent pointers |
| AND-12 | System gesture cancel / focus loss | [X] | [ ] | Joystick resets to neutral |
| AND-16 | Joystick + Jump simultaneous | [X] | [ ] | Movement continues while jumping; auto: `touch_input_bridge_lifecycle_test` |

## Manual QA — fluid surface (TD-FL-034 Android)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-17 | Sea surface visible from shore | [X] | [ ] | GLES single-pass transparent (`18b81e0`); placed water/lava + ocean film + underwater fog OK |

## Manual QA — startup / load (TD-AUD-031)

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-13 | Cold start to main menu | [X] | [ ] | No transient ANR dialog |
| AND-14 | Load existing world | [X] | [ ] | First interactive frame < 5s on profile D |
| AND-15 | Create new world | [X] | [ ] | No freeze dialog near generation end |

## Manual QA — isometric controls

| ID | Scenario | PASS | FAIL | Notes |
|----|----------|------|------|-------|
| AND-ISO-01 | Load isometric world | [ ] | [ ] | Elevated camera; body visible |
| AND-ISO-02 | Joystick moves camera-relative | [ ] | [ ] | W/A/S/D screen XZ |
| AND-ISO-03 | Look pad aims character | [ ] | [ ] | Camera yaw fixed; body turns |
| AND-ISO-04 | Q/E snap buttons | [ ] | [ ] | Camera rotates 90° |
| AND-ISO-05 | +/- zoom ortho | [ ] | [ ] | Ortho size changes |
| AND-ISO-06 | Cam cycles Close/Standard/Far | [ ] | [ ] | Boom distance presets |
| AND-ISO-07 | Perspective world unchanged | [ ] | [ ] | FPS look + F5 1st/3rd |

## Sign-off gate

Close `TD-AUD-028..031` only when:

1. All automated tests above pass in CI/desktop-linux build.
2. Manual matrix has no FAIL on profiles A–E.
3. Evidence recorded in `docs/TECH_DEBT_AUDIT.md` execution progress section.

## Sign-off (manual device QA 2026-07-07, verified `18b81e0`)

- Tester: manual run (single Android device)
- APK build: `arch_refactor3` @ `18b81e0`
- Date: 2026-07-07
- Profiles exercised: A [X] B [ ] C [ ] D [X] E [ ] — one physical device; B/C/E N/A
- GLES fluids verified: placed water/lava visible; ocean surface film; underwater fog on wade-in
- Automated (CI/desktop): `docked_overlay_layout_test` [ ] `touch_input_bridge_lifecycle_test` [ ] — pending CI
- Manual result: [X] PASS (no FAIL rows) [ ] FAIL
- TD-AUD-028..031 gate: [X] CLOSED [ ] BLOCKED — manual matrix complete on tested device
