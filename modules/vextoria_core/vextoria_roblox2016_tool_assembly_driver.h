// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_tool_assembly_driver.h                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         VEXTORIA ENGINE                                */
/**************************************************************************/

#pragma once

#include "core/math/transform_3d.h"
#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "scene/3d/node_3d.h"

#include <cstdint>
#include <vector>

class VextoriaRoblox2016ToolAssemblyDriver : public RefCounted {
	GDCLASS(VextoriaRoblox2016ToolAssemblyDriver, RefCounted);

	std::vector<uint64_t> members;
	std::vector<Transform3D> handle_to_member;
	int last_write_count = 0;

protected:
	static void _bind_methods();

public:
	void configure(const Array &p_members, const Array &p_handle_to_member);
	int configure_graph(Node3D *p_handle, const Array &p_candidates, const Array &p_welds);
	Array get_members() const;
	Transform3D get_member_frame(int p_index) const;
	int apply_target(const Transform3D &p_target_handle_world);
	void clear();
	int get_member_count() const;
	int get_last_write_count() const;
};
