/**************************************************************************/
/*  vextoria_explorer_tree.cpp                                            */
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

#include "vextoria_explorer_tree.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"

void VextoriaExplorerTree::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source_root", "root"), &VextoriaExplorerTree::set_source_root);
	ClassDB::bind_method(D_METHOD("get_source_root"), &VextoriaExplorerTree::get_source_root);
	ClassDB::bind_method(D_METHOD("get_item_for_node", "node"), &VextoriaExplorerTree::get_item_for_node);
	ClassDB::bind_method(D_METHOD("ensure_item_for_node", "node"), &VextoriaExplorerTree::ensure_item_for_node);
	ClassDB::bind_method(D_METHOD("get_node_for_item", "item"), &VextoriaExplorerTree::get_node_for_item);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "source_root", PROPERTY_HINT_NODE_TYPE, "Node", PROPERTY_USAGE_NONE), "set_source_root", "get_source_root");
	ADD_SIGNAL(MethodInfo("item_added", PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::OBJECT, "item")));
	ADD_SIGNAL(MethodInfo("item_removing", PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::OBJECT, "item")));
}

void VextoriaExplorerTree::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			get_tree()->connect(SNAME("node_added"), callable_mp(this, &VextoriaExplorerTree::_node_added));
			get_tree()->connect(SNAME("node_removed"), callable_mp(this, &VextoriaExplorerTree::_node_removed));
			get_tree()->connect(SNAME("node_renamed"), callable_mp(this, &VextoriaExplorerTree::_node_renamed));
			get_tree()->connect(SNAME("tree_changed"), callable_mp(this, &VextoriaExplorerTree::_tree_changed));
			_clear_items();
			if (source_root) {
				_add_subtree(source_root);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			get_tree()->disconnect(SNAME("node_added"), callable_mp(this, &VextoriaExplorerTree::_node_added));
			get_tree()->disconnect(SNAME("node_removed"), callable_mp(this, &VextoriaExplorerTree::_node_removed));
			get_tree()->disconnect(SNAME("node_renamed"), callable_mp(this, &VextoriaExplorerTree::_node_renamed));
			get_tree()->disconnect(SNAME("tree_changed"), callable_mp(this, &VextoriaExplorerTree::_tree_changed));
		} break;
		case NOTIFICATION_PROCESS: {
			set_process(false);
			if (order_dirty && source_root && node_items.has(source_root)) {
				order_dirty = false;
				_sync_order_for(source_root, node_items[source_root]);
			}
		} break;
	}
}

void VextoriaExplorerTree::set_source_root(Node *p_root) {
	if (source_root == p_root) {
		return;
	}
	_clear_items();
	source_root = p_root;
	if (is_inside_tree() && source_root) {
		_add_subtree(source_root);
	}
}

TreeItem *VextoriaExplorerTree::get_item_for_node(Node *p_node) const {
	TreeItem *const *item = node_items.getptr(p_node);
	return item ? *item : nullptr;
}

TreeItem *VextoriaExplorerTree::ensure_item_for_node(Node *p_node) {
	if (p_node && !node_items.has(p_node)) {
		_add_subtree(p_node, false);
	}
	return get_item_for_node(p_node);
}

Node *VextoriaExplorerTree::get_node_for_item(TreeItem *p_item) const {
	Node *const *node = item_nodes.getptr(p_item);
	return node ? *node : nullptr;
}

void VextoriaExplorerTree::_add_subtree(Node *p_node, bool p_recurse) {
	if (!source_root || p_node->is_internal() || (p_node != source_root && !source_root->is_ancestor_of(p_node))) {
		return;
	}
	for (Node *ancestor = p_node; ancestor && ancestor != source_root; ancestor = ancestor->get_parent()) {
		if (ancestor->has_meta(SNAME("_vextoria_explorer_excluded"))) {
			return;
		}
	}
	TreeItem *parent_item = nullptr;
	if (p_node != source_root) {
		Node *parent = p_node->get_parent();
		while (parent && !node_items.has(parent)) {
			parent = parent->get_parent();
		}
		if (!parent) {
			return;
		}
		parent_item = node_items[parent];
	}
	if ((p_node == source_root || p_node->has_meta(SNAME("_vextoria_instance"))) && !node_items.has(p_node)) {
		TreeItem *item = create_item(parent_item);
		item->set_text(0, p_node->get_name());
		item->set_metadata(0, p_node);
		node_items.insert(p_node, item);
		item_nodes.insert(item, p_node);
		emit_signal(SNAME("item_added"), p_node, item);
	}
	if (p_recurse) {
		for (int i = 0; i < p_node->get_child_count(false); ++i) {
			_add_subtree(p_node->get_child(i, false));
		}
	}
	order_dirty = true;
	set_process(true);
}

void VextoriaExplorerTree::_remove_item(TreeItem *p_item) {
	for (TreeItem *child = p_item->get_first_child(); child; child = child->get_next()) {
		_remove_item(child);
	}
	Node *node = item_nodes[p_item];
	emit_signal(SNAME("item_removing"), node, p_item);
	item_nodes.erase(p_item);
	node_items.erase(node);
}

void VextoriaExplorerTree::_clear_items() {
	if (get_root()) {
		_remove_item(get_root());
	}
	clear();
	node_items.clear();
	item_nodes.clear();
	order_dirty = false;
	set_process(false);
}

void VextoriaExplorerTree::_sync_order_children(Node *p_node, TreeItem *p_parent_item, TreeItem *&r_previous) {
	for (int i = 0; i < p_node->get_child_count(false); ++i) {
		Node *child = p_node->get_child(i, false);
		TreeItem **item_ptr = node_items.getptr(child);
		if (item_ptr && (*item_ptr)->get_parent() == p_parent_item) {
			TreeItem *item = *item_ptr;
			if (r_previous) {
				if (item->get_prev() != r_previous) {
					item->move_after(r_previous);
				}
			} else if (p_parent_item->get_first_child() != item) {
				item->move_before(p_parent_item->get_first_child());
			}
			r_previous = item;
			_sync_order_for(child, item);
		} else {
			_sync_order_children(child, p_parent_item, r_previous);
		}
	}
}

void VextoriaExplorerTree::_sync_order_for(Node *p_node, TreeItem *p_parent_item) {
	TreeItem *previous = nullptr;
	_sync_order_children(p_node, p_parent_item, previous);
}

void VextoriaExplorerTree::_node_added(Node *p_node) {
	if (source_root && (p_node == source_root || source_root->is_ancestor_of(p_node))) {
		// SceneTree emits node_added for each descendant. Walk only this node to
		// avoid quadratic rescans when a populated world enters the tree.
		_add_subtree(p_node, false);
	}
}

void VextoriaExplorerTree::_node_removed(Node *p_node) {
	TreeItem **item_ptr = node_items.getptr(p_node);
	if (!item_ptr) {
		return;
	}
	TreeItem *item = *item_ptr;
	_remove_item(item);
	if (p_node == source_root) {
		source_root = nullptr;
		clear();
	} else {
		TreeItem *parent = item->get_parent();
		if (parent) {
			parent->remove_child(item);
		}
		memdelete(item);
	}
}

void VextoriaExplorerTree::_node_renamed(Node *p_node) {
	TreeItem **item_ptr = node_items.getptr(p_node);
	if (item_ptr) {
		(*item_ptr)->set_text(0, p_node->get_name());
	}
}

void VextoriaExplorerTree::_tree_changed() {
	if (source_root && node_items.has(source_root)) {
		order_dirty = true;
		set_process(true);
	}
}
