// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#include "part_gizmo_plugin.h"

#include "../vextoria_instance.h"

#include "core/math/triangle_mesh.h"
#include "scene/3d/mesh_instance_3d.h"

bool VextoriaPartGizmoPlugin::has_gizmo(Node3D *p_node) {
	return Object::cast_to<Part>(p_node) != nullptr;
}

String VextoriaPartGizmoPlugin::get_gizmo_name() const {
	return "VextoriaPart";
}

bool VextoriaPartGizmoPlugin::can_be_hidden() const {
	return false;
}

void VextoriaPartGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	Part *part = Object::cast_to<Part>(p_gizmo->get_node_3d());
	ERR_FAIL_NULL(part);
	MeshInstance3D *visual = Object::cast_to<MeshInstance3D>(part->get_node_or_null(NodePath("Visual")));
	if (!visual || !visual->is_visible() || visual->get_mesh().is_null()) {
		return;
	}
	Ref<TriangleMesh> triangles = visual->get_mesh()->generate_triangle_mesh();
	if (triangles.is_null()) {
		return;
	}
	// Sized Brick/Wedge meshes already use Part-local coordinates. Authored unit
	// resources use the internal visual's size transform. Bake only that frame,
	// leaving Part's world transform to Godot's gizmo BVH/intersection code.
	const Transform3D local_frame = visual->get_transform();
	if (local_frame != Transform3D()) {
		const Vector<Face3> source = triangles->get_faces();
		Vector<Vector3> vertices;
		vertices.resize(source.size() * 3);
		for (int face = 0; face < source.size(); face++) {
			for (int corner = 0; corner < 3; corner++) {
				vertices.write[face * 3 + corner] = local_frame.xform(source[face].vertex[corner]);
			}
		}
		triangles.instantiate();
		triangles->create(vertices);
	}
	p_gizmo->add_collision_triangles(triangles);
}
