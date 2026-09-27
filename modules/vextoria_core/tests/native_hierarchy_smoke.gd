extends SceneTree

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
	model.add_child(folder)
	folder.owner = scene_root
	var script: Node = ClassDB.instantiate("VextoriaScript")
	script.name = "LampScript"
	folder.add_child(script)
	script.owner = scene_root
	script.set("Source", "return true")
	var part: RigidBody3D = ClassDB.instantiate("Part")
	part.name = "Wall"
	script.add_child(part)
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
	scene_root.add_child(other_model)
	other_model.owner = scene_root
	other_model.position = Vector3(20, -3, 6)
	var world_before_reparent := part.global_transform
	folder.reparent(other_model, true)
	assert(folder.get_parent() == other_model and part.get_parent_node_3d() == other_model, "reparent must update logical and spatial ancestry")
	assert(part.global_transform.is_equal_approx(world_before_reparent), "Folder reparent must preserve Part world CFrame")
	other_model.position += Vector3(3, 0, 0)
	assert(part.global_position.is_equal_approx(world_before_reparent.origin + Vector3(3, 0, 0)), "new Model transform must reach Part")
	var world_before_return := part.global_transform
	folder.reparent(model, true)
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
	assert(reopened_part.get_parent_node_3d() == reopened_model, "saved hierarchy must retain spatial pass-through")
	assert(reopened_model.get("Locked") and reopened_script.get("Source") == "return true", "native properties must survive save/reopen")
	assert(DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK, "smoke output cleanup failed")
	reopened_root.free()
	print("VEXTORIA_NATIVE_HIERARCHY_PASS: spatial Model, non-spatial Folder/Script, real Part, save/reopen")
	quit(0)
