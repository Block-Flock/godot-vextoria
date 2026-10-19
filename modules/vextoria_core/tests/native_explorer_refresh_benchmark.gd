extends SceneTree

const ROW_COUNT := 2000

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	assert(ClassDB.class_exists("VextoriaExplorerTree"), "native Explorer control is missing")
	var source := Node.new()
	source.name = "Workspace"
	root.add_child(source)
	var explorer: Tree = ClassDB.instantiate("VextoriaExplorerTree")
	root.add_child(explorer)
	explorer.call("set_source_root", source)
	var rows: Array[Node] = []
	for index in range(ROW_COUNT):
		var row := Node.new()
		row.name = "SearchRow_%04d" % index if index < ROW_COUNT - 1 else "FinalTarget"
		row.set_meta("_vextoria_instance", true)
		source.add_child(row)
		rows.push_back(row)
	await process_frame
	await process_frame

	explorer.call("apply_search_filter", "no-match")
	var started_usec := Time.get_ticks_usec()
	for row in rows:
		explorer.call("set_node_lock_presentation", row, true, null)
	await process_frame
	await process_frame
	var elapsed_ms := float(Time.get_ticks_usec() - started_usec) / 1000.0
	var source_item: TreeItem = explorer.call("get_item_for_node", source)
	var target_item: TreeItem = explorer.call("get_item_for_node", rows.back())
	assert(not source_item.is_visible() and not target_item.is_visible(), "batched no-match refresh left rows visible")
	explorer.call("apply_search_filter", "target")
	assert(source_item.is_visible() and target_item.is_visible(), "search failed after a batched presentation refresh")
	print("VEXTORIA_NATIVE_EXPLORER_REFRESH_BENCHMARK: rows=%d refreshes=%d elapsed_ms=%.3f" % [ROW_COUNT, ROW_COUNT, elapsed_ms])
	source.free()
	explorer.free()
	quit(0)
