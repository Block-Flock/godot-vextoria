// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#include "vextoria_creator_selection_state.h"

#include "core/object/class_db.h"
#include "core/os/mutex.h"
#include "scene/main/node.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {
std::unordered_map<int64_t, std::vector<uint64_t>> selections;
Mutex selections_mutex;

std::vector<uint64_t> *get_selection(int64_t p_owner_object_id) {
	if (p_owner_object_id == 0) {
		return nullptr;
	}
	return &selections[p_owner_object_id];
}

void prune_invalid_nodes(std::vector<uint64_t> &r_ids) {
	r_ids.erase(std::remove_if(r_ids.begin(), r_ids.end(), [](uint64_t p_id) {
		return ObjectDB::get_instance(ObjectID(p_id)) == nullptr;
	}), r_ids.end());
}
} // namespace

bool VextoriaCreatorSelectionState::select_node(int64_t p_owner_object_id, Node *p_node) {
	ERR_FAIL_COND_V(p_owner_object_id == 0 || p_node == nullptr, false);
	MutexLock lock(selections_mutex);
	std::vector<uint64_t> *selection = get_selection(p_owner_object_id);
	ERR_FAIL_NULL_V(selection, false);
	prune_invalid_nodes(*selection);
	const uint64_t node_id = uint64_t(p_node->get_instance_id());
	if (std::find(selection->begin(), selection->end(), node_id) != selection->end()) {
		return false;
	}
	selection->push_back(node_id);
	return true;
}

bool VextoriaCreatorSelectionState::deselect_node(int64_t p_owner_object_id, Node *p_node) {
	ERR_FAIL_COND_V(p_owner_object_id == 0 || p_node == nullptr, false);
	MutexLock lock(selections_mutex);
	auto found = selections.find(p_owner_object_id);
	if (found == selections.end()) {
		return false;
	}
	const uint64_t node_id = uint64_t(p_node->get_instance_id());
	auto &selection = found->second;
	auto node = std::find(selection.begin(), selection.end(), node_id);
	if (node == selection.end()) {
		return false;
	}
	selection.erase(node);
	if (selection.empty()) {
		selections.erase(found);
	}
	return true;
}

bool VextoriaCreatorSelectionState::has_node(int64_t p_owner_object_id, Node *p_node) {
	if (p_owner_object_id == 0 || p_node == nullptr) {
		return false;
	}
	MutexLock lock(selections_mutex);
	auto found = selections.find(p_owner_object_id);
	if (found == selections.end()) {
		return false;
	}
	prune_invalid_nodes(found->second);
	const uint64_t node_id = uint64_t(p_node->get_instance_id());
	return std::find(found->second.begin(), found->second.end(), node_id) != found->second.end();
}

Array VextoriaCreatorSelectionState::get_selected_nodes(int64_t p_owner_object_id) {
	Array result;
	if (p_owner_object_id == 0) {
		return result;
	}
	MutexLock lock(selections_mutex);
	auto found = selections.find(p_owner_object_id);
	if (found == selections.end()) {
		return result;
	}
	prune_invalid_nodes(found->second);
	for (uint64_t id : found->second) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id)));
		if (node != nullptr) {
			result.push_back(node);
		}
	}
	if (found->second.empty()) {
		selections.erase(found);
	}
	return result;
}

void VextoriaCreatorSelectionState::clear_selection(int64_t p_owner_object_id) {
	if (p_owner_object_id == 0) {
		return;
	}
	MutexLock lock(selections_mutex);
	selections.erase(p_owner_object_id);
}

void VextoriaCreatorSelectionState::forget_owner(int64_t p_owner_object_id) {
	clear_selection(p_owner_object_id);
}

void VextoriaCreatorSelectionState::_bind_methods() {
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("select_node", "owner_object_id", "node"), &VextoriaCreatorSelectionState::select_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("deselect_node", "owner_object_id", "node"), &VextoriaCreatorSelectionState::deselect_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("has_node", "owner_object_id", "node"), &VextoriaCreatorSelectionState::has_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("get_selected_nodes", "owner_object_id"), &VextoriaCreatorSelectionState::get_selected_nodes);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("clear_selection", "owner_object_id"), &VextoriaCreatorSelectionState::clear_selection);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("forget_owner", "owner_object_id"), &VextoriaCreatorSelectionState::forget_owner);
}
