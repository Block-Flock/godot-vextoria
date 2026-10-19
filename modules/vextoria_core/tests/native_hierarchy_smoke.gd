extends SceneTree

class ParentChangeCounter extends RefCounted:
	var count := 0

	func record() -> void:
		count += 1

class CompletedReparentCounter extends RefCounted:
	var node: Node
	var parents: Array[Variant] = []

	func record() -> void:
		parents.append(node.get_parent())

class ParentChangingCounter extends RefCounted:
	var node: Node
	var transitions: Array[Array] = []

	func record(old_parent: Node, new_parent: Node) -> void:
		transitions.append([old_parent, new_parent, node.get_parent()])

class PropertyChangeCounter extends RefCounted:
	var properties: Array[StringName] = []

	func record(property: StringName) -> void:
		properties.append(property)

class InstancePropertyChangeCounter extends RefCounted:
	var changes: Array[Array] = []

	func record(property: StringName, previous: Variant, current: Variant) -> void:
		changes.append([property, previous, current])

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	assert(ClassDB.is_parent_class("Model", "Node3D"), "Model must carry a spatial frame")
	assert(ClassDB.is_parent_class("Folder", "Node"), "Folder must be a Node")
	assert(not ClassDB.is_parent_class("Folder", "Node3D"), "Folder must not have a transform")
	assert(ClassDB.is_parent_class("VextoriaScript", "Node"), "Script must be a Node")
	assert(not ClassDB.is_parent_class("VextoriaScript", "Node3D"), "Script must not have a transform")

	var scene_root := Node3D.new()
	scene_root.name = "NativeHierarchySmoke"
	root.add_child(scene_root)
	var model: Node3D = ClassDB.instantiate("Model")
	model.name = "House"
	scene_root.add_child(model)
	model.owner = scene_root
	model.position = Vector3(4, 2, 7)
	model.set("Locked", true)
	assert(model.has_meta("_edit_lock_"), "Model must use Godot's editor lock")
	var folder: Node = ClassDB.instantiate("Folder")
	folder.name = "Details"
	assert(folder.has_signal("vextoria_parent_changed"), "native Node must expose the Vextoria parent-change bridge")
	assert(folder.has_signal("vextoria_instance_property_changed"), "native Instance must expose old/new property values")
	assert(folder.get("Tags") == PackedStringArray(), "native Instance Tags must default to empty")
	var instance_property_changes := InstancePropertyChangeCounter.new()
	folder.connect("vextoria_instance_property_changed", Callable(instance_property_changes, "record"))
	folder.set("Tags", PackedStringArray(["fixture", "folder"]))
	folder.set("Tags", PackedStringArray(["fixture", "folder"]))
	folder.set("Archivable", false)
	assert(folder.get("Tags") == PackedStringArray(["fixture", "folder"]), "native Folder Tags property must round trip")
	assert(instance_property_changes.changes == [
		[&"Tags", PackedStringArray(), PackedStringArray(["fixture", "folder"])],
		[&"Archivable", true, false],
	], "native Instance changes must include exact previous/current values and suppress no-op writes")
	var parent_changes := ParentChangeCounter.new()
	folder.connect("vextoria_parent_changed", Callable(parent_changes, "record"))
	assert(folder.has_signal("vextoria_reparented"), "native Node must expose an atomic completed-reparent bridge")
	var completed_reparents := CompletedReparentCounter.new()
	completed_reparents.node = folder
	folder.connect("vextoria_reparented", Callable(completed_reparents, "record"))
	assert(folder.has_signal("vextoria_parent_changing"), "native Node must expose a pre-mutation parent transition")
	var parent_changing := ParentChangingCounter.new()
	parent_changing.node = folder
	folder.connect("vextoria_parent_changing", Callable(parent_changing, "record"))
	model.add_child(folder)
	assert(parent_changes.count == 1, "adding a child must emit exactly one native parent-change signal")
	var folder_sibling: Node = ClassDB.instantiate("Folder")
	folder_sibling.name = "Details2"
	model.add_child(folder_sibling)
	assert(folder.call("get_unique_sibling_name", model, "Details2", PackedStringArray(["Details3"])) == "Details4", "native name resolver must exclude its subject, include native siblings and reserved compatibility names")
	assert(model.call("get_unique_sibling_name", scene_root, "AvailableName", PackedStringArray()) == "AvailableName", "Model native name resolver must leave an unused sibling name unchanged")
	folder_sibling.free()
	folder.owner = scene_root
	var script: Node = ClassDB.instantiate("VextoriaScript")
	script.name = "LampScript"
	script.set_meta("_vextoria_instance", true)
	script.set("Tags", PackedStringArray(["script-tag"]))
	folder.add_child(script)
	script.owner = scene_root
	assert(script.has_signal("vextoria_property_changed"), "native Script must expose property synchronization")
	var property_changes := PropertyChangeCounter.new()
	script.connect("vextoria_property_changed", Callable(property_changes, "record"))
	script.set("Source", "return true")
	script.set("Source", "return true")
	script.set("Disabled", true)
	script.set("Disabled", true)
	assert(property_changes.properties == [&"Source", &"Disabled"], "native Script edits must signal only actual Source/Disabled changes")
	var part: RigidBody3D = ClassDB.instantiate("Part")
	part.name = "Wall"
	part.set("Tags", PackedStringArray(["part-tag"]))
	script.add_child(part)
	var part_sibling: RigidBody3D = ClassDB.instantiate("Part")
	part_sibling.name = "Wall2"
	script.add_child(part_sibling)
	assert(part.call("get_unique_sibling_name", script, "Wall2", PackedStringArray(["Wall3"])) == "Wall4", "Part native name resolver must include native siblings and reserved compatibility names")
	part_sibling.free()
	part.owner = scene_root
	part.set("Anchored", true)
	part.set("Size", Vector3(2, 3, 1))
	assert(part.get_parent_node_3d() == model, "Folder and Script must pass through the nearest spatial parent")
	assert(part.global_position.is_equal_approx(model.global_position), "non-spatial containers must not add an offset")
	model.position = Vector3(-6, 4, 11)
	assert(part.global_position.is_equal_approx(model.global_position), "Model movement must reach Part across non-spatial containers")
	var visual: MeshInstance3D = part.get_node("Visual")
	model.visible = false
	assert(not visual.is_visible_in_tree(), "Model visibility must reach Part across non-spatial containers")
	model.visible = true
	assert(visual.is_visible_in_tree(), "Part visual must restore when Model becomes visible")
	assert(folder.get_children().size() == 1 and script.get_children().size() == 1, "Godot hierarchy must own its actual script and Part")
	var plain := Node.new()
	model.add_child(plain)
	var plain_part: RigidBody3D = ClassDB.instantiate("Part")
	plain.add_child(plain_part)
	plain_part.set("Anchored", true)
	assert(plain_part.get_parent_node_3d() == null, "ordinary Godot Nodes must not acquire Vextoria pass-through behavior")
	var other_model: Node3D = ClassDB.instantiate("Model")
	other_model.name = "OtherHouse"
	other_model.set_meta("_vextoria_instance", true)
	other_model.set("Tags", PackedStringArray(["model-tag"]))
	scene_root.add_child(other_model)
	assert(model.call("get_unique_sibling_name", scene_root, "OtherHouse", PackedStringArray()) == "OtherHouse2", "Model name resolver must suffix a native spatial sibling collision")
	other_model.owner = scene_root
	other_model.position = Vector3(20, -3, 6)
	var world_before_reparent := part.global_transform
	folder.reparent(other_model, true)
	assert(parent_changes.count == 3, "reparent must report detach and attach to native observers")
	assert(completed_reparents.parents == [other_model], "reparent completion must emit once after the final parent is installed")
	assert(parent_changing.transitions == [[model, other_model, model]], "native reparent must announce old/new parents while old ancestry is intact")
	assert(folder.get_parent() == other_model and part.get_parent_node_3d() == other_model, "reparent must update logical and spatial ancestry")
	assert(part.global_transform.is_equal_approx(world_before_reparent), "Folder reparent must preserve Part world CFrame")
	other_model.position += Vector3(3, 0, 0)
	assert(part.global_position.is_equal_approx(world_before_reparent.origin + Vector3(3, 0, 0)), "new Model transform must reach Part")
	var world_before_return := part.global_transform
	folder.reparent(model, true)
	assert(parent_changes.count == 5, "return reparent must report both native parent transitions")
	assert(completed_reparents.parents == [other_model, model], "second reparent completion must not expose a transient detached parent")
	assert(parent_changing.transitions.size() == 2 && parent_changing.transitions[1] == [other_model, model, other_model], "return reparent must preserve old ancestry during pre-change notification")
	assert(part.global_transform.is_equal_approx(world_before_return), "return reparent must preserve Part world CFrame")
	var packed := PackedScene.new()
	assert(packed.pack(scene_root) == OK, "native hierarchy must pack")
	var path := "user://native_hierarchy_smoke_%d.tscn" % Time.get_ticks_usec()
	assert(ResourceSaver.save(packed, path) == OK, "native hierarchy must save")
	var reopened: PackedScene = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	assert(reopened != null, "native hierarchy must reload")
	var reopened_root := reopened.instantiate()
	var reopened_model := reopened_root.get_node("House")
	var reopened_folder := reopened_root.get_node("House/Details")
	var reopened_script := reopened_root.get_node("House/Details/LampScript")
	var reopened_part := reopened_root.get_node("House/Details/LampScript/Wall")
	assert(reopened_model.get_class() == "Model" and reopened_model is Node3D, "Model must retain native spatial identity")
	assert(reopened_folder.get_class() == "Folder" and not reopened_folder is Node3D, "Folder must retain native non-spatial identity")
	assert(reopened_script.get_class() == "VextoriaScript" and not reopened_script is Node3D, "Script must retain native non-spatial identity")
	assert(reopened_part.get_class() == "Part" and reopened_part.get("Size") == Vector3(2, 3, 1), "Part must retain native physics identity")
	assert(reopened_folder.get("Tags") == PackedStringArray(["fixture", "folder"]), "Folder Tags must survive save/reopen")
	assert(not reopened_folder.get("Archivable"), "Folder Archivable must survive save/reopen")
	assert(reopened_script.get("Tags") == PackedStringArray(["script-tag"]), "Script Tags must survive save/reopen")
	assert(reopened_part.get("Tags") == PackedStringArray(["part-tag"]), "Part Tags must survive save/reopen")
	assert(reopened_root.get_node("OtherHouse").get("Tags") == PackedStringArray(["model-tag"]), "Model Tags must survive save/reopen")
	assert(reopened_part.get_parent_node_3d() == reopened_model, "saved hierarchy must retain spatial pass-through")
	assert(reopened_model.get("Locked") and reopened_script.get("Source") == "return true" and reopened_script.get("Disabled"), "native properties must survive save/reopen")
	model.remove_child(folder)
	assert(parent_changes.count == 6 and folder.get_parent() == null, "remove_child must report the final detached state")
	assert(parent_changing.transitions.size() == 3 && parent_changing.transitions[2] == [model, null, model], "native detach must announce removal before clearing the parent")
	assert(completed_reparents.parents.size() == 2, "remove_child alone must not masquerade as an atomic reparent")
	model.add_child(folder)
	assert(parent_changes.count == 7 and folder.get_parent() == model, "re-adding a detached child must report the new parent")
	assert(completed_reparents.parents.size() == 2, "add_child alone must not masquerade as an atomic reparent")
	assert(DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK, "smoke output cleanup failed")
	reopened_root.free()
	print("VEXTORIA_NATIVE_HIERARCHY_PASS: spatial Model, non-spatial Folder/Script, native Tags and Script property signals, real Part, parent-change bridge, save/reopen")
	quit(0)
