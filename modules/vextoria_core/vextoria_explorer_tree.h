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

class Texture2D;

class VextoriaExplorerTree : public Tree {
	GDCLASS(VextoriaExplorerTree, Tree);

	struct NodePresentation {
		Ref<Texture2D> icon;
		bool renameable = false;
		bool model_non_editable = false;
		bool model_editable_color = false;
		bool linked = false;
		Ref<Texture2D> linked_icon;
		String linked_tooltip;
		bool locked = false;
		Ref<Texture2D> lock_icon;
	};

	Node *source_root = nullptr;
	int64_t selection_owner_id = 0;
	HashMap<Node *, TreeItem *> node_items;
	HashMap<TreeItem *, Node *> item_nodes;
	HashMap<Node *, NodePresentation> node_presentations;
	String search_query;
	ObjectID pending_scroll_node;
	bool order_dirty = false;
	bool search_filter_dirty = false;
	bool search_request_pending = false;

	void _add_subtree(Node *p_node, bool p_recurse = true);
	void _remove_item(TreeItem *p_item);
	void _clear_items();
	void _sync_order_for(Node *p_node, TreeItem *p_parent_item);
	void _sync_order_children(Node *p_node, TreeItem *p_parent_item, TreeItem *&r_previous);
	void _node_added(Node *p_node);
	void _node_removed(Node *p_node);
	void _node_renamed(Node *p_node);
	void _tree_changed();
	void _multi_selected(TreeItem *p_item, int p_column, bool p_selected);
	Node *_apply_search_filter(TreeItem *p_item, const String &p_query);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_source_root(Node *p_root);
	Node *get_source_root() const { return source_root; }
	bool set_selection_owner(int64_t p_owner_object_id);
	TreeItem *get_item_for_node(Node *p_node) const;
	TreeItem *ensure_item_for_node(Node *p_node);
	bool set_instance_selected(Node *p_node, bool p_selected);
	Node *get_node_for_item(TreeItem *p_item) const;
	void set_node_class_presentation(Node *p_node, const Ref<Texture2D> &p_icon, bool p_renameable);
	void set_node_model_presentation(Node *p_node, bool p_non_editable, bool p_editable_color);
	void set_node_link_presentation(Node *p_node, bool p_linked, const Ref<Texture2D> &p_icon, const String &p_tooltip);
	void set_node_lock_presentation(Node *p_node, bool p_locked, const Ref<Texture2D> &p_icon);
	bool refresh_node_presentation(Node *p_node);
	void sync_item_name(TreeItem *p_item);
	bool is_node_excluded(Node *p_node) const;
	Node *apply_search_filter(const String &p_query);
	void queue_search_filter(const String &p_query);
};
