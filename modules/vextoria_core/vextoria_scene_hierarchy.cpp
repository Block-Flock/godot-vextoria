// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#include "vextoria_scene_hierarchy.h"

#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "scene/main/node.h"
#include "vextoria_instance.h"

#include <algorithm>
#include <vector>

namespace {
struct ParentChangeState {
	ObjectID requested_parent;
	bool commit_started = false;
};

HashMap<ObjectID, ParentChangeState> active_parent_changes;

void collect_instance_children(Node *p_parent, std::vector<Node *> &r_instances) {
	if (p_parent == nullptr) {
		return;
	}
	const int32_t child_count = p_parent->get_child_count(false);
	for (int32_t i = 0; i < child_count; i++) {
		Node *child = p_parent->get_child(i, false);
		if (VextoriaSceneHierarchy::is_instance_node(child)) {
			r_instances.push_back(child);
		} else {
			// Godot implementation Nodes are transparent in the Roblox Instance
			// hierarchy. Stop at the first Instance so its descendants remain
			// children of that Instance rather than being flattened here.
			collect_instance_children(child, r_instances);
		}
	}
}

std::vector<Node *> collect_instance_children(Node *p_parent) {
	std::vector<Node *> instances;
	collect_instance_children(p_parent, instances);
	return instances;
}

std::vector<Node *> collect_instance_descendants(Node *p_root) {
	std::vector<Node *> descendants;
	if (p_root == nullptr) {
		return descendants;
	}

	std::vector<Node *> pending;
	std::vector<Node *> root_children = collect_instance_children(p_root);
	pending.reserve(root_children.size());
	for (auto it = root_children.rbegin(); it != root_children.rend(); ++it) {
		pending.push_back(*it);
	}
	while (!pending.empty()) {
		Node *current = pending.back();
		pending.pop_back();
		descendants.push_back(current);

		std::vector<Node *> children = collect_instance_children(current);
		for (auto it = children.rbegin(); it != children.rend(); ++it) {
			pending.push_back(*it);
		}
	}
	return descendants;
}
} // namespace

Node *VextoriaSceneHierarchy::create_generic_instance() {
	return memnew(VextoriaInstance);
}

Node3D *VextoriaSceneHierarchy::create_generic_spatial_instance() {
	return memnew(VextoriaSpatialInstance);
}

bool VextoriaSceneHierarchy::is_instance_node(Node *p_node) {
	// Native object constructors register identity with Node itself. Node keeps
	// the metadata fallback for imported/custom proxies that have not migrated.
	return p_node != nullptr && p_node->is_vextoria_instance_native();
}

bool VextoriaSceneHierarchy::is_instance_ancestor_of(Node *p_ancestor, Node *p_descendant) {
	if (!is_instance_node(p_ancestor) || !is_instance_node(p_descendant) || p_ancestor == p_descendant) {
		return false;
	}
	// Walk the Godot tree but only recognize marked Vextoria Instances. Some
	// legacy proxies still have private engine Nodes between logical parents;
	// those Nodes must not alter Roblox Instance ancestry.
	for (Node *parent = p_descendant->get_parent(); parent != nullptr; parent = parent->get_parent()) {
		if (is_instance_node(parent) && parent == p_ancestor) {
			return true;
		}
	}
	return false;
}

Array VextoriaSceneHierarchy::collect_descendant_event_dispatch_plan(Node *p_moving_root, Node *p_first_ancestor, Node *p_shared_ancestor_branch) {
	Array plan;
	if (!is_instance_node(p_moving_root) || !is_instance_node(p_first_ancestor) ||
		(p_shared_ancestor_branch != nullptr && !is_instance_node(p_shared_ancestor_branch))) {
		return plan;
	}

	// Match Instance::signalDescendantAdded/Removing: visit the moving Instance
	// first, then its descendants in hierarchy order. Materialize every event
	// recipient before callbacks run, because a Luau callback may reparent or
	// destroy another branch while the transaction is being observed.
	std::vector<Node *> affected_nodes;
	affected_nodes.reserve(1 + p_moving_root->get_child_count());
	affected_nodes.push_back(p_moving_root);
	std::vector<Node *> descendants = collect_instance_descendants(p_moving_root);
	affected_nodes.insert(affected_nodes.end(), descendants.begin(), descendants.end());

	for (Node *affected : affected_nodes) {
		for (Node *ancestor = p_first_ancestor; ancestor != nullptr;) {
			if (p_shared_ancestor_branch != nullptr &&
				(ancestor == p_shared_ancestor_branch || is_instance_ancestor_of(ancestor, p_shared_ancestor_branch))) {
				break;
			}

			Array event_pair;
			event_pair.push_back(ancestor);
			event_pair.push_back(affected);
			plan.push_back(event_pair);

			// Godot helper Nodes are implementation details, not Instance
			// ancestors. Continue to the next marked Node in the native tree.
			Node *parent = ancestor->get_parent();
			while (parent != nullptr && !is_instance_node(parent)) {
				parent = parent->get_parent();
			}
			ancestor = parent;
		}
	}
	return plan;
}

