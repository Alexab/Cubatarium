# Thrash autopsy 121131 vs 102527

| Field | 102527 | 121131 |
|---|---:|---:|
| `rows` | 6 | 6 |
| `stale_visual_med` | 5.0 | 23.5 |
| `stale_visual_max` | 11.0 | 30.0 |
| `publication_progress_med` | 0.0 | 1.5 |
| `transparent_cmd_reorder_med` | 0.0 | 1.0 |
| `periods_reorder_without_progress` | 0 | 0 |
| `fraction_reorder_without_progress` | 0.0 | 0.0 |
| `dark_face_stale_near_med` | 56.0 | 61.0 |
| `stalled_med` | 57.0 | 24.0 |
| `near_focus_holes_med` | 0.0 | 0.0 |

**Decision:** C1 (narrow publicationVersion to any_fresh): 121131 shows higher stale_visual and cmd_reorder with more publication_progress mid; holes counters remain 0 (missing-mesh telem). Default per plan A2.3.

```json
{
  "compare": [
    {
      "label": "102527",
      "rows": 6,
      "stale_visual_med": 5.0,
      "stale_visual_max": 11.0,
      "publication_progress_med": 0.0,
      "transparent_cmd_reorder_med": 0.0,
      "periods_reorder_without_progress": 0,
      "fraction_reorder_without_progress": 0.0,
      "dark_face_stale_near_med": 56.0,
      "stalled_med": 57.0,
      "near_focus_holes_med": 0.0,
      "note": "pubver_changed_without_fresh / pass_mesh_rev_lag require post-C1 exe"
    },
    {
      "label": "121131",
      "rows": 6,
      "stale_visual_med": 23.5,
      "stale_visual_max": 30.0,
      "publication_progress_med": 1.5,
      "transparent_cmd_reorder_med": 1.0,
      "periods_reorder_without_progress": 0,
      "fraction_reorder_without_progress": 0.0,
      "dark_face_stale_near_med": 61.0,
      "stalled_med": 24.0,
      "near_focus_holes_med": 0.0,
      "note": "pubver_changed_without_fresh / pass_mesh_rev_lag require post-C1 exe"
    }
  ],
  "decision": "C1 (narrow publicationVersion to any_fresh): 121131 shows higher stale_visual and cmd_reorder with more publication_progress mid; holes counters remain 0 (missing-mesh telem). Default per plan A2.3.",
  "c2": "skip until opaque_transparent_batch_mismatch telem on new flights"
}
```
