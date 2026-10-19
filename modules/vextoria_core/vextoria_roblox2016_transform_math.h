// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_transform_math.h                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         VEXTORIA ENGINE                                */
/**************************************************************************/

#pragma once

#include "core/math/transform_3d.h"
#include "core/math/vector3.h"
#include "core/object/object.h"
#include "scene/3d/node_3d.h"

class VextoriaRoblox2016TransformMath : public Object {
	GDCLASS(VextoriaRoblox2016TransformMath, Object);

protected:
	static void _bind_methods();

public:
	static Transform3D from_serialized_rows(const Vector3 &p_position, const Vector3 &p_row0, const Vector3 &p_row1, const Vector3 &p_row2);
	static Transform3D rigid_frame(const Transform3D &p_frame);
	static Transform3D get_world_cframe(Node3D *p_node);
	static void set_world_cframe(Node3D *p_node, const Transform3D &p_cframe);
	static bool has_imported_world_cframe(Node3D *p_node);
	static Transform3D get_imported_world_cframe(Node3D *p_node);
	static void set_imported_world_cframe(Node3D *p_node, const Transform3D &p_cframe);
	static void commit_imported_world_cframe(Node3D *p_node);
	static void copy_imported_world_cframe(Node3D *p_source, Node3D *p_target);
	static Transform3D with_local_size(const Transform3D &p_frame, const Vector3 &p_size);
	static Transform3D solve_weld_part1(const Transform3D &p_part0_world, const Transform3D &p_c0, const Transform3D &p_c1);
	static Transform3D solve_weld_neighbor(const Transform3D &p_known_world, const Transform3D &p_c0, const Transform3D &p_c1, bool p_known_is_part0);
	static Transform3D solve_motor6d_part1(const Transform3D &p_part0_world, const Transform3D &p_c0, const Transform3D &p_transform, const Transform3D &p_c1);
	static Transform3D relative_frame(const Transform3D &p_parent_world, const Transform3D &p_child_world);
	static Basis attachment_set_axis(const Basis &p_basis, const Vector3 &p_axis);
	static Basis attachment_set_secondary_axis(const Basis &p_basis, const Vector3 &p_secondary_axis);
	static Basis attachment_set_axes(const Basis &p_basis, const Vector3 &p_axis, const Vector3 &p_secondary_axis);
	static Basis attachment_basis_from_euler_xyz(const Vector3 &p_euler_radians);
	static Vector3 attachment_euler_xyz(const Basis &p_basis);
};
