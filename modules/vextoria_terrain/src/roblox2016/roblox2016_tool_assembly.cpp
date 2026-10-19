#include "roblox2016_tool_assembly.h"
#include "roblox2016_transform_math.h"

#include "core/object/class_db.h"


Transform3D Roblox2016ToolAssembly::solve_grip_handle(
		const Transform3D &p_right_arm_world,
		const Transform3D &p_c0,
		const Transform3D &p_c1) {
	// Roblox's RightGrip weld is:
	//     RightArm * C0 == Handle * C1
	return Roblox2016TransformMath::solve_weld_part1(
		p_right_arm_world,
		p_c0,
		p_c1);
}

Transform3D Roblox2016ToolAssembly::solve_tool_root(
		const Transform3D &p_target_handle_world,
		const Transform3D &p_handle_relative_to_tool) {
	return Roblox2016TransformMath::solve_weld_part1(
		p_target_handle_world,
		Transform3D(),
		p_handle_relative_to_tool);
}

Transform3D Roblox2016ToolAssembly::solve_part_from_handle(
		const Transform3D &p_target_handle_world,
		const Transform3D &p_source_handle_world,
		const Transform3D &p_source_part_world) {
	const Transform3D handle_to_part = Roblox2016TransformMath::relative_frame(
		p_source_handle_world,
		p_source_part_world);
	return Roblox2016TransformMath::rigid_frame(
		p_target_handle_world * handle_to_part);
}

void Roblox2016ToolAssembly::_bind_methods() {
	ClassDB::bind_static_method(
		"Roblox2016ToolAssembly",
		D_METHOD("solve_grip_handle", "right_arm_world", "c0", "c1"),
		&Roblox2016ToolAssembly::solve_grip_handle);
	ClassDB::bind_static_method(
		"Roblox2016ToolAssembly",
		D_METHOD("solve_tool_root", "target_handle_world", "handle_relative_to_tool"),
		&Roblox2016ToolAssembly::solve_tool_root);
	ClassDB::bind_static_method(
		"Roblox2016ToolAssembly",
		D_METHOD("solve_part_from_handle", "target_handle_world", "source_handle_world", "source_part_world"),
		&Roblox2016ToolAssembly::solve_part_from_handle);
}

