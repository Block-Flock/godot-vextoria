// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_transform_math.cpp                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         VEXTORIA ENGINE                                */
/**************************************************************************/

#include "vextoria_roblox2016_transform_math.h"

#include "core/object/class_db.h"

Transform3D VextoriaRoblox2016TransformMath::from_serialized_rows(const Vector3 &p_position, const Vector3 &p_row0, const Vector3 &p_row1, const Vector3 &p_row2) {
	// Roblox stores the matrix as rows; Godot Basis stores its axis vectors as columns.
	// Both use +X right, +Y up and -Z forward, so only the storage layout changes.
	const Basis basis(
			Vector3(p_row0.x, p_row1.x, p_row2.x),
			Vector3(p_row0.y, p_row1.y, p_row2.y),
			Vector3(p_row0.z, p_row1.z, p_row2.z));
	return Transform3D(basis.orthonormalized(), p_position);
}

Transform3D VextoriaRoblox2016TransformMath::rigid_frame(const Transform3D &p_frame) {
	return Transform3D(p_frame.basis.orthonormalized(), p_frame.origin);
}

Transform3D VextoriaRoblox2016TransformMath::get_world_cframe(Node3D *p_node) {
	ERR_FAIL_NULL_V(p_node, Transform3D());
	return rigid_frame(p_node->get_global_transform());
}

void VextoriaRoblox2016TransformMath::set_world_cframe(Node3D *p_node, const Transform3D &p_cframe) {
	ERR_FAIL_NULL(p_node);
	// Roblox CFrame is a world-space rigid frame. Keep authored Size outside
	// this operation, matching PartInstance's separate CFrame and Size state.
	p_node->set_global_transform(rigid_frame(p_cframe));
}

bool VextoriaRoblox2016TransformMath::has_imported_world_cframe(Node3D *p_node) {
	ERR_FAIL_NULL_V(p_node, false);
	return p_node->has_meta(SNAME("_vextoria_roblox2016_imported_cframe"));
}

Transform3D VextoriaRoblox2016TransformMath::get_imported_world_cframe(Node3D *p_node) {
	ERR_FAIL_NULL_V(p_node, Transform3D());
	ERR_FAIL_COND_V(!has_imported_world_cframe(p_node), Transform3D());
	return p_node->get_meta(SNAME("_vextoria_roblox2016_imported_cframe")).operator Transform3D();
}

void VextoriaRoblox2016TransformMath::set_imported_world_cframe(Node3D *p_node, const Transform3D &p_cframe) {
	ERR_FAIL_NULL(p_node);
	const Transform3D frame = rigid_frame(p_cframe);
	p_node->set_meta(SNAME("_vextoria_roblox2016_imported_cframe"), frame);
	set_world_cframe(p_node, frame);
}

void VextoriaRoblox2016TransformMath::commit_imported_world_cframe(Node3D *p_node) {
	ERR_FAIL_NULL(p_node);
	if (!has_imported_world_cframe(p_node)) {
		return;
	}
	const Transform3D world_frame = rigid_frame(p_node->get_global_transform());
	p_node->set_meta(SNAME("_vextoria_roblox2016_imported_cframe"), world_frame);
}

void VextoriaRoblox2016TransformMath::copy_imported_world_cframe(Node3D *p_source, Node3D *p_target) {
	ERR_FAIL_NULL(p_source);
	ERR_FAIL_NULL(p_target);
	const StringName key = SNAME("_vextoria_roblox2016_imported_cframe");
	if (p_source->has_meta(key)) {
		p_target->set_meta(key, p_source->get_meta(key));
	} else if (p_target->has_meta(key)) {
		p_target->remove_meta(key);
	}
}

Transform3D VextoriaRoblox2016TransformMath::with_local_size(const Transform3D &p_frame, const Vector3 &p_size) {
	return Transform3D(p_frame.basis.orthonormalized().scaled_local(p_size), p_frame.origin);
}

Transform3D VextoriaRoblox2016TransformMath::solve_weld_part1(const Transform3D &p_part0_world, const Transform3D &p_c0, const Transform3D &p_c1) {
	// Classic JointInstance invariant: Part0 * C0 == Part1 * C1.
	return rigid_frame(p_part0_world * p_c0 * p_c1.affine_inverse());
}

Transform3D VextoriaRoblox2016TransformMath::solve_weld_neighbor(const Transform3D &p_known_world, const Transform3D &p_c0, const Transform3D &p_c1, bool p_known_is_part0) {
	return p_known_is_part0
			? solve_weld_part1(p_known_world, p_c0, p_c1)
			: rigid_frame(p_known_world * p_c1 * p_c0.affine_inverse());
}

Transform3D VextoriaRoblox2016TransformMath::solve_motor6d_part1(const Transform3D &p_part0_world, const Transform3D &p_c0, const Transform3D &p_transform, const Transform3D &p_c1) {
	// Motor6D invariant: Part0 * C0 * Transform == Part1 * C1.
	return rigid_frame(p_part0_world * p_c0 * p_transform * p_c1.affine_inverse());
}

Transform3D VextoriaRoblox2016TransformMath::relative_frame(const Transform3D &p_parent_world, const Transform3D &p_child_world) {
	return rigid_frame(p_parent_world.affine_inverse() * p_child_world);
}

void VextoriaRoblox2016TransformMath::_bind_methods() {
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("from_serialized_rows", "position", "row0", "row1", "row2"), &VextoriaRoblox2016TransformMath::from_serialized_rows);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("rigid_frame", "frame"), &VextoriaRoblox2016TransformMath::rigid_frame);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("get_world_cframe", "node"), &VextoriaRoblox2016TransformMath::get_world_cframe);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("set_world_cframe", "node", "cframe"), &VextoriaRoblox2016TransformMath::set_world_cframe);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("has_imported_world_cframe", "node"), &VextoriaRoblox2016TransformMath::has_imported_world_cframe);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("get_imported_world_cframe", "node"), &VextoriaRoblox2016TransformMath::get_imported_world_cframe);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("set_imported_world_cframe", "node", "cframe"), &VextoriaRoblox2016TransformMath::set_imported_world_cframe);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("commit_imported_world_cframe", "node"), &VextoriaRoblox2016TransformMath::commit_imported_world_cframe);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("copy_imported_world_cframe", "source", "target"), &VextoriaRoblox2016TransformMath::copy_imported_world_cframe);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("with_local_size", "frame", "size"), &VextoriaRoblox2016TransformMath::with_local_size);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("solve_weld_part1", "part0_world", "c0", "c1"), &VextoriaRoblox2016TransformMath::solve_weld_part1);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("solve_weld_neighbor", "known_world", "c0", "c1", "known_is_part0"), &VextoriaRoblox2016TransformMath::solve_weld_neighbor);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("solve_motor6d_part1", "part0_world", "c0", "transform", "c1"), &VextoriaRoblox2016TransformMath::solve_motor6d_part1);
	ClassDB::bind_static_method("VextoriaRoblox2016TransformMath", D_METHOD("relative_frame", "parent_world", "child_world"), &VextoriaRoblox2016TransformMath::relative_frame);
}
