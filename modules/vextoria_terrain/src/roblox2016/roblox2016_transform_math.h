#ifndef VEXTORIA_ROBLOX2016_TRANSFORM_MATH_H
#define VEXTORIA_ROBLOX2016_TRANSFORM_MATH_H

#include "core/object/object.h"
#include "core/math/transform_3d.h"
#include "core/math/vector3.h"


// Canonical Roblox-2016 rigid-transform math shared by the standalone test
// harness and the built-in Vextoria Godot module.
//
// Roblox serialized CoordinateFrame matrices are row-major (R00..R22), while
// Godot Basis stores its three axis vectors as columns. Roblox and Godot use
// the same +X right / +Y up / -Z forward convention for these frames, so the
// conversion is a storage-layout transpose only: there is no handedness
// reflection and no axis sign correction.
//
// Joint helpers intentionally compose first and orthonormalize the final frame
// once. CFrame, C0 and C1 are rigid transforms; repeatedly normalizing every
// operand introduces avoidable drift during equip/reparent/save cycles.
class Roblox2016TransformMath : public Object {
	GDCLASS(Roblox2016TransformMath, Object)

public:
	static Transform3D from_serialized_rows(
		const Vector3 &p_position,
		const Vector3 &p_row0,
		const Vector3 &p_row1,
		const Vector3 &p_row2);

	static Transform3D rigid_frame(const Transform3D &p_frame);

	// Classic JointInstance/Weld invariant:
	//     Part0 * C0 == Part1 * C1
	static Transform3D solve_weld_part1(
		const Transform3D &p_part0_world,
		const Transform3D &p_c0,
		const Transform3D &p_c1);

	static Transform3D solve_weld_neighbor(
		const Transform3D &p_known_world,
		const Transform3D &p_c0,
		const Transform3D &p_c1,
		bool p_known_is_part0);

	// Motor6D invariant:
	//     Part0 * C0 * Transform == Part1 * C1
	static Transform3D solve_motor6d_part1(
		const Transform3D &p_part0_world,
		const Transform3D &p_c0,
		const Transform3D &p_transform,
		const Transform3D &p_c1);

	static Transform3D relative_frame(
		const Transform3D &p_parent_world,
		const Transform3D &p_child_world);

protected:
	static void _bind_methods();
};


#endif
