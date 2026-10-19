# Native Vextoria object cutover

`Part`, `Model`, `Folder`, and `VextoriaScript` are Godot ClassDB Nodes. The
Godot SceneTreeDock, EditorSelection, Inspector, and EditorUndoRedoManager own
their editor identity; this module does not create a second Explorer tree.

`VextoriaInstance` is registered as an internal, non-CreateDialog base and is
instantiated for generic non-spatial Roblox Instance proxies through
`VextoriaSceneHierarchy.create_generic_instance()`. This gives classes without
a specialized native node or authored scene the engine-owned Archivable, Tags,
Locked, sibling naming, and Node identity surface. Class-specific scenes retain
precedence; non-Instance network objects retain ordinary Nodes. Specialized
spatial compatibility proxies and their remaining managed behaviors are still
transitional and are not covered by this generic base cutover.

`VextoriaSpatialInstance` is the corresponding internal `Node3D` base for
generic spatial Instances. The native `Model` class now derives from it, and
`VextoriaSceneHierarchy.create_generic_spatial_instance()` constructs the
fallback proxy used by `Dynamic`. Both implementation bases remain hidden
from CreateDialog; specialized spatial classes keep their own node/scene paths.

`Part` is a `RigidBody3D` with internal mesh and collision children. Those
children are not independent Vextoria objects and are not serialized; a
reopened `Part` reconstructs them from its native `Size` and `Color` properties.
`Anchored` freezes the body and `CanCollide` disables its shape without
destroying the body. This preserves the same physics identity across edits.
Native Part also owns independent transient Tool freeze reasons: storage (0)
and held assembly (1). Clearing storage after parenting into a character must
not clear held ownership. Effective freeze is authored Anchored OR either
runtime reason; changing Anchored also respects those owners. These reasons
are not serialized, and the inherited effective `freeze` property is excluded
from storage so duplication/reopen cannot turn a temporarily held Part into a
permanently frozen authored object. The client integration gate exercises
reason overlap, Anchored edits, and duplication separately from live Tool
equip/unequip tests.
`Archivable`, `Tags`, and editor `Locked` now use one common state owner in
Godot's native `Node`; the native Instance subclasses expose reflected Roblox-
facing properties instead of maintaining parallel fields or using metadata as
the primary store. Existing `VextoriaTags` and `_edit_lock_` metadata remain
readable for old scenes and migrate away on the first native write. Godot's
SceneTreeDock, 2D/3D viewport picking and lock actions call the same Node lock
API; ordinary non-Vextoria Godot nodes retain their legacy metadata lock path.
The editor registers a native Part gizmo with Godot's Node3DEditor. Its picking
triangles come from the actual internal visual mesh, with the visual's local
size frame baked into Part-local geometry. Selection therefore resolves to
the authored Part rather than an unowned implementation child, including
when CanCollide is false. The normal editor selection, transform, lock and
Inspector paths remain authoritative; there is no second Vextoria picker.
Geometry replacement, size edits and mesh Resource.changed refresh the editor
gizmo. These extra mesh subscriptions exist only in editor mode, not gameplay.
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
managed MultiMesh cache remains in service. The current setting is also stored
under `vextoria/rendering/normal_maps_enabled` so Parts created later inherit
the active render policy during native construction. Complete legacy rendering semantics
and touch/assembly mirrors remain outside this slice.

