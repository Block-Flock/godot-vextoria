// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
#include "vextoria_part_renderer.h"

#include "vextoria_instance.h"

#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/material.h"
#include "servers/rendering/rendering_server.h"

VextoriaPartRenderer::VextoriaPartRenderer() {
	set_process(true);
}

VextoriaPartRenderer::~VextoriaPartRenderer() {
	clear_parts();
}

bool VextoriaPartRenderer::eligible(const Part *p_part, const Admission &p_admission) const {
	// Arbitrary authored geometry/material overrides retain their exact native
	// standalone renderer. Never silently batch them as a catalog primitive.
	return p_admission.allowed && p_admission.primitive_visible && p_part->is_anchored() && p_part->is_inside_tree() && p_part->is_visible_in_tree() &&
			(!p_part->external_geometry || p_part->registry_shape_mesh.is_valid()) &&
			(p_part->material_registry_owned || p_part->appearance_material.is_null());
}

void VextoriaPartRenderer::set_batch_state(Part *p_part, bool p_batched) {
	const bool changed = p_part->render_batched != p_batched;
	Admission *admission = admitted.getptr(p_part->get_instance_id());
	p_part->render_batched = p_batched;
	p_part->visual->set_visible(!p_batched && (!admission || admission->primitive_visible));
	if (changed || (admission && !admission->state_published)) {
		if (admission) {
			admission->state_published = true;
		}
		p_part->emit_signal(SNAME("vextoria_batch_state_changed"), p_batched);
	}
}

void VextoriaPartRenderer::admit_part(Part *p_part, bool p_allowed, bool p_shadows, bool p_primitive_visible) {
	ERR_FAIL_NULL(p_part);
	ERR_FAIL_COND(!is_inside_tree() || !p_part->is_inside_tree());
	ERR_FAIL_COND_MSG(get_world_3d() != p_part->get_world_3d(), "Native Part renderer cannot admit another viewport's Part.");
	ERR_FAIL_COND_MSG(p_part->render_owner.is_valid() && p_part->render_owner != get_instance_id(), "Part already belongs to another native renderer.");
	ObjectID id = p_part->get_instance_id();
	if (admitted.has(id)) {
		set_part_policy(p_part, p_allowed, p_shadows, p_primitive_visible);
		return;
	}
	Admission admission;
	admission.allowed = p_allowed;
	admission.shadows = p_shadows;
	admission.primitive_visible = p_primitive_visible;
	admission.previously_notified_transform = p_part->is_transform_notification_enabled();
	admitted.insert(id, admission);
	p_part->render_owner = get_instance_id();
	p_part->set_notify_transform(true);
	p_part->connect(SNAME("vextoria_property_changed"), callable_mp(this, &VextoriaPartRenderer::property_changed).bind(uint64_t(id)));
	p_part->connect(SNAME("tree_exiting"), callable_mp(this, &VextoriaPartRenderer::part_exiting).bind(uint64_t(id)));
	queue_part(id);
}

void VextoriaPartRenderer::set_part_policy(Part *p_part, bool p_allowed, bool p_shadows, bool p_primitive_visible) {
	ERR_FAIL_NULL(p_part);
	Admission *admission = admitted.getptr(p_part->get_instance_id());
	ERR_FAIL_NULL(admission);
	if (admission->allowed == p_allowed && admission->shadows == p_shadows && admission->primitive_visible == p_primitive_visible) {
		return;
	}
	admission->allowed = p_allowed;
	admission->shadows = p_shadows;
	admission->primitive_visible = p_primitive_visible;
	// Explicit policy changes must also reach initially standalone Parts.
	queue_part(p_part->get_instance_id());
}

void VextoriaPartRenderer::property_changed(const StringName &p_property, uint64_t p_id) {
	invalidate_part(ObjectID(p_id));
}

void VextoriaPartRenderer::part_exiting(uint64_t p_id) {
	Part *part = ObjectDB::get_instance<Part>(ObjectID(p_id));
	if (part) {
		remove_part(part);
	}
}

void VextoriaPartRenderer::invalidate_part(ObjectID p_id) {
	const Admission *admission = admitted.getptr(p_id);
	Part *part = ObjectDB::get_instance<Part>(p_id);
	if (!admission || !part || (admission->slot < 0 && !eligible(part, *admission))) {
		return;
	}
	queue_part(p_id);
}

void VextoriaPartRenderer::queue_part(ObjectID p_id) {
	if (!dirty.has(p_id)) {
		dirty.insert(p_id);
		pending.push_back(p_id);
	}
}

Ref<Mesh> VextoriaPartRenderer::resolve_mesh(Part *p_part) {
	Ref<Mesh> mesh = p_part->get_unit_render_mesh();
	if (mesh.is_valid()) {
		return mesh;
	}
	// Isolated fork/editor fixtures have no client catalog. Brick still has a
	// shared native unit mesh; other missing shapes stay standalone, never lie.
	if (p_part->shape_kind == 0) {
		if (fallback_box.is_null()) {
			Ref<BoxMesh> box;
			box.instantiate();
			box->set_size(Vector3(1, 1, 1));
			fallback_box = box;
		}
		return fallback_box;
	}
	return Ref<Mesh>();
}

