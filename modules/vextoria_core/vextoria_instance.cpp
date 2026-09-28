/**************************************************************************/
/*  vextoria_instance.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "vextoria_instance.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/callable_mp.h"
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "scene/resources/surface_tool.h"

VextoriaInstance::VextoriaInstance() {
	// Roblox Folder/Script have no CFrame, but spatial descendants must still
	// inherit the nearest Model/Part frame. Node3D honors this opt-in marker.
	set_meta("_vextoria_spatial_passthrough", true);
}

static void collect_passthrough_spatial_roots(Node *p_node, LocalVector<Node3D *> &r_roots) {
	for (int i = 0; i < p_node->get_child_count(false); ++i) {
		Node *child = p_node->get_child(i, false);
		if (Node3D *spatial = Object::cast_to<Node3D>(child)) {
			r_roots.push_back(spatial);
		} else if (child->has_meta("_vextoria_spatial_passthrough")) {
			collect_passthrough_spatial_roots(child, r_roots);
		}
	}
}

void VextoriaInstance::reparent(RequiredParam<Node> p_parent, bool p_keep_global_transform) {
	if (!p_keep_global_transform) {
		Node::reparent(p_parent, false);
		return;
	}

	// A Folder/Script has no own transform for Node::reparent to preserve.
	// Preserve the world frames of its first spatial descendants instead; all
	// deeper Node3Ds continue to inherit from those roots normally.
	LocalVector<Node3D *> spatial_roots;
	collect_passthrough_spatial_roots(this, spatial_roots);
	LocalVector<Transform3D> world_frames;
	world_frames.reserve(spatial_roots.size());
	for (Node3D *spatial : spatial_roots) {
		world_frames.push_back(spatial->get_global_transform());
	}
	Node::reparent(p_parent, true);
	for (uint32_t i = 0; i < spatial_roots.size(); ++i) {
		spatial_roots[i]->set_global_transform(world_frames[i]);
	}
}

bool VextoriaInstance::is_locked() const {
	return has_meta("_edit_lock_");
}

void VextoriaInstance::set_locked(bool p_locked) {
	if (p_locked) {
		set_meta("_edit_lock_", true);
	} else if (has_meta("_edit_lock_")) {
		remove_meta("_edit_lock_");
	}
}

void VextoriaInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_archivable"), &VextoriaInstance::is_archivable);
	ClassDB::bind_method(D_METHOD("set_archivable", "archivable"), &VextoriaInstance::set_archivable);
	ClassDB::bind_method(D_METHOD("is_locked"), &VextoriaInstance::is_locked);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &VextoriaInstance::set_locked);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Archivable"), "set_archivable", "is_archivable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Locked"), "set_locked", "is_locked");
}

bool Model::is_locked() const {
	return has_meta("_edit_lock_");
}

void Model::set_locked(bool p_locked) {
	if (p_locked) {
		set_meta("_edit_lock_", true);
	} else if (has_meta("_edit_lock_")) {
		remove_meta("_edit_lock_");
	}
}

void Model::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_archivable"), &Model::is_archivable);
	ClassDB::bind_method(D_METHOD("set_archivable", "archivable"), &Model::set_archivable);
	ClassDB::bind_method(D_METHOD("is_locked"), &Model::is_locked);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &Model::set_locked);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Archivable"), "set_archivable", "is_archivable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Locked"), "set_locked", "is_locked");
}

Part::Part() {
	box_mesh.instantiate();
	box_shape.instantiate();
	material.instantiate();
	box_mesh->set_size(size);
	box_shape->set_size(size);
	material->set_albedo(color);

	visual = memnew(MeshInstance3D);
	visual->set_name("Visual");
	visual->set_mesh(box_mesh);
	visual->set_material_override(material);
	add_child(visual, false, INTERNAL_MODE_BACK);

	collision = memnew(CollisionShape3D);
	collision->set_name("Collision");
	collision->set_shape(box_shape);
	add_child(collision, false, INTERNAL_MODE_BACK);

	// Non-persistent internal group used by runtime rendering settings to reach
	// native Parts without maintaining a second managed ownership registry.
	add_to_group(SNAME("_vextoria_native_parts"));
	normal_maps_enabled = ProjectSettings::get_singleton()->get_setting(
			"vextoria/rendering/normal_maps_enabled", true);
	try_resolve_material_asset(material_kind);
}

bool Part::is_locked() const {
	return has_meta("_edit_lock_");
}

Part::~Part() {
	clear_registry_shape_binding();
	if (appearance_material.is_valid()) {
		appearance_material->disconnect_changed(callable_mp(this, &Part::appearance_material_changed));
	}
}

void Part::set_locked(bool p_locked) {
	if (p_locked) {
		set_meta("_edit_lock_", true);
	} else if (has_meta("_edit_lock_")) {
		remove_meta("_edit_lock_");
	}
}

void Part::set_size(const Vector3 &p_size) {
	ERR_FAIL_COND_MSG(p_size.x <= 0 || p_size.y <= 0 || p_size.z <= 0, "Part.Size must be positive on every axis.");
	if (size == p_size) {
		return;
	}
	size = p_size;
	box_mesh->set_size(size);
	box_shape->set_size(size);
	if (external_geometry) {
		visual->set_scale(size);
		collision->set_scale(size);
	} else if (builtin_wedge) {
		rebuild_wedge_geometry();
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Size"));
}

void Part::set_color(const Color &p_color) {
	if (color == p_color) {
		return;
	}
	const bool old_opaque = material_registry_opaque;
	color = p_color;
	const bool new_opaque = color.a >= 0.975f;
	if (material_registry_owned && old_opaque != new_opaque) {
		if (!try_resolve_material_asset(material_kind)) {
			update_appearance_color();
		}
	} else {
		update_appearance_color();
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Color"));
}

static const char *vextoria_part_shape_names[] = {
	"Brick", "Sphere", "Cylinder", "Cone", "Wedge", "Corner", "Bevel", "Concave",
	"Truss", "Frame", "Octant", "Torus", "BeveledCorner", "ConcaveCorner",
	"TriangleCorner", "TriangleConcaveCorner"
};

namespace {
struct PartShapeCacheEntry {
	Ref<Mesh> mesh;
	Ref<Shape3D> collision;
	Callable invalidated;
	uint64_t revision = 0;
};

struct PartShapeCache {
	Mutex mutex;
	PartShapeCacheEntry entries[16];
};

// Bounded by the authored shape registry, not by the number of world Parts.
// Released at SCENE teardown while RenderingServer/PhysicsServer are alive.
PartShapeCache *part_shape_cache = nullptr;

void invalidate_part_shape_cache(int p_kind) {
	if (!part_shape_cache) {
		return;
	}
	MutexLock lock(part_shape_cache->mutex);
	PartShapeCacheEntry &entry = part_shape_cache->entries[p_kind];
	entry.collision.unref();
	entry.revision++;
}
} // namespace

void Part::clear_shape_asset_cache() {
	if (!part_shape_cache) {
		return;
	}
	for (PartShapeCacheEntry &entry : part_shape_cache->entries) {
		if (entry.mesh.is_valid()) {
			entry.mesh->disconnect_changed(entry.invalidated);
		}
	}
	memdelete(part_shape_cache);
	part_shape_cache = nullptr;
}

void Part::clear_registry_shape_binding() {
	if (registry_shape_mesh.is_valid()) {
		registry_shape_mesh->disconnect_changed(callable_mp(this, &Part::queue_registry_shape_refresh));
		registry_shape_mesh.unref();
	}
}

void Part::queue_registry_shape_refresh() {
	if (!registry_shape_refresh_pending.exchange(true)) {
		callable_mp(this, &Part::refresh_registry_shape).call_deferred();
	}
}

void Part::refresh_registry_shape() {
	registry_shape_refresh_pending.store(false);
	if (!external_geometry || registry_shape_mesh.is_null()) {
		return;
	}
	Ref<Shape3D> previous_collision = geometry_collision;
	if (try_resolve_shape_assets(shape_kind) && previous_collision != geometry_collision) {
		apply_external_geometry();
		emit_signal(SNAME("vextoria_property_changed"), SNAME("GeometryCollision"));
	}
}

bool Part::try_resolve_shape_assets(int p_kind) {
	if (p_kind <= 0 || p_kind == 4 || p_kind >= 16) {
		return false;
	}

	const String shape_name = vextoria_part_shape_names[p_kind];
	const String mesh_path = "res://resources/shapes/meshes/" + shape_name + ".tres";
	if (!ResourceLoader::exists(mesh_path)) {
		return false;
	}

	// Authored unit meshes are immutable here; Part size lives on the internal
	// nodes. Reuse them rather than reparsing the same asset for every Part.
	Ref<Mesh> resolved_mesh = ResourceLoader::load(mesh_path, "", ResourceLoader::CACHE_MODE_REUSE);
	if (resolved_mesh.is_null()) {
		return false;
	}
	if (!part_shape_cache) {
		part_shape_cache = memnew(PartShapeCache);
	}
	Ref<Shape3D> resolved_collision;
	for (int attempt = 0; attempt < 3; attempt++) {
		uint64_t revision;
		{
			MutexLock lock(part_shape_cache->mutex);
			PartShapeCacheEntry &entry = part_shape_cache->entries[p_kind];
			if (entry.mesh != resolved_mesh) {
				if (entry.mesh.is_valid()) {
					entry.mesh->disconnect_changed(entry.invalidated);
				}
				entry.mesh = resolved_mesh;
				entry.collision.unref();
				entry.invalidated = callable_mp_static(&invalidate_part_shape_cache).bind(p_kind);
				entry.mesh->connect_changed(entry.invalidated);
				entry.revision++;
			}
			if (entry.collision.is_valid()) {
				resolved_collision = entry.collision;
				break;
			}
			revision = entry.revision;
		}
		// Primitive meshes can emit changed while lazily generating their arrays.
		// Build outside the lock and only publish a collider for a stable revision.
		if (p_kind == 8 || p_kind == 9) {
			Ref<BoxShape3D> box;
			box.instantiate();
			box->set_size(Vector3(1, 1, 1));
			resolved_collision = box;
		} else if (Object::cast_to<ArrayMesh>(resolved_mesh.ptr()) != nullptr) {
			resolved_collision = resolved_mesh->create_trimesh_shape();
		} else {
			resolved_collision = resolved_mesh->create_convex_shape();
		}
		if (resolved_collision.is_null()) {
			return false;
		}
		MutexLock lock(part_shape_cache->mutex);
		PartShapeCacheEntry &entry = part_shape_cache->entries[p_kind];
		if (entry.revision == revision) {
			entry.collision = resolved_collision;
			// CollisionShape3D changes a Shape's debug appearance on attachment.
			// Only source mesh revisions invalidate this derived resource cache.
			break;
		}
		resolved_collision.unref();
	}
	if (resolved_collision.is_null()) {
		return false;
	}

	geometry_mesh = resolved_mesh;
	geometry_collision = resolved_collision;
	if (registry_shape_mesh != resolved_mesh) {
		clear_registry_shape_binding();
		registry_shape_mesh = resolved_mesh;
		registry_shape_mesh->connect_changed(callable_mp(this, &Part::queue_registry_shape_refresh));
	}
	return true;
}

void Part::set_shape_kind(int p_kind) {
	ERR_FAIL_COND_MSG(p_kind < 0 || p_kind > 15, "Part.Shape is outside the supported authored shape range.");
	if (shape_kind == p_kind) {
		return;
	}
	shape_kind = p_kind;
	if (shape_kind == 0) {
		reset_builtin_geometry();
	} else if (shape_kind == 4) {
		set_builtin_wedge_geometry();
	} else {
		builtin_wedge = false;
		external_geometry = true;
		// The native Part resolves Vextoria's authored shape registry directly.
		// Isolated engine tests intentionally have no client asset pack, so retain
		// the explicit GeometryMesh/GeometryCollision fallback for those fixtures
		// and for future custom-resource shapes.
		try_resolve_shape_assets(shape_kind);
		if (geometry_mesh.is_valid() && geometry_collision.is_valid()) {
			apply_external_geometry();
		}
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Shape"));
}

static const char *vextoria_part_material_names[] = {
	"SmoothPlastic", "Brick", "Concrete", "Dirt", "Fabric", "Grass", "Ice", "Marble",
	"Metal", "MetalGrid", "MetalPlate", "Neon", "Planks", "Plastic", "Plywood",
	"RustyIron", "Sand", "Sandstone", "Snow", "Stone", "Wood"
};

bool Part::try_resolve_material_asset(int p_kind) {
	if (p_kind < 0 || p_kind >= 21) {
		return false;
	}

	const String material_name = vextoria_part_material_names[p_kind];
	const String material_path = "res://resources/materials/parts/" + material_name + ".tres";
	if (!ResourceLoader::exists(material_path)) {
		return false;
	}

	// Keep mutable material parameters private, but let ResourceLoader reuse
	// external shader/texture dependencies. IGNORE_DEEP reloaded those heavy
	// dependencies too, once per Part and again at every alpha transition.
	Ref<Material> resolved_material = ResourceLoader::load(material_path, "", ResourceLoader::CACHE_MODE_IGNORE);
	if (resolved_material.is_null()) {
		return false;
	}

	const bool opaque = color.a >= 0.975f;
	Ref<ShaderMaterial> shader_material = resolved_material;
	if (shader_material.is_valid()) {
		if (!opaque) {
			Ref<Shader> source_shader = shader_material->get_shader();
			if (source_shader.is_valid() && source_shader->get_path().ends_with("part.gdshader")) {
				Ref<Shader> transparent_shader = ResourceLoader::load(
						"res://resources/shaders/part/part_transparent.gdshader",
						"Shader", ResourceLoader::CACHE_MODE_REUSE);
				if (transparent_shader.is_valid()) {
					shader_material->set_shader(transparent_shader);
				}
			}
		}
		shader_material->set_shader_parameter(SNAME("use_normal_texture"), normal_maps_enabled);
	}

	set_appearance_material(resolved_material);
	material_registry_owned = true;
	material_registry_opaque = opaque;
	return true;
}

void Part::set_material_kind(int p_kind) {
	ERR_FAIL_COND_MSG(p_kind < 0 || p_kind > 20, "Part.Material is outside the supported authored material range.");
	if (material_kind == p_kind) {
		// A default-valued Material setter can still be the first opportunity to
		// resolve the client asset pack after an isolated/native construction.
		if (!material_registry_owned) {
			try_resolve_material_asset(p_kind);
		}
		return;
	}
	material_kind = p_kind;
	try_resolve_material_asset(material_kind);
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Material"));
}

void Part::set_normal_maps_enabled(bool p_enabled) {
	if (normal_maps_enabled == p_enabled) {
		return;
	}
	normal_maps_enabled = p_enabled;
	if (!material_registry_owned) {
		return;
	}
	Ref<ShaderMaterial> shader_material = appearance_material;
	if (shader_material.is_valid()) {
		shader_material->set_shader_parameter(SNAME("use_normal_texture"), normal_maps_enabled);
		appearance_material_changed();
	}
}

void Part::update_appearance_color() {
	material->set_albedo(color);
	if (BaseMaterial3D *base_material = Object::cast_to<BaseMaterial3D>(applied_appearance.ptr())) {
		base_material->set_albedo(color);
	} else if (appearance_uses_color_uniform) {
		Object::cast_to<ShaderMaterial>(applied_appearance.ptr())->set_shader_parameter(SNAME("color"), color);
	}
}

Ref<Material> Part::get_appearance_material() const {
	return appearance_material;
}

void Part::_validate_property(PropertyInfo &p_property) const {
	if (registry_shape_mesh.is_valid() &&
			(p_property.name == SNAME("GeometryMesh") || p_property.name == SNAME("GeometryCollision"))) {
		// These resources are derived from Shape. Saving them as overrides would
		// detach the scene instance from registry revision updates after reload.
		p_property.usage &= ~PROPERTY_USAGE_STORAGE;
	}
	if (p_property.name == SNAME("AppearanceMaterial") && material_registry_owned) {
		// Registry appearance is derived from Material/Color/settings. Persisting
		// it as an authored override would disable that derivation on duplicate
		// or scene reload. Explicit custom resources retain normal Godot storage.
		p_property.usage &= ~PROPERTY_USAGE_STORAGE;
	}
}

void Part::set_appearance_material(const Ref<Material> &p_material) {
	if (appearance_material == p_material) {
		return;
	}
	// An explicit authored resource overrides registry selection. Only the
	// registry resolver may opt back in, after installing its private source.
	// Otherwise a later alpha edit silently replaces an Inspector assignment.
	material_registry_owned = false;
	if (appearance_material.is_valid()) {
		appearance_material->disconnect_changed(callable_mp(this, &Part::appearance_material_changed));
	}
	appearance_material = p_material;
	if (appearance_material.is_valid()) {
		appearance_material->connect_changed(callable_mp(this, &Part::appearance_material_changed));
	}
	appearance_material_changed();
	emit_signal(SNAME("vextoria_property_changed"), SNAME("AppearanceMaterial"));
}

void Part::appearance_material_changed() {
	appearance_uses_color_uniform = false;
	if (appearance_material.is_null()) {
		applied_appearance.unref();
		visual->set_material_override(material);
		return;
	}
	// Tint belongs to the Part, never to a shared authored Material asset.
	applied_appearance = appearance_material->duplicate();
	ERR_FAIL_COND_MSG(applied_appearance.is_null(), "Part.AppearanceMaterial could not be instanced.");
	if (ShaderMaterial *shader_material = Object::cast_to<ShaderMaterial>(applied_appearance.ptr())) {
		Ref<Shader> shader = shader_material->get_shader();
		if (shader.is_valid()) {
			List<PropertyInfo> uniforms;
			shader->get_shader_uniform_list(&uniforms);
			for (const PropertyInfo &uniform : uniforms) {
				if (uniform.name == SNAME("color") && uniform.type == Variant::COLOR) {
					appearance_uses_color_uniform = true;
					break;
				}
			}
		}
	}
	update_appearance_color();
	visual->set_material_override(applied_appearance);
}

void Part::set_anchored(bool p_anchored) {
	if (anchored == p_anchored) {
		return;
	}
	anchored = p_anchored;
	set_freeze_enabled(anchored);
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Anchored"));
}

void Part::set_can_collide(bool p_can_collide) {
	if (can_collide == p_can_collide) {
		return;
	}
	can_collide = p_can_collide;
	// Keep the shape installed so toggling CanCollide does not rebuild the body
	// or invalidate joints. A zero layer alone is insufficient: the body's
	// mask could still request contacts with another object's layer.
	collision->set_disabled(!can_collide);
	emit_signal(SNAME("vextoria_property_changed"), SNAME("CanCollide"));
}

void Part::reset_builtin_geometry() {
	clear_registry_shape_binding();
	external_geometry = false;
	builtin_wedge = false;
	visual->set_scale(Vector3(1, 1, 1));
	collision->set_scale(Vector3(1, 1, 1));
	visual->set_mesh(box_mesh);
	collision->set_shape(box_shape);
}

void Part::rebuild_wedge_geometry() {
	const Vector3 half = size * 0.5f;
	const Vector3 vertices[6] = {
		Vector3(half.x, half.y, half.z), Vector3(half.x, -half.y, half.z),
		Vector3(half.x, -half.y, -half.z), Vector3(-half.x, half.y, half.z),
		Vector3(-half.x, -half.y, half.z), Vector3(-half.x, -half.y, -half.z)
	};
	// The authored five faces are a sloped top, two triangles on each of the
	// rectangular sides, two end triangles, and a rectangular bottom. Godot's
	// front winding is opposite the outward cross-product used for lighting.
	static constexpr int triangles[8][3] = {
		{ 0, 3, 4 }, { 0, 4, 1 }, { 3, 0, 2 }, { 3, 2, 5 },
		{ 0, 1, 2 }, { 3, 5, 4 }, { 5, 2, 1 }, { 5, 1, 4 }
	};
	Ref<SurfaceTool> surface;
	surface.instantiate();
	surface->begin(Mesh::PRIMITIVE_TRIANGLES);
	for (const auto &triangle : triangles) {
		const Vector3 &a = vertices[triangle[0]];
		const Vector3 &b = vertices[triangle[1]];
		const Vector3 &c = vertices[triangle[2]];
		Vector3 normal = (b - a).cross(c - a).normalized();
		surface->set_normal(normal);
		surface->set_uv(Vector2(0, 0));
		surface->add_vertex(a);
		surface->set_uv(Vector2(1, 0));
		surface->add_vertex(c);
		surface->set_uv(Vector2(0, 1));
		surface->add_vertex(b);
	}
	wedge_mesh = surface->commit();
	Vector<Vector3> hull_points;
	for (const Vector3 &vertex : vertices) {
		hull_points.push_back(vertex);
	}
	wedge_shape.instantiate();
	wedge_shape->set_points(hull_points);
	visual->set_mesh(wedge_mesh);
	collision->set_shape(wedge_shape);
}

void Part::set_builtin_wedge_geometry() {
	clear_registry_shape_binding();
	external_geometry = false;
	visual->set_scale(Vector3(1, 1, 1));
	collision->set_scale(Vector3(1, 1, 1));
	if (builtin_wedge) {
		visual->set_mesh(wedge_mesh);
		collision->set_shape(wedge_shape);
		return;
	}
	builtin_wedge = true;
	rebuild_wedge_geometry();
}

void Part::set_builtin_geometry_kind(int p_kind) {
	ERR_FAIL_COND_MSG(p_kind < 0 || p_kind > 2, "Part.Geometry must be Brick, Wedge or Resource.");
	if (p_kind == 2) {
		builtin_wedge = false;
		external_geometry = true;
		apply_external_geometry();
	} else if (p_kind == 1) {
		set_builtin_wedge_geometry();
		if (shape_kind != 4) {
			shape_kind = 4;
			emit_signal(SNAME("vextoria_property_changed"), SNAME("Shape"));
		}
	} else {
		reset_builtin_geometry();
		if (shape_kind != 0) {
			shape_kind = 0;
			emit_signal(SNAME("vextoria_property_changed"), SNAME("Shape"));
		}
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("Geometry"));
}

void Part::apply_external_geometry() {
	visual->set_mesh(geometry_mesh);
	collision->set_shape(geometry_collision);
	visual->set_scale(size);
	collision->set_scale(size);
}

void Part::set_geometry_mesh(const Ref<Mesh> &p_mesh) {
	if (geometry_mesh == p_mesh) {
		return;
	}
	clear_registry_shape_binding();
	geometry_mesh = p_mesh;
	if (external_geometry) {
		visual->set_mesh(geometry_mesh);
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("GeometryMesh"));
}

Ref<Mesh> Part::get_geometry_mesh() const {
	return geometry_mesh;
}

void Part::set_geometry_collision(const Ref<Shape3D> &p_shape) {
	if (geometry_collision == p_shape) {
		return;
	}
	clear_registry_shape_binding();
	geometry_collision = p_shape;
	if (external_geometry) {
		collision->set_shape(geometry_collision);
	}
	emit_signal(SNAME("vextoria_property_changed"), SNAME("GeometryCollision"));
}

Ref<Shape3D> Part::get_geometry_collision() const {
	return geometry_collision;
}

void Part::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_size"), &Part::get_size);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Part::set_size);
	ClassDB::bind_method(D_METHOD("get_color"), &Part::get_color);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &Part::set_color);
	ClassDB::bind_method(D_METHOD("get_shape_kind"), &Part::get_shape_kind);
	ClassDB::bind_method(D_METHOD("set_shape_kind", "shape"), &Part::set_shape_kind);
	ClassDB::bind_method(D_METHOD("get_material_kind"), &Part::get_material_kind);
	ClassDB::bind_method(D_METHOD("set_material_kind", "material"), &Part::set_material_kind);
	ClassDB::bind_method(D_METHOD("set_normal_maps_enabled", "enabled"), &Part::set_normal_maps_enabled);
	ClassDB::bind_method(D_METHOD("is_archivable"), &Part::is_archivable);
	ClassDB::bind_method(D_METHOD("set_archivable", "archivable"), &Part::set_archivable);
	ClassDB::bind_method(D_METHOD("is_locked"), &Part::is_locked);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &Part::set_locked);
	ClassDB::bind_method(D_METHOD("is_anchored"), &Part::is_anchored);
	ClassDB::bind_method(D_METHOD("set_anchored", "anchored"), &Part::set_anchored);
	ClassDB::bind_method(D_METHOD("get_can_collide"), &Part::get_can_collide);
	ClassDB::bind_method(D_METHOD("set_can_collide", "can_collide"), &Part::set_can_collide);
	ClassDB::bind_method(D_METHOD("reset_builtin_geometry"), &Part::reset_builtin_geometry);
	ClassDB::bind_method(D_METHOD("set_builtin_wedge_geometry"), &Part::set_builtin_wedge_geometry);
	ClassDB::bind_method(D_METHOD("get_builtin_geometry_kind"), &Part::get_builtin_geometry_kind);
	ClassDB::bind_method(D_METHOD("set_builtin_geometry_kind", "kind"), &Part::set_builtin_geometry_kind);
	ClassDB::bind_method(D_METHOD("get_geometry_mesh"), &Part::get_geometry_mesh);
	ClassDB::bind_method(D_METHOD("set_geometry_mesh", "mesh"), &Part::set_geometry_mesh);
	ClassDB::bind_method(D_METHOD("get_geometry_collision"), &Part::get_geometry_collision);
	ClassDB::bind_method(D_METHOD("set_geometry_collision", "shape"), &Part::set_geometry_collision);
	ClassDB::bind_method(D_METHOD("get_appearance_material"), &Part::get_appearance_material);
	ClassDB::bind_method(D_METHOD("set_appearance_material", "material"), &Part::set_appearance_material);
	ADD_SIGNAL(MethodInfo("vextoria_property_changed", PropertyInfo(Variant::STRING_NAME, "property")));
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Size"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "Shape", PROPERTY_HINT_ENUM, "Brick,Sphere,Cylinder,Cone,Wedge,Corner,Bevel,Concave,Truss,Frame,Octant,Torus,BeveledCorner,ConcaveCorner,TriangleCorner,TriangleConcaveCorner"), "set_shape_kind", "get_shape_kind");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "Material", PROPERTY_HINT_ENUM, "SmoothPlastic,Brick,Concrete,Dirt,Fabric,Grass,Ice,Marble,Metal,MetalGrid,MetalPlate,Neon,Planks,Plastic,Plywood,RustyIron,Sand,Sandstone,Snow,Stone,Wood"), "set_material_kind", "get_material_kind");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "Geometry", PROPERTY_HINT_ENUM, "Brick,Wedge,Resource"), "set_builtin_geometry_kind", "get_builtin_geometry_kind");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "GeometryMesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_geometry_mesh", "get_geometry_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "GeometryCollision", PROPERTY_HINT_RESOURCE_TYPE, "Shape3D"), "set_geometry_collision", "get_geometry_collision");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "Color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "AppearanceMaterial", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_appearance_material", "get_appearance_material");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Archivable"), "set_archivable", "is_archivable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Locked"), "set_locked", "is_locked");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Anchored"), "set_anchored", "is_anchored");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "CanCollide"), "set_can_collide", "get_can_collide");
}

void VextoriaScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_source"), &VextoriaScript::get_source);
	ClassDB::bind_method(D_METHOD("set_source", "source"), &VextoriaScript::set_source);
	ClassDB::bind_method(D_METHOD("is_disabled"), &VextoriaScript::is_disabled);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &VextoriaScript::set_disabled);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "Source", PROPERTY_HINT_MULTILINE_TEXT), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Disabled"), "set_disabled", "is_disabled");
}
