// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#include "vextoria_scene_hierarchy.h"

#include "core/object/class_db.h"
#include "scene/main/node.h"

#include <algorithm>
#include <vector>

namespace {
bool is_vextoria_instance_node(Node *p_node) {
	return p_node != nullptr && p_node->has_meta("_vextoria_instance") && p_node->get_meta("_vextoria_instance").booleanize();
}

std::vector<Node *> get_instance_children(Node *p_parent) {
	std::vector<Node *> instances;
	if (p_parent == nullptr) {
		return instances;
	}
	const int32_t child_count = p_parent->get_child_count();
	instances.reserve(child_count);
	for (int32_t i = 0; i < child_count; i++) {
		Node *child = p_parent->get_child(i);
		if (is_vextoria_instance_node(child)) {
			instances.push_back(child);
		}
	}
	return instances;
}
} // namespace

int64_t VextoriaSceneHierarchy::get_instance_child_index(Node *p_node) {
	if (!is_vextoria_instance_node(p_node)) {
		return -1;
	}
	Node *parent = p_node->get_parent();
	if (parent == nullptr) {
		return -1;
	}
	std::vector<Node *> instances = get_instance_children(parent);
	auto found = std::find(instances.begin(), instances.end(), p_node);
	return found == instances.end() ? -1 : int64_t(found - instances.begin());
}

bool VextoriaSceneHierarchy::move_instance_child(Node *p_node, int64_t p_index) {
	if (!is_vextoria_instance_node(p_node)) {
		return false;
	}
	Node *parent = p_node->get_parent();
	if (parent == nullptr) {
		return false;
	}
	std::vector<Node *> instances = get_instance_children(parent);
	auto found = std::find(instances.begin(), instances.end(), p_node);
	if (found == instances.end()) {
		return false;
	}
	if (instances.empty()) {
		return false;
	}

	const int64_t old_index = int64_t(found - instances.begin());
	const int64_t target_index = CLAMP(p_index, int64_t(0), int64_t(instances.size() - 1));
	if (old_index == target_index) {
		return true;
	}

	// The requested order is applied only to the filtered Instance sequence.
	// Godot helper children remain unmarked and are never mistaken for Roblox
	// siblings. Moving each desired node immediately before the node currently
	// occupying its filtered slot preserves helper-node relative order.
	Node *moving = instances[size_t(old_index)];
	instances.erase(instances.begin() + old_index);
	instances.insert(instances.begin() + target_index, moving);
	for (size_t i = 0; i < instances.size(); i++) {
		std::vector<Node *> current = get_instance_children(parent);
		if (current[i] == instances[i]) {
			continue;
		}
		parent->move_child(instances[i], current[i]->get_index());
	}
	return true;
}

bool VextoriaSceneHierarchy::set_instance_parent(Node *p_node, Node *p_parent) {
	if (!is_vextoria_instance_node(p_node)) {
		return false;
	}
	if (p_parent != nullptr && !is_vextoria_instance_node(p_parent)) {
		return false;
	}
	if (p_node == p_parent || (p_parent != nullptr && p_node->is_ancestor_of(p_parent))) {
		return false;
	}

	Node *old_parent = p_node->get_parent();
	if (old_parent == p_parent) {
		return true;
	}

	if (p_parent == nullptr) {
		if (old_parent != nullptr) {
			old_parent->remove_child(p_node);
		}
		return p_node->get_parent() == nullptr;
	}

	if (old_parent == nullptr) {
		p_parent->add_child(p_node);
	} else {
		p_node->reparent(p_parent, true);
	}
	return p_node->get_parent() == p_parent;
}

void VextoriaSceneHierarchy::_bind_methods() {
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("get_instance_child_index", "node"), &VextoriaSceneHierarchy::get_instance_child_index);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("move_instance_child", "node", "index"), &VextoriaSceneHierarchy::move_instance_child);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("set_instance_parent", "node", "parent"), &VextoriaSceneHierarchy::set_instance_parent);
}