Ref<Material> VextoriaPartRenderer::resolve_material(Part *p_part, const String &p_key) {
	const Ref<Material> *cached = materials.getptr(p_key);
	if (cached) {
		return *cached;
	}
	Ref<Material> material = p_part->get_unit_render_material();
	if (material.is_valid()) {
		materials.insert(p_key, material);
	}
	return material;
}

void VextoriaPartRenderer::remove_slot(ObjectID p_id) {
	Admission *admission = admitted.getptr(p_id);
	if (!admission || admission->slot < 0) {
		return;
	}
	Batch &batch = batches[admission->batch];
	const int slot = admission->slot;
	const int last = batch.parts.size() - 1;
	if (slot != last) {
		ObjectID moved = batch.parts[last];
		batch.parts.write[slot] = moved;
		admitted[moved].slot = slot;
		batch.mesh->set_instance_transform(slot, batch.mesh->get_instance_transform(last));
		batch.mesh->set_instance_color(slot, batch.mesh->get_instance_color(last));
	}
	batch.parts.resize(last);
	batch.mesh->set_visible_instance_count(last);
	RenderingServer::get_singleton()->instance_set_visible(batch.instance, last > 0);
	admission->slot = -1;
	admission->batch = String();
	batched_count--;
}

void VextoriaPartRenderer::remove_part(Part *p_part) {
	ERR_FAIL_NULL(p_part);
	const ObjectID id = p_part->get_instance_id();
	Admission *admission = admitted.getptr(id);
	if (!admission) {
		return;
	}
	const bool notified = admission->previously_notified_transform;
	remove_slot(id);
	dirty.erase(id);
	p_part->disconnect(SNAME("vextoria_property_changed"), callable_mp(this, &VextoriaPartRenderer::property_changed).bind(uint64_t(id)));
	p_part->disconnect(SNAME("tree_exiting"), callable_mp(this, &VextoriaPartRenderer::part_exiting).bind(uint64_t(id)));
	p_part->render_owner = ObjectID();
	p_part->set_notify_transform(notified);
	set_batch_state(p_part, false);
	admitted.erase(id);
}

