# Asynchronous item icon readback checkpoint

This is a verified intermediate checkpoint, not final goal acceptance.

The Release application build and extended acceptance runner passed: 26 CTests,
all eight native reports, including 3727 presentation checks. Ten new native
checks cover an isolated disk cache, deferred manifest publication, separate
read/draw framebuffer and pixel-pack state restoration, byte-exact RGBA PNG
roundtrip after reopening the cache, and cancellation of pending readbacks.
Normal rendering never waits on a fence; only the synthetic native test calls
`glFinish`. Item persistence uses a bounded GPU pixel-pack queue and drains at
most two completed images per flush. Block and creature disk caching retain
their existing synchronous behavior. Visible item icons are available before
the optional disk write completes. Shutdown cancels remaining GPU jobs.

The adjacent `readback-first-series/summary.json` is a failed performance gate:
armor passed (14.445625 to 13.87829 ms median run p95); warm catalog +10.73% and
cold loading-frame p95 +30.47% did not pass. Runtime resources, QA world and
configuration were restored. These failed runs are retained, not counted as
successful acceptance. The next change budgets disk validation and rendering
in the icon work queue and delays manifest serialization until readbacks drain.

All native runs mute audio temporarily and restore ordinary user settings.
