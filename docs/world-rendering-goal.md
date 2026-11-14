# ClassiCraft world and rendering comparison: project handoff

This is a project brief and proposed `/goal`, not proof that the described systems already exist. It records Garrett's direction as discussed on Stream01, so a new ClassiCraft project chat can pick up the work.

## Current handoff

- GitHub repository: https://github.com/imag187/ClassiCraft
- Carryover branch: `Stream01-carryover`. At the time this brief was written it contained the Studio world-authoring prototype and `worlds/test-world/world-project.json`.
- Local checkouts observed on Stream01: `C:\Machine-Setup\ClassiCraft` and `D:\Software\ClassiCraft`. They are separate clones; check their branch and working-tree state before changing either. The user has also mentioned a Documents/GitHub location, so determine the actual workspace in the new chat instead of assuming a path.
- A newer client and world-rendering milestone may exist only on Office05, or on another branch or repository. The user reported Studio-created assets loaded into a client with WASD movement. The GitHub branches visible when this brief was written did not show that newer work. Inspect first; do not overwrite it or assume the older Godot/TypeScript prototype represents the desired client. That prototype was explicitly retired.
- The existing Studio prototype has a C++/Dear ImGui content library with a Death Knight, abilities and talents, plus early tile/map/world editors. See `studio/README.md` and `worlds/test-world/README.md`. The test-world file is currently copied manually to and from the Studio executable folder; it is not automatically linked to the repository.

## Product direction

ClassiCraft combines an action RPG and creator tools. Studio is where creators define the base game and custom game content: classes, abilities, talents, items, progression rules, biomes, objects, terrain, maps, generation rules and eventually logic. The client lets players create and play worlds using those definitions, or join a locally hosted, friend-hosted or dedicated world. The same world can eventually be opened for inspection and editing in Studio.

Worlds should eventually be seed-based and persistent, with editable terrain, building, digging, biomes, authored settlements and a combination of hand-authored and generated dungeons. The base progression ranges are Starter, Novice, Apprentice, Journeyman, Skilled, Pro and Expert, but creators can define their own caps and ranges. These are future direction, not requirements to implement a full MMO for the comparison.

The map editor and world editor are distinct. A map editor authors terrain and local layouts; the world editor composes or opens a world, its maps, game objects, spawns, collision and logic. The creator wants placement and browsing to feel approachable, inspired by RollerCoaster Tycoon and other visual editors. A permanent test world should survive format changes through explicit migration or a documented compatibility path.

## Voxel-object idea

Creators should be able to build fine-grained voxel art using a hybrid 3D paint and modeling workflow: create filled boxes and pyramids, paint individual voxels or groups, select and transform parts, and group parts into larger editable objects. A tree might combine a trunk with grouped pyramids; a table might group a top and four legs. Grouping is a productivity and source-editing feature, not a demand for players to ungroup runtime objects.

Keep **editable source** (parts, groups, colors, transforms, behavior and original animation data) distinct from **built game-ready assets**. Reopening the source permits editing or ungrouping; a built object has only the runtime data and supported interactions it needs. The runtime can still make an object destructible: the source author defines material, collision, damage responses, anchored regions, drops and damage states. Visual particles can be local effects; authoritative persistent changes belong to the world simulation.

Evaluate visible-surface extraction: hide internal faces between adjacent opaque voxels while retaining enough volume/material data to expose new surfaces when destructible regions change. Rebuild affected object or chunk surfaces when needed; do not make a tree visually hollow simply because its interior faces were omitted from the initial mesh. Handle transparent materials correctly when in scope. This is a proposed optimization, not a guarantee of performance.

Stylized or cel-shaded 3D and pre-rendered 2.5D are both candidates. A voxel-built asset might be rendered directly in 3D, or rendered to directional animation frames and packed into atlases with metadata for the 2.5D presentation. Separate camera orientation from object facing; reuse relative views when possible instead of blindly creating every combination. The world simulation remains in world coordinates in either case. Do not require both modes in the final game: the purpose of the experiment is to decide whether to use 3D, 2.5D, or both.

