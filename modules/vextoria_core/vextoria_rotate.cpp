// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
#include "vextoria_rotate.h"

#include "vextoria_instance.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

Rotate::Rotate() {
	set_exclude_nodes_from_collision(true);
}

bool Rotate::_is_joint_enabled() const {
	// Unlike an ordinary Godot joint, an incomplete Rotate never attaches one
	// endpoint to the world during staged property loading.
	if (!enabled || get_node_a().is_empty() || get_node_b().is_empty()) {
		return false;
	}
	const PhysicsBody3D *a = Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_a()));
	const PhysicsBody3D *b = Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_b()));
	if (!a || !b) {
		return false;
	}
	const Part *part_a = Object::cast_to<Part>(a);
	const Part *part_b = Object::cast_to<Part>(b);
	return !(part_a && part_b && part_a->is_anchored() && part_b->is_anchored());
}

void Rotate::disconnect_endpoints() {
	for (ObjectID id : { endpoint_a, endpoint_b }) {
		Object *body = ObjectDB::get_instance(id);
		Callable callback = callable_mp(this, &Rotate::endpoint_property_changed);
		if (body && body->has_signal(SNAME("vextoria_property_changed")) && body->is_connected(SNAME("vextoria_property_changed"), callback)) {
			body->disconnect(SNAME("vextoria_property_changed"), callback);
		}
	}
	endpoint_a = ObjectID();
	endpoint_b = ObjectID();
}

void Rotate::watch_endpoints(PhysicsBody3D *p_a, PhysicsBody3D *p_b) {
	disconnect_endpoints();
	endpoint_a = p_a ? p_a->get_instance_id() : ObjectID();
	endpoint_b = p_b ? p_b->get_instance_id() : ObjectID();
	for (ObjectID id : { endpoint_a, endpoint_b }) {
		Object *body = ObjectDB::get_instance(id);
		Callable callback = callable_mp(this, &Rotate::endpoint_property_changed);
		if (body && body->has_signal(SNAME("vextoria_property_changed")) && !body->is_connected(SNAME("vextoria_property_changed"), callback)) {
			body->connect(SNAME("vextoria_property_changed"), callback);
		}
	}
}

void Rotate::endpoint_property_changed(const StringName &p_property) {
	if (p_property == SNAME("Anchored")) {
		_update_joint();
	}
}

void Rotate::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		watch_endpoints(Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_a())), Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_b())));
		_update_joint();
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		disconnect_endpoints();
	}
}

Rotate::~Rotate() {
	disconnect_endpoints();
}

void Rotate::_configure_joint(RID p_joint, PhysicsBody3D *p_body_a, PhysicsBody3D *p_body_b) {
	ERR_FAIL_NULL(p_body_b);
	// Both authored frames are body-local. The editor node transform must
	// never recapture their pivots or change the native axle direction.
	PhysicsServer3D *server = PhysicsServer3D::get_singleton();
	server->joint_make_hinge(p_joint, p_body_a->get_rid(), c0.orthonormalized(), p_body_b->get_rid(), c1.orthonormalized());
	for (int i = 0; i < PARAM_MAX; i++) {
		server->hinge_joint_set_param(p_joint, PhysicsServer3D::HingeJointParam(i), params[i]);
	}
	for (int i = 0; i < FLAG_MAX; i++) {
		server->hinge_joint_set_flag(p_joint, PhysicsServer3D::HingeJointFlag(i), flags[i]);
	}
}

void Rotate::set_c0(const Transform3D &p_frame) {
	if (c0 == p_frame) {
		return;
	}
	c0 = p_frame;
	_update_joint();
	emit_signal(SNAME("vextoria_property_changed"), SNAME("C0"));
}

void Rotate::set_c1(const Transform3D &p_frame) {
	if (c1 == p_frame) {
		return;
	}
	c1 = p_frame;
	_update_joint();
	emit_signal(SNAME("vextoria_property_changed"), SNAME("C1"));
}

void Rotate::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	if (is_configured()) {
		_disconnect_signals();
	}
	enabled = p_enabled;
	_update_joint();
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Enabled"));
}

void Rotate::configure_rotate(PhysicsBody3D *p_body_a, PhysicsBody3D *p_body_b, const Transform3D &p_c0, const Transform3D &p_c1, bool p_enabled) {
	ERR_FAIL_NULL(p_body_a);
	ERR_FAIL_NULL(p_body_b);
	ERR_FAIL_COND(p_body_a == p_body_b || !is_inside_tree() || !p_body_a->is_inside_tree() || !p_body_b->is_inside_tree());
	ERR_FAIL_COND_MSG(get_world_3d() != p_body_a->get_world_3d() || get_world_3d() != p_body_b->get_world_3d(), "Rotate endpoints must belong to its World3D.");
	c0 = p_c0;
	c1 = p_c1;
	enabled = p_enabled;
	set_nodes(get_path_to(p_body_a), get_path_to(p_body_b));
	watch_endpoints(p_body_a, p_body_b);
}

void Rotate::clear_rotate() {
	enabled = false;
	set_nodes(NodePath(), NodePath());
	disconnect_endpoints();
}

void Rotate::set_part0(const NodePath &p_path) {
	set_node_a(p_path);
	if (is_inside_tree()) {
		watch_endpoints(Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_a())), Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_b())));
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Part0"));
}

void Rotate::set_part1(const NodePath &p_path) {
	set_node_b(p_path);
	if (is_inside_tree()) {
		watch_endpoints(Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_a())), Object::cast_to<PhysicsBody3D>(get_node_or_null(get_node_b())));
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Part1"));
}

void Rotate::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "node_a" || p_property.name == "node_b") {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
}

void Rotate::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_part0", "path"), &Rotate::set_part0);
	ClassDB::bind_method(D_METHOD("set_part1", "path"), &Rotate::set_part1);
	ClassDB::bind_method(D_METHOD("set_c0", "frame"), &Rotate::set_c0);
	ClassDB::bind_method(D_METHOD("get_c0"), &Rotate::get_c0);
	ClassDB::bind_method(D_METHOD("set_c1", "frame"), &Rotate::set_c1);
	ClassDB::bind_method(D_METHOD("get_c1"), &Rotate::get_c1);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &Rotate::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &Rotate::is_enabled);
	ClassDB::bind_method(D_METHOD("configure_rotate", "part0", "part1", "c0", "c1", "enabled"), &Rotate::configure_rotate, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("clear_rotate"), &Rotate::clear_rotate);
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "C0"), "set_c0", "get_c0");
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "C1"), "set_c1", "get_c1");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "Part0", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PhysicsBody3D"), "set_part0", "get_node_a");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "Part1", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PhysicsBody3D"), "set_part1", "get_node_b");
	ADD_SIGNAL(MethodInfo("vextoria_property_changed", PropertyInfo(Variant::STRING_NAME, "property")));
}
