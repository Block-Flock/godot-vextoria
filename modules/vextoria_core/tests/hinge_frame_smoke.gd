extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var scene_root := Node3D.new()
	root.add_child(scene_root)
	var body_a := RigidBody3D.new()
	body_a.name = "Frame"
	body_a.freeze = true
	scene_root.add_child(body_a)
	var body_b := RigidBody3D.new()
	body_b.name = "Door"
	body_b.freeze = true
	body_b.position = Vector3(2, 0, 0)
	scene_root.add_child(body_b)
	for body in [body_a, body_b]:
		var shape := CollisionShape3D.new()
		shape.shape = BoxShape3D.new()
		body.add_child(shape)

	var hinge := HingeJoint3D.new()
	hinge.name = "Rotate"
	hinge.position = Vector3(1, 0, 0)
	scene_root.add_child(hinge)
	hinge.node_a = NodePath("../Frame")
	hinge.node_b = NodePath("../Door")
	hinge.set("solver_velocity_iterations", 8)
	hinge.set("solver_position_iterations", 4)
	assert(hinge.get("solver_velocity_iterations") == 8 and hinge.get("solver_position_iterations") == 4,
		"native joint solver overrides did not round-trip")
	var hole_frame := Transform3D(Basis(Vector3.UP, 0.35), Vector3(1.25, 0.5, -0.25))
	hinge.call("set_reference_frame_b_global", hole_frame)
	assert(hinge.call("has_reference_frame_b_global"), "body-B reference frame was not enabled")
	var actual: Transform3D = hinge.call("get_reference_frame_b_global")
	assert(actual.origin.distance_to(hole_frame.origin) < 0.0001,
		"body-B reference position changed")
	assert(actual.basis.x.distance_to(hole_frame.basis.x) < 0.0001,
		"body-B reference rotation changed")
	await physics_frame
	await physics_frame
	hinge.call("clear_reference_frame_b_global")
	assert(not hinge.call("has_reference_frame_b_global"), "hinge did not restore shared-frame mode")
	print("VEXTORIA_HINGE_FRAME_PASS: independent body-B frame and solver overrides")
	quit(0)
