extends SceneTree

var failed := false

func _initialize() -> void:
	var parent := Node.new()
	parent.name = "InstanceParent"
	parent.set_meta("_vextoria_instance", true)
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
	var first_child := Node.new()
	first_child.set_meta("_vextoria_instance", true)
	first.add_child(first_child)
	var detached := Node.new()
	detached.name = "Detached"
	detached.set_meta("_vextoria_instance", true)

	_assert_index(first, 0)
	_assert_index(second, 1)
	_assert_index(third, 2)
	_assert_index(helper, -1)
	_assert(_instance_names_from_api(parent) == ["First", "Second", "Third"], "native child API included helpers or changed order")
	_assert(_move_instance_child(third, 0), "native Instance reorder returned false")
	_assert_index(third, 0)
	_assert_index(first, 1)
	_assert_index(second, 2)
	_assert(helper.get_index() == 2, "unmarked helper relative order changed unexpectedly")
	_assert(_instance_names(parent) == ["Third", "First", "Second"], "filtered Instance order did not match native reorder")
	_assert(_move_instance_child(second, 1), "second native Instance reorder returned false")
	_assert(_instance_names(parent) == ["Third", "Second", "First"], "middle Instance reorder produced the wrong sequence")
	_assert(_move_instance_child(first, 99), "clamped native Instance reorder returned false")
	_assert_index(first, 2)
	_assert(_instance_names(parent) == ["Third", "Second", "First"], "out-of-range index was not clamped")
	_assert(not _move_instance_child(helper, 0), "unmarked helper was accepted as an Instance")
	_assert(not _set_instance_parent(helper, second), "unmarked child was accepted by native parent setter")
	_assert(not _set_instance_parent(first, helper), "unmarked parent was accepted by native parent setter")
	_assert(not _set_instance_parent(first, first), "self-parenting was accepted")
	_assert(not _set_instance_parent(first, first_child), "cyclic reparent was accepted")
	_assert(_set_instance_parent(first, parent), "native no-op parent assignment failed")
	_assert(_set_instance_parent(detached, parent), "detached Instance attach failed")
	_assert(_set_instance_parent(detached, null), "native Instance detach failed")
	_assert(detached.get_parent() == null, "native Instance detach left a Godot parent")
	_assert_index(detached, -1)
	_assert(_set_instance_parent(detached, parent), "detached Instance reattach failed")
	_assert(_instance_names_from_api(parent) == ["Third", "Second", "First", "Detached"], "native child API omitted attached Instance")

	parent.free()
	if failed:
		quit(1)
	else:
		print("VEXTORIA_NATIVE_SCENE_HIERARCHY_PASS: filtered-index, helper-node exclusion, ordered move, native parent attach/detach, cycle rejection, clamp")
		quit(0)

func _instance_names(parent: Node) -> Array[String]:
	var result: Array[String] = []
	for child in parent.get_children():
		if child.has_meta("_vextoria_instance") and child.get_meta("_vextoria_instance"):
			result.append(child.name)
	return result

func _instance_names_from_api(parent: Node) -> Array[String]:
	var result: Array[String] = []
	var children: Array = ClassDB.class_call_static("VextoriaSceneHierarchy", "get_instance_children", parent)
	for child: Node in children:
		result.append(child.name)
	return result

func _assert_index(node: Node, expected: int) -> void:
	_assert(_get_instance_child_index(node) == expected,
		"unexpected filtered child index for %s" % node.name)

func _get_instance_child_index(node: Node) -> int:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "get_instance_child_index", node)

func _move_instance_child(node: Node, index: int) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "move_instance_child", node, index)

func _set_instance_parent(node: Node, parent: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "set_instance_parent", node, parent)

func _assert(condition: bool, message: String) -> void:
	if not condition:
		failed = true
		push_error(message)
