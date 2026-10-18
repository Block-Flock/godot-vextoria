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

	var part: Node3D = ClassDB.instantiate("Part")
	root.add_child(part)
	part.set("Size", Vector3(2.0, 3.0, 4.0))
	ClassDB.class_call_static(math_class, "set_world_cframe", part, target_cframe)
	assert(part.get("Size") == Vector3(2.0, 3.0, 4.0), "setting CFrame must not overwrite Part.Size")
	assert(ClassDB.class_call_static(math_class, "get_world_cframe", part).is_equal_approx(target_cframe), "native Part CFrame did not round-trip")
	print("VEXTORIA_NATIVE_TRANSFORM_MATH_PASS serialized_rows weld_forward_reverse motor6d relative local_size world_cframe_scaled_parent part_size")
	quit(0)