void VextoriaPartRenderer::update_part(ObjectID p_id) {
	Admission *admission = admitted.getptr(p_id);
	Part *part = ObjectDB::get_instance<Part>(p_id);
	if (!admission || !part) {
		return;
	}
	Ref<Mesh> unit_mesh;
	if (eligible(part, *admission)) {
		unit_mesh = resolve_mesh(part);
	}
	if (unit_mesh.is_null()) {
		remove_slot(p_id);
		set_batch_state(part, false);
		part->visual->set_cast_shadows_setting(admission->shadows ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		return;
	}
	const Transform3D transform = part->get_global_transform() * Transform3D(Basis::from_scale(part->size));
	const AABB bounds = transform.xform(unit_mesh->get_aabb());
	const Vector3 center = transform.origin;
	const Vector3 radius = (bounds.position - center).abs().max((bounds.get_end() - center).abs());
	real_t chunk_size = 64;
	while (radius[radius.max_axis_index()] > chunk_size * 0.5f) {
		chunk_size *= 2;
	}
	const Vector3 coord = (center / chunk_size).floor();
	const String material_key = itos(part->material_kind) + ":" + itos(part->color.a < 0.975f) + ":" + itos(part->normal_maps_enabled);
	const String key = material_key + ":" + itos(uint64_t(unit_mesh->get_instance_id())) + ":" + itos(admission->shadows) + ":" + rtos(chunk_size) + ":" + String(coord);
	if (admission->slot >= 0 && admission->batch != key) {
		remove_slot(p_id);
	}
	Batch *batch = batches.getptr(key);
	if (!batch) {
		Ref<Material> material = resolve_material(part, material_key);
		if (material.is_null()) {
			remove_slot(p_id);
			set_batch_state(part, false);
			return;
		}
		Batch created;
		created.material = material;
		created.mesh.instantiate();
		created.mesh->set_transform_format(MultiMesh::TRANSFORM_3D);
		created.mesh->set_use_colors(true);
		created.mesh->set_mesh(unit_mesh);
		created.mesh->set_instance_count(64);
		created.mesh->set_visible_instance_count(0);
		const Vector3 padding = Vector3(1, 1, 1) * (chunk_size * 0.5f + 4);
		const AABB cull_bounds(coord * chunk_size - padding, Vector3(1, 1, 1) * chunk_size + padding * 2);
		created.mesh->set_custom_aabb(cull_bounds);
		RenderingServer *server = RenderingServer::get_singleton();
		created.instance = server->instance_create();
		server->instance_set_scenario(created.instance, get_world_3d()->get_scenario());
		server->instance_set_base(created.instance, created.mesh->get_rid());
		server->instance_set_transform(created.instance, Transform3D());
		server->instance_set_custom_aabb(created.instance, cull_bounds);
		server->instance_geometry_set_material_override(created.instance, material->get_rid());
		server->instance_geometry_set_cast_shadows_setting(created.instance, admission->shadows ? RSE::SHADOW_CASTING_SETTING_ON : RSE::SHADOW_CASTING_SETTING_OFF);
		batches.insert(key, created);
		batch = batches.getptr(key);
	}
	if (admission->slot < 0) {
		const int slot = batch->parts.size();
		if (slot == batch->mesh->get_instance_count()) {
			Vector<Transform3D> transforms;
			Vector<Color> colors;
			for (int i = 0; i < slot; i++) {
				transforms.push_back(batch->mesh->get_instance_transform(i));
				colors.push_back(batch->mesh->get_instance_color(i));
			}
			batch->mesh->set_instance_count(slot * 2);
			for (int i = 0; i < slot; i++) {
				batch->mesh->set_instance_transform(i, transforms[i]);
				batch->mesh->set_instance_color(i, colors[i]);
			}
		}
		batch->parts.push_back(p_id);
		admission->slot = slot;
		admission->batch = key;
		batched_count++;
		batch->mesh->set_visible_instance_count(batch->parts.size());
		RenderingServer::get_singleton()->instance_set_visible(batch->instance, true);
	}
	batch->mesh->set_instance_transform(admission->slot, transform);
	batch->mesh->set_instance_color(admission->slot, part->color.srgb_to_linear());
	set_batch_state(part, true);
}

void VextoriaPartRenderer::clear_parts() {
	Vector<ObjectID> ids;
	for (const KeyValue<ObjectID, Admission> &entry : admitted) {
		ids.push_back(entry.key);
	}
	for (ObjectID id : ids) {
		Part *part = ObjectDB::get_instance<Part>(id);
		if (part) {
			remove_part(part);
		}
	}
	admitted.clear();
	for (const KeyValue<String, Batch> &entry : batches) {
		RenderingServer::get_singleton()->free_rid(entry.value.instance);
	}
	batches.clear();
	materials.clear();
	fallback_box.unref();
	dirty.clear();
	pending.clear();
	pending_cursor = 0;
	batched_count = 0;
}

void VextoriaPartRenderer::set_update_limit(int p_limit) {
	ERR_FAIL_COND(p_limit < 1);
	update_limit = p_limit;
}

Transform3D VextoriaPartRenderer::get_part_render_transform(Part *p_part) const {
	ERR_FAIL_NULL_V(p_part, Transform3D());
	const Admission *admission = admitted.getptr(p_part->get_instance_id());
	ERR_FAIL_COND_V(!admission || admission->slot < 0, Transform3D());
	return batches[admission->batch].mesh->get_instance_transform(admission->slot);
}

AABB VextoriaPartRenderer::get_part_render_bounds(Part *p_part) const {
	ERR_FAIL_NULL_V(p_part, AABB());
	const Admission *admission = admitted.getptr(p_part->get_instance_id());
	ERR_FAIL_COND_V(!admission || admission->slot < 0, AABB());
	return batches[admission->batch].mesh->get_custom_aabb();
}

void VextoriaPartRenderer::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		clear_parts();
	} else if (p_what == NOTIFICATION_PROCESS) {
		const uint64_t started = OS::get_singleton()->get_ticks_usec();
		int processed = 0;
		while (pending_cursor < pending.size() && processed < update_limit) {
			ObjectID id = pending[pending_cursor++];
			if (dirty.erase(id)) {
				update_part(id);
				processed++;
			}
			if (OS::get_singleton()->get_ticks_usec() - started >= update_budget_usec) {
				break;
			}
		}
		if (pending_cursor == pending.size()) {
			pending.clear();
			pending_cursor = 0;
		}
	}
}

void VextoriaPartRenderer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("admit_part", "part", "allowed", "shadows", "primitive_visible"), &VextoriaPartRenderer::admit_part, DEFVAL(true), DEFVAL(true), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("remove_part", "part"), &VextoriaPartRenderer::remove_part);
	ClassDB::bind_method(D_METHOD("set_part_policy", "part", "allowed", "shadows", "primitive_visible"), &VextoriaPartRenderer::set_part_policy, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("clear_parts"), &VextoriaPartRenderer::clear_parts);
	ClassDB::bind_method(D_METHOD("get_admitted_count"), &VextoriaPartRenderer::get_admitted_count);
	ClassDB::bind_method(D_METHOD("get_batched_count"), &VextoriaPartRenderer::get_batched_count);
	ClassDB::bind_method(D_METHOD("get_batch_count"), &VextoriaPartRenderer::get_batch_count);
	ClassDB::bind_method(D_METHOD("get_pending_count"), &VextoriaPartRenderer::get_pending_count);
	ClassDB::bind_method(D_METHOD("get_part_render_transform", "part"), &VextoriaPartRenderer::get_part_render_transform);
	ClassDB::bind_method(D_METHOD("get_part_render_bounds", "part"), &VextoriaPartRenderer::get_part_render_bounds);
	ClassDB::bind_method(D_METHOD("set_update_limit", "limit"), &VextoriaPartRenderer::set_update_limit);
	ClassDB::bind_method(D_METHOD("get_update_limit"), &VextoriaPartRenderer::get_update_limit);
}
