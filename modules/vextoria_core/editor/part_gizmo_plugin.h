// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include "editor/scene/3d/node_3d_editor_gizmos.h"

// Pick the authored Part, never its internal Visual/Collision children. This
// only supplies geometry to Godot's existing viewport selection/transform flow.
class VextoriaPartGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(VextoriaPartGizmoPlugin, EditorNode3DGizmoPlugin);

public:
	bool has_gizmo(Node3D *p_node) override;
	String get_gizmo_name() const override;
	bool can_be_hidden() const override;
	void redraw(EditorNode3DGizmo *p_gizmo) override;
};