The native resolver reuses immutable unit meshes and external shader/texture
dependencies via Godot's ResourceLoader cache. Material registry `.tres` files
are loaded with `CACHE_MODE_REUSE` as shared templates; each Part duplicates the
template before applying alpha and normal-map settings. Thus file parsing is
cached without sharing mutable material parameters between Parts. That
per-Part registry copy is also installed directly for rendering instead of
being duplicated a second time; authored overrides still receive an isolated
applied copy. This follows
the native text-resource loader's cache-mode contract in
`scene/resources/resource_format_text.cpp`. Size stays on the internal nodes;
sharing a unit mesh must not share Part size, tint, alpha or normal-map policy.
Resource-backed registry entries now reuse unit colliders in a bounded 16-slot
native cache. Mesh Resource.changed invalidates the slot and coalesces deferred
refreshes on existing Parts; generation occurs outside the cache lock and is
published only for a stable mesh revision. Brick/Wedge retain their private
size-dependent shapes. Derived GeometryMesh/GeometryCollision are rebuilt from
Shape on duplication/reopen; explicit overrides retain storage and detach from
registry refresh. Debug-appearance changes on a Shape do not invalidate the
mesh-derived cache. Cached shapes have Godot's usual shared Resource semantics:
use an explicit private override for per-Part geometry edits.
The cache is released during scene-module teardown, before physics/rendering
servers disappear. Full-world frame-time profiling remains separate work; this
does not establish a whole-world performance target.
This reuse principle was compared with geometry pooling in
`ROBLOX-main/App/v8world/TriangleMesh.cpp::setCompoundMeshData`, which keys its
Bullet decomposition by geometry and scale. Our authored unit-collider reuse
is a Godot-native implementation, not a port of that Bullet decomposition or
a claim of equivalent arbitrary nonuniform-scale physics.
An explicit AppearanceMaterial override relinquishes registry ownership, so
subsequent Color alpha or rendering-policy changes cannot replace that authored
resource. Setting Material explicitly selects the registry again.
Registry-generated appearance is not stored as an authored AppearanceMaterial
in Godot scenes or duplicated nodes: it is rebuilt from semantic Material state.
Custom overrides retain normal Resource storage. This distinction preserves
opacity transitions and normal-map policy after native scene roundtrips.
The ClassDB default for the optional authored AppearanceMaterial override is
explicitly null; it must not retain a live registry shader through engine teardown.

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
changes. It owns row presentation mechanics (collapse, class icon, rename
policy, linked-model visibility/color, and linked/locked badges), native
search, and the TreeItem-to-gameplay-Node selection event. The client publishes
Roblox/Creator metadata and adapts selection to gameplay callbacks such as
gizmos, properties, and Luau signals; it does not maintain a duplicate native
Explorer hierarchy. This does not yet make the entire gameplay object API
native or replace the runtime Creator UI with Godot's editor SceneTreeDock.
When a detached Creator world admits a descendant before its parent has been
explicitly admitted, `ensure_item_for_node` creates marked ancestors first so
the item does not attach to the wrong branch.

`VextoriaSceneHierarchy` owns the native Roblox-Instance hierarchy bridge.
Its child-enumeration API returns the first marked `_vextoria_instance`
descendants beneath a parent in Godot traversal order; unmarked implementation
Nodes are transparent, while traversal stops at each marked Instance. Parent,
descendant, direct-child lookup, filtered index, and reorder all use that same
logical projection. Reorder preserves world transforms when helper-wrapped
siblings must be normalized into physical Godot siblings, and native sibling
name resolution consults the same logical child list. This is the engine
source used for Roblox-facing `Instance.Parent`, `GetChildren()`, and `Index`;
the managed child list is retained only for non-native compatibility objects.
Its `set_instance_parent` operation validates marked Instance nodes, rejects
self/cyclic parenting, and performs native attach, reparent, or detach through
Godot's SceneTree. `can_set_instance_parent` exposes structural preflight. The
native `begin_instance_parent_change` /
`commit_instance_parent_change` / `end_instance_parent_change` contract holds
a per-Node reentrancy sentry across client lifecycle callbacks, permits one
commit to the requested native parent, and rejects nested or mismatched
mutations. The raw native setter permits only a same-parent no-op while that
transaction is active. Runtime ParentLocked state is stored transiently on
Godot `Node`, so generic Node-backed Vextoria Instances, `VextoriaInstance`,
`Model`, and `Part` share one native state owner. It is separate from the
Studio edit `Locked` state; preflight permits a same-parent no-op but rejects
an actual attach/reparent/detach while the runtime lock is set. The lock API
is exposed through `VextoriaSceneHierarchy`. `Node::reparent()` emits
`vextoria_reparented` once
after a successful final attach; the existing parent-change signal still
reports the internal remove/add transitions. These are structural operations
only: NetworkParent bookkeeping, Roblox lifecycle events, parent-lock and
service/DataModel lock lifecycle wiring, class-specific validation policy,
descendant event ordering, temporary-container replication, and Luau callbacks
remain client compatibility responsibilities.

The hierarchy also implements `is_instance_ancestor_of` natively. It walks the
Godot parent chain while matching only marked Instance Nodes, so private engine
helper Nodes do not become Roblox-visible ancestors. This follows
`ROBLOX-main/App/include/v8tree/Instance.h::Instance::isAncestorOf`, which
recursively checks the descendant's parent chain. The client delegates both
`IsAncestorOf` and `IsDescendantOf` to this engine query for native-backed
Instances and retains its legacy fallback for classes not yet migrated.

