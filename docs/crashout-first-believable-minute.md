# CRASHOUT production driver: the first believable minute

Date: 2026-10-29

This document turns the current CRASHOUT production discussion into an explicit
VICE engineering target. It is not a request to make VICE game-specific. It is a
consumer-driven acceptance target: CRASHOUT defines the experience; VICE supplies
generic capabilities that make it possible.

## Target experience

The first major showcase milestone is intentionally small but visually demanding:

- Tyler is seated naturally in his car during a school break.
- The car interior is convincing at close range.
- A radio station is audible through believable in-cabin playback.
- Exterior school/parking-lot ambience is present beyond the glass.
- Tyler can look around, interact with the immediate presentation, put the car in
  motion, pull out, drive around one compact South Florida-inspired block and park.
- The same authored area is convincing in bright daytime and at night.
- The scene must look and feel impressive enough that the reaction is not merely
  "the engine works," but "this already looks like a real premium game."

No mission system, police loop, economy, large map or full social simulation is
required for this milestone.

The purpose is to prove the physical, visual and sensory ceiling of CRASHOUT before
scaling the game around it.

## Showcase quality gate

This milestone is a gate, not just another prototype step.

Do not expand CRASHOUT into the full envisioned city and feature set before the
showcase scene proves that VICE can hit the desired quality bar in a real playable
build.

The development order is:

1. Make one tiny area look and feel exceptional.
2. Prove Tyler, the car, camera, lighting, materials, audio and driving in that area.
3. Prove the same area in both daytime and night lighting conditions.
4. Make the sequence stable and repeatable in the normal native Runtime path.
5. Only then expand the game outward toward the broader CRASHOUT product scope.

The reason is deliberate risk control: it is cheaper to discover renderer,
animation, audio, vehicle or asset-pipeline limits on one block than after a large
amount of game content has already been built around them.

A larger but visually mediocre build does not satisfy this gate.

## Current verdict

As of 2026-10-29, VICE is approximately:

- **70% ready to make this sequence function end to end.**
- **40% ready to make the same sequence presentation-quality / trailer-grade.**

These percentages are directional engineering judgments, not tracked project
metrics. The useful conclusion is that the remaining risk is now concentrated in
presentation quality, polish and production integration rather than in proving that
VICE can move a character into a vehicle and drive it.

## Already proven or substantially present

### Vehicle foundation

VICE already supports authored physical vehicles with:

- four-wheel ray suspension;
- steering;
- material-limited tyre traction;
- braking and reverse;
- reaction forces;
- persistent vehicle controls/state;
- checkpoint resume;
- Runtime and Editor-preview parity.

This is a real physical foundation, but it is not yet production driving quality.

### Character + vehicle interaction

VICE already supports:

- walking;
- entering a rigid carrier/vehicle;
- safe exit checks;
- carried identity/state;
- gameplay cameras;
- mounted checkpoint/restart.

This proves the interaction topology. It does not yet prove polished doors,
seated/entry animation, hand placement or camera collision behavior.

### Character presentation

VICE already supports weighted skeletal character presentation and imported
animation clips. That is sufficient to stop treating the player as a rigid test
mannequin.

Production gaps remain in:

- animation blending;
- IK;
- retargeting;
- root motion;
- hand/foot placement;
- seat-specific pose solving;
- production character art and facial detail;
- GPU skinning / crowd-scale performance where later required.

### Audio

VICE already has native spatial audio and has validated actual stereo PCM pan,
falloff and range behavior.

For this milestone, the remaining audio work is not "make sound exist." It is:

- production streaming for radio/music;
- bus/mixer architecture suitable for vehicle interior/exterior states;
- cabin filtering and occlusion;
- smooth state changes when doors/windows open or close;
- layered exterior ambience;
- road, tyre, suspension and vehicle feedback;
- reliable music/radio playback integrated with gameplay state.

### Rendering

VICE already has a substantially more advanced rendering architecture than the
current CRASHOUT prototype content exercises, including modern large-scene and
high-end lighting infrastructure.

The immediate problem is integration and art quality: CRASHOUT's current prototype
surfaces/assets do not yet exploit the full production path consistently. The
engine must make high-quality authored game assets look correct and stable in the
native Runtime, not merely possess advanced renderer features in isolation.

## What must improve for this milestone

### 1. Character seating and interaction polish

Required outcome:

- Tyler sits at the correct height and orientation for the authored seat.
- Pelvis/spine/legs remain believable across idle variation.
- Hands can align naturally with the steering wheel when appropriate.
- Head/eyes can support subtle look behavior.
- Entry and exit do not visibly snap through the car.
- Character presentation remains stable while the vehicle moves and turns.

VICE should provide generic animation/IK/attachment capabilities. CRASHOUT owns the
actual Tyler rig, clips, seat definitions and behavior.

### 2. Production driving feel

Required outcome:

- vehicle mass and acceleration feel believable;
- steering has useful speed-sensitive behavior;
- braking is readable and controllable;
- suspension communicates weight transfer;
- tyres respond coherently to road material;
- wheels visibly match suspension/steering state;
- camera motion reinforces weight without becoming nauseating;
- road/contact feedback is audible and visible;
- the vehicle can be tuned without game-specific hacks in engine core.

