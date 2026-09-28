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

func _pick_native_part(part: Node3D, camera: Camera3D, surface: Control, root: Node, local_point := Vector3.ZERO) -> bool:
	var selection := EditorInterface.get_selection()
	selection.clear()
	EditorInterface.inspect_object(root)
	for attempt in range(3):
		await get_tree().process_frame
	var world_point := part.global_transform * local_point
	var pick_position := camera.unproject_position(world_point)
	if camera.is_position_behind(world_point) or not Rect2(Vector2.ZERO, Vector2(camera.get_viewport().size)).has_point(pick_position):
		return false
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		click.position = pick_position
		click.global_position = surface.global_position + pick_position
		surface.gui_input.emit(click)
		await get_tree().process_frame
	for attempt in range(10):
		await get_tree().process_frame
		if selection.get_selected_nodes() == [part] and EditorInterface.get_inspector().get_edited_object() == part:
			return true
	return false

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
	# A newly installed edited-scene root is propagated to SceneTreeDock on
	# deferred editor updates. Do not click Create until its tree shows that root.
	var root_visible := false
	for attempt in range(60):
		await get_tree().process_frame
		for tree in EditorInterface.get_base_control().find_children("*", "Tree", true, false):
			if _find_tree_item(tree.get_root(), root.name) != null:
				root_visible = true
				break
		if root_visible:
			break
	if not root_visible:
		_fail("SceneTreeDock did not display the edited-scene root")
		return
	# Editor startup can still restore its active scene tab after the tree first
	# shows a programmatically installed root. Let that initial tab switch settle
	# before testing CreateDialog's own selection behavior.
	for attempt in range(30):
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
	var selection: EditorSelection = EditorInterface.get_selection()
	create_dialog.get_ok_button().pressed.emit()
	var part: Node = null
	# The SceneTreeDock create action and selection notification are deferred by
	# the editor. Wait for its own selection update; do not select the Part here.
	for attempt in range(30):
		await get_tree().process_frame
		root = EditorInterface.get_edited_scene_root()
		for selected_node in selection.get_selected_nodes():
			if selected_node is Node and selected_node.get_class() == "Part" and selected_node.get_parent() == root:
				part = selected_node
				break
		if part != null:
			break
	if part == null:
		_fail("SceneTreeDock/CreateDialog did not insert and select an actual native Part; root=" + str(root) + " children=" + str(root.get_children()) + " selection=" + str(selection.get_selected_nodes()))
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
	var has_shape := false
	var has_material := false
	var has_geometry := false
	var has_geometry_mesh := false
	var has_geometry_collision := false
	var has_appearance := false
	for property in part.get_property_list():
		has_size = has_size or property.name == "Size"
		has_color = has_color or property.name == "Color"
		has_shape = has_shape or property.name == "Shape"
		has_material = has_material or property.name == "Material"
		has_geometry = has_geometry or property.name == "Geometry"
		has_geometry_mesh = has_geometry_mesh or property.name == "GeometryMesh"
		has_geometry_collision = has_geometry_collision or property.name == "GeometryCollision"
		has_appearance = has_appearance or property.name == "AppearanceMaterial"
	if not has_size or not has_color or not has_shape or not has_material or not has_geometry or not has_geometry_mesh or not has_geometry_collision or not has_appearance:
		_fail("ClassDB did not expose Part Size/Color/Shape/Material and native resources to the Inspector")
		return

	# Feed the editor viewport's real input surface, not EditorSelection.add_node.
	# A native Part's internal Visual/Collision children are deliberately unowned
	# scene implementation details. Its visible surface must still select Part.
	EditorInterface.set_main_screen_editor("3D")
	for attempt in range(10):
		await get_tree().process_frame
	var viewport := EditorInterface.get_editor_viewport_3d(0)
	var camera := viewport.get_camera_3d()
	var surface: Control = null
	for child in viewport.get_parent().get_parent().get_children():
		if not child is Control:
			continue
		for connection in child.get_signal_connection_list("gui_input"):
			if connection.callable.get_object() == viewport.get_parent().get_parent():
				surface = child
				break
	if surface == null or camera == null:
		_fail("native editor viewport input surface/camera was not found; camera=" + str(camera) + " children=" + str(viewport.get_parent().get_parent().get_children()))
		return
	# Viewport picking follows the rendered mesh, not runtime CanCollide.
	part.set("CanCollide", false)
	if not await _pick_native_part(part, camera, surface, root):
		_fail("viewport click did not select/inspect the visible native Part; selected=" + str(selection.get_selected_nodes()))
		return
	part.set("Locked", true)
	if await _pick_native_part(part, camera, surface, root):
		_fail("viewport picking ignored native Part Locked")
		return
	part.set("Locked", false)
	part.set("Shape", 4)
	if not await _pick_native_part(part, camera, surface, root):
		_fail("viewport picking lost native Wedge after geometry replacement")
		return
	var pick_mesh := BoxMesh.new()
	pick_mesh.size = Vector3.ONE
	part.set("GeometryMesh", pick_mesh)
	part.set("GeometryCollision", BoxShape3D.new())
	part.set("Geometry", 2)
	# A point beyond the unscaled unit mesh proves the internal size frame was
	# baked into Part-local picking triangles, rather than silently discarded.
	part.set("Size", Vector3(4, 3, 2))
	part.rotation = Vector3(0.15, 0.3, -0.1)
	if not await _pick_native_part(part, camera, surface, root, Vector3(1.5, 0, 0)):
		_fail("viewport picking did not follow the scaled/rotated native resource mesh")
		return
	pick_mesh.size = Vector3(2, 1, 1)
	if not await _pick_native_part(part, camera, surface, root, Vector3(3, 0, 0)):
		_fail("viewport picking did not refresh after an authored mesh revision")
		return
	part.set("GeometryMesh", ArrayMesh.new())
	if await _pick_native_part(part, camera, surface, root):
		_fail("viewport retained picking triangles after the native visual became empty")
		return
	part.set("Shape", 0)
	part.set("GeometryMesh", null)
	part.set("GeometryCollision", null)
	part.set("Size", Vector3(4, 1, 2))
	part.rotation = Vector3.ZERO
	part.set("CanCollide", true)
	if not await _pick_native_part(part, camera, surface, root):
		_fail("viewport picking did not recover after returning to native Brick")
		return
	var editor_capture := OS.get_environment("VEXTORIA_NATIVE_EDITOR_CAPTURE")
	if not editor_capture.is_empty():
		await RenderingServer.frame_post_draw
		if get_tree().root.get_texture().get_image().save_png(editor_capture) != OK:
			_fail("could not save the native editor viewport/selection capture")
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
	editor_undo_redo.add_do_property(part, "Shape", 4)
	editor_undo_redo.add_undo_property(part, "Shape", 0)
	editor_undo_redo.add_do_property(part, "Material", 8)
	editor_undo_redo.add_undo_property(part, "Material", 0)
	var authored_appearance := StandardMaterial3D.new()
	authored_appearance.roughness = 0.35
	editor_undo_redo.add_do_property(part, "AppearanceMaterial", authored_appearance)
	editor_undo_redo.add_undo_property(part, "AppearanceMaterial", null)
	editor_undo_redo.add_do_property(part, "position", Vector3(8, 4, 2))
	editor_undo_redo.add_undo_property(part, "position", Vector3.ZERO)
	editor_undo_redo.commit_action()
	if part.get("Size") != Vector3(5, 2, 3) or part.position != Vector3(8, 4, 2) or part.get("Shape") != 4 or part.get("Material") != 8:
		_fail("native Part property/transform edit was not applied")
		return
	if not undo_stack.undo() or part.get("Size") != Vector3(4, 1, 2) or part.position != Vector3.ZERO or part.get("Shape") != 0 or part.get("Material") != 0:
		_fail("native Part property/transform undo failed")
		return
	if part.get("AppearanceMaterial") != null:
		_fail("native Part material undo failed")
		return
	if not undo_stack.redo() or part.get("Size") != Vector3(5, 2, 3) or part.position != Vector3(8, 4, 2) or part.get("Shape") != 4 or part.get("Material") != 8:
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
	if reopened_part.get("Shape") != 4 or reopened_part.get("Material") != 8 or reopened_part.get("Geometry") != 1 or reopened_part.get_node("Visual").mesh is not ArrayMesh or reopened_part.get_node("Collision").shape is not ConvexPolygonShape3D:
		_fail("save/reopen lost native Shape/Material or Wedge geometry")
		return
	if reopened_part.get("AppearanceMaterial") is not StandardMaterial3D or not is_equal_approx(reopened_part.get_node("Visual").material_override.roughness, 0.35):
		_fail("save/reopen lost native appearance")
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
	print(PASS_MARKER + ": viewport picking, SceneTreeDock, Inspector, property/transform undo, native save/reopen, creation undo")
	get_tree().quit(0)
