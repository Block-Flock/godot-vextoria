# Native Vextoria object cutover

`Part`, `Model`, `Folder`, and `VextoriaScript` are Godot ClassDB Nodes. The
Godot SceneTreeDock, EditorSelection, Inspector, and EditorUndoRedoManager own
their editor identity; this module does not create a second Explorer tree.

`Part` is a `RigidBody3D` with internal mesh and collision children. Those
children are not independent Vextoria objects and are not serialized; a
reopened `Part` reconstructs them from its native `Size` and `Color` properties.
`Anchored` freezes the body and `CanCollide` disables its shape without
destroying the body. This preserves the same physics identity across edits.
`Locked` reads and writes Godot's `_edit_lock_` state, so the Inspector and
viewport lock action cannot disagree.

`Model` is spatial (`Node3D`); `Folder` and `VextoriaScript` are non-spatial
Godot `Node`s. This matches the recovered Roblox inheritance distinction
(`ModelInstance` derives from `PVInstance`, while `Folder` derives from
`Instance`). Native non-spatial Vextoria containers opt into a narrow
`Node3D` pass-through in the fork: a Part nested under Folder/Script still
inherits its nearest Model frame and visibility. Ordinary Godot Nodes do not
opt in. Reparenting a Folder/Script with `keep_global_transform=true` saves
the first spatial descendants' world frames, so moving a folder between
models does not silently move its parts. This is implemented in engine/node
and native class behavior, not a mirrored Creator Explorer.

`VextoriaExplorerTree` is a native `Tree` whose item hierarchy follows a
source Godot `Node` subtree. It admits gameplay nodes tagged
`_vextoria_instance`, ignores internal implementation children and excluded
subtrees, and maintains Node/TreeItem identity across name, order, and parent
changes. The client still adapts Creator actions and display metadata in C#;
this does not yet make the entire gameplay object API native.
When a detached Creator world admits a descendant before its parent has been
explicitly admitted, `ensure_item_for_node` creates marked ancestors first so
the item does not attach to the wrong branch.

The hinge frame split follows the observable C0/C1 contract in
`ROBLOX-main/App/v8datamodel/JointInstance.cpp` and
`ROBLOX-main/App/include/v8world/RotateJoint.h`. Body A and body B retain
independent authored local frames. The native smoke checks the frame and
solver API; a physical push-door end-to-end test remains a separate gate.

The property names/defaults and separate Anchored/CanCollide semantics were
compared with `ROBLOX-main/App/v8datamodel/PartInstance.cpp` (constructor,
`setPartSizeUi`, `setAnchored`, `setCanCollide`). No Roblox source text is
included here. Roblox's joint reconstruction, material/shape variants,
network ownership, Luau callbacks, and complete `Instance` lifecycle are
**not yet ported**. The managed client remains in service until those
behaviors and the whole-world RBXL fixtures pass native conformance tests.

Smoke coverage:

- `tests/part_runtime_smoke.gd`: native physics identity, mesh and collision
  changes, toggles, save/reopen.
- `tests/editor_slice_smoke.gd`: real Godot CreateDialog/SceneTreeDock,
  selection/Inspector identity, property/transform and create undo, save/reopen.
- `tests/part_visual_capture.gd`: Vulkan editor-target image of an authored
  native Part, for visual review alongside the executable geometry checks.
- `tests/hinge_frame_smoke.gd`: fork-level joint solver override and
  independent body-B hinge frame contract. The engine implementation lives in
  `scene/3d/physics/joints` and `servers/physics_3d`, not a C# shim.
- `tests/native_hierarchy_smoke.gd`: Model/Folder/Script/Part ClassDB ancestry,
  real Godot parentage, transform/visibility pass-through, CFrame-preserving
  Folder reparent, ordinary-Node isolation, and PackedScene save/reopen.
- `tests/native_explorer_smoke.gd`: native SceneTree-backed hierarchy, identity,
  exclusion, rename, reparent, ordering, removal, detached-root admission, and
  out-of-order ancestor admission.
