// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include "core/object/object.h"
#include "core/variant/array.h"

class Node;

// Native source of truth for runtime Creator selection. The managed Creator
// layer remains responsible for domain side effects, while selected identity
// and ordering live in the engine and are represented by actual Godot Nodes.
class VextoriaCreatorSelectionState : public Object {
	GDCLASS(VextoriaCreatorSelectionState, Object);

protected:
	static void _bind_methods();

public:
	static bool select_node(int64_t p_owner_object_id, Node *p_selection_root, Node *p_node);
	static bool deselect_node(int64_t p_owner_object_id, Node *p_node);
	static bool has_node(int64_t p_owner_object_id, Node *p_selection_root, Node *p_node);
	static Array get_selected_nodes(int64_t p_owner_object_id, Node *p_selection_root);
	static void clear_selection(int64_t p_owner_object_id);
	static void forget_owner(int64_t p_owner_object_id);
};
