# Rejected integrated performance series

All scene validity checks passed, as did FP, armor, warm catalog and cold catalog
loading-frame p95 gates. The 512-resting-drop median p95 failed the unchanged
+10% whole-window threshold (39.773 ms reference versus 58.078 ms candidate).
Raw results and the six drop traces are retained. Swap waits vary substantially;
no slow samples have been removed. This series is not overall acceptance.

The next implementation removes redundant per-item shader parameter uploads in
one contiguous world draw queue. It must retain pixel output and drawing order,
and then pass fresh paired measurements before the goal may be completed.
