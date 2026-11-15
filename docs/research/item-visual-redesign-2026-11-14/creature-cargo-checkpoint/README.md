# Existing creatures and generic cargo contact

The new world-skeleton carry path is now verified with an existing rat (one
hand), sheep (two hands), and an inventory stone block in Creative and Survival.
NPCs remain registry entities; each is seeded with one finite seed solely as QA
property. Eight routed stages cover grab/windup/cancel/gentle placement/regrab,
raising aim from ground, another windup, release and landing. Male FP, female
front TP, and male back TP creature views pass; blocks cover male FP/female TP.
There are 128 native frames / 732 checks across ten runs.
Maximum measured palm gap is 0.00010195 m. Seed property is counted across NPC,
player and world; gentle placement preserves health, and surviving thrown
creatures return to ordinary AI locomotion. The real throw impulse is checked
before collision. Resistance and fatigue retain their existing mode rules.

The initial rat run exposed a 0.09549 m gap with the former centre-plane
contacts. That rejected report and frame are retained. Generic contacts now lie
on the physical box face toward the carrier, clamped to its extents. Individually
authored contacts supersede this fallback for all 70 typed items. Physics,
mass/size limits, ownership, health rules and throw distance are unchanged.
This is a simplified physical-volume grasp, not an anatomical grip or ragdoll.
FP rat/sheep, lowered-grab sheep, block, front TP rat/sheep and back TP sheep
frames were inspected. Large cargo naturally occludes some front-view carrier
geometry; the back view and numeric bone contacts provide the additional check.

Complete reports and all original hashes are retained. The archive contains all
prototype grabbed/rewound original frames. Every runner verifies source/author,
world, runtime-resource and input-config conservation/restoration. The full
Release regression passes: 25 CTests and all native profiles (gameplay 59,
ownership 57, ownership UI 18, player window 7, icons 209, Item Lab 1041,
presentations 3660 and the window profile). Style/include audits pass. CI now
runs both generic subjects per mode alongside the existing item throw sweep;
no remote CI execution is claimed.

This closes the creature/generic-contact compatibility gap introduced by the
shared carry arms. Utility pulses, remaining Item Lab/display-lighting matrix,
final performance and the consolidated final acceptance remain.
