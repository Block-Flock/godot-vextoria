#ifndef VEXTORIA_ROBLOX2016_TOOL_ASSEMBLY_DRIVER_H
#define VEXTORIA_ROBLOX2016_TOOL_ASSEMBLY_DRIVER_H

#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/math/transform_3d.h"
#include "scene/3d/node_3d.h"

#include <cstdint>
#include <vector>


/// Hot-path driver for an equipped Roblox Tool rigid assembly.
///
/// Owns the Handle-connected graph and authored weld-frame solution. Cached
/// members are weak instance IDs: destroying a Tool must not retain its nodes
/// or dereference freed physics bodies. Only graph invalidation rebuilds the
/// adjacency; ordinary grip updates apply the cached native frames.
class Roblox2016ToolAssemblyDriver : public RefCounted {
	GDCLASS(Roblox2016ToolAssemblyDriver, RefCounted)

	std::vector<uint64_t> _members;
	std::vector<Transform3D> _handle_to_member;
	int _last_write_count = 0;

public:
	void configure(const Array &p_members, const Array &p_handle_to_member);
	int configure_graph(Node3D *p_handle, const Array &p_candidates, const Array &p_welds);
	Array get_members() const;
	Transform3D get_member_frame(int p_index) const;
	int apply_target(const Transform3D &p_target_handle_world);
	void clear();
	int get_member_count() const;
	int get_last_write_count() const;

protected:
	static void _bind_methods();
};


#endif
