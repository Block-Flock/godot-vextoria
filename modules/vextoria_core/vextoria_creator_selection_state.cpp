// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#include "vextoria_creator_selection_state.h"

#include "core/object/class_db.h"
#include "core/os/mutex.h"
#include "scene/main/node.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct OwnerSelection {
	uint64_t root_object_id = 0;
	std::vector<uint64_t> node_object_ids;
};

std::unordered_map<int64_t, OwnerSelection> selections;
Mutex selections_mutex;

bool is_within_selection_root(Node *p_root, Node *p_node) {
	return p_root != nullptr && p_node != nullptr && (p_root == p_node || p_root->is_ancestor_of(p_node));
}

OwnerSelection *get_selection(int64_t p_owner_object_id, Node *p_root, bool p_create) {
	if (p_owner_object_id == 0 || p_root == nullptr) {
		return nullptr;
	}
	auto found = selections.find(p_owner_object_id);
	if (found == selections.end()) {
		if (!p_create) {
			return nullptr;
		}
		OwnerSelection selection;
		selection.root_object_id = uint64_t(p_root->get_instance_id());
		found = selections.emplace(p_owner_object_id, std::move(selection)).first;
	}
	if (found->second.root_object_id != uint64_t(p_root->get_instance_id())) {
		return nullptr;
	}
	return &found->second;
}

void prune_invalid_nodes(std::vector<uint64_t> &r_ids, Node *p_root) {
	r_ids.erase(std::remove_if(r_ids.begin(), r_ids.end(), [p_root](uint64_t p_id) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(p_id)));
		return !is_within_selection_root(p_root, node);
	}), r_ids.end());
}
} // namespace

bool VextoriaCreatorSelectionState::select_node(int64_t p_owner_object_id, Node *p_selection_root, Node *p_node) {
	if (p_owner_object_id == 0 || !is_within_selection_root(p_selection_root, p_node)) {
		return false;
	}
	MutexLock lock(selections_mutex);
	OwnerSelection *owner_selection = get_selection(p_owner_object_id, p_selection_root, true);
	if (owner_selection == nullptr) {
		return false;
	}
	std::vector<uint64_t> &selection = owner_selection->node_object_ids;
	prune_invalid_nodes(selection, p_selection_root);
	const uint64_t node_id = uint64_t(p_node->get_instance_id());
	if (std::find(selection.begin(), selection.end(), node_id) != selection.end()) {
		return false;
	}
	selection.push_back(node_id);
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
	auto &selection = found->second.node_object_ids;
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

bool VextoriaCreatorSelectionState::has_node(int64_t p_owner_object_id, Node *p_selection_root, Node *p_node) {
	if (p_owner_object_id == 0 || p_selection_root == nullptr || p_node == nullptr || !is_within_selection_root(p_selection_root, p_node)) {
		return false;
	}
	MutexLock lock(selections_mutex);
	OwnerSelection *owner_selection = get_selection(p_owner_object_id, p_selection_root, false);
	if (owner_selection == nullptr) {
		return false;
	}
	prune_invalid_nodes(owner_selection->node_object_ids, p_selection_root);
	const uint64_t node_id = uint64_t(p_node->get_instance_id());
	return std::find(owner_selection->node_object_ids.begin(), owner_selection->node_object_ids.end(), node_id) != owner_selection->node_object_ids.end();
}

Array VextoriaCreatorSelectionState::get_selected_nodes(int64_t p_owner_object_id, Node *p_selection_root) {
	Array result;
	if (p_owner_object_id == 0 || p_selection_root == nullptr) {
		return result;
	}
	MutexLock lock(selections_mutex);
	OwnerSelection *owner_selection = get_selection(p_owner_object_id, p_selection_root, false);
	if (owner_selection == nullptr) {
		return result;
	}
	prune_invalid_nodes(owner_selection->node_object_ids, p_selection_root);
	for (uint64_t id : owner_selection->node_object_ids) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(id)));
		if (node != nullptr) {
			result.push_back(node);
		}
	}
	if (owner_selection->node_object_ids.empty()) {
		selections.erase(p_owner_object_id);
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
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("select_node", "owner_object_id", "selection_root", "node"), &VextoriaCreatorSelectionState::select_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("deselect_node", "owner_object_id", "node"), &VextoriaCreatorSelectionState::deselect_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("has_node", "owner_object_id", "selection_root", "node"), &VextoriaCreatorSelectionState::has_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("get_selected_nodes", "owner_object_id", "selection_root"), &VextoriaCreatorSelectionState::get_selected_nodes);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("clear_selection", "owner_object_id"), &VextoriaCreatorSelectionState::clear_selection);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("forget_owner", "owner_object_id"), &VextoriaCreatorSelectionState::forget_owner);
}
