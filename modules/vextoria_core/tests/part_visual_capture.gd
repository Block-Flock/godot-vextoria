extends SceneTree

func _initialize() -> void:
	call_deferred("_capture")

func _capture() -> void:
	root.size = Vector2i(800, 600)
	var scene_root := Node3D.new()
	root.add_child(scene_root)
	var part: RigidBody3D = ClassDB.instantiate("Part")
	part.set("Anchored", true)
	part.set("Size", Vector3(4, 2, 3))
	part.set("Color", Color(0.85, 0.25, 0.13))
	scene_root.add_child(part)
	var camera := Camera3D.new()
	camera.position = Vector3(7, 5, 8)
	scene_root.add_child(camera)
	camera.look_at(Vector3.ZERO)
	camera.current = true
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-45, -30, 0)
	scene_root.add_child(light)
	var environment := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode = Environment.BG_COLOR
	settings.background_color = Color(0.16, 0.18, 0.21)
	environment.environment = settings
	scene_root.add_child(environment)
	for frame in range(5):
		await process_frame
	var path := OS.get_environment("VEXTORIA_NATIVE_PART_CAPTURE")
	assert(not path.is_empty(), "Set VEXTORIA_NATIVE_PART_CAPTURE to a task-owned PNG path")
	assert(root.get_texture().get_image().save_png(path) == OK, "native Part capture failed")
	print("VEXTORIA_NATIVE_PART_CAPTURE_PASS: " + path)
	quit(0)
