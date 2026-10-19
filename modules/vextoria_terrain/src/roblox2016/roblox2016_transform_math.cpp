#include "roblox2016_transform_math.h"

#include "core/object/class_db.h"


Transform3D Roblox2016TransformMath::from_serialized_rows(
		const Vector3 &p_position,
		const Vector3 &p_row0,
		const Vector3 &p_row1,
		const Vector3 &p_row2) {
	// Godot Basis(Vector3, Vector3, Vector3) receives basis columns. Transpose
	// Roblox's serialized rows into those columns exactly once.
	const Basis basis(
		Vector3(p_row0.x, p_row1.x, p_row2.x),
		Vector3(p_row0.y, p_row1.y, p_row2.y),
		Vector3(p_row0.z, p_row1.z, p_row2.z));
	return Transform3D(basis.orthonormalized(), p_position);
}

Transform3D Roblox2016TransformMath::rigid_frame(const Transform3D &p_frame) {
	return Transform3D(p_frame.get_basis().orthonormalized(), p_frame.get_origin());
}

Transform3D Roblox2016TransformMath::solve_weld_part1(
		const Transform3D &p_part0_world,
		const Transform3D &p_c0,
		const Transform3D &p_c1) {
	return rigid_frame(p_part0_world * p_c0 * p_c1.affine_inverse());
}

Transform3D Roblox2016TransformMath::solve_weld_neighbor(
		const Transform3D &p_known_world,
		const Transform3D &p_c0,
		const Transform3D &p_c1,
		bool p_known_is_part0) {
	return p_known_is_part0
		? solve_weld_part1(p_known_world, p_c0, p_c1)
		: rigid_frame(p_known_world * p_c1 * p_c0.affine_inverse());
}

Transform3D Roblox2016TransformMath::solve_motor6d_part1(
		const Transform3D &p_part0_world,
		const Transform3D &p_c0,
		const Transform3D &p_transform,
		const Transform3D &p_c1) {
	return rigid_frame(
		p_part0_world
		* p_c0
		* p_transform
		* p_c1.affine_inverse());
}

Transform3D Roblox2016TransformMath::relative_frame(
		const Transform3D &p_parent_world,
		const Transform3D &p_child_world) {
	return rigid_frame(p_parent_world.affine_inverse() * p_child_world);
}

void Roblox2016TransformMath::_bind_methods() {
	ClassDB::bind_static_method(
		"Roblox2016TransformMath",
		D_METHOD("from_serialized_rows", "position", "row0", "row1", "row2"),
		&Roblox2016TransformMath::from_serialized_rows);
	ClassDB::bind_static_method(
		"Roblox2016TransformMath",
		D_METHOD("rigid_frame", "frame"),
		&Roblox2016TransformMath::rigid_frame);
	ClassDB::bind_static_method(
		"Roblox2016TransformMath",
		D_METHOD("solve_weld_part1", "part0_world", "c0", "c1"),
		&Roblox2016TransformMath::solve_weld_part1);
	ClassDB::bind_static_method(
		"Roblox2016TransformMath",
		D_METHOD("solve_weld_neighbor", "known_world", "c0", "c1", "known_is_part0"),
		&Roblox2016TransformMath::solve_weld_neighbor);
	ClassDB::bind_static_method(
		"Roblox2016TransformMath",
		D_METHOD("solve_motor6d_part1", "part0_world", "c0", "transform", "c1"),
		&Roblox2016TransformMath::solve_motor6d_part1);
	ClassDB::bind_static_method(
		"Roblox2016TransformMath",
		D_METHOD("relative_frame", "parent_world", "child_world"),
		&Roblox2016TransformMath::relative_frame);
}

