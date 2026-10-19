// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_tool_state_machine.h                               */
/**************************************************************************/

#pragma once

#include "core/object/object.h"

#include <cstdint>

class VextoriaRoblox2016ToolStateMachine : public Object {
	GDCLASS(VextoriaRoblox2016ToolStateMachine, Object);

	// Preserve the serialized/source-derived Roblox 2016 backend ordering.
	enum State : int32_t {
		NOTHING = 0,
		HAS_HANDLE = 1,
		IN_WORKSPACE = 2,
		IN_CHARACTER = 3,
		HAS_TORSO = 4,
		EQUIPPED = 5,
	};

protected:
	static void _bind_methods();

public:
	static int compute_desired_state(bool p_has_required_handle, bool p_in_workspace, bool p_in_character, bool p_has_torso, bool p_has_grip_prerequisites);
	static int64_t begin_transition(int64_t p_tool_object_id, int p_target);
	// Advance exactly one state rung if the caller still owns the transition.
	// Returns the new state, or -1 if invalidated/rejected.
	static int advance_toward(int64_t p_tool_object_id, int p_target, int64_t p_expected_generation);
	static int get_state(int64_t p_tool_object_id);
	static int64_t get_transition_generation(int64_t p_tool_object_id);
	// Atomically claim a Tool while it is still in Workspace. This only
	// serializes duplicate pickup requests for the same Tool; it is not a timer.
	static int64_t try_claim_world_pickup(int64_t p_tool_object_id);
	static void release_world_pickup(int64_t p_tool_object_id, int64_t p_claim_token);
	static void forget_tool(int64_t p_tool_object_id);
};
