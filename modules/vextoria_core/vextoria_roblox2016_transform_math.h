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

class VextoriaRoblox2016TransformMath : public Object {
	GDCLASS(VextoriaRoblox2016TransformMath, Object);

protected:
	static void _bind_methods();

public:
	static Transform3D from_serialized_rows(const Vector3 &p_position, const Vector3 &p_row0, const Vector3 &p_row1, const Vector3 &p_row2);
	static Transform3D rigid_frame(const Transform3D &p_frame);
	static Transform3D with_local_size(const Transform3D &p_frame, const Vector3 &p_size);
	static Transform3D solve_weld_part1(const Transform3D &p_part0_world, const Transform3D &p_c0, const Transform3D &p_c1);
	static Transform3D solve_weld_neighbor(const Transform3D &p_known_world, const Transform3D &p_c0, const Transform3D &p_c1, bool p_known_is_part0);
	static Transform3D solve_motor6d_part1(const Transform3D &p_part0_world, const Transform3D &p_c0, const Transform3D &p_transform, const Transform3D &p_c1);
	static Transform3D relative_frame(const Transform3D &p_parent_world, const Transform3D &p_child_world);
};
