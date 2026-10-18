// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_tool_state_machine.cpp                             */
/**************************************************************************/

#include "vextoria_roblox2016_tool_state_machine.h"

#include "core/object/class_db.h"
#include "core/os/mutex.h"

#include <unordered_map>

namespace {
constexpr int TOOL_STATE_MIN = 0;
constexpr int TOOL_STATE_MAX = 5;

struct ToolStateRecord {
	int32_t state = 0;
	int64_t transition_generation = 0;
};

std::unordered_map<int64_t, ToolStateRecord> tool_states;
Mutex tool_states_mutex;

ToolStateRecord *get_record(int64_t p_tool_object_id) {
	if (p_tool_object_id == 0) {
		return nullptr;
	}
	return &tool_states[p_tool_object_id];
}
} //namespace

int VextoriaRoblox2016ToolStateMachine::compute_desired_state(bool p_has_required_handle, bool p_in_workspace, bool p_in_character, bool p_has_torso, bool p_has_grip_prerequisites) {
	if (!p_has_required_handle || !p_in_workspace) {
		return NOTHING;
	}
	if (!p_in_character) {
		return IN_WORKSPACE;
	}
	if (!p_has_torso) {
		return IN_CHARACTER;
	}
	if (!p_has_grip_prerequisites) {
		return HAS_TORSO;
	}
	return EQUIPPED;
}

int64_t VextoriaRoblox2016ToolStateMachine::begin_transition(int64_t p_tool_object_id, int p_target) {
	ERR_FAIL_COND_V(p_tool_object_id == 0, 0);
	ERR_FAIL_COND_V(p_target < TOOL_STATE_MIN || p_target > TOOL_STATE_MAX, 0);
	MutexLock lock(tool_states_mutex);
	ToolStateRecord *record = get_record(p_tool_object_id);
	ERR_FAIL_NULL_V(record, 0);
	if (p_target == record->state) {
		return record->transition_generation;
	}
	if (record->transition_generation >= 0x7fffffffffffffffLL) {
		record->transition_generation = 1;
	} else {
		++record->transition_generation;
		if (record->transition_generation == 0) {
			record->transition_generation = 1;
		}
	}
	return record->transition_generation;
}

int VextoriaRoblox2016ToolStateMachine::advance_toward(int64_t p_tool_object_id, int p_target, int64_t p_expected_generation) {
	MutexLock lock(tool_states_mutex);
	ToolStateRecord *record = get_record(p_tool_object_id);
	if (record == nullptr || p_target < TOOL_STATE_MIN || p_target > TOOL_STATE_MAX || p_expected_generation != record->transition_generation || p_target == record->state) {
		return -1;
	}
	record->state += p_target > record->state ? 1 : -1;
	return record->state;
}

int VextoriaRoblox2016ToolStateMachine::get_state(int64_t p_tool_object_id) {
	MutexLock lock(tool_states_mutex);
	const auto found = tool_states.find(p_tool_object_id);
	return found == tool_states.end() ? NOTHING : found->second.state;
}

int64_t VextoriaRoblox2016ToolStateMachine::get_transition_generation(int64_t p_tool_object_id) {
	MutexLock lock(tool_states_mutex);
	const auto found = tool_states.find(p_tool_object_id);
	return found == tool_states.end() ? 0 : found->second.transition_generation;
}

void VextoriaRoblox2016ToolStateMachine::forget_tool(int64_t p_tool_object_id) {
	MutexLock lock(tool_states_mutex);
	tool_states.erase(p_tool_object_id);
}

void VextoriaRoblox2016ToolStateMachine::_bind_methods() {
	ClassDB::bind_static_method("VextoriaRoblox2016ToolStateMachine", D_METHOD("compute_desired_state", "has_required_handle", "in_workspace", "in_character", "has_torso", "has_grip_prerequisites"), &VextoriaRoblox2016ToolStateMachine::compute_desired_state);
	ClassDB::bind_static_method("VextoriaRoblox2016ToolStateMachine", D_METHOD("begin_transition", "tool_object_id", "target"), &VextoriaRoblox2016ToolStateMachine::begin_transition);
	ClassDB::bind_static_method("VextoriaRoblox2016ToolStateMachine", D_METHOD("advance_toward", "tool_object_id", "target", "expected_generation"), &VextoriaRoblox2016ToolStateMachine::advance_toward);
	ClassDB::bind_static_method("VextoriaRoblox2016ToolStateMachine", D_METHOD("get_state", "tool_object_id"), &VextoriaRoblox2016ToolStateMachine::get_state);
	ClassDB::bind_static_method("VextoriaRoblox2016ToolStateMachine", D_METHOD("get_transition_generation", "tool_object_id"), &VextoriaRoblox2016ToolStateMachine::get_transition_generation);
	ClassDB::bind_static_method("VextoriaRoblox2016ToolStateMachine", D_METHOD("forget_tool", "tool_object_id"), &VextoriaRoblox2016ToolStateMachine::forget_tool);
}
