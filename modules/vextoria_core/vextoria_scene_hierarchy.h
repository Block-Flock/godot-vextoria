// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include "core/variant/array.h"
#include "core/object/object.h"

class Node;
class Node3D;
class String;

// Roblox Instance ordering is the order of marked Instance Nodes, not the
// raw Godot child index (a Godot parent may also own private helper Nodes).
class VextoriaSceneHierarchy : public Object {
	GDCLASS(VextoriaSceneHierarchy, Object);
	static bool set_instance_parent_internal(Node *p_node, Node *p_parent);

protected:
	static void _bind_methods();

public:
	static Node *create_generic_instance();
	static Node3D *create_generic_spatial_instance();
	static Node *find_instance_child_by_name(Node *p_parent, const String &p_name);
	static Array get_instance_children(Node *p_parent);
	static Array get_instance_descendants(Node *p_parent);
	static Node *get_instance_parent(Node *p_node);
	static bool can_set_instance_parent(Node *p_node, Node *p_parent);
	static bool is_instance_parent_locked(Node *p_node);
	static bool lock_instance_parent(Node *p_node);
	static bool unlock_instance_parent(Node *p_node);
	static bool begin_instance_parent_change(Node *p_node, Node *p_parent);
	static bool commit_instance_parent_change(Node *p_node, Node *p_parent);
	static void end_instance_parent_change(Node *p_node);
	static int64_t get_instance_child_index(Node *p_node);
	static bool move_instance_child(Node *p_node, int64_t p_index);
	static bool set_instance_parent(Node *p_node, Node *p_parent);
};
