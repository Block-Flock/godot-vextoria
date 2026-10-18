extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var scene_root := Node3D.new()
	scene_root.name = "ModelPrimaryPartSmoke"
	root.add_child(scene_root)

	var model: Node3D = ClassDB.instantiate("Model")
	model.name = "Assembly"
	scene_root.add_child(model)
	var folder: Node = ClassDB.instantiate("Folder")
	model.add_child(folder)
	var primary: RigidBody3D = ClassDB.instantiate("Part")
	primary.name = "Primary"
	primary.set("Anchored", true)
	primary.set("Size", Vector3(2, 3, 4))
	folder.add_child(primary)
	var member: RigidBody3D = ClassDB.instantiate("Part")
	member.name = "Member"
	member.set("Anchored", true)
	member.set("Size", Vector3(1, 2, 5))
	model.add_child(member)
	var external: RigidBody3D = ClassDB.instantiate("Part")
	external.name = "External"
	external.set("Anchored", true)
	scene_root.add_child(external)

	model.global_transform = Transform3D(Basis.from_euler(Vector3(0.1, -0.2, 0.05)), Vector3(3, 1, -4))
	primary.global_transform = Transform3D(Basis.from_euler(Vector3(-0.4, 0.3, 0.2)), Vector3(6, 2, 1))
	member.global_transform = Transform3D(Basis.from_euler(Vector3(0.25, -0.1, 0.6)), Vector3(-2, 5, 7))
	external.global_transform = Transform3D(Basis.from_euler(Vector3(0.2, 0.4, -0.3)), Vector3(20, 9, 11))

	model.set("PrimaryPart", primary)
	assert(model.get("PrimaryPart") == primary, "Model.PrimaryPart must resolve to its native Part")
	assert(model.call("get_primary_part_cframe").is_equal_approx(primary.global_transform), "native GetPrimaryPartCFrame must return the Part's world rigid frame")

	var old_primary := primary.global_transform
	var old_member := member.global_transform
	var old_external := external.global_transform
	var primary_size: Vector3 = primary.get("Size")
	var member_size: Vector3 = member.get("Size")
	var target := Transform3D(Basis.from_euler(Vector3(-0.7, 0.8, -0.25)), Vector3(-9, 12, 6))
	var expected_delta := target * old_primary.affine_inverse()
	assert(model.call("set_primary_part_cframe", target), "native SetPrimaryPartCFrame should succeed with a live primary Part")
	assert(primary.global_transform.is_equal_approx(expected_delta * old_primary), "primary Part did not reach the requested rigid CFrame")
	assert(member.global_transform.is_equal_approx(expected_delta * old_member), "nested/ordinary descendant Part did not retain its relative frame")
	assert(model.call("get_primary_part_cframe").is_equal_approx(target), "native primary-part CFrame getter/setter round trip diverged")
	assert(primary.get("Size") == primary_size and member.get("Size") == member_size, "rigid Model movement changed authored Part.Size")
	assert(external.global_transform.is_equal_approx(old_external), "moving a Model changed an unrelated Part")

	model.owner = scene_root
	folder.owner = scene_root
	primary.owner = scene_root
	member.owner = scene_root
	external.owner = scene_root
	var packed := PackedScene.new()
	assert(packed.pack(scene_root) == OK, "Model PrimaryPart scene must pack")
	var path := "user://model_primary_part_smoke_%d.tscn" % Time.get_ticks_usec()
	assert(ResourceSaver.save(packed, path) == OK, "Model PrimaryPart scene must save")
	var reopened_scene: PackedScene = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	assert(reopened_scene != null, "Model PrimaryPart scene must reopen")
	var reopened_root := reopened_scene.instantiate()
	root.add_child(reopened_root)
	var reopened_model := reopened_root.get_node("Assembly")
	var reopened_primary := reopened_root.find_child("Primary", true, false)
	assert(reopened_primary != null, "authored Primary Part must survive native scene save/reopen")
	assert(reopened_model.get("PrimaryPart") == reopened_primary, "Model.PrimaryPart reference must survive native scene save/reopen")
	assert(reopened_model.call("get_primary_part_cframe").is_equal_approx(target), "PrimaryPart CFrame must survive native scene save/reopen")

	model.set("PrimaryPart", null)
	assert(model.get("PrimaryPart") == null, "clearing Model.PrimaryPart must clear native state")
	assert(not model.call("set_primary_part_cframe", Transform3D.IDENTITY), "SetPrimaryPartCFrame must fail without a live PrimaryPart")
	model.set("PrimaryPart", primary)
	folder.remove_child(primary)
	scene_root.add_child(primary)
	assert(model.get("PrimaryPart") == null, "reparenting PrimaryPart outside the Model must clear its native reference")
	assert(not model.call("set_primary_part_cframe", Transform3D.IDENTITY), "SetPrimaryPartCFrame must fail after PrimaryPart leaves the Model")
	assert(DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK, "smoke output cleanup failed")
	reopened_root.free()
	print("VEXTORIA_NATIVE_MODEL_PRIMARY_PART_PASS: native reference, rigid descendant movement, size preservation, unrelated-part isolation, save/reopen, clear/no-primary, reparent cleanup")
	quit(0)
