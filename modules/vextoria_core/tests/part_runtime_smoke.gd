extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	assert(ClassDB.is_parent_class("Part", "RigidBody3D"), "Part must be the native physics body")
	var scene_root := Node3D.new()
	scene_root.name = "NativePartSmoke"
	root.add_child(scene_root)
	var part: RigidBody3D = ClassDB.instantiate("Part")
	part.name = "TestPart"
	scene_root.add_child(part)
	part.owner = scene_root
	part.set("Size", Vector3(3, 2, 5))
	part.set("Color", Color(0.8, 0.2, 0.1))
	part.set("Anchored", true)
	part.set("Locked", true)
	assert(part.freeze, "Anchored must freeze the native body")
	assert(part.has_meta("_edit_lock_") and part.get("Locked"), "Locked must share Godot's editor lock state")
	part.remove_meta("_edit_lock_")
	assert(not part.get("Locked"), "Godot editor unlock must update Vextoria Locked")
	assert(part.get_child_count() == 0, "Part internals must not become Explorer objects")
	assert(part.get_child_count(true) == 2, "Part must own visual and collision internals")
	var visual: MeshInstance3D = part.get_node("Visual")
	var collision: CollisionShape3D = part.get_node("Collision")
	var visual_material: StandardMaterial3D = visual.material_override as StandardMaterial3D
	assert(visual.mesh is BoxMesh and visual.mesh.size == Vector3(3, 2, 5), "Size must change native mesh")
	assert(collision.shape is BoxShape3D and collision.shape.size == Vector3(3, 2, 5), "Size must change native collision")
	assert(visual_material != null and visual_material.albedo_color.is_equal_approx(Color(0.8, 0.2, 0.1)), "Color must change native material")
	# Gameplay may temporarily install a non-box shape. Switching back to Brick
	# must restore the native resources at the current authored size.
	visual.mesh = BoxMesh.new()
	collision.shape = BoxShape3D.new()
	part.call("reset_builtin_geometry")
	assert(visual.mesh is BoxMesh and visual.mesh.size == Vector3(3, 2, 5), "Brick did not restore native visual size")
	assert(collision.shape is BoxShape3D and collision.shape.size == Vector3(3, 2, 5), "Brick did not restore native collision size")
	part.set("Geometry", 1)
	assert(visual.mesh is ArrayMesh and visual.mesh.get_aabb().size.is_equal_approx(Vector3(3, 2, 5)), "native wedge mesh lost authored size")
	var wedge_arrays: Array = visual.mesh.surface_get_arrays(0)
	var wedge_vertices: PackedVector3Array = wedge_arrays[Mesh.ARRAY_VERTEX]
	assert(wedge_vertices.size() == 24, "native wedge must retain eight authored triangles")
	assert(wedge_vertices[0].is_equal_approx(Vector3(1.5, 1, 2.5)) and wedge_vertices[1].is_equal_approx(Vector3(-1.5, -1, 2.5)), "native wedge was mirrored or its slope reversed")
	assert(collision.shape is ConvexPolygonShape3D and collision.shape.points.size() == 6, "native wedge collision must be a six-point convex hull")
	part.set("Size", Vector3(4, 3, 6))
	assert(visual.mesh.get_aabb().size.is_equal_approx(Vector3(4, 3, 6)), "native wedge resize did not update visual")
	assert(collision.shape.points[0].is_equal_approx(Vector3(2, 1.5, 3)), "native wedge resize did not update collision")
	var wedge_duplicate := part.duplicate()
	assert(wedge_duplicate.get("Geometry") == 1 and wedge_duplicate.get_node("Visual").mesh is ArrayMesh, "Godot duplication lost native wedge state")
	assert(wedge_duplicate.get_node("Collision").shape is ConvexPolygonShape3D, "Godot duplication lost native wedge hull")
	assert(wedge_duplicate.get_child_count(true) == 2, "Godot duplication copied internal geometry twice")
	wedge_duplicate.free()
	part.call("reset_builtin_geometry")
	part.set("Size", Vector3(3, 2, 5))

	await physics_frame
	await physics_frame
	var query := PhysicsRayQueryParameters3D.create(Vector3(0, 5, 0), Vector3(0, -5, 0))
	var hit := scene_root.get_world_3d().direct_space_state.intersect_ray(query)
	assert(hit.get("collider") == part, "ray must hit the Part itself, not a proxy")
	part.set("CanCollide", false)
	await physics_frame
	await physics_frame
	hit = scene_root.get_world_3d().direct_space_state.intersect_ray(query)
	assert(hit.is_empty(), "CanCollide=false must remove the Part from contacts")
	part.set("CanCollide", true)

	var packed := PackedScene.new()
	assert(packed.pack(scene_root) == OK, "native Part must pack")
	var path := "user://native_part_smoke_%d.tscn" % Time.get_ticks_usec()
	assert(ResourceSaver.save(packed, path) == OK, "native Part must save")
	var reopened: PackedScene = ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE)
	assert(reopened != null, "native Part must reload")
	var reopened_root := reopened.instantiate()
	var reopened_part := reopened_root.get_node("TestPart")
	assert(reopened_part.get_class() == "Part", "reload must keep native class")
	assert(reopened_part.get("Size") == Vector3(3, 2, 5), "reload must keep Size")
	assert(reopened_part.get("Anchored") and reopened_part.get("CanCollide"), "reload must keep physics properties")
	assert(reopened_part.get_child_count() == 0 and reopened_part.get_child_count(true) == 2, "reload must reconstruct only internal geometry")
	assert(DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK, "smoke output cleanup failed")
	reopened_root.free()

	# The same native class must also participate in normal Godot dynamics when
	# it is not anchored; a decorative mesh-only Part would fail this check.
	var dynamic_part: RigidBody3D = ClassDB.instantiate("Part")
	scene_root.add_child(dynamic_part)
	dynamic_part.position = Vector3(10, 8, 0)
	var starting_y := dynamic_part.position.y
	for frame in range(12):
		await physics_frame
	assert(dynamic_part.position.y < starting_y - 0.02, "unanchored Part did not fall under gravity")
	print("VEXTORIA_NATIVE_PART_RUNTIME_PASS: physics identity, mesh, collider, properties, save/reopen")
	quit(0)
