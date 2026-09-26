@tool
extends EditorPlugin

const PASS_MARKER := "VEXTORIA_EDITOR_SLICE_PASS"

func _enter_tree() -> void:
	call_deferred("_run_smoke")

func _fail(message: String) -> void:
	push_error("Vextoria editor slice smoke failed: " + message)
	get_tree().quit(1)

func _find_tree_item(item: TreeItem, expected: String) -> TreeItem:
	if item == null:
		return null
	if item.get_text(0) == expected:
		return item
	var child: TreeItem = item.get_first_child()
	while child != null:
		var found: TreeItem = _find_tree_item(child, expected)
		if found != null:
			return found
		child = child.get_next()
	return null

func _find_node_named(parent: Node, expected_name: String) -> Node:
	if parent.name == expected_name:
		return parent
	for child in parent.get_children():
		var found := _find_node_named(child, expected_name)
		if found != null:
			return found
	return null

func _run_smoke() -> void:
	await get_tree().process_frame
	for native_class in ["Part", "Model", "Folder", "VextoriaScript"]:
		if not ClassDB.class_exists(native_class) or not ClassDB.can_instantiate(native_class):
			_fail("native class is not creatable through ClassDB: " + native_class)
			return
		if not ClassDB.is_parent_class(native_class, "Node"):
			_fail("native class is not in the scene tree hierarchy: " + native_class)
			return

	var root: Node = EditorInterface.get_edited_scene_root()
	if root == null:
		root = Node.new()
		root.name = "VextoriaEditorSliceSmoke"
		EditorInterface.add_root_node(root)
	await get_tree().process_frame

	# Invoke SceneTreeDock's real Add/Create button. This opens its private
	# CreateDialog, whose create signal is wired to SceneTreeDock::_create().
	var add_button: Button = null
	for button in EditorInterface.get_base_control().find_children("*", "Button", true, false):
		if button.tooltip_text == "Add/Create a New Node.":
			add_button = button
			break
	if add_button == null:
		_fail("SceneTreeDock Add/Create button was not found")
		return
	add_button.pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame

	var create_dialog: ConfirmationDialog = null
	var create_tree: Tree = null
	var part_item: TreeItem = null
	for tree in EditorInterface.get_base_control().find_children("*", "Tree", true, false):
		var candidate := _find_tree_item(tree.get_root(), "Part")
		if candidate == null:
			continue
		var ancestor: Node = tree
		while ancestor != null and not ancestor is ConfirmationDialog:
			ancestor = ancestor.get_parent()
		if ancestor is ConfirmationDialog:
			create_dialog = ancestor
			create_tree = tree
			part_item = candidate
			break
	if create_dialog == null or part_item == null:
		_fail("SceneTreeDock CreateDialog did not list the native Part class")
		return
	for native_class in ["Model", "Folder", "VextoriaScript"]:
		if _find_tree_item(create_tree.get_root(), native_class) == null:
			_fail("SceneTreeDock CreateDialog did not list native class: " + native_class)
			return

	create_tree.set_selected(part_item, 0)
	await get_tree().process_frame
	if create_dialog.get_ok_button().disabled:
		_fail("CreateDialog marked native Part non-instantiable")
		return
	create_dialog.get_ok_button().pressed.emit()
	await get_tree().process_frame
	await get_tree().process_frame

	root = EditorInterface.get_edited_scene_root()
	var selection: EditorSelection = EditorInterface.get_selection()
	var part: Node = null
	for selected_node in selection.get_selected_nodes():
		if selected_node is Node and selected_node.get_class() == "Part" and selected_node.get_parent() == root:
			part = selected_node
			break
	if part == null:
		_fail("SceneTreeDock/CreateDialog did not insert and select an actual native Part")
		return
	if part.owner != root:
		_fail("CreateDialog Part was not owned by the edited scene")
		return

	var explorer_tree: Tree = null
	var part_tree_item: TreeItem = null
	for tree in EditorInterface.get_base_control().find_children("*", "Tree", true, false):
		var candidate: TreeItem = _find_tree_item(tree.get_root(), part.name)
		if candidate != null:
			# SceneTreeDock tree entries carry the native NodePath as metadata.
			var metadata: Variant = candidate.get_metadata(0)
			if metadata == part.get_path():
				explorer_tree = tree
				part_tree_item = candidate
				break
	if explorer_tree == null:
		_fail("SceneTreeDock did not display the created native Part")
		return
	explorer_tree.set_selected(part_tree_item, 0)
	await get_tree().process_frame

	EditorInterface.inspect_object(part)
	await get_tree().process_frame
	var inspector: EditorInspector = EditorInterface.get_inspector()
	if inspector == null or inspector.get_edited_object() != part:
		_fail("the Inspector is not editing the same native Part")
		return

	var has_size := false
	var has_color := false
	for property in part.get_property_list():
		has_size = has_size or property.name == "Size"
		has_color = has_color or property.name == "Color"
	if not has_size or not has_color:
		_fail("ClassDB did not expose Part.Size and Part.Color to the Inspector")
		return

	var editor_undo_redo: EditorUndoRedoManager = EditorInterface.get_editor_undo_redo()
	var scene_history_id: int = editor_undo_redo.get_object_history_id(part)
	var undo_stack: UndoRedo = editor_undo_redo.get_history_undo_redo(scene_history_id)
	if undo_stack == null or not undo_stack.has_undo():
		_fail("native CreateDialog action was not recorded in EditorUndoRedoManager")
		return

	# These are Godot's own Inspector/Node3D properties on the same native Part.
	# Property and transform edits must share the scene's undo stack with the
	# SceneTreeDock create action, not a Vextoria-side parallel history.
	editor_undo_redo.create_action("Edit native Part", UndoRedo.MERGE_DISABLE, part)
	editor_undo_redo.add_do_property(part, "Size", Vector3(5, 2, 3))
	editor_undo_redo.add_undo_property(part, "Size", Vector3(4, 1, 2))
	editor_undo_redo.add_do_property(part, "position", Vector3(8, 4, 2))
	editor_undo_redo.add_undo_property(part, "position", Vector3.ZERO)
	editor_undo_redo.commit_action()
	if part.get("Size") != Vector3(5, 2, 3) or part.position != Vector3(8, 4, 2):
		_fail("native Part property/transform edit was not applied")
		return
	if not undo_stack.undo() or part.get("Size") != Vector3(4, 1, 2) or part.position != Vector3.ZERO:
		_fail("native Part property/transform undo failed")
		return
	if not undo_stack.redo() or part.get("Size") != Vector3(5, 2, 3) or part.position != Vector3(8, 4, 2):
		_fail("native Part property/transform redo failed")
		return

	var packed := PackedScene.new()
	if packed.pack(root) != OK:
		_fail("Godot could not pack the native Part scene")
		return
	var path := "user://vextoria_editor_smoke_%d.tscn" % Time.get_ticks_usec()
	if ResourceSaver.save(packed, path) != OK:
		_fail("Godot could not save the native Part scene")
		return
	var reopened: PackedScene = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	if reopened == null:
		_fail("Godot could not reopen the native Part scene")
		return
	var reopened_root := reopened.instantiate()
	var reopened_part := reopened_root.get_node_or_null(NodePath(String(part.name)))
	if reopened_part == null or reopened_part.get_class() != "Part" or reopened_part.get("Size") != Vector3(5, 2, 3) or reopened_part.position != Vector3(8, 4, 2):
		_fail("save/reopen lost native Part identity, Size, or transform")
		return
	reopened_root.free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))

	# The previous history action is the native CreateDialog insertion.
	if not undo_stack.undo():
		_fail("could not undo property/transform edit before creation")
		return
	if not undo_stack.undo():
		_fail("EditorUndoRedoManager could not undo native Part creation")
		return
	await get_tree().process_frame
	if part.get_parent() != null or root.find_child("Part", false, false) != null:
		_fail("undo did not remove the created native Part from the edited scene")
		return
	if not undo_stack.has_redo() or not undo_stack.redo():
		_fail("EditorUndoRedoManager could not redo native Part creation")
		return
	await get_tree().process_frame
	if part.get_parent() != root or root.find_child("Part", false, false) != part:
		_fail("redo did not restore the same native Part to the edited scene")
		return

	selection.clear()
	EditorInterface.inspect_object(root)
	await get_tree().process_frame
	print(PASS_MARKER + ": SceneTreeDock, Inspector, property/transform undo, native save/reopen, creation undo")
	get_tree().quit(0)
