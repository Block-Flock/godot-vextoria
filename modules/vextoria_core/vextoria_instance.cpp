/**************************************************************************/
/*  vextoria_instance.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                        VEXTORIA ENGINE                                  */
/*                         https://godotengine.org                        */
/**************************************************************************/
/* Copyright (c) 2026 Vextoria contributors.                              */
/* Copyright (c) 2014-present Godot Engine contributors.                   */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "vextoria_instance.h"

#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"

VextoriaInstance::VextoriaInstance() {
	// Roblox Folder/Script have no CFrame, but spatial descendants must still
	// inherit the nearest Model/Part frame. Node3D honors this opt-in marker.
	set_meta("_vextoria_spatial_passthrough", true);
}

static void collect_passthrough_spatial_roots(Node *p_node, LocalVector<Node3D *> &r_roots) {
	for (int i = 0; i < p_node->get_child_count(false); ++i) {
		Node *child = p_node->get_child(i, false);
		if (Node3D *spatial = Object::cast_to<Node3D>(child)) {
			r_roots.push_back(spatial);
		} else if (child->has_meta("_vextoria_spatial_passthrough")) {
			collect_passthrough_spatial_roots(child, r_roots);
		}
	}
}

void VextoriaInstance::reparent(RequiredParam<Node> p_parent, bool p_keep_global_transform) {
	if (!p_keep_global_transform) {
		Node::reparent(p_parent, false);
		return;
	}

	// A Folder/Script has no own transform for Node::reparent to preserve.
	// Preserve the world frames of its first spatial descendants instead; all
	// deeper Node3Ds continue to inherit from those roots normally.
	LocalVector<Node3D *> spatial_roots;
	collect_passthrough_spatial_roots(this, spatial_roots);
	LocalVector<Transform3D> world_frames;
	world_frames.reserve(spatial_roots.size());
	for (Node3D *spatial : spatial_roots) {
		world_frames.push_back(spatial->get_global_transform());
	}
	Node::reparent(p_parent, true);
	for (uint32_t i = 0; i < spatial_roots.size(); ++i) {
		spatial_roots[i]->set_global_transform(world_frames[i]);
	}
}

bool VextoriaInstance::is_locked() const {
	return has_meta("_edit_lock_");
}

void VextoriaInstance::set_locked(bool p_locked) {
	if (p_locked) {
		set_meta("_edit_lock_", true);
	} else if (has_meta("_edit_lock_")) {
		remove_meta("_edit_lock_");
	}
}

void VextoriaInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_archivable"), &VextoriaInstance::is_archivable);
	ClassDB::bind_method(D_METHOD("set_archivable", "archivable"), &VextoriaInstance::set_archivable);
	ClassDB::bind_method(D_METHOD("is_locked"), &VextoriaInstance::is_locked);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &VextoriaInstance::set_locked);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Archivable"), "set_archivable", "is_archivable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Locked"), "set_locked", "is_locked");
}

bool Model::is_locked() const {
	return has_meta("_edit_lock_");
}

void Model::set_locked(bool p_locked) {
	if (p_locked) {
		set_meta("_edit_lock_", true);
	} else if (has_meta("_edit_lock_")) {
		remove_meta("_edit_lock_");
	}
}

void Model::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_archivable"), &Model::is_archivable);
	ClassDB::bind_method(D_METHOD("set_archivable", "archivable"), &Model::set_archivable);
	ClassDB::bind_method(D_METHOD("is_locked"), &Model::is_locked);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &Model::set_locked);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Archivable"), "set_archivable", "is_archivable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Locked"), "set_locked", "is_locked");
}

Part::Part() {
	box_mesh.instantiate();
	box_shape.instantiate();
	material.instantiate();
	box_mesh->set_size(size);
	box_shape->set_size(size);
	material->set_albedo(color);

	visual = memnew(MeshInstance3D);
	visual->set_name("Visual");
	visual->set_mesh(box_mesh);
	visual->set_material_override(material);
	add_child(visual, false, INTERNAL_MODE_BACK);

	collision = memnew(CollisionShape3D);
	collision->set_name("Collision");
	collision->set_shape(box_shape);
	add_child(collision, false, INTERNAL_MODE_BACK);
}

bool Part::is_locked() const {
	return has_meta("_edit_lock_");
}

void Part::set_locked(bool p_locked) {
	if (p_locked) {
		set_meta("_edit_lock_", true);
	} else if (has_meta("_edit_lock_")) {
		remove_meta("_edit_lock_");
	}
}

void Part::set_size(const Vector3 &p_size) {
	ERR_FAIL_COND_MSG(p_size.x <= 0 || p_size.y <= 0 || p_size.z <= 0, "Part.Size must be positive on every axis.");
	if (size == p_size) {
		return;
	}
	size = p_size;
	box_mesh->set_size(size);
	box_shape->set_size(size);
}

void Part::set_color(const Color &p_color) {
	color = p_color;
	material->set_albedo(color);
}

void Part::set_anchored(bool p_anchored) {
	anchored = p_anchored;
	set_freeze_enabled(anchored);
}

void Part::set_can_collide(bool p_can_collide) {
	can_collide = p_can_collide;
	// Keep the shape installed so toggling CanCollide does not rebuild the body
	// or invalidate joints. A zero layer alone is insufficient: the body's
	// mask could still request contacts with another object's layer.
	collision->set_disabled(!can_collide);
}

void Part::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_size"), &Part::get_size);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Part::set_size);
	ClassDB::bind_method(D_METHOD("get_color"), &Part::get_color);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &Part::set_color);
	ClassDB::bind_method(D_METHOD("is_archivable"), &Part::is_archivable);
	ClassDB::bind_method(D_METHOD("set_archivable", "archivable"), &Part::set_archivable);
	ClassDB::bind_method(D_METHOD("is_locked"), &Part::is_locked);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &Part::set_locked);
	ClassDB::bind_method(D_METHOD("is_anchored"), &Part::is_anchored);
	ClassDB::bind_method(D_METHOD("set_anchored", "anchored"), &Part::set_anchored);
	ClassDB::bind_method(D_METHOD("get_can_collide"), &Part::get_can_collide);
	ClassDB::bind_method(D_METHOD("set_can_collide", "can_collide"), &Part::set_can_collide);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Size"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "Color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Archivable"), "set_archivable", "is_archivable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Locked"), "set_locked", "is_locked");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Anchored"), "set_anchored", "is_anchored");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "CanCollide"), "set_can_collide", "get_can_collide");
}

void VextoriaScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_source"), &VextoriaScript::get_source);
	ClassDB::bind_method(D_METHOD("set_source", "source"), &VextoriaScript::set_source);
	ClassDB::bind_method(D_METHOD("is_disabled"), &VextoriaScript::is_disabled);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &VextoriaScript::set_disabled);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "Source", PROPERTY_HINT_MULTILINE_TEXT), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Disabled"), "set_disabled", "is_disabled");
}
