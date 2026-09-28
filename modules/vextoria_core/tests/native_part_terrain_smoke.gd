extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _check(condition: bool, reason: String) -> bool:
	if not condition:
		push_error("Native Part/terrain integration failed: " + reason)
		quit(1)
	return condition

func _run() -> void:
	var world := Node3D.new()
	root.add_child(world)
	var data = ClassDB.instantiate("TerrainData")
	data.set_chunk_size(32)
	data.set_chunk_height(64)
	data.set_cell_size(1.0)
	for z in range(8, 16):
		for y in range(8, 16):
			for x in range(8, 16):
				data.set_cell(x, y, z, 2, 255)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.position = Vector3(30, 32, 40)
	camera.look_at(Vector3(12, 15, 12))
	camera.make_current()
	var light := DirectionalLight3D.new()
	world.add_child(light)
	light.rotation_degrees = Vector3(-45, -30, 0)
	var terrain: Node3D = ClassDB.instantiate("RobloxSmoothTerrainManager")
	terrain.set("terrain_data", data)
	terrain.set("maximum_lod_level", 0)
	terrain.set("view_distance", 128.0)
	terrain.set("collision_enabled", true)
	terrain.set("collision_distance", 128.0)
	world.add_child(terrain)
	for frame in range(240):
		await process_frame
		if terrain.is_streaming_complete() and terrain.get_rendered_chunk_count() > 0:
			break
	if not _check(terrain.is_streaming_complete() and terrain.get_rendered_chunk_count() > 0, "terrain did not finish streaming"):
		return
	var terrain_material := StandardMaterial3D.new()
	terrain_material.albedo_color = Color.GREEN
	for child in terrain.get_children():
		if child is MeshInstance3D and String(child.name).begins_with("Solid_"):
			child.material_override = terrain_material
	var renderer: Node3D = ClassDB.instantiate("VextoriaPartRenderer")
	world.add_child(renderer)
	var part: Node3D = ClassDB.instantiate("Part")
	part.set("Anchored", true)
	part.set("Size", Vector3(2, 2, 2))
	part.set("Color", Color.RED)
	world.add_child(part)
	part.position = Vector3(12, 23, 12)
	renderer.admit_part(part)
	for frame in range(8):
		await process_frame
	if not _check(part.is_render_batched(), "native Part did not render alongside terrain"):
		return
	if DisplayServer.get_name() != "headless":
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
		if not _check(red > 5 and green > 5, "terrain and native Part were not both visible"):
			return
		var capture := OS.get_environment("VEXTORIA_NATIVE_TERRAIN_CAPTURE")
		if not capture.is_empty() and not _check(image.save_png(capture) == OK, "capture could not be saved"):
			return
	part.set("Anchored", false)
	for frame in range(180):
		await physics_frame
	if not _check(not part.is_render_batched() and part.position.y > 17.0 and part.position.y < 18.5, "native Part fell through terrain or failed to settle on its top: " + str(part.position)):
		return
	print("VEXTORIA_NATIVE_PART_TERRAIN_PASS: simultaneous pixels, batch-to-physics transition, top-surface collision; settled=", part.position)
	world.free()
	quit(0)
