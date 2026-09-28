// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/multimesh.h"

class Part;
class Material;

// Admission, slots, invalidations and RenderingServer resources share one
// native lifetime. The gameplay adapter must not maintain parallel slot maps.
class VextoriaPartRenderer : public Node3D {
	GDCLASS(VextoriaPartRenderer, Node3D);

	struct Admission {
		bool allowed = true;
		bool shadows = true;
		bool previously_notified_transform = false;
		String batch;
		int slot = -1;
	};
	struct Batch {
		Ref<MultiMesh> mesh;
		Ref<Material> material;
		RID instance;
		Vector<ObjectID> parts;
	};
	HashMap<ObjectID, Admission> admitted;
	HashMap<String, Batch> batches;
	HashMap<String, Ref<Material>> materials;
	HashSet<ObjectID> dirty;
	Vector<ObjectID> pending;
	int pending_cursor = 0;
	int batched_count = 0;
	int update_limit = 96;
	uint64_t update_budget_usec = 1200;
	Ref<Mesh> fallback_box;

	void property_changed(const StringName &p_property, uint64_t p_id);
	void part_exiting(uint64_t p_id);
	void remove_slot(ObjectID p_id);
	void update_part(ObjectID p_id);
	bool eligible(const Part *p_part, const Admission &p_admission) const;
	Ref<Mesh> resolve_mesh(Part *p_part);
	Ref<Material> resolve_material(Part *p_part, const String &p_key);
	void set_batch_state(Part *p_part, bool p_batched);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void admit_part(Part *p_part, bool p_allowed = true, bool p_shadows = true);
	void remove_part(Part *p_part);
	void set_part_policy(Part *p_part, bool p_allowed, bool p_shadows);
	void invalidate_part(ObjectID p_id);
	void clear_parts();
	int get_admitted_count() const { return admitted.size(); }
	int get_batched_count() const { return batched_count; }
	int get_batch_count() const { return batches.size(); }
	int get_pending_count() const { return dirty.size(); }
	Transform3D get_part_render_transform(Part *p_part) const;
	AABB get_part_render_bounds(Part *p_part) const;
	void set_update_limit(int p_limit);
	int get_update_limit() const { return update_limit; }
	VextoriaPartRenderer();
	~VextoriaPartRenderer();
};
