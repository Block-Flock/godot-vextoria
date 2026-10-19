#include "roblox2016_csg_compiler.h"

#include "scene/resources/mesh.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "modules/csg/csg_shape.h"
#include "modules/csg/csg_shape.h"
#include "modules/csg/csg_shape.h"
#include "modules/csg/csg_shape.h"
#include "modules/csg/csg_shape.h"
#include "modules/csg/csg_shape.h"
#include "core/object/class_db.h"
#include "core/variant/array.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"

#include <algorithm>
#include <cmath>
#include <vector>


namespace {

constexpr real_t HALF_PI = static_cast<real_t>(1.57079632679489661923);

static Vector3 safe_size(const Vector3 &p_size) {
	return Vector3(
		std::max(static_cast<real_t>(0.001), static_cast<real_t>(std::abs(p_size.x))),
		std::max(static_cast<real_t>(0.001), static_cast<real_t>(std::abs(p_size.y))),
		std::max(static_cast<real_t>(0.001), static_cast<real_t>(std::abs(p_size.z))));
}

static CSGShape3D::Operation sanitize_operation(int p_operation) {
	return static_cast<CSGShape3D::Operation>(std::clamp(p_operation, 0, 2));
}

static void push_tri(std::vector<Vector3> &p_vertices, std::vector<int32_t> &p_indices,
		const Vector3 &a, const Vector3 &b, const Vector3 &c) {
	const int32_t base = static_cast<int32_t>(p_vertices.size());
	p_vertices.push_back(a);
	p_vertices.push_back(b);
	p_vertices.push_back(c);
	p_indices.push_back(base);
	// Godot uses clockwise winding for front faces. Roblox's 2016 mesh
	// builder uses the opposite order, so keep the explicit winding conversion
	// without reflecting the local geometry.
	p_indices.push_back(base + 2);
	p_indices.push_back(base + 1);
}

static void push_quad(std::vector<Vector3> &p_vertices, std::vector<int32_t> &p_indices,
		const Vector3 &a, const Vector3 &b, const Vector3 &c, const Vector3 &d) {
	push_tri(p_vertices, p_indices, a, b, c);
	push_tri(p_vertices, p_indices, a, c, d);
}

static Ref<ArrayMesh> make_triangle_mesh(const std::vector<Vector3> &p_vertices,
		const std::vector<int32_t> &p_indices) {
	if (p_vertices.empty() || p_indices.empty()) {
		return Ref<ArrayMesh>();
	}

	PackedVector3Array vertices;
	vertices.resize(static_cast<int64_t>(p_vertices.size()));
	for (int64_t i = 0; i < static_cast<int64_t>(p_vertices.size()); ++i) {
		vertices.set(i, p_vertices[static_cast<size_t>(i)]);
	}

	PackedInt32Array indices;
	indices.resize(static_cast<int64_t>(p_indices.size()));
	for (int64_t i = 0; i < static_cast<int64_t>(p_indices.size()); ++i) {
		indices.set(i, p_indices[static_cast<size_t>(i)]);
	}

	PackedVector3Array normals;
	normals.resize(static_cast<int64_t>(p_vertices.size()));
	for (size_t i = 0; i + 2 < p_indices.size(); i += 3) {
		const int32_t ia = p_indices[i];
		const int32_t ib = p_indices[i + 1];
		const int32_t ic = p_indices[i + 2];
		// Indices are (a,c,b) for Godot culling, while the authored outward
		// normal remains (b-a) x (c-a). Recalculate per face from that source
		// order rather than copying a global sign.
		const Vector3 normal = (p_vertices[static_cast<size_t>(ic)] - p_vertices[static_cast<size_t>(ia)])
			.cross(p_vertices[static_cast<size_t>(ib)] - p_vertices[static_cast<size_t>(ia)]).normalized();
		normals.set(ia, normal);
		normals.set(ib, normal);
		normals.set(ic, normal);
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_INDEX] = indices;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

static Ref<ArrayMesh> make_wedge_mesh(const Vector3 &p_size) {
	const Vector3 h = safe_size(p_size) * static_cast<real_t>(0.5);
	std::vector<Vector3> vertices;
	std::vector<int32_t> indices;

	// Match Roblox's 2016 Mesh::makeWedge geometry exactly. The ridge is along
	// local +Z; putting it on -Z mirrors every WedgePart even when its CFrame
	// is imported correctly. push_tri converts the source winding to Godot's
	// front-face convention while make_triangle_mesh restores outward normals.
	const Vector3 v0(h.x, h.y, h.z);
	const Vector3 v1(h.x, -h.y, h.z);
	const Vector3 v2(h.x, -h.y, -h.z);
	const Vector3 v3(-h.x, h.y, h.z);
	const Vector3 v4(-h.x, -h.y, h.z);
	const Vector3 v5(-h.x, -h.y, -h.z);

	push_quad(vertices, indices, v0, v3, v4, v1); // back (+Z)
	push_quad(vertices, indices, v3, v0, v2, v5); // front / sloped top
	push_tri(vertices, indices, v0, v1, v2);       // right (+X)
	push_tri(vertices, indices, v3, v5, v4);       // left (-X)
	push_quad(vertices, indices, v5, v2, v1, v4); // bottom (-Y)
	return make_triangle_mesh(vertices, indices);
}

static Ref<ArrayMesh> make_corner_wedge_mesh(const Vector3 &p_size) {
	const Vector3 h = safe_size(p_size) * static_cast<real_t>(0.5);
	std::vector<Vector3> vertices;
	std::vector<int32_t> indices;

	const Vector3 a(-h.x, -h.y, -h.z);
	const Vector3 b(h.x, -h.y, -h.z);
	const Vector3 c(h.x, -h.y, h.z);
	const Vector3 d(-h.x, -h.y, h.z);
	const Vector3 top(-h.x, h.y, -h.z);

	push_quad(vertices, indices, a, b, c, d);
	push_tri(vertices, indices, a, top, b);
	push_tri(vertices, indices, b, top, c);
	push_tri(vertices, indices, c, top, d);
	push_tri(vertices, indices, d, top, a);
	return make_triangle_mesh(vertices, indices);
}

static void add_truss_box(CSGCombiner3D *p_group, const Vector3 &p_position, const Vector3 &p_size) {
	CSGBox3D *box = memnew(CSGBox3D);
	box->set_size(safe_size(p_size));
	box->set_position(p_position);
	box->set_operation(CSGShape3D::OPERATION_UNION);
	p_group->add_child(box, false, Node::INTERNAL_MODE_FRONT);
}

static CSGCombiner3D *make_truss(const Vector3 &p_size) {
	const Vector3 size = safe_size(p_size);
	const Vector3 half = size * static_cast<real_t>(0.5);
	const real_t rail = std::max(static_cast<real_t>(0.05),
		std::min({ size.x, size.y, size.z }) * static_cast<real_t>(0.12));

	CSGCombiner3D *group = memnew(CSGCombiner3D);
	add_truss_box(group, Vector3(-half.x + rail * 0.5, 0, -half.z + rail * 0.5), Vector3(rail, size.y, rail));
	add_truss_box(group, Vector3(half.x - rail * 0.5, 0, -half.z + rail * 0.5), Vector3(rail, size.y, rail));
	add_truss_box(group, Vector3(-half.x + rail * 0.5, 0, half.z - rail * 0.5), Vector3(rail, size.y, rail));
	add_truss_box(group, Vector3(half.x - rail * 0.5, 0, half.z - rail * 0.5), Vector3(rail, size.y, rail));

	const int bars = std::max(2, static_cast<int>(std::round(size.y /
		std::max(rail * static_cast<real_t>(4.0), static_cast<real_t>(0.5)))));
	for (int i = 0; i <= bars; ++i) {
		const real_t y = -half.y + size.y * static_cast<real_t>(i) / static_cast<real_t>(bars);
		add_truss_box(group, Vector3(0, y, -half.z + rail * 0.5), Vector3(size.x, rail, rail));
		add_truss_box(group, Vector3(0, y, half.z - rail * 0.5), Vector3(size.x, rail, rail));
	}
	return group;
}

} // namespace

Roblox2016CSGCompiler::Roblox2016CSGCompiler() {
	set_process(false);
}

void Roblox2016CSGCompiler::_dispose_root() {
	if (_root) {
		_root->queue_free();
	}
	_root = nullptr;
}

void Roblox2016CSGCompiler::_set_failure(const String &p_error) {
	_last_error = p_error;
	_status = STATUS_FAILED;
	_pending_frames = 0;
	_compile_retries = 0;
	set_process(false);
	emit_signal("compile_failed", _last_error);
}

CSGShape3D *Roblox2016CSGCompiler::_build_mesh_operand(const Dictionary &p_desc) {
	const Variant mesh_variant = p_desc.get("mesh", Variant());
	const Ref<Mesh> mesh = mesh_variant;
	if (mesh.is_null()) {
		_set_failure("CSG mesh operand is missing a valid Mesh resource.");
		return nullptr;
	}

	CSGMesh3D *node = memnew(CSGMesh3D);
	node->set_mesh(mesh);
	return node;
}

CSGShape3D *Roblox2016CSGCompiler::_build_primitive(const Dictionary &p_desc) {
	const int shape = static_cast<int>(p_desc.get("shape", SHAPE_BLOCK));
	const Vector3 size = safe_size(p_desc.get("size", Vector3(1, 1, 1)));

	switch (shape) {
		case SHAPE_BALL: {
			CSGSphere3D *sphere = memnew(CSGSphere3D);
			sphere->set_radius(0.5);
			sphere->set_radial_segments(24);
			sphere->set_rings(12);
			sphere->set_smooth_faces(true);
			sphere->set_scale(size);
			return sphere;
		}
		case SHAPE_CYLINDER: {
			CSGCylinder3D *cylinder = memnew(CSGCylinder3D);
			cylinder->set_radius(0.5);
			cylinder->set_height(1.0);
			cylinder->set_sides(32);
			cylinder->set_smooth_faces(true);
			// Godot cylinders point along +Y; classic Roblox cylinders point along X.
			cylinder->set_scale(Vector3(size.y, size.x, size.z));
			cylinder->rotate_z(HALF_PI);
			return cylinder;
		}
		case SHAPE_WEDGE: {
			CSGMesh3D *wedge = memnew(CSGMesh3D);
			wedge->set_mesh(make_wedge_mesh(size));
			return wedge;
		}
		case SHAPE_CORNER_WEDGE: {
			CSGMesh3D *corner = memnew(CSGMesh3D);
			corner->set_mesh(make_corner_wedge_mesh(size));
			return corner;
		}
		case SHAPE_TRUSS:
			return make_truss(size);
		case SHAPE_BLOCK:
		default: {
			CSGBox3D *box = memnew(CSGBox3D);
			box->set_size(size);
			return box;
		}
	}
}

CSGShape3D *Roblox2016CSGCompiler::_build_node(const Dictionary &p_desc, bool p_is_root) {
	const String kind = p_desc.get("kind", String());
	CSGShape3D *node = nullptr;

	if (kind == "group") {
		CSGCombiner3D *group = memnew(CSGCombiner3D);
		const Array children = p_desc.get("children", Array());
		if (children.is_empty()) {
			memdelete(group);
			_set_failure("CSG group contains no operands.");
			return nullptr;
		}
		for (int64_t i = 0; i < children.size(); ++i) {
			if (children[i].get_type() != Variant::DICTIONARY) {
				memdelete(group);
				_set_failure("CSG group child is not a Dictionary operand.");
				return nullptr;
			}
			CSGShape3D *child = _build_node(children[i], false);
			if (!child) {
				memdelete(group);
				return nullptr;
			}
			group->add_child(child, false, Node::INTERNAL_MODE_FRONT);
		}
		node = group;
	} else if (kind == "primitive") {
		node = _build_primitive(p_desc);
	} else if (kind == "mesh") {
		node = _build_mesh_operand(p_desc);
	} else {
		_set_failure("Unsupported CSG operand kind: " + kind);
		return nullptr;
	}

	if (!node) {
		return nullptr;
	}

	const Transform3D transform = p_desc.get("transform", Transform3D());
	node->set_transform(transform * node->get_transform());
	if (!p_is_root) {
		node->set_operation(sanitize_operation(static_cast<int>(p_desc.get("operation", OP_UNION))));
	}
	_operand_count++;
	return node;
}

bool Roblox2016CSGCompiler::set_operation_tree(const Dictionary &p_tree) {
	cancel_compile();
	_dispose_root();
	_baked_mesh.unref();
	_baked_transform = Transform3D();
	_last_error = String();
	_operand_count = 0;

	CSGShape3D *built = _build_node(p_tree, true);
	if (!built) {
		if (_status != STATUS_FAILED) {
			_set_failure("Unable to construct CSG operation tree.");
		}
		return false;
	}

	_root = Object::cast_to<CSGCombiner3D>(built);
	if (!_root) {
		// get_meshes() only bakes the root shape. Wrap a primitive root in a
		// combiner so every accepted tree has a consistent root representation.
		_root = memnew(CSGCombiner3D);
		_root->add_child(built, false, Node::INTERNAL_MODE_FRONT);
	}
	_root->set_name("Roblox2016CSGCompileRoot");
	_root->set_snap(0.0005);
	_root->set_calculate_tangents(true);
	_root->set_use_collision(false);
	_root->set_visible(false);
	add_child(_root, false, Node::INTERNAL_MODE_FRONT);
	_status = STATUS_READY;
	return true;
}

void Roblox2016CSGCompiler::clear_operation_tree() {
	cancel_compile();
	_dispose_root();
	_baked_mesh.unref();
	_baked_transform = Transform3D();
	_last_error = String();
	_operand_count = 0;
	_status = STATUS_EMPTY;
}

bool Roblox2016CSGCompiler::begin_compile() {
	if (!_root || _status != STATUS_READY) {
		return false;
	}
	_status = STATUS_COMPILING;
	_pending_frames = 2;
	_compile_retries = 0;
	set_process(true);
	return true;
}

void Roblox2016CSGCompiler::cancel_compile() {
	if (_status == STATUS_COMPILING) {
		_status = _root ? STATUS_READY : STATUS_EMPTY;
	}
	_pending_frames = 0;
	_compile_retries = 0;
	set_process(false);
}

void Roblox2016CSGCompiler::_vextoria_process(double p_delta) {
	(void)p_delta;
	if (_status != STATUS_COMPILING || !_root) {
		set_process(false);
		return;
	}
	if (_pending_frames-- > 0) {
		return;
	}

	const Array meshes = _root->get_meshes();
	if (meshes.size() < 2 || meshes[1].get_type() != Variant::OBJECT) {
		if (++_compile_retries < 12) {
			_pending_frames = 1;
			return;
		}
		_set_failure("Godot CSG evaluator did not produce a baked mesh.");
		_dispose_root();
		return;
	}

	_baked_transform = meshes[0];
	_baked_mesh = meshes[1];
	if (_baked_mesh.is_null() || _baked_mesh->get_surface_count() == 0) {
		_set_failure("CSG result was empty or invalid.");
		_dispose_root();
		return;
	}

	_status = STATUS_COMPLETE;
	set_process(false);
	_dispose_root();
	emit_signal("compile_completed", _baked_mesh, _baked_transform);
}

int Roblox2016CSGCompiler::get_status() const { return _status; }
String Roblox2016CSGCompiler::get_last_error() const { return _last_error; }
Ref<Mesh> Roblox2016CSGCompiler::get_baked_mesh() const { return _baked_mesh; }
Transform3D Roblox2016CSGCompiler::get_baked_transform() const { return _baked_transform; }
int Roblox2016CSGCompiler::get_operand_count() const { return _operand_count; }

Ref<Shape3D> Roblox2016CSGCompiler::create_collision_shape(bool p_dynamic_body) const {
	if (_baked_mesh.is_null()) {
		return Ref<Shape3D>();
	}

	if (p_dynamic_body) {
		Ref<ConvexPolygonShape3D> convex = _baked_mesh->create_convex_shape(true, false);
		Ref<Shape3D> result = convex;
		return result;
	}

	Ref<ConcavePolygonShape3D> concave = _baked_mesh->create_trimesh_shape();
	Ref<Shape3D> result = concave;
	return result;
}

void Roblox2016CSGCompiler::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_operation_tree", "tree"), &Roblox2016CSGCompiler::set_operation_tree);
	ClassDB::bind_method(D_METHOD("clear_operation_tree"), &Roblox2016CSGCompiler::clear_operation_tree);
	ClassDB::bind_method(D_METHOD("begin_compile"), &Roblox2016CSGCompiler::begin_compile);
	ClassDB::bind_method(D_METHOD("cancel_compile"), &Roblox2016CSGCompiler::cancel_compile);
	ClassDB::bind_method(D_METHOD("get_status"), &Roblox2016CSGCompiler::get_status);
	ClassDB::bind_method(D_METHOD("get_last_error"), &Roblox2016CSGCompiler::get_last_error);
	ClassDB::bind_method(D_METHOD("get_baked_mesh"), &Roblox2016CSGCompiler::get_baked_mesh);
	ClassDB::bind_method(D_METHOD("get_baked_transform"), &Roblox2016CSGCompiler::get_baked_transform);
	ClassDB::bind_method(D_METHOD("create_collision_shape", "dynamic_body"), &Roblox2016CSGCompiler::create_collision_shape, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_operand_count"), &Roblox2016CSGCompiler::get_operand_count);

	ADD_SIGNAL(MethodInfo("compile_completed",
		PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"),
		PropertyInfo(Variant::TRANSFORM3D, "transform")));
	ADD_SIGNAL(MethodInfo("compile_failed", PropertyInfo(Variant::STRING, "error")));

	BIND_ENUM_CONSTANT(OP_UNION);
	BIND_ENUM_CONSTANT(OP_INTERSECTION);
	BIND_ENUM_CONSTANT(OP_SUBTRACTION);
	BIND_ENUM_CONSTANT(SHAPE_BALL);
	BIND_ENUM_CONSTANT(SHAPE_BLOCK);
	BIND_ENUM_CONSTANT(SHAPE_CYLINDER);
	BIND_ENUM_CONSTANT(SHAPE_WEDGE);
	BIND_ENUM_CONSTANT(SHAPE_CORNER_WEDGE);
	BIND_ENUM_CONSTANT(SHAPE_TRUSS);
	BIND_ENUM_CONSTANT(STATUS_EMPTY);
	BIND_ENUM_CONSTANT(STATUS_READY);
	BIND_ENUM_CONSTANT(STATUS_COMPILING);
	BIND_ENUM_CONSTANT(STATUS_COMPLETE);
	BIND_ENUM_CONSTANT(STATUS_FAILED);
}

