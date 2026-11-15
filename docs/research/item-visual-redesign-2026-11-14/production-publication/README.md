# Production publication of all 70 items

> Historical checkpoint. Subsequent requirements and final status are recorded
> in the [consolidated acceptance audit](../final-audit/README.md).

All 70 published item resources now use the authored v3 geometry, UV, materials,
textures, carry profiles and visual bindings. Publication generation and manifest
hash are recorded in `publication.json`. The transaction is committed and no
publication journal remains. All 70 previous published directories are preserved
under `_itemgen_reference`; every file in their reference hash inventory verified.
Item Lab resolves these archives for its left old/new comparison pane.

`origin-review.json` reconciles all 70 item source paths, construction parts,
previous/new binary hashes and triangle counts, atlas sizes, individual recorded
studio observations and comparison-sheet hashes. Different hashes alone are not
artistic evidence: the per-item observations and construction cards supply that
part of the review. No external mesh, UV or pixel input is used by this production
line. Familiar functional archetypes are intentional. Historical Kenney, KayKit
and Quaternius import declarations remain historical records, not new production
inputs. Direct Luanti copying was not established by the original audit. This
review is not an exhaustive external-catalogue comparison or legal certification.

All non-visual item-definition fields match the initial baseline. The sole
historical visual-definition change is the waterskin drink-hand animation, not a
change to economy, capacity, ownership or physics. This publication changes no
item definitions or game balances.

The exact Release desktop build and integrated acceptance pass against published
runtime resources without a temporary candidate substitution: 25 CTests,
gameplay 59, ownership 57, ownership UI 18, player window 7, icons 209, Item Lab
1041 and presentations 3660. The window profile also passes. Reports, executable
and manifest fingerprints are retained under `release/`.

Fresh Git checkouts of `9311be577` pass under both autocrlf modes: all 70
published assets validate and regenerate byte-identically; republication preserves
all 70 previous-generation archives. Working production and author files remain
unchanged. See `release/checkout-proof.json`.

Final use/carry/display and performance gates remain; this record does not mark
the complete redesign goal achieved.
