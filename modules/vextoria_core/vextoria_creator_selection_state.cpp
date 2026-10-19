// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#include "vextoria_creator_selection_state.h"

#include "core/object/class_db.h"
#include "core/os/mutex.h"
#include "core/variant/callable.h"
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
std::unordered_map<uint64_t, Callable> parent_change_callables;
Mutex selections_mutex;
VextoriaCreatorSelectionState *selection_callback_target = nullptr;

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

void disconnect_node_parent_change(uint64_t p_node_id) {
	for (const auto &owner : selections) {
		if (std::find(owner.second.node_object_ids.begin(), owner.second.node_object_ids.end(), p_node_id) != owner.second.node_object_ids.end()) {
			return;
		}
	}
	auto callable = parent_change_callables.find(p_node_id);
	if (callable == parent_change_callables.end()) {
		return;
	}
	if (Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(p_node_id)))) {
		if (node->is_connected(SNAME("vextoria_parent_changing"), callable->second)) {
			node->disconnect(SNAME("vextoria_parent_changing"), callable->second);
		}
	}
	parent_change_callables.erase(callable);
}

bool connect_node_parent_change(Node *p_node) {
	const uint64_t node_id = uint64_t(p_node->get_instance_id());
	if (parent_change_callables.find(node_id) != parent_change_callables.end()) {
		return true;
	}
	if (selection_callback_target == nullptr) {
		VextoriaCreatorSelectionState::initialize();
	}
	Callable callable = Callable(selection_callback_target, "_on_selected_node_parent_changing").bind(int64_t(node_id));
	if (p_node->connect(SNAME("vextoria_parent_changing"), callable) != OK) {
		return false;
	}
	parent_change_callables.emplace(node_id, callable);
	return true;
}

void prune_invalid_nodes(OwnerSelection &r_selection, Node *p_root) {
	auto &ids = r_selection.node_object_ids;
	std::vector<uint64_t> removed_ids;
	ids.erase(std::remove_if(ids.begin(), ids.end(), [&removed_ids, p_root](uint64_t p_id) {
		Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(p_id)));
		if (is_within_selection_root(p_root, node)) {
			return false;
		}
		removed_ids.push_back(p_id);
		return true;
	}), ids.end());
	for (uint64_t removed_id : removed_ids) {
		disconnect_node_parent_change(removed_id);
	}
}
} // namespace

void VextoriaCreatorSelectionState::initialize() {
	if (selection_callback_target == nullptr) {
		selection_callback_target = memnew(VextoriaCreatorSelectionState);
	}
}

void VextoriaCreatorSelectionState::shutdown() {
	{
		MutexLock lock(selections_mutex);
		for (const auto &entry : parent_change_callables) {
			if (Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(entry.first)))) {
				if (node->is_connected(SNAME("vextoria_parent_changing"), entry.second)) {
					node->disconnect(SNAME("vextoria_parent_changing"), entry.second);
				}
			}
		}
		parent_change_callables.clear();
		selections.clear();
	}
	if (selection_callback_target != nullptr) {
		memdelete(selection_callback_target);
		selection_callback_target = nullptr;
	}
}

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
	prune_invalid_nodes(*owner_selection, p_selection_root);
	const uint64_t node_id = uint64_t(p_node->get_instance_id());
	if (std::find(selection.begin(), selection.end(), node_id) != selection.end()) {
		return false;
	}
	if (!connect_node_parent_change(p_node)) {
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
	disconnect_node_parent_change(node_id);
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
	prune_invalid_nodes(*owner_selection, p_selection_root);
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
	prune_invalid_nodes(*owner_selection, p_selection_root);
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
	auto found = selections.find(p_owner_object_id);
	if (found == selections.end()) {
		return;
	}
	std::vector<uint64_t> ids = found->second.node_object_ids;
	selections.erase(found);
	for (uint64_t id : ids) {
		disconnect_node_parent_change(id);
	}
}

void VextoriaCreatorSelectionState::forget_owner(int64_t p_owner_object_id) {
	clear_selection(p_owner_object_id);
}

void VextoriaCreatorSelectionState::_on_selected_node_parent_changing(Node *p_old_parent, Node *p_new_parent, int64_t p_node_object_id) {
	(void)p_old_parent;
	Node *node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(uint64_t(p_node_object_id))));
	if (node == nullptr) {
		return;
	}
	std::vector<int64_t> owners_to_deselect;
	{
		MutexLock lock(selections_mutex);
		for (const auto &owner : selections) {
			if (std::find(owner.second.node_object_ids.begin(), owner.second.node_object_ids.end(), uint64_t(p_node_object_id)) == owner.second.node_object_ids.end()) {
				continue;
			}
			Node *root = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(owner.second.root_object_id)));
			if (node != root && !is_within_selection_root(root, p_new_parent)) {
				owners_to_deselect.push_back(owner.first);
			}
		}
	}
	for (int64_t owner_id : owners_to_deselect) {
		deselect_node(owner_id, node);
	}
}

void VextoriaCreatorSelectionState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_selected_node_parent_changing", "old_parent", "new_parent", "node_object_id"), &VextoriaCreatorSelectionState::_on_selected_node_parent_changing);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("select_node", "owner_object_id", "selection_root", "node"), &VextoriaCreatorSelectionState::select_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("deselect_node", "owner_object_id", "node"), &VextoriaCreatorSelectionState::deselect_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("has_node", "owner_object_id", "selection_root", "node"), &VextoriaCreatorSelectionState::has_node);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("get_selected_nodes", "owner_object_id", "selection_root"), &VextoriaCreatorSelectionState::get_selected_nodes);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("clear_selection", "owner_object_id"), &VextoriaCreatorSelectionState::clear_selection);
	ClassDB::bind_static_method("VextoriaCreatorSelectionState", D_METHOD("forget_owner", "owner_object_id"), &VextoriaCreatorSelectionState::forget_owner);
}
