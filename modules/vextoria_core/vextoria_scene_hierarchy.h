// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include "core/variant/array.h"
#include "core/object/object.h"

class Node;

// Roblox Instance ordering is the order of marked Instance Nodes, not the
// raw Godot child index (a Godot parent may also own private helper Nodes).
class VextoriaSceneHierarchy : public Object {
	GDCLASS(VextoriaSceneHierarchy, Object);

protected:
	static void _bind_methods();

public:
	static Array get_instance_children(Node *p_parent);
	static Node *get_instance_parent(Node *p_node);
	static bool can_set_instance_parent(Node *p_node, Node *p_parent);
	static int64_t get_instance_child_index(Node *p_node);
	static bool move_instance_child(Node *p_node, int64_t p_index);
	static bool set_instance_parent(Node *p_node, Node *p_parent);
};
