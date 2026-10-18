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
#include "scene/resources/texture.h"
#include "scene/main/scene_tree.h"

static Ref<Texture2D> _get_node_texture_meta(Node *p_node, const StringName &p_key) {
	if (p_node == nullptr || !p_node->has_meta(p_key)) {
		return Ref<Texture2D>();
	}
	Variant value = p_node->get_meta(p_key);
	if (value.get_type() != Variant::OBJECT) {
		return Ref<Texture2D>();
	}
	return Ref<Texture2D>(Object::cast_to<Texture2D>(value));
}

static void _sync_explorer_button(TreeItem *p_item, int p_id, bool p_enabled, const Ref<Texture2D> &p_icon, const String &p_tooltip) {
	const int button_index = p_item->get_button_by_id(0, p_id);
	if (!p_enabled || p_icon.is_null()) {
		if (button_index >= 0) {
			p_item->erase_button(0, button_index);
		}
		return;
	}
	if (button_index < 0) {
		p_item->add_button(0, p_icon, p_id, false, p_tooltip);
	} else {
		p_item->set_button(0, button_index, p_icon);
		p_item->set_button_tooltip_text(0, button_index, p_tooltip);
	}
}

void VextoriaExplorerTree::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source_root", "root"), &VextoriaExplorerTree::set_source_root);
	ClassDB::bind_method(D_METHOD("get_source_root"), &VextoriaExplorerTree::get_source_root);
	ClassDB::bind_method(D_METHOD("get_item_for_node", "node"), &VextoriaExplorerTree::get_item_for_node);
	ClassDB::bind_method(D_METHOD("ensure_item_for_node", "node"), &VextoriaExplorerTree::ensure_item_for_node);
	ClassDB::bind_method(D_METHOD("set_instance_selected", "node", "selected"), &VextoriaExplorerTree::set_instance_selected);
	ClassDB::bind_method(D_METHOD("get_node_for_item", "item"), &VextoriaExplorerTree::get_node_for_item);
	ClassDB::bind_method(D_METHOD("refresh_node_presentation", "node"), &VextoriaExplorerTree::refresh_node_presentation);
	ClassDB::bind_method(D_METHOD("sync_item_name", "item"), &VextoriaExplorerTree::sync_item_name);
	ClassDB::bind_method(D_METHOD("is_node_excluded", "node"), &VextoriaExplorerTree::is_node_excluded);
	ClassDB::bind_method(D_METHOD("apply_search_filter", "query"), &VextoriaExplorerTree::apply_search_filter);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "source_root", PROPERTY_HINT_NODE_TYPE, "Node", PROPERTY_USAGE_NONE), "set_source_root", "get_source_root");
	ADD_SIGNAL(MethodInfo("item_added", PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::OBJECT, "item")));
	ADD_SIGNAL(MethodInfo("item_removing", PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::OBJECT, "item")));
	ADD_SIGNAL(MethodInfo("instance_selected", PropertyInfo(Variant::OBJECT, "node"), PropertyInfo(Variant::BOOL, "selected")));
}

void VextoriaExplorerTree::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			connect(SNAME("multi_selected"), callable_mp(this, &VextoriaExplorerTree::_multi_selected));
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
			disconnect(SNAME("multi_selected"), callable_mp(this, &VextoriaExplorerTree::_multi_selected));
			get_tree()->disconnect(SNAME("node_added"), callable_mp(this, &VextoriaExplorerTree::_node_added));
			get_tree()->disconnect(SNAME("node_removed"), callable_mp(this, &VextoriaExplorerTree::_node_removed));
			get_tree()->disconnect(SNAME("node_renamed"), callable_mp(this, &VextoriaExplorerTree::_node_renamed));
			get_tree()->disconnect(SNAME("tree_changed"), callable_mp(this, &VextoriaExplorerTree::_tree_changed));
		} break;
		case NOTIFICATION_PROCESS: {
			if (order_dirty && source_root && node_items.has(source_root)) {
				order_dirty = false;
				_sync_order_for(source_root, node_items[source_root]);
			}
			if (pending_scroll_node.is_valid()) {
				Node *node = Object::cast_to<Node>(ObjectDB::get_instance(pending_scroll_node));
				pending_scroll_node = ObjectID();
				if (node != nullptr) {
					TreeItem *item = get_item_for_node(node);
					if (item != nullptr) {
						scroll_to_item(item);
					}
				}
			}
			set_process(order_dirty || pending_scroll_node.is_valid());
		} break;
	}
}

