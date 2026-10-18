// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_tool_assembly.h                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         VEXTORIA ENGINE                                */
/**************************************************************************/

#pragma once

#include "core/math/transform_3d.h"
#include "core/object/object.h"

class VextoriaRoblox2016ToolAssembly : public Object {
	GDCLASS(VextoriaRoblox2016ToolAssembly, Object);

protected:
	static void _bind_methods();

public:
	static Transform3D solve_grip_handle(const Transform3D &p_right_arm_world, const Transform3D &p_c0, const Transform3D &p_c1);
	static Transform3D solve_tool_root(const Transform3D &p_target_handle_world, const Transform3D &p_handle_relative_to_tool);
	static Transform3D solve_part_from_handle(const Transform3D &p_target_handle_world, const Transform3D &p_source_handle_world, const Transform3D &p_source_part_world);
};
