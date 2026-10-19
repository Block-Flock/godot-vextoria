#ifndef VEXTORIA_ROBLOX2016_TOOL_ASSEMBLY_H
#define VEXTORIA_ROBLOX2016_TOOL_ASSEMBLY_H

#include "core/object/object.h"
#include "core/math/transform_3d.h"


// Native Roblox 2016 Tool::upTo_Equipped transform equations. The managed
// datamodel still owns Instance/network lifecycle; this class keeps the frame
// math identical in the dynamic extension and built-in Godot-fork paths.
class Roblox2016ToolAssembly : public Object {
	GDCLASS(Roblox2016ToolAssembly, Object)

public:
	static Transform3D solve_grip_handle(
		const Transform3D &p_right_arm_world,
		const Transform3D &p_c0,
		const Transform3D &p_c1);
	static Transform3D solve_tool_root(
		const Transform3D &p_target_handle_world,
		const Transform3D &p_handle_relative_to_tool);
	static Transform3D solve_part_from_handle(
		const Transform3D &p_target_handle_world,
		const Transform3D &p_source_handle_world,
		const Transform3D &p_source_part_world);

protected:
	static void _bind_methods();

};


#endif
