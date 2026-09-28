extends SceneTree

# Independent recovered geometry contract: WedgePoly.cpp / addWedge<false>
# has its two upper corners at local +Z, not the X taper of PrismMesh.
func _initialize() -> void:
	call_deferred("_run")

func _check(value: bool, message: String) -> bool:
	if not value:
		push_error("Native wedge batching failed: " + message)
		quit(1)
	return value

func _has_source_ridge(mesh: Mesh) -> bool:
	var upper := 0
	for surface in range(mesh.get_surface_count()):
		var vertices: PackedVector3Array = mesh.surface_get_arrays(surface)[Mesh.ARRAY_VERTEX]
		for vertex in vertices:
			if vertex.y > 0.49:
				upper += 1
				if vertex.z < 0.49:
					return false
	return upper > 0

func _run() -> void:
	await process_frame
	if not _check(DisplayServer.get_name() != "headless", "requires a real rendering backend; use xvfb-run with Vulkan Mobile/Forward+"):
		return
	var world := Node3D.new()
	root.add_child(world)
	var renderer: Node3D = ClassDB.instantiate("VextoriaPartRenderer")
	world.add_child(renderer)
	var size := Vector3(36, 6, 12)
	var parts := []
	for side in range(2):
		var part: RigidBody3D = ClassDB.instantiate("Part")
		world.add_child(part)
		part.set("Anchored", true)
		part.set("Size", size)
		part.set("Shape", 4)
		part.position.z = -6 if side == 0 else 6
		part.rotation.y = 0 if side == 0 else PI
		renderer.admit_part(part)
		parts.append(part)
	for frame in range(20):
		await process_frame
	for part in parts:
		if not _check(part.is_render_batched(), "wedge was not batched without a client catalog"):
			return
		var mesh: Mesh = renderer.get_part_render_state(part)["mesh"]
		if not _check(_has_source_ridge(mesh), "batch mesh changed the authored local +Z ridge"):
			return
		var unit := mesh.surface_get_arrays(0)
		var physical: Array = part.get_node("Visual").mesh.surface_get_arrays(0)
		var transform: Transform3D = renderer.get_part_render_transform(part)
		for index in range(unit[Mesh.ARRAY_VERTEX].size()):
			var vertex: Vector3 = unit[Mesh.ARRAY_VERTEX][index]
			if not _check((vertex * size).is_equal_approx(physical[Mesh.ARRAY_VERTEX][index]), "batch/standalone geometry disagrees"):
				return
			var normal: Vector3 = (unit[Mesh.ARRAY_NORMAL][index] / size).normalized()
			# RenderingServer stores normals in 16-bit octahedral form. Decoding
			# before a 6:1 inverse-scale transform amplifies its quantization error.
			if not _check(normal.distance_to(physical[Mesh.ARRAY_NORMAL][index]) < 0.0002, "nonuniform scaling changed lighting normal: expected=" + str(normal) + " actual=" + str(physical[Mesh.ARRAY_NORMAL][index])):
				return
			if vertex.y > 0.49:
				var ridge := transform * vertex
				if not _check(absf(ridge.z) < 0.001 and absf(ridge.y - 3) < 0.001, "opposite roof CFrames did not meet at the same ridge: " + str(ridge) + " pose=" + str(part.global_transform)):
					return
	var legacy := OS.get_environment("VEXTORIA_LEGACY_WEDGE_RESOURCE")
	if not legacy.is_empty():
		if not _check(not _has_source_ridge(load(legacy)), "negative baseline did not reject the legacy PrismMesh"):
			return
	world.free()
	print("VEXTORIA_NATIVE_WEDGE_BATCH_PASS: source +Z ridge, opposing roof frames, shared batch/standalone topology, outward normals")
	quit(0)
