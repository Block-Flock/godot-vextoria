// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_tool_assembly.cpp                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         VEXTORIA ENGINE                                */
/**************************************************************************/

#include "vextoria_roblox2016_tool_assembly.h"

#include "core/object/class_db.h"
#include "vextoria_roblox2016_transform_math.h"

Transform3D VextoriaRoblox2016ToolAssembly::solve_grip_handle(const Transform3D &p_right_arm_world, const Transform3D &p_c0, const Transform3D &p_c1) {
	// Roblox RightGrip: RightArm * C0 == Handle * C1.
	return VextoriaRoblox2016TransformMath::solve_weld_part1(p_right_arm_world, p_c0, p_c1);
}

Transform3D VextoriaRoblox2016ToolAssembly::solve_tool_root(const Transform3D &p_target_handle_world, const Transform3D &p_handle_relative_to_tool) {
	return VextoriaRoblox2016TransformMath::solve_weld_part1(p_target_handle_world, Transform3D(), p_handle_relative_to_tool);
}

Transform3D VextoriaRoblox2016ToolAssembly::solve_part_from_handle(const Transform3D &p_target_handle_world, const Transform3D &p_source_handle_world, const Transform3D &p_source_part_world) {
	const Transform3D handle_to_part = VextoriaRoblox2016TransformMath::relative_frame(p_source_handle_world, p_source_part_world);
	return VextoriaRoblox2016TransformMath::rigid_frame(p_target_handle_world * handle_to_part);
}

void VextoriaRoblox2016ToolAssembly::_bind_methods() {
	ClassDB::bind_static_method("VextoriaRoblox2016ToolAssembly", D_METHOD("solve_grip_handle", "right_arm_world", "c0", "c1"), &VextoriaRoblox2016ToolAssembly::solve_grip_handle);
	ClassDB::bind_static_method("VextoriaRoblox2016ToolAssembly", D_METHOD("solve_tool_root", "target_handle_world", "handle_relative_to_tool"), &VextoriaRoblox2016ToolAssembly::solve_tool_root);
	ClassDB::bind_static_method("VextoriaRoblox2016ToolAssembly", D_METHOD("solve_part_from_handle", "target_handle_world", "source_handle_world", "source_part_world"), &VextoriaRoblox2016ToolAssembly::solve_part_from_handle);
}