Native `VextoriaInstance`, `Model`, and `Part` also expose
`get_unique_sibling_name`. The fork resolves suffixes against tagged native
Instance siblings in the supplied Godot parent and a reserved-name list for
transitional non-native compatibility objects. It builds the used-name set
once per mutation instead of repeatedly rescanning siblings for every suffix;
the client can therefore delegate native Instance naming to the engine while
retaining a fallback for classes not yet represented by native classes.

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

## Roblox 2016 transforms and held Tool graph

The fork-native `VextoriaRoblox2016TransformMath` class owns row-major
serialized CFrame conversion, rigid-frame normalization, local-axis size
application, Weld and Motor6D frame equations, and relative-frame conversion.
`VextoriaRoblox2016ToolAssembly` owns the RightGrip and Tool-root equations;
`VextoriaRoblox2016ToolAssemblyDriver` owns cached Handle-connected membership
and applies authored weld-relative frames to live Godot `Node3D` members.
Members are retained as weak Godot `ObjectID` values so deletion cannot leave
stale native pointers in the hot path. The existing C# compatibility facade
prefers these fork classes and keeps the extension implementation as a
transitional fallback for non-Vextoria Godot builds. The fork-native
`VextoriaRoblox2016ToolStateMachine` is authoritative for the six ordered
backend states and reentrant transition generations, indexed by each Tool's
Godot ObjectID and explicitly erased during `PreDelete`; the managed
compatibility layer still resolves Roblox hierarchy prerequisites and
dispatches the corresponding lifecycle effects/signals. It also owns an
atomic per-Tool Workspace pickup claim: duplicate concurrent requests for one
Tool serialize, claims are released on transition out of `IN_WORKSPACE`, and
distinct Tools have no artificial time-based cooldown. Script events,
physics lifecycle, and replication remain managed and are not claimed as
migrated by these classes.

Smoke coverage:

- The client fork smoke stages `engine/tests/native_creator_selection_state_smoke.gd`
  into its test project: it covers engine-owned selection membership/order,
  duplicate suppression, root eligibility, stale-Node pruning, immediate
  ancestry-removal behavior, multiple owners, and per-owner cleanup. Native
  state records Godot ObjectIDs and connects one parent-changing listener per
  selected Node; the managed Creator adapter materializes gameplay Instances
  and still dispatches domain side effects.
- `tests/native_tool_state_machine_smoke.gd`: all 32 prerequisite combinations,
  ordered equip/unequip rungs, stale/reentrant transition rejection, per-Tool
  pickup claim/release and cross-Tool independence, invalid state targets, and
  explicit ObjectID cleanup.
- `tests/part_runtime_smoke.gd`: native physics identity, mesh and collision
  changes, toggles, save/reopen.
- `tests/editor_slice_smoke.gd`: real Godot CreateDialog/SceneTreeDock,
  viewport-click selection/Inspector identity, Locked and noncollidable Parts,
  Wedge/resource replacement, rotated/scaled meshes, source mesh revisions,
  empty-geometry rejection, property/transform and create undo, save/reopen.
  Set `VEXTORIA_NATIVE_EDITOR_CAPTURE` to an absolute PNG path to retain the
  real editor viewport, SceneTreeDock selection and Inspector capture.
- `tests/part_visual_capture.gd`: Vulkan editor-target image of an authored
  native Part, for visual review alongside the executable geometry checks.
- `tests/hinge_frame_smoke.gd`: fork-level joint solver override and
  independent body-B hinge frame contract. The engine implementation lives in
  `scene/3d/physics/joints` and `servers/physics_3d`, not a C# shim.
- `tests/native_hierarchy_smoke.gd`: Model/Folder/Script/Part ClassDB ancestry,
  real Godot parentage, transform/visibility pass-through, CFrame-preserving
  Folder reparent, ordinary-Node isolation, native `VextoriaScript.Source` /
  `Disabled` change notifications with no-op suppression, native sibling-name
  collision/suffix resolution across Folder, Model, and Part, and PackedScene
  save/reopen.
- `tests/native_transform_math_smoke.gd`: serialized CFrame basis/position,
  forward/reverse Weld equations, Motor6D equation, relative frames, and
  nonuniform local size.
- `tests/native_explorer_smoke.gd`: native SceneTree-backed hierarchy and
  bidirectional row identity, confirms identity is not duplicated in
  `TreeItem` metadata, and covers exclusion, rename, reparent, ordering,
  removal, detached-root admission, and out-of-order ancestor admission.
