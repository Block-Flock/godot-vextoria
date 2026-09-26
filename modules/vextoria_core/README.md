# Native Vextoria object cutover

`Part`, `Model`, `Folder`, and `VextoriaScript` are Godot ClassDB Nodes. The
Godot SceneTreeDock, EditorSelection, Inspector, and EditorUndoRedoManager own
their editor identity; this module does not create a second Explorer tree.

`Part` is a `RigidBody3D` with internal mesh and collision children. Those
children are not independent Vextoria objects and are not serialized; a
reopened `Part` reconstructs them from its native `Size` and `Color` properties.
`Anchored` freezes the body and `CanCollide` disables its shape without
destroying the body. This preserves the same physics identity across edits.

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