void VextoriaExplorerTree::_multi_selected(TreeItem *p_item, int p_column, bool p_selected) {
	if (p_column != 0) {
		return;
	}
	Node *node = get_node_for_item(p_item);
	if (node != nullptr) {
		emit_signal(SNAME("instance_selected"), node, p_selected);
	}
}

void VextoriaExplorerTree::set_source_root(Node *p_root) {
	if (source_root == p_root) {
		return;
	}
	_clear_items();
	search_query = String();
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
	if (!p_node || !source_root || (p_node != source_root && !source_root->is_ancestor_of(p_node))) {
		return nullptr;
	}
	if (p_node && !node_items.has(p_node)) {
		Node *parent = p_node->get_parent();
		if (parent && !node_items.has(parent)) {
			ensure_item_for_node(parent);
		}
		_add_subtree(p_node, false);
	}
	return get_item_for_node(p_node);
}

bool VextoriaExplorerTree::set_instance_selected(Node *p_node, bool p_selected) {
	TreeItem *item = get_item_for_node(p_node);
	if (item == nullptr) {
		return false;
	}
	if (!p_selected) {
		item->deselect(0);
		return true;
	}
	for (TreeItem *ancestor = item->get_parent(); ancestor != nullptr; ancestor = ancestor->get_parent()) {
		ancestor->set_collapsed(false);
	}
	item->select(0);
	pending_scroll_node = p_node->get_instance_id();
	set_process(true);
	return true;
}

Node *VextoriaExplorerTree::get_node_for_item(TreeItem *p_item) const {
	Node *const *node = item_nodes.getptr(p_item);
	return node ? *node : nullptr;
}

bool VextoriaExplorerTree::refresh_node_presentation(Node *p_node) {
	TreeItem *item = get_item_for_node(p_node);
	if (item == nullptr || p_node == nullptr) {
		return false;
	}

	const bool first_presentation = !item->has_meta(SNAME("_vextoria_presentation_initialized"));
	if (first_presentation && p_node != source_root) {
		item->set_collapsed(true);
	}

	Ref<Texture2D> icon = _get_node_texture_meta(p_node, SNAME("_vextoria_explorer_icon"));
	if (icon.is_valid()) {
		item->set_icon(0, icon);
	}
	const bool renameable = p_node->has_meta(SNAME("_vextoria_explorer_renameable")) &&
			p_node->get_meta(SNAME("_vextoria_explorer_renameable")).booleanize();
	item->set_editable(0, renameable);

	const bool model_non_editable = p_node->has_meta(SNAME("_vextoria_explorer_model_non_editable")) &&
			p_node->get_meta(SNAME("_vextoria_explorer_model_non_editable")).booleanize();
	const bool model_editable_color = p_node->has_meta(SNAME("_vextoria_explorer_model_editable_color")) &&
			p_node->get_meta(SNAME("_vextoria_explorer_model_editable_color")).booleanize();
	item->set_meta(SNAME("_model_non_editable"), model_non_editable);
	if (model_editable_color) {
		item->set_custom_color(0, Color("#FFBC58"));
	} else {
		item->clear_custom_color(0);
	}
	const bool force_invisible = item->has_meta(SNAME("_force_invisible")) &&
			item->get_meta(SNAME("_force_invisible")).booleanize();
	if (search_query.is_empty()) {
		item->set_visible(!force_invisible && !model_non_editable);
	}

	const bool linked = p_node->has_meta(SNAME("_vextoria_explorer_linked")) &&
			p_node->get_meta(SNAME("_vextoria_explorer_linked")).booleanize();
	const String linked_tooltip = p_node->has_meta(SNAME("_vextoria_explorer_link_tooltip")) ?
			String(p_node->get_meta(SNAME("_vextoria_explorer_link_tooltip"))) : String();
	_sync_explorer_button(item, 1, linked, _get_node_texture_meta(p_node, SNAME("_vextoria_explorer_link_icon")), linked_tooltip);

	const bool locked = p_node->has_meta(SNAME("_vextoria_explorer_locked")) &&
			p_node->get_meta(SNAME("_vextoria_explorer_locked")).booleanize();
	_sync_explorer_button(item, 2, locked, _get_node_texture_meta(p_node, SNAME("_vextoria_explorer_lock_icon")), "Locked");

	item->set_meta(SNAME("_vextoria_presentation_initialized"), true);
	if (!search_query.is_empty()) {
		_apply_search_filter(get_root(), search_query);
	}
	return true;
}