Node *VextoriaSceneHierarchy::find_instance_child_by_name(Node *p_parent, const String &p_name) {
	for (Node *child : collect_instance_children(p_parent)) {
		if (child->get_name() == p_name) {
			return child;
		}
	}
	return nullptr;
}

Array VextoriaSceneHierarchy::get_instance_children(Node *p_parent) {
	Array result;
	for (Node *child : collect_instance_children(p_parent)) {
		result.push_back(child);
	}
	return result;
}

Array VextoriaSceneHierarchy::get_instance_descendants(Node *p_parent) {
	Array result;
	for (Node *descendant : collect_instance_descendants(p_parent)) {
		result.push_back(descendant);
	}
	return result;
}

Array VextoriaSceneHierarchy::get_instance_recursive_child_search_order(Node *p_parent) {
	// Roblox's recursive FindFirstChild implementation visits each child subtree
	// before advancing to the next sibling. Preserve that observable preorder,
	// while leaving the current parent's direct-child check to the caller.
	return get_instance_descendants(p_parent);
}

Node *VextoriaSceneHierarchy::get_instance_parent(Node *p_node) {
	if (!is_instance_node(p_node)) {
		return nullptr;
	}
	for (Node *parent = p_node->get_parent(); parent != nullptr; parent = parent->get_parent()) {
		if (is_instance_node(parent)) {
			return parent;
		}
	}
	return nullptr;
}

bool VextoriaSceneHierarchy::can_set_instance_parent(Node *p_node, Node *p_parent) {
	if (!is_instance_node(p_node)) {
		return false;
	}
	if (p_parent != nullptr && !is_instance_node(p_parent)) {
		return false;
	}
	if (get_instance_parent(p_node) == p_parent && (p_parent != nullptr || p_node->get_parent() == nullptr)) {
		return true;
	}
	if (is_instance_parent_locked(p_node)) {
		return false;
	}
	return p_node != p_parent && (p_parent == nullptr || !p_node->is_ancestor_of(p_parent));
}

bool VextoriaSceneHierarchy::is_instance_parent_locked(Node *p_node) {
	return is_instance_node(p_node) && p_node->is_vextoria_parent_locked();
}

bool VextoriaSceneHierarchy::lock_instance_parent(Node *p_node) {
	if (!is_instance_node(p_node)) {
		return false;
	}
	p_node->set_vextoria_parent_locked(true);
	return true;
}

bool VextoriaSceneHierarchy::unlock_instance_parent(Node *p_node) {
	if (!is_instance_node(p_node)) {
		return false;
	}
	p_node->set_vextoria_parent_locked(false);
	return true;
}

int64_t VextoriaSceneHierarchy::get_instance_child_index(Node *p_node) {
	if (!is_instance_node(p_node)) {
		return -1;
	}
	Node *parent = get_instance_parent(p_node);
	if (parent == nullptr) {
		return -1;
	}
	std::vector<Node *> instances = collect_instance_children(parent);
	auto found = std::find(instances.begin(), instances.end(), p_node);
	return found == instances.end() ? -1 : int64_t(found - instances.begin());
}

bool VextoriaSceneHierarchy::move_instance_child(Node *p_node, int64_t p_index) {
	if (!is_instance_node(p_node)) {
		return false;
	}
	Node *parent = get_instance_parent(p_node);
	if (parent == nullptr) {
		return false;
	}
	std::vector<Node *> instances = collect_instance_children(parent);
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
	// Godot's move_child only orders physical siblings. Normalize helper-wrapped
	// logical siblings under their actual Instance parent before applying the
	// filtered order. Keep world transforms: helper Nodes are storage details,
	// not Roblox Instance parents or transform frames.
	for (Node *instance : instances) {
		if (instance->get_parent() != parent) {
			instance->reparent(parent, true);
			if (instance->get_parent() != parent) {
				return false;
			}
		}
	}

	// The requested order is applied only to the filtered Instance sequence.
	// Godot helper children remain unmarked and are never mistaken for Roblox
	// siblings. Moving each desired node immediately before the node currently
	// occupying its filtered slot preserves helper-node relative order.
	Node *moving = instances[size_t(old_index)];
	instances.erase(instances.begin() + old_index);
	instances.insert(instances.begin() + target_index, moving);
	for (size_t i = 0; i < instances.size(); i++) {
		std::vector<Node *> current = collect_instance_children(parent);
		if (current[i] == instances[i]) {
			continue;
		}
		parent->move_child(instances[i], current[i]->get_index());
	}
	return true;
}

