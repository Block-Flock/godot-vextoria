// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "scene/3d/physics/joints/hinge_joint_3d.h"

// One Godot node and joint RID per logical Rotate. Authored attachment frames
// are local to each body; the node's editor transform is not a physics frame.
class Rotate : public HingeJoint3D {
	GDCLASS(Rotate, HingeJoint3D);
	Transform3D c0;
	Transform3D c1;
	bool enabled = true;
	ObjectID endpoint_a;
	ObjectID endpoint_b;
	void watch_endpoints(PhysicsBody3D *p_a, PhysicsBody3D *p_b);
	void disconnect_endpoints();
	void endpoint_property_changed(const StringName &p_property);

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;
	void _notification(int p_what);
	bool _is_joint_enabled() const override;
	void _configure_joint(RID p_joint, PhysicsBody3D *p_body_a, PhysicsBody3D *p_body_b) override;

public:
	Rotate();
	~Rotate();
	void set_c0(const Transform3D &p_frame);
	Transform3D get_c0() const { return c0; }
	void set_c1(const Transform3D &p_frame);
	Transform3D get_c1() const { return c1; }
	void set_enabled(bool p_enabled);
	bool is_enabled() const { return enabled; }
	void configure_rotate(PhysicsBody3D *p_body_a, PhysicsBody3D *p_body_b, const Transform3D &p_c0, const Transform3D &p_c1, bool p_enabled = true);
	void clear_rotate();
	void set_part0(const NodePath &p_path);
	void set_part1(const NodePath &p_path);
};