Likely reusable VICE work includes drivetrain/wheel rotational behavior, better
wheel presentation hooks, richer telemetry and production tuning surfaces.

### 3. Interior-quality rendering

The camera will sit extremely close to materials and geometry. Therefore this
milestone must be judged from the driver's seat, not from a distant exterior shot.

Required outcome:

- stable high-quality PBR materials;
- convincing glass and transparency behavior;
- readable dashboard/interior surfaces;
- physically coherent direct and indirect light;
- stable shadows through/around the cabin;
- reflections that do not distract or obviously break;
- correct exposure transitioning between bright exterior and darker cabin;
- temporal stability while turning the camera and driving;
- no obvious large-world or depth artifacts in the target block.

The engine should prioritize correctness and stability over adding another exotic
renderer feature that this scene does not use.

### 4. Daylight and night target

The environment must not look like a generic gray-box demo in either condition.

The first block should support a bright South Florida daylight look:

- hard warm sun;
- bright sky contribution;
- strong but believable contrast;
- vegetation and painted surfaces responding correctly to light;
- convincing car paint/glass/interior response;
- readable deep shade under structures and inside the vehicle;
- atmosphere/exposure that survives looking from cabin to exterior.

The same block must also support a convincing night look:

- believable street, parking-lot, building and vehicle lighting;
- readable dark values without flattening everything with fill light;
- stable emissive/light-source relationships;
- convincing reflections on glass, paint and road surfaces;
- interior/dashboard illumination that does not destroy outside visibility;
- exposure transitions that remain controlled when looking between bright lamps,
  dashboard displays and dark surroundings;
- no obvious shadow, temporal or reflection instability while driving.

CRASHOUT owns the art direction. VICE must expose stable generic lighting,
material, sky/environment and post-processing controls.

### 5. Car-cabin audio state

Required outcome:

- radio/music sounds like it originates from the car audio environment;
- exterior ambience is attenuated/filtered while doors and windows are closed;
- opening a door/window changes the soundscape smoothly;
- closing it restores the enclosed state;
- vehicle/road sounds remain spatially coherent;
- mix transitions are data-driven and reusable rather than hardcoded for CRASHOUT.

### 6. Camera quality

Required outcome:

- seated look camera has correct pivot and limits;
- third-person drive camera feels deliberate and stable;
- camera collision avoids clipping through car/world geometry;
- transition between seated/interior/exterior states is smooth;
- camera behavior does not expose physics jitter;
- FOV/exposure/motion choices can be tuned per authored game camera without engine
  genre assumptions.

### 7. One coherent authored block

Do not expand map size for this milestone.

A tiny area is sufficient:

- school parking lot or equivalent starting area;
- one compact surrounding block;
- enough buildings/props/vegetation to create believable depth;
- one destination/parking point.

The block must use production-style asset import, materials, lighting and Runtime
playback paths. It should not rely on editor-only or screenshot-only tricks.

## Acceptance test

The milestone is complete when a native CRASHOUT build can perform this sequence in
both an authored daytime state and an authored night state:

1. Start with Tyler already seated in the authored car.
2. Observe a production-quality character pose and vehicle interior.
3. Play a radio/music stream through the in-car mix.
4. Hear exterior ambience as a distinct outside environment.
5. Change an authored cabin openness state and hear the mix respond smoothly.
6. Look around without clipping, severe temporal artifacts or unstable exposure.
7. Put the vehicle in motion and leave the parking space.
8. Drive one authored block with convincing suspension, steering, braking and road
   feedback.
9. See wheel/suspension presentation agree with physical state.
10. Park and stop without any special scripted teleport or hidden state reset.
11. Maintain stable presentation, audio and simulation throughout the sequence.
12. Repeat the same route at night without the scene losing visual quality or
    exposing lighting/reflection/exposure weaknesses.
13. Run the same game build through the normal VICE project/Runtime path on Tier 1
    platforms as support reaches them.

The scene should withstand close observation. A technically correct sequence that
still looks like an engine prototype does not pass the presentation milestone.

## Engineering priority rule

Until this milestone is convincing, prefer work that directly removes a visible or
interaction-quality blocker in this scene over speculative engine expansion.

That means, for example:

- animation blending/IK before unrelated animation generalization;
- wheel/suspension presentation before exotic vehicle subclasses;
- cabin audio/mixing before broad audio features with no CRASHOUT consumer;
- stable interior/day/night lighting and materials before another high-end renderer
  feature that the target scene does not exercise;
- camera collision and feel before unrelated camera modes;
- production asset ergonomics before map expansion.

Every engine change must remain generic and regression-tested. The consumer loop is:

**CRASHOUT requirement -> generic VICE capability -> CRASHOUT integration -> VICE
regression + consumer validation.**

## Non-goals for this milestone

Do not block completion on:

- full traffic simulation;
- police;
- economy;
- phone/social systems;
- full NPC cognition;
- weather simulation beyond what the authored showcase needs;
- a large map;
- multiplayer;
- final licensed vehicle content;
- the full CRASHOUT opening/story.

Those systems matter later. This milestone exists to prove that VICE can make one
ordinary CRASHOUT minute feel physically, visually and sonically premium before the
team expands the game around that proven standard.