bool VextoriaSceneHierarchy::set_instance_parent(Node *p_node, Node *p_parent) {
	// Raw engine callers may change an Instance parent directly, but they may
	// not bypass a compatibility lifecycle transaction already in progress.
	if (p_node != nullptr && active_parent_changes.has(p_node->get_instance_id())) {
		// Lifecycle setup may reassert the already-committed target. This is a
		// true no-op and cannot bypass the transaction's authorized commit.
		return get_instance_parent(p_node) == p_parent;
	}
	return set_instance_parent_internal(p_node, p_parent);
}

bool VextoriaSceneHierarchy::begin_instance_parent_change(Node *p_node, Node *p_parent) {
	if (!can_set_instance_parent(p_node, p_parent)) {
		return false;
	}
	const ObjectID node_id = p_node->get_instance_id();
	if (active_parent_changes.has(node_id)) {
		return false;
	}
	ParentChangeState state;
	state.requested_parent = p_parent != nullptr ? p_parent->get_instance_id() : ObjectID();
	active_parent_changes.insert(node_id, state);
	return true;
}


bool VextoriaSceneHierarchy::commit_instance_parent_change(Node *p_node, Node *p_parent) {
	if (p_node == nullptr || !is_instance_node(p_node)) {
		return false;
	}
	const ObjectID node_id = p_node->get_instance_id();
	if (!active_parent_changes.has(node_id)) {
		return false;
	}
	ParentChangeState &state = active_parent_changes[node_id];
	const ObjectID requested_parent = p_parent != nullptr ? p_parent->get_instance_id() : ObjectID();
	if (state.commit_started || state.requested_parent != requested_parent || !can_set_instance_parent(p_node, p_parent)) {
		return false;
	}
	state.commit_started = true;
	return set_instance_parent_internal(p_node, p_parent);
}


void VextoriaSceneHierarchy::end_instance_parent_change(Node *p_node) {
	if (p_node != nullptr) {
		active_parent_changes.erase(p_node->get_instance_id());
	}
}

bool VextoriaSceneHierarchy::set_instance_parent_internal(Node *p_node, Node *p_parent) {
	if (!can_set_instance_parent(p_node, p_parent)) {
		return false;
	}
	Node *old_parent = p_node->get_parent();
	if (get_instance_parent(p_node) == p_parent && (p_parent != nullptr || p_node->get_parent() == nullptr)) {
		return true;
	}
	if (p_parent == nullptr) {
		if (old_parent != nullptr) {
			old_parent->remove_child(p_node);
		}
		return get_instance_parent(p_node) == nullptr;
	}
	if (old_parent == nullptr) {
		p_parent->add_child(p_node);
	} else {
		p_node->reparent(p_parent, true);
	}
	return p_node->get_parent() == p_parent;
}

void VextoriaSceneHierarchy::_bind_methods() {
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("create_generic_instance"), &VextoriaSceneHierarchy::create_generic_instance);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("create_generic_spatial_instance"), &VextoriaSceneHierarchy::create_generic_spatial_instance);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("is_instance_node", "node"), &VextoriaSceneHierarchy::is_instance_node);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("is_instance_ancestor_of", "ancestor", "descendant"), &VextoriaSceneHierarchy::is_instance_ancestor_of);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("collect_descendant_event_dispatch_plan", "moving_root", "first_ancestor", "shared_ancestor_branch"), &VextoriaSceneHierarchy::collect_descendant_event_dispatch_plan);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("find_instance_child_by_name", "parent", "name"), &VextoriaSceneHierarchy::find_instance_child_by_name);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("get_instance_children", "parent"), &VextoriaSceneHierarchy::get_instance_children);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("get_instance_descendants", "parent"), &VextoriaSceneHierarchy::get_instance_descendants);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("get_instance_recursive_child_search_order", "parent"), &VextoriaSceneHierarchy::get_instance_recursive_child_search_order);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("get_instance_parent", "node"), &VextoriaSceneHierarchy::get_instance_parent);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("can_set_instance_parent", "node", "parent"), &VextoriaSceneHierarchy::can_set_instance_parent);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("is_instance_parent_locked", "node"), &VextoriaSceneHierarchy::is_instance_parent_locked);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("lock_instance_parent", "node"), &VextoriaSceneHierarchy::lock_instance_parent);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("unlock_instance_parent", "node"), &VextoriaSceneHierarchy::unlock_instance_parent);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("begin_instance_parent_change", "node", "parent"), &VextoriaSceneHierarchy::begin_instance_parent_change);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("commit_instance_parent_change", "node", "parent"), &VextoriaSceneHierarchy::commit_instance_parent_change);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("end_instance_parent_change", "node"), &VextoriaSceneHierarchy::end_instance_parent_change);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("get_instance_child_index", "node"), &VextoriaSceneHierarchy::get_instance_child_index);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("move_instance_child", "node", "index"), &VextoriaSceneHierarchy::move_instance_child);
	ClassDB::bind_static_method("VextoriaSceneHierarchy", D_METHOD("set_instance_parent", "node", "parent"), &VextoriaSceneHierarchy::set_instance_parent);
}
