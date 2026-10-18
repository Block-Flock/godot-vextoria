extends SceneTree

var failed := false

func _initialize() -> void:
	var parent := Node.new()
	parent.name = "InstanceParent"
	root.add_child(parent)

	var first := Node.new()
	first.name = "First"
	first.set_meta("_vextoria_instance", true)
	var helper := Node.new()
	helper.name = "GodotHelper"
	var second := Node.new()
	second.name = "Second"
	second.set_meta("_vextoria_instance", true)
	var third := Node.new()
	third.name = "Third"
	third.set_meta("_vextoria_instance", true)
	parent.add_child(first)
	parent.add_child(helper)
	parent.add_child(second)
	parent.add_child(third)

	_assert_index(first, 0)
	_assert_index(second, 1)
	_assert_index(third, 2)
	_assert_index(helper, -1)
	_assert(VextoriaSceneHierarchy.move_instance_child(third, 0), "native Instance reorder returned false")
	_assert_index(third, 0)
	_assert_index(first, 1)
	_assert_index(second, 2)
	_assert(helper.get_index() == 2, "unmarked helper relative order changed unexpectedly")
	_assert(_instance_names(parent) == ["Third", "First", "Second"], "filtered Instance order did not match native reorder")
	_assert(VextoriaSceneHierarchy.move_instance_child(second, 1), "second native Instance reorder returned false")
	_assert(_instance_names(parent) == ["Third", "Second", "First"], "middle Instance reorder produced the wrong sequence")
	_assert(VextoriaSceneHierarchy.move_instance_child(first, 99), "clamped native Instance reorder returned false")
	_assert_index(first, 2)
	_assert(_instance_names(parent) == ["Third", "Second", "First"], "out-of-range index was not clamped")
	_assert(not VextoriaSceneHierarchy.move_instance_child(helper, 0), "unmarked helper was accepted as an Instance")

	var detached := Node.new()
	detached.set_meta("_vextoria_instance", true)
	_assert_index(detached, -1)
	detached.free()
	parent.free()
	if failed:
		quit(1)
	else:
		print("VEXTORIA_NATIVE_SCENE_HIERARCHY_PASS: filtered-index, helper-node exclusion, ordered move, clamp, detached")
		quit(0)

func _instance_names(parent: Node) -> Array[String]:
	var result: Array[String] = []
	for child in parent.get_children():
		if child.has_meta("_vextoria_instance") and child.get_meta("_vextoria_instance"):
			result.append(child.name)
	return result

func _assert_index(node: Node, expected: int) -> void:
	_assert(VextoriaSceneHierarchy.get_instance_child_index(node) == expected,
		"unexpected filtered child index for %s" % node.name)

func _assert(condition: bool, message: String) -> void:
	if not condition:
		failed = true
		push_error(message)
