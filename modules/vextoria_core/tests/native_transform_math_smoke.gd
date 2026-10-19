extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	assert(ClassDB.class_exists("VextoriaRoblox2016TransformMath"), "fork-native transform math is not registered")
	var math_class := "VextoriaRoblox2016TransformMath"
	var angle := 0.73
	var c := cos(angle)
	var s := sin(angle)
	var position := Vector3(13.25, -7.5, 2.125)
	var frame: Transform3D = ClassDB.class_call_static(
		math_class,
		"from_serialized_rows",
		position,
		Vector3(c, 0.0, s),
		Vector3(0.0, 1.0, 0.0),
		Vector3(-s, 0.0, c))
	assert(frame.origin.is_equal_approx(position), "serialized CFrame translation changed")
	assert(frame.basis.is_equal_approx(Basis(Vector3.UP, angle)), "serialized CFrame row/column conversion mirrored or transposed the frame")

	var part0 := Transform3D(Basis(Vector3(1.0, 2.0, -1.0).normalized(), 0.61), Vector3(4.0, 8.0, -2.0))
	var c0 := Transform3D(Basis(Vector3.FORWARD, -0.37), Vector3(0.5, 2.0, 1.0))
	var c1 := Transform3D(Basis(Vector3.RIGHT, 0.82), Vector3(-1.0, 0.25, 3.0))
	var part1: Transform3D = ClassDB.class_call_static(math_class, "solve_weld_part1", part0, c0, c1)
	assert((part0 * c0).is_equal_approx(part1 * c1), "native Weld solver violates Part0*C0 == Part1*C1")
	var reverse: Transform3D = ClassDB.class_call_static(math_class, "solve_weld_neighbor", part1, c0, c1, false)
	assert(reverse.is_equal_approx(part0), "reverse traversal of authored Weld frames differs")
	var motor_transform := Transform3D(Basis(Vector3.UP, -0.29), Vector3(0.0, 0.4, 0.0))
	var motor_part1: Transform3D = ClassDB.class_call_static(math_class, "solve_motor6d_part1", part0, c0, motor_transform, c1)
	assert((part0 * c0 * motor_transform).is_equal_approx(motor_part1 * c1), "native Motor6D solver violates Part0*C0*Transform == Part1*C1")

	var relative: Transform3D = ClassDB.class_call_static(math_class, "relative_frame", part0, part1)
	assert((part0 * relative).is_equal_approx(part1), "native relative-frame conversion differs")
	var sized: Transform3D = ClassDB.class_call_static(math_class, "with_local_size", frame, Vector3(2.0, 3.0, 4.0))
	assert(sized.basis.x.length() == 2.0 and sized.basis.y.length() == 3.0 and sized.basis.z.length() == 4.0, "nonuniform size was not applied in local axes")
	assert(sized.origin == position, "local size application changed the authored position")

	var parent := Node3D.new()
	parent.transform = Transform3D(Basis(Vector3.UP, 0.41).scaled(Vector3(2.0, 3.0, 0.5)), Vector3(-8.0, 1.0, 5.0))
	root.add_child(parent)
	var child := Node3D.new()
	child.transform = Transform3D(Basis(Vector3.RIGHT, -0.32).scaled(Vector3(1.5, 0.75, 2.0)), Vector3(1.0, 2.0, -3.0))
	parent.add_child(child)
	var world_cframe: Transform3D = ClassDB.class_call_static(math_class, "get_world_cframe", child)
	assert(world_cframe.origin.is_equal_approx(child.global_position), "world CFrame getter changed the world position")
	assert(world_cframe.basis.is_equal_approx(child.global_transform.basis.orthonormalized()), "world CFrame getter did not return the rigid world orientation")
	var target_cframe := Transform3D(Basis(Vector3(1.0, 2.0, 3.0).normalized(), 0.93), Vector3(17.0, -4.0, 9.0))
	ClassDB.class_call_static(math_class, "set_world_cframe", child, target_cframe)
	assert(child.global_transform.origin.is_equal_approx(target_cframe.origin), "world CFrame setter did not place a scaled-parent child at the requested world position")
	assert(child.global_transform.basis.orthonormalized().is_equal_approx(target_cframe.basis), "world CFrame setter did not apply the requested rigid world orientation")
	assert(ClassDB.class_call_static(math_class, "get_world_cframe", child).is_equal_approx(target_cframe), "native world CFrame did not round-trip")
	var native_spatial: Node3D = ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_spatial_instance")
	parent.add_child(native_spatial)
	var cframe_editor_property := false
	for property_info in native_spatial.get_property_list():
		if property_info.name == "CFrame":
			cframe_editor_property = (property_info.usage & PROPERTY_USAGE_EDITOR) != 0 and (property_info.usage & PROPERTY_USAGE_STORAGE) == 0
	assert(cframe_editor_property, "native world CFrame must be Inspector-visible but derived rather than serialized twice")
	native_spatial.set("CFrame", target_cframe)
	assert(native_spatial.get("CFrame").is_equal_approx(target_cframe), "native spatial Instance CFrame property did not preserve world frame under scaled parent")
	var detached_parent := Node3D.new()
	detached_parent.transform = Transform3D(Basis(Vector3.UP, -0.52).scaled(Vector3(1.5, 2.5, 0.75)), Vector3(3, 7, -6))
	var detached_spatial: Node3D = ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_spatial_instance")
	detached_parent.add_child(detached_spatial)
	detached_spatial.set("CFrame", target_cframe)
	assert(detached_spatial.get("CFrame").is_equal_approx(target_cframe), "native CFrame getter/setter failed before SceneTree attachment")
	root.add_child(detached_parent)
	assert(detached_spatial.global_transform.basis.orthonormalized().is_equal_approx(target_cframe.basis)
		and detached_spatial.global_transform.origin.is_equal_approx(target_cframe.origin), "detached native CFrame changed after entering the SceneTree")
	var native_model: Node3D = ClassDB.instantiate("Model")
	root.add_child(native_model)
	native_model.set("CFrame", target_cframe)
	assert(native_model.get("CFrame").is_equal_approx(target_cframe), "native Model did not inherit the spatial Instance CFrame property")
	assert(not ClassDB.class_call_static(math_class, "has_imported_world_cframe", child), "fresh Node unexpectedly has imported CFrame metadata")
	ClassDB.class_call_static(math_class, "set_imported_world_cframe", child, target_cframe)
	assert(ClassDB.class_call_static(math_class, "has_imported_world_cframe", child), "native importer did not retain authored world CFrame")
	assert(ClassDB.class_call_static(math_class, "get_imported_world_cframe", child).is_equal_approx(target_cframe), "native imported CFrame metadata changed")
	assert(child.global_transform.is_equal_approx(target_cframe), "native imported CFrame setter failed to preserve world pose under the scaled parent")
	var imported_clone := Node3D.new()
	root.add_child(imported_clone)
	ClassDB.class_call_static(math_class, "copy_imported_world_cframe", child, imported_clone)
	assert(ClassDB.class_call_static(math_class, "get_imported_world_cframe", imported_clone).is_equal_approx(target_cframe), "native clone did not copy authored CFrame metadata")
	var edited_cframe := Transform3D(Basis(Vector3.UP, -0.41), Vector3(-3.0, 2.0, 11.0))
	child.global_transform = edited_cframe
	ClassDB.class_call_static(math_class, "commit_imported_world_cframe", child)
	assert(ClassDB.class_call_static(math_class, "get_imported_world_cframe", child).is_equal_approx(edited_cframe), "native editor commit did not update imported CFrame metadata")
	var plain_node := Node3D.new()
	root.add_child(plain_node)
	ClassDB.class_call_static(math_class, "copy_imported_world_cframe", plain_node, imported_clone)
	assert(not ClassDB.class_call_static(math_class, "has_imported_world_cframe", imported_clone), "copying from a Node without imported metadata did not clear the target")

	var part: Node3D = ClassDB.instantiate("Part")
	root.add_child(part)
	part.set("Size", Vector3(2.0, 3.0, 4.0))
	part.set("CFrame", target_cframe)
	assert(part.get("Size") == Vector3(2.0, 3.0, 4.0), "setting CFrame must not overwrite Part.Size")
	assert(part.get("CFrame").is_equal_approx(target_cframe), "native Part CFrame property did not round-trip")

	# Roblox-main/App/v8datamodel/Attachment.cpp keeps Axis authoritative when
	# SecondaryAxis changes and composes/decomposes Attachment rotation in XYZ order.
	var attachment_basis: Basis = ClassDB.class_call_static(math_class, "attachment_set_axis", Basis.IDENTITY, Vector3(1.0, 2.0, 3.0))
	assert(attachment_basis.x.is_equal_approx(Vector3(1.0, 2.0, 3.0).normalized()), "native Attachment Axis did not preserve the authored direction")
	assert(absf(attachment_basis.x.dot(attachment_basis.y)) < 0.00001, "native Attachment Axis setter left non-orthogonal axes")
	var fixed_axis := attachment_basis.x
	attachment_basis = ClassDB.class_call_static(math_class, "attachment_set_secondary_axis", attachment_basis, Vector3(0.9, 0.2, -0.1))
	assert(attachment_basis.x.is_equal_approx(fixed_axis), "native SecondaryAxis setter changed the authored Axis")
	assert(absf(attachment_basis.x.dot(attachment_basis.y)) < 0.00001, "native SecondaryAxis setter did not project to the orthogonal plane")
	var set_axes_basis: Basis = ClassDB.class_call_static(math_class, "attachment_set_axes", Basis.IDENTITY, Vector3(0.0, 0.0, 1.0), Vector3(0.0, 1.0, 0.0))
	assert(set_axes_basis.is_equal_approx(Basis(Vector3.BACK, Vector3.UP, Vector3.LEFT)), "native Attachment SetAxes did not apply the authored XYZ basis")
	var euler_xyz := Vector3(0.31, -0.62, 1.17)
	var attachment_xyz: Basis = ClassDB.class_call_static(math_class, "attachment_basis_from_euler_xyz", euler_xyz)
	assert(attachment_xyz.is_equal_approx(Basis.from_euler(euler_xyz, EULER_ORDER_XYZ)), "native Attachment Euler conversion differs from Roblox XYZ order")
	var round_trip_xyz: Vector3 = ClassDB.class_call_static(math_class, "attachment_euler_xyz", attachment_xyz)
	assert(Basis.from_euler(round_trip_xyz, EULER_ORDER_XYZ).is_equal_approx(attachment_xyz), "native Attachment XYZ orientation did not round-trip")
	var attachment_instance: Node3D = ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_spatial_instance")
	root.add_child(attachment_instance)
	assert(ClassDB.class_call_static("VextoriaSceneHierarchy", "is_instance_node", attachment_instance), "native Attachment does not carry Vextoria Instance identity")
	assert(attachment_instance.get_class() == "VextoriaSpatialInstance", "native Attachment was reduced to a generic Node3D")
	assert(attachment_instance.get("RobloxVisible") == false, "native Attachment.Visible default differs from Roblox")
	assert(attachment_instance.is_visible(), "Roblox Attachment.Visible must not hide its Godot descendants")
	attachment_instance.set("RobloxVisible", true)
	assert(attachment_instance.get("RobloxVisible") == true, "native Attachment.Visible property did not round-trip")
	assert(attachment_instance.is_visible(), "Roblox Attachment.Visible altered Godot subtree visibility")
	print("VEXTORIA_NATIVE_TRANSFORM_MATH_PASS serialized_rows weld_forward_reverse motor6d relative local_size native_cframe_property_scaled_parent imported_metadata_clone_editor_commit part_size attachment_axis_projection_xyz_euler native_attachment_instance_visible_isolated")
	quit(0)
