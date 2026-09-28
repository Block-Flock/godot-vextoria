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
The gameplay Brick path now retains this native `BoxMesh`/`BoxShape3D` pair at
the authored `Size` instead of replacing it with a managed unit resource and
scaling it a second time. `reset_builtin_geometry` restores that pair when a
Part changes from an authored non-box shape back to Brick. Other shape variants now resolve the client's deterministic
`res://resources/shapes/meshes/<Shape>.tres` registry from native C++, with
the same convex/trimesh and Truss/Frame box-collider rules that the managed
resolver used.
The Wedge path also owns a native eight-triangle mesh and six-point convex
collision hull, rebuilt at the authored Size. Its local orientation follows
the source-derived wedge geometry contract already used by the client and was
compared with `ROBLOX-main/App/include/v8world/WedgeMesh.h` and
`ROBLOX-main/App/v8world/WedgePoly.cpp`; no Roblox source text is copied.
The authored shape assets still ship with the client project, but Part now
selects and installs them natively. MultiMesh batching retains its managed
resource cache as a separate rendering path.
`Geometry` is native ClassDB/Inspector state for Brick (0) and Wedge (1), so
Godot duplication, scene serialization and editor undo preserve the selected
geometry without a gameplay wrapper. This is not the full legacy Shape enum.
The gameplay adapter explicitly switches geometry ownership when installing
an authored shape, preventing a later Size edit from rebuilding a stale
Wedge over that resource.
Resource geometry (2) now uses the native `GeometryMesh` and
`GeometryCollision` properties. Godot owns their installation, sized internal
frames, duplication and scene persistence. The low-level resource properties remain as a fallback for isolated engine
tests and future custom resource shapes, but normal gameplay Shape changes no
longer require managed geometry staging. Touch/assembly mirrors remain
separate migration work.

`AppearanceMaterial` is an authored Material Resource on the native Part.
The Part instances it privately and owns tint application to BaseMaterial3D
albedo or an authored ShaderMaterial's Color-typed `color` uniform. Tint never
modifies the shared authored resource. Duplication and scene save/reopen retain
the source resource and create independent applied materials. Resource.changed
rebuilds the applied instance; Godot's material parameter setters do not emit
that signal themselves, so programmatic source edits must call emit_changed.
Standalone Part material asset selection is now native: the Material enum maps
directly to `res://resources/materials/parts/<Material>.tres`, including the
transparent part shader when alpha crosses the existing 0.975 opacity
threshold. Native Parts register in an internal non-persistent group so the
existing normal-map setting can update native standalone materials while the
managed MultiMesh cache remains in service. Complete legacy rendering semantics
and touch/assembly mirrors remain outside this slice.

Part also exposes native authored `Shape` and `Material` enum state and emits
`vextoria_property_changed` whenever native Size, Color, physics, Shape,
Material, appearance, or geometry authoring state changes. This is the bridge
used by the temporary managed compatibility facade so Godot Inspector edits and
the Roblox-facing API converge on the same authored Part state. Brick/Wedge
geometry is generated natively, and resource-backed Shape plus standalone
Material selection are also resolved natively. The managed compatibility layer
still owns batched MultiMesh resource caches and the Roblox-facing object API.

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
