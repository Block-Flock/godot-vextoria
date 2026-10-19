extends SceneTree

func _initialize() -> void:
	assert(ClassDB.class_exists("VextoriaRoblox2016ToolStateMachine"),
		"native Roblox 2016 Tool state machine is not registered")
	var machine := "VextoriaRoblox2016ToolStateMachine"

	# Match Tool::computeDesiredState() and the six ordered backend states in
	# ROBLOX-main/App/v8datamodel/Tool.cpp.
	assert(ClassDB.class_call_static(machine, "compute_desired_state", false, true, true, true, true) == 0,
		"RequiresHandle without a Handle must remain NOTHING")
	assert(ClassDB.class_call_static(machine, "compute_desired_state", true, false, false, false, false) == 0,
		"Tool outside Workspace must remain NOTHING")
	assert(ClassDB.class_call_static(machine, "compute_desired_state", true, true, false, false, false) == 2,
		"live Workspace Tool must reach IN_WORKSPACE")
	assert(ClassDB.class_call_static(machine, "compute_desired_state", true, true, true, false, false) == 3,
		"character Tool without torso must stop at IN_CHARACTER")
	assert(ClassDB.class_call_static(machine, "compute_desired_state", true, true, true, true, false) == 4,
		"character Tool without grip prerequisites must stop at HAS_TORSO")
	assert(ClassDB.class_call_static(machine, "compute_desired_state", true, true, true, true, true) == 5,
		"fully valid R6 Tool must reach EQUIPPED")

	const tool_id := 783421
	var generation: int = ClassDB.class_call_static(machine, "begin_transition", tool_id, 5)
	assert(generation > 0, "native state machine did not issue a transition generation")
	assert(ClassDB.class_call_static(machine, "get_state", tool_id) == 0,
		"begin_transition must not publish a state before its rungs advance")
	for expected_state in range(1, 6):
		var reached: int = ClassDB.class_call_static(machine, "advance_toward", tool_id, 5, generation)
		assert(reached == expected_state, "Tool state transition skipped or misordered a Roblox state rung")
	assert(ClassDB.class_call_static(machine, "advance_toward", tool_id, 5, generation) == -1,
		"completed transition advanced past EQUIPPED")

	var unequip_generation: int = ClassDB.class_call_static(machine, "begin_transition", tool_id, 0)
	assert(unequip_generation != generation, "reentrant unequip did not invalidate the prior transition")
	assert(ClassDB.class_call_static(machine, "advance_toward", tool_id, 0, generation) == -1,
		"stale equip callback was allowed to continue after unequip")
	for expected_state in [4, 3, 2, 1, 0]:
		var reached: int = ClassDB.class_call_static(machine, "advance_toward", tool_id, 0, unequip_generation)
		assert(reached == expected_state, "Tool unequip did not descend one Roblox state rung at a time")

	# Handle.Touched is connected while the Tool is at IN_WORKSPACE. Concurrent
	# callbacks for that one Tool must be serialized natively, but Roblox does
	# not impose a cooldown on picking up separate tools.
	const pickup_tool_a := 783422
	const pickup_tool_b := 783423
	var pickup_generation_a: int = ClassDB.class_call_static(machine, "begin_transition", pickup_tool_a, 2)
	var pickup_generation_b: int = ClassDB.class_call_static(machine, "begin_transition", pickup_tool_b, 2)
	for expected_state in [1, 2]:
		assert(ClassDB.class_call_static(machine, "advance_toward", pickup_tool_a, 2, pickup_generation_a) == expected_state)
		assert(ClassDB.class_call_static(machine, "advance_toward", pickup_tool_b, 2, pickup_generation_b) == expected_state)
	var claim_a: int = ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_a)
	assert(claim_a > 0, "live Workspace Tool pickup was not claimable")
	assert(ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_a) == 0,
		"duplicate callback claimed the same in-flight Workspace Tool")
	var claim_b: int = ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_b)
	assert(claim_b > 0, "claiming one Tool incorrectly rate-limited a distinct Tool")
	ClassDB.class_call_static(machine, "release_world_pickup", pickup_tool_a, claim_a)
	var replacement_claim_a: int = ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_a)
	assert(replacement_claim_a > 0 && replacement_claim_a != claim_a,
		"released pickup claim could not be immediately reacquired")
	ClassDB.class_call_static(machine, "release_world_pickup", pickup_tool_a, claim_a)
	assert(ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_a) == 0,
		"stale claim release cleared a newer claim")
	var leave_workspace_generation: int = ClassDB.class_call_static(machine, "begin_transition", pickup_tool_a, 0)
	assert(ClassDB.class_call_static(machine, "advance_toward", pickup_tool_a, 0, leave_workspace_generation) == 1,
		"Workspace pickup claim test did not advance the native state")
	assert(ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_a) == 0,
		"Tool outside Workspace remained eligible for pickup")
	assert(ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_b) == 0,
		"state transition on one Tool incorrectly cleared another Tool's claim")
	ClassDB.class_call_static(machine, "release_world_pickup", pickup_tool_a, replacement_claim_a)
	ClassDB.class_call_static(machine, "release_world_pickup", pickup_tool_b, claim_b)
	assert(ClassDB.class_call_static(machine, "try_claim_world_pickup", pickup_tool_b) > claim_b,
		"untouched Tool claim could not be released and reacquired")
	ClassDB.class_call_static(machine, "forget_tool", pickup_tool_a)
	ClassDB.class_call_static(machine, "forget_tool", pickup_tool_b)

	var reset_generation: int = ClassDB.class_call_static(machine, "begin_transition", tool_id, 5)
	ClassDB.class_call_static(machine, "forget_tool", tool_id)
	assert(ClassDB.class_call_static(machine, "get_state", tool_id) == 0,
		"forget_tool did not release native state during Instance teardown")
	assert(ClassDB.class_call_static(machine, "get_transition_generation", tool_id) == 0,
		"forget_tool retained a stale transition generation")
	assert(ClassDB.class_call_static(machine, "advance_toward", tool_id, 5, reset_generation) == -1,
		"forgotten Tool accepted a late deferred transition")
	print("VEXTORIA_NATIVE_TOOL_STATE_MACHINE_PASS: Roblox 2016 reducer, ordered equip/unequip, native per-Tool pickup claims, stale callback rejection, teardown")
	quit(0)