void VextoriaExplorerTree::sync_item_name(TreeItem *p_item) {
	if (p_item == nullptr) {
		return;
	}
	Node *const *node = item_nodes.getptr(p_item);
	if (node != nullptr && *node != nullptr) {
		p_item->set_text(0, (*node)->get_name());
	}
}

bool VextoriaExplorerTree::is_node_excluded(Node *p_node) const {
	if (!p_node || !source_root || p_node == source_root || !source_root->is_ancestor_of(p_node)) {
		return false;
	}
	for (Node *ancestor = p_node; ancestor && ancestor != source_root; ancestor = ancestor->get_parent()) {
		if (ancestor->has_meta(SNAME("_vextoria_explorer_excluded"))) {
			return true;
		}
	}
	return false;
}

Node *VextoriaExplorerTree::_apply_search_filter(TreeItem *p_item, const String &p_query) {
	if (p_item == nullptr) {
		return nullptr;
	}

	Node **node_ptr = item_nodes.getptr(p_item);
	Node *node = node_ptr ? *node_ptr : nullptr;
	const bool force_invisible = p_item->has_meta(SNAME("_force_invisible")) &&
			p_item->get_meta(SNAME("_force_invisible")).booleanize();
	Node *first_match = nullptr;
	if (p_query.is_empty()) {
		const bool model_non_editable = p_item->has_meta(SNAME("_model_non_editable")) &&
				p_item->get_meta(SNAME("_model_non_editable")).booleanize();
		p_item->set_visible(!force_invisible && !model_non_editable);
	} else {
		const bool matches = !force_invisible && node != nullptr &&
				String(node->get_name()).to_lower().contains(p_query);
		p_item->set_visible(matches);
		if (matches) {
			first_match = node;
			for (TreeItem *parent = p_item->get_parent(); parent != nullptr; parent = parent->get_parent()) {
				if (parent->has_meta(SNAME("_force_invisible")) &&
						parent->get_meta(SNAME("_force_invisible")).booleanize()) {
					break;
				}
				parent->set_visible(true);
			}
		}
	}

	for (TreeItem *child = p_item->get_first_child(); child != nullptr; child = child->get_next()) {
		Node *child_match = _apply_search_filter(child, p_query);
		if (first_match == nullptr) {
			first_match = child_match;
		}
	}
	return first_match;
}

Node *VextoriaExplorerTree::apply_search_filter(const String &p_query) {
	search_query = p_query.to_lower();
	return _apply_search_filter(get_root(), search_query);
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
		node_items.insert(p_node, item);
		item_nodes.insert(item, p_node);
		refresh_node_presentation(p_node);
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
	if (node != nullptr && pending_scroll_node == node->get_instance_id()) {
		pending_scroll_node = ObjectID();
	}
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
	pending_scroll_node = ObjectID();
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
