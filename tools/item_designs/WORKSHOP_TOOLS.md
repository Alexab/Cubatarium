# Workshop tool wave

All 25 tool IDs now have authored candidate recipes. The wave adds the 21
missing designs and retains the four pilot constructions (stone/iron picks,
copper axe, gold sword). These remain staged candidates pending complete
artistic, interaction and performance acceptance; they are not published by
seeding or building.

`python tools/itemgen.py seed-workshop` writes only missing recipe files.
The workshop constructor reads no foreign mesh, UV or image files. Its
references are functional archetypes: hafted heads, woodworking joinery,
knapped stone, cast copper and forged metal. Recipe coordinate drawings and
procedural surface marks live in this repository. This construction history
does not automatically certify artistic independence or legal clearance.

| Family | Construction and silhouette | Candidate sources |
|---|---|---|
| Pickaxe | Separate bridge, curved point and opposite adze/chisel; visible socket, wedge or lashing. Keeping these members separate avoids a convex-hull plate joining the points. | [wood](wood_pickaxe/design.yaml), [stone](stone_pickaxe/design.yaml), [copper](copper_pickaxe/design.yaml), [iron](iron_pickaxe/design.yaml), [gold](gold_pickaxe/design.yaml) |
| Axe | Convex cheek and thin working edge; wood dowel, flaked stone ear, copper rivets, iron poll or fine gold inlay. | [wood](wood_axe/design.yaml), [stone](stone_axe/design.yaml), [copper](copper_axe/design.yaml), [iron](iron_axe/design.yaml), [gold](gold_axe/design.yaml) |
| Shovel | Thick central web narrows into cutting bevels; angled side cheeks form a shallow dish; rear spine joins the haft. Squared wood and pointed iron outlines differ. | [wood](wood_shovel/design.yaml), [stone](stone_shovel/design.yaml), [copper](copper_shovel/design.yaml), [iron](iron_shovel/design.yaml), [gold](gold_shovel/design.yaml) |
| Hoe | Angled neck separates the shaft and working blade; blade turns into the working plane, with a metal tang or primitive binding. | [wood](wood_hoe/design.yaml), [stone](stone_hoe/design.yaml), [copper](copper_hoe/design.yaml), [iron](iron_hoe/design.yaml), [gold](gold_hoe/design.yaml) |
| Sword | Faceted blade web, thin edge, grip, guard and pommel; wooden laminate key, bound chipped stone, broad copper shoulders, slim iron point or asymmetric gold guard. | [wood](wood_sword/design.yaml), [stone](stone_sword/design.yaml), [copper](copper_sword/design.yaml), [iron](iron_sword/design.yaml), [gold](gold_sword/design.yaml) |

Material changes include construction changes, rather than tinting the same
geometry. Handles remain wooden. New wood surfaces have an authored UV grain
direction. Working faces receive restrained joinery, flake, forge or inlay
marks, independently of side/bevel surfaces. All source overlays remain read
only. The source atlas size is 256 px; new pick recipes explicitly use 150
texels/metre and other new tools 185. A packing overflow fails; it never silently
shrinks the author's UVs. Material/part directions are documented in the recipe.

The `blade` primitive constructs both web faces, sloping bevels and closed edge
walls. It validates a convex outline, positive web/edge thickness and inset.
Tests check outward normals, watertight shared edges, UV direction, untouched
definitions, 1200-triangle budgets, and uniqueness of actual position data
independently of texture/UV bytes. These are technical gates, not quality scores.

Every tool has a separate TP tilt, independent of physical size and FP fitting.
Native review identified the inherited -65-degree tilt as overly foreshortened;
the tool set now uses -25 degrees with a mirrored 20-degree outward cant and
an independent 1.65 TP scale while keeping
either grip on the same palm. Neither setting changes collision size, mass,
ground-model size or FP fitting.

For a quick native art iteration, run
`python tools/run_item_design_probes.py --skip-build --presentation-only --id copper_pickaxe --world CarryAimQA`.
Omit `--id` to substitute all candidates. Selected IDs also filter the native
fixture, including after v3 production publication. This checks both hands/body forms, FP
framing and loose-item submissions, restores runtime resources, and writes a
separate `presentation-probe.json`. It does not claim inventory/gameplay or
performance acceptance. Run the full probe after gameplay/render code changes.

Remaining review includes full world lighting, tiny icons, movement and contact
quality; stone/wood working blades and narrow hoe silhouettes need particular
attention. Recheck tool proportions alongside the armor/material waves before
publication. A successful triangle submission or a unique mesh hash alone is
not artistic acceptance.
