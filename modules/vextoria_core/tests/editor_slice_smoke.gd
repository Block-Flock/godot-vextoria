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

func _run_smoke() -> void:
	await get_tree().process_frame
	for native_class in ["Part", "Model", "Folder", "VextoriaScript"]:
		if not ClassDB.class_exists(native_class) or not ClassDB.can_instantiate(native_class):
			_fail("native class is not creatable through ClassDB: " + native_class)
			return
		if not ClassDB.is_parent_class(native_class, "Node"):
			_fail("native class is not in the scene tree hierarchy: " + native_class)
			return

	EditorInterface.popup_create_dialog(func(_created: Object) -> void: pass, "Node")
	await get_tree().process_frame
	await get_tree().process_frame
	var create_dialog_has_part := false
	for tree in EditorInterface.get_base_control().find_children("*", "Tree", true, false):
		if _find_tree_item(tree.get_root(), "Part") != null:
			create_dialog_has_part = true
			var popup: Node = tree
			while popup != null and not popup is Window:
				popup = popup.get_parent()
			if popup is Window:
				(popup as Window).hide()
			break
	if not create_dialog_has_part:
		_fail("Godot CreateDialog did not list the native Part class")
		return

	var root: Node = EditorInterface.get_edited_scene_root()
	if root == null:
		root = Node.new()
		root.name = "VextoriaEditorSliceSmoke"
		EditorInterface.add_root_node(root)

	var part: Node = ClassDB.instantiate("Part") as Node
	if part == null:
		_fail("ClassDB did not create a native Part Node")
		return
	part.name = "NativePart"
	root.add_child(part)
	part.owner = root
	if root.find_child("NativePart", false, false) != part:
		_fail("the edited scene tree does not contain the authoritative native Part")
		return

	await get_tree().process_frame
	await get_tree().process_frame
	var explorer_tree: Tree = null
	var native_part_item: TreeItem = null
	for tree in EditorInterface.get_base_control().find_children("*", "Tree", true, false):
		var candidate: TreeItem = _find_tree_item(tree.get_root(), "NativePart")
		if candidate != null:
			explorer_tree = tree
			native_part_item = candidate
			break
	if explorer_tree == null:
		_fail("SceneTreeDock did not display the native Part")
		return
	explorer_tree.set_selected(native_part_item, 0)
	await get_tree().process_frame
	await get_tree().process_frame
	var selection: EditorSelection = EditorInterface.get_selection()
	if not selection.get_selected_nodes().has(part):
		_fail("SceneTreeDock selection did not select the native Part through EditorSelection")
		return

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

	part.set("Size", Vector3(5, 2, 3))
	if part.get("Size") != Vector3(5, 2, 3):
		_fail("Inspector-visible Part.Size did not round-trip")
		return

	print(PASS_MARKER + ": CreateDialog, SceneTreeDock selection, EditorSelection, Inspector identity, Size/Color")
	get_tree().quit(0)
