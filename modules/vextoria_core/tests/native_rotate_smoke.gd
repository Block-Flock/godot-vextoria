extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _check(condition: bool, message: String) -> bool:
	if not condition:
		push_error("Native Rotate failed: " + message)
		quit(1)
	return condition

func _settle(frames := 30) -> void:
	for frame in range(frames):
		await physics_frame

func _run() -> void:
	await process_frame
	var world := Node3D.new()
	root.add_child(world)
	var a: RigidBody3D = ClassDB.instantiate("Part")
	var b: RigidBody3D = ClassDB.instantiate("Part")
	a.name = "Post"
	b.name = "Door"
	world.add_child(a)
	world.add_child(b)
	a.owner = world
	b.owner = world
	a.set("Anchored", true)
	b.set("Anchored", false)
	a.set("CanCollide", false)
	b.set("CanCollide", false)
	b.gravity_scale = 0
	a.global_transform = Transform3D(Basis.from_euler(Vector3(0.2, 0.5, -0.1)), Vector3(2, 4, 3))
	var c0 := Transform3D(Basis(Vector3.RIGHT, PI / 2), Vector3(1, 0, 0))
	var c1 := Transform3D(Basis(Vector3.RIGHT, PI / 2), Vector3(-1, 0, 0))
	b.global_transform = a.global_transform * c0 * c1.affine_inverse()
	var rotate: HingeJoint3D = ClassDB.instantiate("Rotate")
	rotate.name = "AuthoredRotate"
	world.add_child(rotate)
	rotate.owner = world
	rotate.solver_velocity_iterations = 24
	rotate.solver_position_iterations = 16
	var rid := rotate.get_rid()
	# A deliberately unrelated editor pose must not recapture the axle.
	rotate.global_transform = Transform3D(Basis.from_euler(Vector3(1, 2, 3)), Vector3(80, 90, 100))
	rotate.configure_rotate(a, b, c0, c1)
	await _settle(4)
	var axis := (a.global_basis * c0.basis.z).normalized()
	var before := b.global_basis
	b.apply_torque_impulse(axis * 2)
	await _settle(45)
	var frame_a := a.global_transform * c0
	var frame_b := b.global_transform * c1
	print("Rotate diagnostic: type=", PhysicsServer3D.joint_get_type(rid), " frozen=", b.freeze, " angular_velocity=", b.angular_velocity, " angle=", before.get_rotation_quaternion().angle_to(b.global_basis.get_rotation_quaternion()))
	if not _check(frame_a.origin.distance_to(frame_b.origin) < 0.03 and frame_a.basis.z.dot(frame_b.basis.z) > 0.999, "authored pivot/axle drift"):
		return
	if not _check(not b.global_basis.is_equal_approx(before), "door cannot rotate when pushed"):
		return
	if not _check(rotate.get_child_count(true) == 0 and rotate.get_rid() == rid, "second physics owner or RID churn"):
		return
	rotate.set_flag(HingeJoint3D.FLAG_ENABLE_MOTOR, true)
	rotate.set_param(HingeJoint3D.PARAM_MOTOR_TARGET_VELOCITY, 0.5)
	rotate.set_param(HingeJoint3D.PARAM_MOTOR_MAX_IMPULSE, 1.0)
	if not _check(PhysicsServer3D.hinge_joint_get_flag(rid, PhysicsServer3D.HINGE_JOINT_FLAG_ENABLE_MOTOR), "motor flag did not reach native physics"):
		return
	rotate.set("Enabled", false)
	if not _check(PhysicsServer3D.joint_get_type(rid) != PhysicsServer3D.JOINT_TYPE_HINGE, "disabled hinge still solves"):
		return
	rotate.set("Enabled", true)
	b.set("Anchored", true)
	if not _check(PhysicsServer3D.joint_get_type(rid) != PhysicsServer3D.JOINT_TYPE_HINGE, "fixed source parts retain a solver"):
		return
	b.set("Anchored", false)
	if not _check(PhysicsServer3D.joint_get_type(rid) == PhysicsServer3D.JOINT_TYPE_HINGE, "unanchoring did not restore the hinge"):
		return
	var packed := PackedScene.new()
	if not _check(packed.pack(world) == OK, "native scene pack"):
		return
	var clone := packed.instantiate()
	root.add_child(clone)
	var reopened = clone.get_node("AuthoredRotate")
	if not _check(reopened.get_class() == "Rotate" and reopened.get("C0") == c0 and reopened.get("C1") == c1 and reopened.get_node(reopened.get("Part0")) == clone.get_node("Post") and reopened.get_flag(HingeJoint3D.FLAG_ENABLE_MOTOR) and reopened.get_param(HingeJoint3D.PARAM_MOTOR_TARGET_VELOCITY) == 0.5, "scene persistence lost identity, frames or motor settings"):
		return
	clone.free()
	for cycle in range(20):
		rotate.clear_rotate()
		rotate.configure_rotate(a, b, c0, c1)
	if not _check(rotate.get_rid() == rid, "reconfiguration changed RID"):
		return
	rotate.clear_rotate()
	world.free()
	print("VEXTORIA_NATIVE_ROTATE_PASS: independent local frames, pushed rotation, one owner/RID, anchoring, persistence and teardown")
	quit(0)
