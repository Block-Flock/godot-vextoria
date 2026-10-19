extends SceneTree

var failed := false

func _initialize() -> void:
	var generic_instance := ClassDB.class_call_static(
		"VextoriaSceneHierarchy", "create_generic_instance") as Node
	_assert(generic_instance != null and generic_instance.get_class() == "VextoriaInstance",
		"native hierarchy factory did not create the generic Vextoria Instance base")
	_assert(not ClassDB.can_instantiate("VextoriaInstance"),
		"generic Vextoria implementation base leaked into Godot's public CreateDialog class list")
	var generic_spatial := ClassDB.class_call_static(
		"VextoriaSceneHierarchy", "create_generic_spatial_instance") as Node3D
	_assert(generic_spatial != null and generic_spatial.get_class() == "VextoriaSpatialInstance",
		"native hierarchy factory did not create the generic spatial Vextoria Instance base")
	_assert(not ClassDB.can_instantiate("VextoriaSpatialInstance"),
		"generic spatial implementation base leaked into Godot's public CreateDialog class list")
	_assert(ClassDB.is_parent_class("Model", "VextoriaSpatialInstance"),
		"native Model does not inherit the shared spatial Instance base")
	if generic_instance != null:
		root.add_child(generic_instance)
	if generic_spatial != null:
		root.add_child(generic_spatial)

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
	first_child.name = "FirstChild"
	first_child.set_meta("_vextoria_instance", true)
	first.add_child(first_child)
	var godot_helper_between_instances := Node.new()
	first.add_child(godot_helper_between_instances)
	var nested_instance := Node.new()
	nested_instance.name = "Nested"
	nested_instance.set_meta("_vextoria_instance", true)
	first_child.add_child(nested_instance)
	var instance_through_helper := Node.new()
	instance_through_helper.name = "ThroughHelper"
	instance_through_helper.set_meta("_vextoria_instance", true)
	godot_helper_between_instances.add_child(instance_through_helper)
	var detached := Node.new()
	detached.name = "Detached"
	detached.set_meta("_vextoria_instance", true)

	# Roblox-main/App/v8datamodel/Attachment.cpp: Parts and Workspace are the
	# only valid Attachment parents, and an Attachment cannot own children.
	var attachment: Node3D = ClassDB.class_call_static(
		"VextoriaSceneHierarchy", "create_generic_spatial_instance")
	attachment.set_meta("_vextoria_instance", true)
	attachment.set_meta("_vextoria_class_name", "Attachment")
	var attachment_part: Node = ClassDB.instantiate("Part")
	attachment_part.set_meta("_vextoria_class_name", "Part")
	var attachment_workspace := Node.new()
	attachment_workspace.set_meta("_vextoria_instance", true)
	attachment_workspace.set_meta("_vextoria_class_name", "World")
	var attachment_folder := Node.new()
	attachment_folder.set_meta("_vextoria_instance", true)
	attachment_folder.set_meta("_vextoria_class_name", "Folder")
	_assert(ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", attachment, attachment_part),
		"Attachment parented to a Part was rejected")
	_assert(ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", attachment, attachment_workspace),
		"Attachment parented to Workspace was rejected")
	_assert(not ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", attachment, attachment_folder),
		"Attachment accepted an invalid Folder parent")
	_assert(not ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", attachment_folder, attachment),
		"Attachment accepted a child")
	_assert(ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", attachment, null),
		"Attachment could not be detached")

	_assert_index(first, 0)
	_assert_index(second, 1)
	_assert_index(third, 2)
	_assert_index(helper, -1)
	_assert(_instance_names_from_api(parent) == ["First", "Second", "Third"], "native child API included helpers or changed order")
	_assert(_recursive_child_search_order(parent) == ["First", "FirstChild", "Nested", "ThroughHelper", "Second", "Third"],
		"native recursive child search order did not preserve Roblox's actual depth-first subtree visitation or skipped helper Nodes")
	_assert(_find_instance_child_by_name(parent, "Second") == second, "native direct-name lookup missed a marked Instance child")
	_assert(_find_instance_child_by_name(parent, "GodotHelper") == null, "native direct-name lookup exposed an unmarked helper Node")
	_assert(_find_instance_child_by_name(parent, "Missing") == null, "native direct-name lookup returned a node for a missing name")
	_assert(_instance_names_from_api(first) == ["FirstChild", "ThroughHelper"], "native Instance child query did not cross a private helper Node")
	_assert(_find_instance_child_by_name(first, "ThroughHelper") == instance_through_helper, "native direct lookup did not treat private helper Nodes as transparent")
	_assert(_instance_parent_from_api(instance_through_helper) == first, "native Instance parent query exposed a private helper Node")
	_assert(_get_instance_child_index(instance_through_helper) == 1, "native Instance child index disagreed with transparent helper traversal")
	_assert(_set_instance_parent(instance_through_helper, first), "native same-logical-parent assignment failed")
	_assert(instance_through_helper.get_parent() == godot_helper_between_instances,
		"same-logical-parent assignment unnecessarily flattened the private helper representation")
	_assert(_move_instance_child(instance_through_helper, 0), "native reorder failed for an Instance nested below a private helper")
	_assert(_instance_names_from_api(first) == ["ThroughHelper", "FirstChild"], "native reorder did not update logical child order")
	_assert(_instance_parent_from_api(instance_through_helper) == first, "native reorder changed logical parent")
	_assert(_get_instance_child_index(first_child) == 1, "native child index did not follow reordered logical siblings")
	_assert(_instance_parent_from_api(first) == parent, "native parent API missed the marked SceneTree parent")
	_assert(_instance_parent_from_api(helper) == null, "native parent API accepted an unmarked node")
	_assert(_is_instance_ancestor_of(parent, first), "native ancestry query missed a direct Instance parent")
	_assert(_is_instance_ancestor_of(first, nested_instance), "native ancestry query did not skip a private Godot helper Node")
	_assert(not _is_instance_ancestor_of(nested_instance, first), "native ancestry query reported a reverse relation")
	_assert(not _is_instance_ancestor_of(first, first), "native ancestry query treated an Instance as its own ancestor")
	_assert(not _is_instance_ancestor_of(helper, first), "native ancestry query accepted an unmarked Godot Node")
	var shared_root := ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_instance") as Node
	var source_parent := ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_instance") as Node
	var destination_parent := ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_instance") as Node
	var moving_root := ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_instance") as Node
	var moving_child := ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_instance") as Node
	var moving_grandchild := ClassDB.class_call_static("VextoriaSceneHierarchy", "create_generic_instance") as Node
	var event_helper := Node.new()
	shared_root.add_child(event_helper)
	event_helper.add_child(source_parent)
	event_helper.add_child(destination_parent)
	source_parent.add_child(moving_root)
	moving_root.add_child(moving_child)
	moving_child.add_child(moving_grandchild)
	var removing_plan: Array = ClassDB.class_call_static("VextoriaSceneHierarchy", "collect_descendant_event_dispatch_plan", moving_root, source_parent, destination_parent)
	_assert(removing_plan.size() == 3, "native DescendantRemoving plan included a common ancestor or omitted a subtree member")
	_assert(_event_pair_is(removing_plan, 0, source_parent, moving_root), "native DescendantRemoving plan did not start with the moving Instance")
	_assert(_event_pair_is(removing_plan, 1, source_parent, moving_child), "native DescendantRemoving plan changed descendant order")
	_assert(_event_pair_is(removing_plan, 2, source_parent, moving_grandchild), "native DescendantRemoving plan omitted a nested descendant")
	var adding_plan: Array = ClassDB.class_call_static("VextoriaSceneHierarchy", "collect_descendant_event_dispatch_plan", moving_root, destination_parent, source_parent)
	_assert(adding_plan.size() == 3, "native DescendantAdded plan included a common ancestor or omitted a subtree member")
	_assert(_event_pair_is(adding_plan, 0, destination_parent, moving_root), "native DescendantAdded plan used the wrong first ancestor")
	_assert(_event_pair_is(adding_plan, 2, destination_parent, moving_grandchild), "native DescendantAdded plan changed descendant order")
	var unshared_plan: Array = ClassDB.class_call_static("VextoriaSceneHierarchy", "collect_descendant_event_dispatch_plan", moving_root, source_parent, null)
	_assert(unshared_plan.size() == 6, "native descendant event plan did not include every non-shared Instance ancestor or skip helper Nodes")
	shared_root.free()
	_assert(_can_set_instance_parent(first, parent), "native parent preflight rejected a valid no-op")
	_assert(_can_set_instance_parent(first, null), "native parent preflight rejected detach")
	_assert(not _can_set_instance_parent(first, helper), "native parent preflight accepted an unmarked parent")
	_assert(not _can_set_instance_parent(first, first_child), "native parent preflight accepted a cycle")
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
	var generic_locked_node := Node.new()
	generic_locked_node.name = "GenericLockedInstance"
	generic_locked_node.set_meta("_vextoria_instance", true)
	parent.add_child(generic_locked_node)
	_assert(_lock_instance_parent(generic_locked_node), "generic Node-backed Instance did not accept ParentLocked")
	_assert(_is_instance_parent_locked(generic_locked_node), "generic Node-backed Instance did not retain ParentLocked")
	_assert(not _can_set_instance_parent(generic_locked_node, second), "generic Node-backed ParentLocked Instance could be reparented")
	_assert(not _set_instance_parent(generic_locked_node, null), "generic Node-backed ParentLocked Instance could be detached")
	generic_locked_node.reparent(parent)
	_assert(generic_locked_node.get_parent() == parent, "same-parent Node.reparent no-op was blocked by ParentLocked")
	generic_locked_node.reparent(second)
	_assert(generic_locked_node.get_parent() == parent, "raw Node.reparent bypassed ParentLocked")
	parent.remove_child(generic_locked_node)
	_assert(generic_locked_node.get_parent() == parent, "raw Node.remove_child bypassed ParentLocked")
	_assert(_unlock_instance_parent(generic_locked_node), "generic Node-backed Instance could not clear ParentLocked")
	_assert(_set_instance_parent(generic_locked_node, second), "unlocked generic Node-backed Instance could not be reparented")
	var locked_detached_node := Node.new()
	locked_detached_node.name = "LockedDetachedInstance"
	locked_detached_node.set_meta("_vextoria_instance", true)
	_assert(_lock_instance_parent(locked_detached_node), "detached Node-backed Instance did not accept ParentLocked")
	second.add_child(locked_detached_node)
	_assert(locked_detached_node.get_parent() == null, "raw Node.add_child bypassed ParentLocked on a detached Instance")
	_assert(_unlock_instance_parent(locked_detached_node), "detached Node-backed Instance could not clear ParentLocked")
	second.add_child(locked_detached_node)
	_assert(locked_detached_node.get_parent() == second, "unlocked Node-backed Instance could not be attached")
	for lockable_class in ["Folder", "Model", "Part"]:
		var lockable_node := ClassDB.instantiate(lockable_class) as Node
		_assert(lockable_node != null, "native %s class could not be instantiated for ParentLocked coverage" % lockable_class)
		if lockable_node != null:
			lockable_node.set_meta("_vextoria_instance", true)
			_assert(_lock_instance_parent(lockable_node), "native %s did not accept ParentLocked" % lockable_class)
			_assert(_is_instance_parent_locked(lockable_node), "native %s ParentLocked state was not readable" % lockable_class)
			_assert(_unlock_instance_parent(lockable_node), "native %s did not clear ParentLocked" % lockable_class)
			lockable_node.free()
	var locked_folder := ClassDB.instantiate("Folder") as Node
	_assert(locked_folder != null, "native Folder class could not be instantiated for parent-lock coverage")
	if locked_folder != null:
		locked_folder.name = "ParentLockedFolder"
		parent.add_child(locked_folder)
		locked_folder.call("set_locked", true)
		_assert(ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", locked_folder, second), "Studio edit Locked incorrectly blocked parent changes")
		_assert(_lock_instance_parent(locked_folder), "native parent lock could not be enabled")
		_assert(_is_instance_parent_locked(locked_folder), "native parent lock state was not readable")
		_assert(locked_folder.call("is_locked"), "runtime ParentLocked corrupted the independent Studio edit Locked state")
		_assert(ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", locked_folder, parent), "same-parent no-op was blocked by ParentLocked")
		_assert(not ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", locked_folder, second), "ParentLocked allowed reparenting")
		_assert(not _set_instance_parent(locked_folder, second), "native setter bypassed ParentLocked")
		_assert(not _set_instance_parent(locked_folder, null), "native setter detached a ParentLocked Instance")
		_assert(_unlock_instance_parent(locked_folder), "native parent lock could not be disabled")
		_assert(not _is_instance_parent_locked(locked_folder), "native parent lock remained set after unlock")
		_assert(_set_instance_parent(locked_folder, second), "unlocked native Instance could not be reparented")
		locked_folder.call("set_locked", false)
	_assert(_begin_instance_parent_change(detached, parent), "native parent transaction did not begin")
	_assert(not _begin_instance_parent_change(detached, null), "nested native parent transaction was accepted")
	_assert(not _commit_instance_parent_change(detached, second), "native parent transaction accepted a different target")
	_assert(not _set_instance_parent(detached, parent), "raw parent setter bypassed an active transaction")
	_assert(_commit_instance_parent_change(detached, parent), "authorized native parent commit failed")
	_assert(not _commit_instance_parent_change(detached, parent), "native parent transaction committed twice")
	_end_instance_parent_change(detached)
	_assert(_begin_instance_parent_change(detached, null), "native detach transaction did not begin")
	_assert(_commit_instance_parent_change(detached, null), "authorized native detach commit failed")
	_end_instance_parent_change(detached)
	_assert(detached.get_parent() == null, "native Instance detach left a Godot parent")
	_assert(_instance_parent_from_api(detached) == null, "native parent API retained a detached parent")
	_assert_index(detached, -1)
	_assert(_set_instance_parent(detached, parent), "detached Instance reattach failed")
	_assert(_instance_names_from_api(parent) == ["Third", "Second", "First", "Detached"], "native child API omitted attached Instance")

	parent.free()
	if failed:
		quit(1)
	else:
		print("VEXTORIA_NATIVE_SCENE_HIERARCHY_PASS: generic native Instance and spatial Instance factories, hidden implementation bases, native Model inheritance, filtered child/parent/ancestry and descendant-event dispatch planning, ParentLocked raw mutation enforcement, transactional attach/detach, cycles, ordering, Attachment parent/child policy")
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

func _instance_parent_from_api(node: Node) -> Node:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "get_instance_parent", node)

func _recursive_child_search_order(parent: Node) -> Array[String]:
	var result: Array[String] = []
	var nodes: Array = ClassDB.class_call_static("VextoriaSceneHierarchy", "get_instance_recursive_child_search_order", parent)
	for node: Node in nodes:
		result.append(node.name)
	return result

func _is_instance_ancestor_of(ancestor: Node, descendant: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "is_instance_ancestor_of", ancestor, descendant)

func _event_pair_is(plan: Array, index: int, expected_ancestor: Node, expected_descendant: Node) -> bool:
	if index < 0 or index >= plan.size():
		return false
	var event_pair: Array = plan[index]
	return event_pair.size() == 2 and event_pair[0] == expected_ancestor and event_pair[1] == expected_descendant

func _find_instance_child_by_name(parent: Node, child_name: String) -> Node:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "find_instance_child_by_name", parent, child_name)

func _assert_index(node: Node, expected: int) -> void:
	_assert(_get_instance_child_index(node) == expected,
		"unexpected filtered child index for %s" % node.name)

func _get_instance_child_index(node: Node) -> int:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "get_instance_child_index", node)

func _move_instance_child(node: Node, index: int) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "move_instance_child", node, index)

func _set_instance_parent(node: Node, parent: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "set_instance_parent", node, parent)

func _can_set_instance_parent(node: Node, parent: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "can_set_instance_parent", node, parent)

func _begin_instance_parent_change(node: Node, parent: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "begin_instance_parent_change", node, parent)

func _commit_instance_parent_change(node: Node, parent: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "commit_instance_parent_change", node, parent)

func _end_instance_parent_change(node: Node) -> void:
	ClassDB.class_call_static("VextoriaSceneHierarchy", "end_instance_parent_change", node)

func _is_instance_parent_locked(node: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "is_instance_parent_locked", node)

func _lock_instance_parent(node: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "lock_instance_parent", node)

func _unlock_instance_parent(node: Node) -> bool:
	return ClassDB.class_call_static("VextoriaSceneHierarchy", "unlock_instance_parent", node)

func _assert(condition: bool, message: String) -> void:
	if not condition:
		failed = true
		push_error(message)
