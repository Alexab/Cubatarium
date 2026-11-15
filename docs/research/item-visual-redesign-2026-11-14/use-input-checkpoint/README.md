# Real mouse input: eating, drinking and bow draw

The native world runner now supports `--interaction use`. Six IDs cover all
current edible models, the waterskin and bow. Each executes eight stages:
equipped, partial hold, hotbar cancellation, restored slot, fully ready,
completed action, restarted hold, second cancellation. Events enter the normal
GLFW input callbacks, then the window manager, UI router and gameplay controller.
Calling the UI-only application mouse router is insufficient for world actions.

Four cases pass: Creative/Survival male FP at 1280x720 UI 100%, and
Creative/Survival female front TP at 1920x1080 UI 200%. Total: 192 native frames.
`coverage.json` records checks per case. Count/instance/container contents and
ammunition are checked after every stage. Only successful full use spends food,
water or ammunition in Survival; Creative retains its free-action policy.
Cancellation and slot round trips cannot revive an old held state. FP draw
inputs match authoritative phases and current water contents. Bow release is
checked synchronously for a real flying arrow; after contact the arrow payload
is counted independently of the no-longer-active projectile flag.

All original frame hashes and complete reports are retained. The archive holds
24 original ready frames; overview sheets contain labelled crops. Apple partial,
ready and completed, waterskin ready and FP/TP bow ready were inspected. These
cases confirm state projection, not final creature-cargo, display-matrix or
performance acceptance. The long HUD version badge still overlaps slots at
UI 200%; it remains an explicit defect to fix before final display acceptance.
World, runtime models, authors, input configuration and measured sources are
verified unchanged/restored by each runner summary.
