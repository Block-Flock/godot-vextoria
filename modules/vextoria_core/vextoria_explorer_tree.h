/**************************************************************************/
/*  vextoria_explorer_tree.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/templates/hash_map.h"
#include "scene/gui/tree.h"

class VextoriaExplorerTree : public Tree {
	GDCLASS(VextoriaExplorerTree, Tree);

	Node *source_root = nullptr;
	HashMap<Node *, TreeItem *> node_items;
	HashMap<TreeItem *, Node *> item_nodes;
	bool order_dirty = false;

	void _add_subtree(Node *p_node, bool p_recurse = true);
	void _remove_item(TreeItem *p_item);
	void _clear_items();
	void _sync_order_for(Node *p_node, TreeItem *p_parent_item);
	void _sync_order_children(Node *p_node, TreeItem *p_parent_item, TreeItem *&r_previous);
	void _node_added(Node *p_node);
	void _node_removed(Node *p_node);
	void _node_renamed(Node *p_node);
	void _tree_changed();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_source_root(Node *p_root);
	Node *get_source_root() const { return source_root; }
	TreeItem *get_item_for_node(Node *p_node) const;
	TreeItem *ensure_item_for_node(Node *p_node);
	Node *get_node_for_item(TreeItem *p_item) const;
};