## Proposed goal

Paste the following into the new ClassiCraft project chat after ensuring the desired branch and any Office05 work are available:

```text
/goal Build and evaluate a focused ClassiCraft world-authoring and rendering prototype that lets us choose stylized real-time 3D, pre-rendered isometric 2.5D, or support for both. Use the project context in docs/world-rendering-goal.md, but verify the actual code and available branches first.

Inspect the repository and any supplied Office05 checkout or branch. Preserve uncommitted work and do not replace or merge over newer work. If the working client is unavailable, document that and continue with a self-contained prototype using the code that is available.

Use Studio as the source of one shared, versioned test world. Create or preserve a small traversable scene with a player spawn, terrain, a wall, a training dummy and a skeleton or clearly marked placeholder. Keep map editing distinct from world composition. Demonstrate saving and reopening the same world and represent seed, content version and generator version without claiming that procedural continents exist.

Build a narrow editable voxel-object workflow: filled boxes and pyramids, painting, selecting and transforming parts, and grouping/ungrouping. Preserve editable source separately from a built runtime asset. Create a small tree or table from grouped shapes and a simple character or enemy where practical. Demonstrate a basic authored impact/damage response; distinguish visual particles from any persistent authoritative damage. Do not implement a complete modeling suite or promise general-purpose destruction.

Produce a fair side-by-side comparison using the same authored scene and, where practical, the same source assets: (1) stylized real-time 3D, potentially cel-shaded, and (2) an isometric 2.5D presentation with pre-rendered directional/animation frames, an atlas and metadata. Show four 90-degree camera orientations and a few zoom levels. Handle object facing and camera orientation correctly; document any visual approximations, including damage states, for sprites. The final recommendation may be 3D, 2.5D or both based on evidence.

For voxel geometry, demonstrate or evaluate removal of internal faces while retaining data sufficient to expose surfaces after an object changes. Measure the cost rather than assuming a particular renderer or voxel format is inherently faster. No custom binary format is required unless a measured need justifies it; preserve editable sources and document generated outputs.

Use the same world coordinates, object definitions, movement, collision and gameplay state for both presentations. Provide WASD movement, wall collision and a basic attack or interaction. Reuse the level-6 Death Knight and its existing abilities when available, and identify any placeholders or missing gameplay. Support an independently runnable test without requiring a production account, hosted server or external service. Do not claim multiplayer or server-authoritative gameplay unless actually implemented and tested.

Use original or appropriately licensed small assets, record their origins and redistribution permissions, and avoid committing purchased or third-party assets without permission to distribute their source. Keep generated build output out of normal commits. Prefer Windows DX11-capable development while preserving a plausible Linux/DXVK and macOS path; test and report actual platform support rather than assuming it.

Verify builds, editor source save/reload, runtime world save/reload, camera rotation, collision and object placement. Run repeatable performance and storage comparisons under the same resolution, hardware, scene, content, camera and workload. Report frame-time or FPS methodology, memory, generated asset size, startup/build time where practical, rendering quality, camera-facing coverage, creator effort and limitations. Distinguish measured findings from assumptions.

Deliver runnable artifacts, source, reproducible build and run instructions, a saved test world, a concise side-by-side comparison, and a recommendation for 3D, 2.5D, or both. If a proposed prerequisite is unavailable or unsupported, document the blocker and provide the strongest honest partial comparison rather than calling placeholders complete. Do not build a marketplace, a full procedural continent, seven production dungeons or a full MMO as part of this goal.
```

## Coordination notes

The goal is intentionally ambitious. Split work into milestones and keep the creator informed about progress, cost and blockers. Ask before irreversible external actions; a draft comparison and local work do not require publishing a release. If independently hosted worlds are later supported, keep server authority, creator ownership and asset redistribution terms explicit.
