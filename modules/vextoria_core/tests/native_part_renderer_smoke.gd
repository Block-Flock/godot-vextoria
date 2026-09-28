extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _check(condition: bool, reason: String) -> bool:
	if not condition:
		push_error("Native renderer smoke failed: " + reason)
		quit(1)
	return condition

func _drain(frames := 8) -> void:
	for frame in range(frames):
		await process_frame

func _part(parent: Node, position: Vector3, color: Color) -> Node3D:
	var part: Node3D = ClassDB.instantiate("Part")
	part.set("Anchored", true)
	part.set("Size", Vector3(2, 2, 2))
	part.set("Color", color)
	parent.add_child(part)
	part.position = position
	return part

func _run() -> void:
	await process_frame
	var world := Node3D.new()
	root.add_child(world)
	var model: Node3D = ClassDB.instantiate("Model")
	world.add_child(model)
	model.position = Vector3(7, 0, 0)
	var folder: Node = ClassDB.instantiate("Folder")
	model.add_child(folder)
	var renderer: Node3D = ClassDB.instantiate("VextoriaPartRenderer")
	world.add_child(renderer)
	var first := _part(folder, Vector3(-6, 2, 3), Color.RED)
	var second := _part(folder, Vector3(-3, 2, 3), Color.GREEN)
	var body_rid: RID = first.get_rid()
	var collider: Node = first.get_node("Collision")
	renderer.admit_part(first)
	renderer.admit_part(second)
	renderer.admit_part(first)
	first.set("Color", Color.BLUE)
	first.set("Color", Color.RED)
	if not _check(renderer.get_pending_count() == 2 and renderer.get_admitted_count() == 2, "duplicate admission/invalidation was not coalesced"):
		return
	await _drain()
	if not _check(renderer.get_batched_count() == 2 and renderer.get_batch_count() == 1, "Parts did not share one native draw batch"):
		return
	if not _check(first.is_render_batched() and not first.get_node("Visual").visible, "native standalone visual was drawn twice"):
		return
	if not _check(first.get_rid() == body_rid and first.get_node("Collision") == collider, "render admission replaced physical identity"):
		return
	var expected := first.global_transform * Transform3D(Basis.from_scale(first.get("Size")), Vector3.ZERO)
	if not _check(renderer.get_part_render_transform(first).is_equal_approx(expected), "native render frame lost Model/Folder inheritance or size"):
		return
	# Optional real rendering check, with no managed gameplay renderer involved.
	if DisplayServer.get_name() != "headless":
		var camera := Camera3D.new()
		world.add_child(camera)
		camera.position = Vector3(10, 10, 16)
		camera.look_at(Vector3(2.5, 2, 3))
		camera.make_current()
		var light := DirectionalLight3D.new()
		world.add_child(light)
		light.rotation_degrees = Vector3(-45, -30, 0)
		await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		var red := 0
		var green := 0
		for y in range(0, image.get_height(), 4):
			for x in range(0, image.get_width(), 4):
				var pixel := image.get_pixel(x, y)
				if pixel.r > 0.05 and pixel.r > pixel.g * 1.4 and pixel.r > pixel.b * 1.4:
					red += 1
				if pixel.g > 0.05 and pixel.g > pixel.r * 1.4 and pixel.g > pixel.b * 1.4:
					green += 1
		if not _check(red > 5 and green > 5, "native batches were not visible in rendered pixels"):
			return
		var capture := OS.get_environment("VEXTORIA_NATIVE_RENDERER_CAPTURE")
		if not capture.is_empty() and not _check(image.save_png(capture) == OK, "could not save native renderer capture"):
			return
	first.position.x = 130
	await _drain()
	renderer.remove_part(second)
	second.set("Color", Color.BLUE)
	await _drain()
	if not _check(renderer.get_batched_count() == 1 and renderer.get_pending_count() == 0, "chunk migration/swap removal left a stale subscription"):
		return
	model.position.x += 12
	model.rotation.y = 0.35
	await _drain()
	expected = first.global_transform * Transform3D(Basis.from_scale(first.get("Size")), Vector3.ZERO)
	if not _check(renderer.get_part_render_transform(first).is_equal_approx(expected), "ancestor transforms did not invalidate native slots"):
		return
	first.set("Anchored", false)
	await _drain()
	if not _check(renderer.get_batched_count() == 0 and first.get_node("Visual").visible, "dynamic Part lost its native standalone renderer"):
		return
	first.position += Vector3(1, 0, 0)
	await _drain()
	if not _check(renderer.get_pending_count() == 0, "dynamic motion churned the static queue"):
		return
	first.set("Anchored", true)
	await _drain()
	if not _check(renderer.get_batched_count() == 1, "dynamic Part did not return to native batching"):
		return
	renderer.set_part_policy(first, false, false, false)
	await _drain()
	if not _check(not first.is_render_batched() and not first.get_node("Visual").visible and first.get_rid() == body_rid, "mesh override policy exposed a primitive or changed physical identity"):
		return
	renderer.set_part_policy(first, false, false, true)
	await _drain()
	if not _check(first.get_node("Visual").visible and first.get_node("Visual").cast_shadow == GeometryInstance3D.SHADOW_CASTING_SETTING_OFF, "standalone policy did not publish primitive/shadow state"):
		return
	renderer.set_part_policy(first, true, true, true)
	await _drain()
	if not _check(first.is_render_batched(), "restoring native primitive policy lost batch admission"):
		return
	var custom := BoxMesh.new()
	custom.size = Vector3(0.75, 0.75, 0.75)
	first.set("GeometryMesh", custom)
	first.set("GeometryCollision", BoxShape3D.new())
	first.set("Geometry", 2)
	await _drain()
	if not _check(not first.is_render_batched() and first.get_node("Visual").mesh == custom, "custom geometry was replaced by a catalog primitive"):
		return
	first.set("Geometry", 0)
	await _drain()
	renderer.set_update_limit(1)
	var before: int = renderer.get_batched_count()
	var extras: Array[Node] = []
	for index in range(70):
		var extra := _part(world, Vector3(10 + index * 0.01, 2, 3), Color.BLUE)
		renderer.admit_part(extra)
		extras.append(extra)
	await process_frame
	if not _check(renderer.get_batched_count() - before <= 1, "native update limit was not enforced"):
		return
	renderer.set_update_limit(96)
	await _drain(40)
	if not _check(renderer.get_batched_count() == 71, "native capacity growth lost admitted Parts"):
		return
	for extra in extras:
		extra.free()
	first.free()
	await _drain()
	if not _check(renderer.get_admitted_count() == 0 and renderer.get_batched_count() == 0, "native node destruction retained admission or slots"):
		return
	renderer.clear_parts()
	if not _check(renderer.get_batch_count() == 0 and renderer.get_pending_count() == 0, "renderer teardown retained resources or work"):
		return
	world.free()
	print("VEXTORIA_NATIVE_PART_RENDERER_PASS: native resources, pixels, hierarchy, coalescing, migration, dynamic/custom transitions, bounded updates and teardown")
	quit(0)
