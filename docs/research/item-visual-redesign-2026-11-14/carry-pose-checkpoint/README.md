# Authored physical carry and shared human arms

This checkpoint closes the generic two-hand tool-contact and old TP carry-pose
defects exposed by the preceding input checkpoint. It is not final production
promotion or the completed visual-redesign acceptance.

## Implementation

All 70 authored recipes now declare independent physical-carry orientation and
right/left contact coordinates. The generator fits those points with the same
transform as geometry. WornEquipmentDrawer uses that exact transform for both
carried geometry and contact queries; the camera/model yaw conventions are
explicit. Canonical wielded FP/TP grips remain separate.

Human v2 uses a shared two-bone arm solver (also used by the bow) and a bounded
upper-body reach pose. Both minimum folded-arm distance and maximum reach are
considered; bones never stretch and physics/feet/cargo positions are untouched.
A low hold can require a forward torso bend beyond horizontal (up to 105 degrees).
The correction blends independently rather than accumulating into locomotion.
The first-person carry path draws only the six arm bones from that same posed
world skeleton, with world depth. This replaces projection-only viewmodel hand
alignment for human v2 physical cargo. Normal wielding and unsupported rigs keep
the existing viewmodel path. Armor arm sections share the filtered bone lookup;
a shelved offhand shield does not reserve the free arm pose.

The bucket now has physical carry contacts at its rim instead of a top-handle /
base pair which let the forearms obscure the vessel. Its normal wielded handle
grip, size, collision, mass and capacity are unchanged. All 213 geometry binary,
texture and UV-layout payloads remain byte-identical to the pre-contact snapshot.

## Native evidence

`all-first/` is the current complete Creative / male / 1280x720 UI 100% routed
input run: all 70 IDs, eight stages each, 560 frames and 3384 checks. Four carrying
stages per ID (280 frames) use the actual world-skeleton FP arms. Maximum measured
palm/contact gap is 0.014425 m. Every stage conserves the one finite unit, stable
instance ID, wear, broken state and container contents. Summary verifies source,
author, world, runtime-resource and configuration restoration. `frame-hashes.json`
records all 560 original PNGs. `native-windup.zip` retains all 70 original windup
frames; the three crop sheets are labelled overview images, not replacement
native screenshots. All three sheets and individual bucket, axe and bread views
were inspected: the payload remains distinguishable; tiny cargo naturally covers
less screen area than the unscaled human hands.

The earlier full female front-view sweep reached 549 frames, then rejected
`wood_torch grabbed`: the low target was 0.5803 m from the shoulder against a
0.5406 m arm reach. `rejected-torch/` retains the diagnostic, not a passing report.
`female-low-reach/` repeats torch and wool after the torso-range correction:
16 frames / 88 checks pass; maximum palm gap is 0.000044 m. This is focused
revalidation, not a new all-70 female sweep. Extreme downward aim in front-view
third person can bring the collision-adjusted camera close to the body; those
frames are not presented as useful item comparison framing.

The integrated Release regression passes: 25 CTests; all native gameplay,
ownership, ownership UI, player-window, icon, Item Lab and presentation profiles;
and the normal FP probe. Counts: gameplay 59, ownership 57, ownership UI 18,
player window 7, icons 209, Item Lab 1040, presentations 3660. See `release/`.
Desktop builds used only Release.
Style/include audits pass (the FP draw path was extracted from RenderFrame).

Fresh-checkout proof at `327c80dc7` passes for both `core.autocrlf=true` and
`false`: 981 signed files, all 70 stages valid, all 70 byte-identical regenerated
and published artifacts, all 70 reference archives preserved. Publication occurs
only in disposable clones. Working production and authored sources are unchanged;
see `release/checkout-proof.json`.

## Remaining completion gates

Repeat the final body/mode and display acceptance matrix; inspect actual use and
bow transitions and generic creature cargo with the new arm route. Final
provenance/independence reconciliation, all-70 production publication, final
performance measurements and the consolidated final report remain. The long
version badge overlap at large HUD scale is also still open. No goal completion
or production-promotion claim is made by this checkpoint.
