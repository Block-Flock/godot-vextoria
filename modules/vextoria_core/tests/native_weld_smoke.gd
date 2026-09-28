extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _check(condition: bool, message: String) -> bool:
	if not condition:
		push_error("Native Weld failed: " + message)
		quit(1)
	return condition

func _settle(frames := 90) -> void:
	for frame in range(frames):
		await physics_frame

func _run() -> void:
	await process_frame
	var world := Node3D.new()
	root.add_child(world)
	var a: RigidBody3D = ClassDB.instantiate("Part")
	var b: RigidBody3D = ClassDB.instantiate("Part")
	a.name = "Anchor"
	b.name = "Follower"
	world.add_child(a)
	world.add_child(b)
	a.owner = world
	b.owner = world
	a.set("Anchored", true)
	a.set("CanCollide", false)
	b.set("CanCollide", false)
	b.gravity_scale = 0
	a.global_transform = Transform3D(Basis.from_euler(Vector3(0.1, 0.4, -0.2)), Vector3(2, 4, 3))
	var c0 := Transform3D(Basis.from_euler(Vector3(0, 0, 0.4)), Vector3(0.5, 1, 0))
	var c1 := Transform3D(Basis.from_euler(Vector3(0.2, 0, 0)), Vector3(-1, 0, 0))
	var expected := a.global_transform * c0 * c1.affine_inverse()
	b.global_transform = Transform3D(expected.basis * Basis.from_euler(Vector3(0, 0.1, 0)), expected.origin + Vector3(0.2, 0.1, -0.15))
	var weld: Generic6DOFJoint3D = ClassDB.instantiate("Weld")
	weld.name = "AuthoredWeld"
	world.add_child(weld)
	weld.owner = world
	weld.solver_velocity_iterations = 24
	weld.solver_position_iterations = 16
	var joint_rid := weld.get_rid()
	weld.configure_weld(a, b, c0, c1)
	await _settle()
	var error := (a.global_transform * c0).origin.distance_to((b.global_transform * c1).origin)
	if not _check(error < 0.03 and b.global_basis.is_equal_approx(expected.basis), "authored independent frames did not converge: pivot error=" + str(error)):
		return
	if not _check(weld.get_child_count(true) == 0 and weld.get_rid() == joint_rid, "logical Weld owns a second node or replaced its joint RID"):
		return
	weld.set("Enabled", false)
	if not _check(PhysicsServer3D.joint_get_type(joint_rid) != PhysicsServer3D.JOINT_TYPE_6DOF and weld.get("C0") == c0, "disable retained physics or lost authored data"):
		return
	weld.set("Enabled", true)
	await _settle(10)
	if not _check(PhysicsServer3D.joint_get_type(joint_rid) == PhysicsServer3D.JOINT_TYPE_6DOF, "reenable did not rebuild native constraint"):
		return
	b.set("Anchored", true)
	if not _check(PhysicsServer3D.joint_get_type(joint_rid) != PhysicsServer3D.JOINT_TYPE_6DOF, "two anchored Parts retained a competing constraint"):
		return
	b.set("Anchored", false)
	if not _check(PhysicsServer3D.joint_get_type(joint_rid) == PhysicsServer3D.JOINT_TYPE_6DOF, "native unanchoring did not reactivate logical Weld"):
		return
	var packed := PackedScene.new()
	if not _check(packed.pack(world) == OK, "could not pack native Weld scene"):
		return
	var clone := packed.instantiate()
	root.add_child(clone)
	var reopened = clone.get_node("AuthoredWeld")
	if not _check(reopened.get_class() == "Weld" and reopened.get("C0") == c0 and reopened.get("C1") == c1 and reopened.get_node(reopened.get("Part0")) == clone.get_node("Anchor") and reopened.get_node(reopened.get("Part1")) == clone.get_node("Follower"), "native scene reload lost Weld identity, endpoints or frames"):
		return
	clone.free()
	for iteration in range(20):
		weld.clear_weld()
		weld.configure_weld(a, b, c0, c1)
	if not _check(weld.get_rid() == joint_rid, "repeated reconfiguration churned joint identity"):
		return
	weld.clear_weld()
	b.free()
	a.free()
	world.free()
	print("VEXTORIA_NATIVE_WELD_PASS: independent authored frames, one node/RID, disable/enable, native anchoring, scene persistence and reconfiguration")
	quit(0)
