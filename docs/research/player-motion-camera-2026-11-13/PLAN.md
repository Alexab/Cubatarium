# Player motion and third-person camera

Branch: `codex/player-motion-camera`; isolated worktree from `2b569cdee`.
The original worktree and its concurrent world/meshing changes are excluded.

## Accepted design

Human v2 only: the 12 body/hair meshes share an articulated pelvis/spine,
shoulder/elbow/wrist and hip/knee/ankle hierarchy. The 30 atlases remain
independent of hair geometry. A uniform 0.9 model transform makes the authored
32-pixel body 1.8 blocks high. Equipment names and their rest coordinates are
retained; feet receive semantic boot anchors. Legacy rigs keep the old profile.

Actual body displacement drives ground travel and vertical poses. Crouch uses
the motor's actual stance blend. Flight is quiet hovering, with small velocity
dependent lean. Water motion uses alternating crawl strokes and supporting
movements at rest. Ground pose and occupied-hand layers blend independently;
two-bone legs solve after quaternion blending without segment stretch.

The perspective camera has a desired boom and a checked render position.
A conservative swept volume encloses the near-plane corners, with a minimum
0.18 radius plus 0.02 margin. Solid world voxels and unknown terrain block it;
water, decoration and living bodies do not. Retraction is immediate, return is
smoothed, and inter-frame travel is checked separately. Physical eyes remain
the source for movement and breathing.

## Research informing the implementation

- [ozz partial blending](https://guillaumeblanc.github.io/ozz-animation/samples/partial_blend/): local-transform upper-body layers.
- [ozz foot IK](https://guillaumeblanc.github.io/ozz-animation/samples/foot_ik/) and [two-bone IK](https://guillaumeblanc.github.io/ozz-animation/documentation/ik/): blend before correction; preserve segment lengths and a stable bend pole.
- [Godot SpringArm](https://docs.godotengine.org/en/stable/tutorials/3d/spring_arm.html): camera near plane needs a shape sweep, rather than one central ray.
- [Unreal SpringArm](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USpringArmComponent): collision probe and recovery of the desired boom.
- [Swim England crawl technique](https://www.swimming.org/masters/improving-front-crawl-technique/1000/): alternating arms, elongated body and flutter kick, adapted to a block character.

## Implementation and acceptance order

1. Versioned articulated resources, matched UV islands, consistent scale and equipment.
2. Real motion inputs, runtime actor state, ground/crouch/run and support legs.
3. Hover, ascent/descent, water, jump/fall/landing and transitions.
4. Swept third-person camera and unified render consumers.
5. Deterministic 180-combination resource checks; rig/contact/lens tests at
   30/60/144 FPS; native Release captures; real motor and collision scenarios.
6. Review curated comparison sheets and clips, record actual evidence and
   concrete unresolved cases, separate resource/motion/camera/report commits.

Controls remain Ctrl sprint, Shift crouch/fly down, Space jump/fly/swim up.
Releasing Space retains the existing water sinking behavior. Speeds, gravity,
buoyancy and physical colliders are outside this change.

## Explicit follow-up scope

- Other humanoid species and full articulation of custom legacy packs.
- Horizontal swim collider, smaller swimming passages, accelerated diving
  and view-directed swimming: these change gameplay collision/breathing.
- Isometric camera protection.
- Previously recorded Treeman/Orc contact failures and general creature polish.

These follow-ups do not substitute for the current human/camera acceptance.
