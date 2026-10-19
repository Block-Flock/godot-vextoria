#include "roblox2016_part3d.h"

#include "scene/resources/mesh.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/mesh.h"
#include "scene/3d/mesh_instance_3d.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"
#include "core/string/print_string.h"

#include <algorithm>
#include <cmath>
#include <vector>


namespace {

constexpr double PI_D = 3.14159265358979323846;

static Vector3 safe_size(const Vector3 &s) {
	return Vector3(std::max(0.05f, std::abs(s.x)), std::max(0.05f, std::abs(s.y)), std::max(0.05f, std::abs(s.z)));
}

static void push_tri(std::vector<Vector3> &vertices, std::vector<int32_t> &indices,
		const Vector3 &a, const Vector3 &b, const Vector3 &c) {
	int32_t base = static_cast<int32_t>(vertices.size());
	vertices.push_back(a);
	vertices.push_back(b);
	vertices.push_back(c);
	indices.push_back(base);
	indices.push_back(base + 1);
	indices.push_back(base + 2);
}

static void push_quad(std::vector<Vector3> &vertices, std::vector<int32_t> &indices,
		const Vector3 &a, const Vector3 &b, const Vector3 &c, const Vector3 &d) {
	push_tri(vertices, indices, a, b, c);
	push_tri(vertices, indices, a, c, d);
}

static void append_box(std::vector<Vector3> &vertices, std::vector<int32_t> &indices,
		const Vector3 &center, const Vector3 &size) {
	Vector3 h = size * 0.5f;
	Vector3 p[8] = {
		center + Vector3(-h.x, -h.y, -h.z), center + Vector3(h.x, -h.y, -h.z),
		center + Vector3(h.x, h.y, -h.z), center + Vector3(-h.x, h.y, -h.z),
		center + Vector3(-h.x, -h.y, h.z), center + Vector3(h.x, -h.y, h.z),
		center + Vector3(h.x, h.y, h.z), center + Vector3(-h.x, h.y, h.z),
	};
	push_quad(vertices, indices, p[4], p[5], p[6], p[7]);
	push_quad(vertices, indices, p[1], p[0], p[3], p[2]);
	push_quad(vertices, indices, p[0], p[4], p[7], p[3]);
	push_quad(vertices, indices, p[5], p[1], p[2], p[6]);
	push_quad(vertices, indices, p[3], p[7], p[6], p[2]);
	push_quad(vertices, indices, p[0], p[1], p[5], p[4]);
}

static Ref<ArrayMesh> commit_mesh(const std::vector<Vector3> &vertices, const std::vector<int32_t> &indices) {
	if (vertices.empty() || indices.empty()) return Ref<ArrayMesh>();

	PackedVector3Array verts;
	verts.resize(static_cast<int64_t>(vertices.size()));
	for (int64_t i = 0; i < static_cast<int64_t>(vertices.size()); ++i) verts.set(i, vertices[static_cast<size_t>(i)]);

	PackedInt32Array idx;
	idx.resize(static_cast<int64_t>(indices.size()));
	for (int64_t i = 0; i < static_cast<int64_t>(indices.size()); ++i) idx.set(i, indices[static_cast<size_t>(i)]);

	PackedVector3Array normals;
	normals.resize(static_cast<int64_t>(vertices.size()));
	for (size_t i = 0; i + 2 < indices.size(); i += 3) {
		int32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
		Vector3 n = (vertices[static_cast<size_t>(ib)] - vertices[static_cast<size_t>(ia)])
				.cross(vertices[static_cast<size_t>(ic)] - vertices[static_cast<size_t>(ia)]).normalized();
		normals.set(ia, n); normals.set(ib, n); normals.set(ic, n);
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_INDEX] = idx;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

} // namespace

Roblox2016Part3D::Roblox2016Part3D() {
	set_freeze_mode(RigidBody3D::FREEZE_MODE_STATIC);
}

void Roblox2016Part3D::_vextoria_ready() {
	_ensure_children();
	rebuild_geometry();
	set_anchored(_anchored);
	set_can_collide(_can_collide);
}

void Roblox2016Part3D::_ensure_children() {
	if (!_mesh_instance) {
		_mesh_instance = memnew(MeshInstance3D);
		_mesh_instance->set_name("RobloxMesh");
		add_child(_mesh_instance, false, INTERNAL_MODE_FRONT);
	}
	if (!_collision_shape) {
		_collision_shape = memnew(CollisionShape3D);
		_collision_shape->set_name("RobloxCollision");
		add_child(_collision_shape, false, INTERNAL_MODE_FRONT);
	}
}

Ref<ArrayMesh> Roblox2016Part3D::_build_mesh() const {
	Vector3 s = safe_size(_part_size);
	Vector3 h = s * 0.5f;
	std::vector<Vector3> vertices;
	std::vector<int32_t> indices;

	switch (_shape_type) {
		case SHAPE_BALL: {
			constexpr int rings = 10;
			constexpr int segments = 20;
			for (int r = 0; r < rings; ++r) {
				double v0 = static_cast<double>(r) / rings;
				double v1 = static_cast<double>(r + 1) / rings;
				double phi0 = -PI_D * 0.5 + PI_D * v0;
				double phi1 = -PI_D * 0.5 + PI_D * v1;
				for (int q = 0; q < segments; ++q) {
					double u0 = static_cast<double>(q) / segments;
					double u1 = static_cast<double>(q + 1) / segments;
					double th0 = 2.0 * PI_D * u0;
					double th1 = 2.0 * PI_D * u1;
					auto p = [&](double phi, double th) {
						return Vector3(
							static_cast<float>(h.x * std::cos(phi) * std::cos(th)),
							static_cast<float>(h.y * std::sin(phi)),
							static_cast<float>(h.z * std::cos(phi) * std::sin(th)));
					};
					Vector3 a = p(phi0, th0), b = p(phi0, th1), c = p(phi1, th1), d = p(phi1, th0);
					push_quad(vertices, indices, a, b, c, d);
				}
			}
			break;
		}
		case SHAPE_CYLINDER: {
			// Roblox cylinders use the X axis as their longitudinal axis.
			constexpr int segments = 24;
			for (int i = 0; i < segments; ++i) {
				double a0 = (2.0 * PI_D * i) / segments;
				double a1 = (2.0 * PI_D * (i + 1)) / segments;
				Vector3 l0(-h.x, static_cast<float>(h.y * std::cos(a0)), static_cast<float>(h.z * std::sin(a0)));
				Vector3 l1(-h.x, static_cast<float>(h.y * std::cos(a1)), static_cast<float>(h.z * std::sin(a1)));
				Vector3 r0(h.x, l0.y, l0.z);
				Vector3 r1(h.x, l1.y, l1.z);
				push_quad(vertices, indices, l0, r0, r1, l1);
				push_tri(vertices, indices, Vector3(-h.x, 0, 0), l1, l0);
				push_tri(vertices, indices, Vector3(h.x, 0, 0), r0, r1);
			}
			break;
		}
		case SHAPE_WEDGE: {
			// Triangular prism. High edge is at local -Z, low edge at +Z.
			Vector3 a(-h.x, -h.y, -h.z), b(h.x, -h.y, -h.z);
			Vector3 c(-h.x, -h.y, h.z), d(h.x, -h.y, h.z);
			Vector3 e(-h.x, h.y, -h.z), f(h.x, h.y, -h.z);
			push_quad(vertices, indices, a, b, d, c);
			push_quad(vertices, indices, a, e, f, b);
			push_quad(vertices, indices, e, c, d, f);
			push_tri(vertices, indices, a, c, e);
			push_tri(vertices, indices, b, f, d);
			break;
		}
		case SHAPE_CORNER_WEDGE: {
			Vector3 a(-h.x, -h.y, -h.z), b(h.x, -h.y, -h.z);
			Vector3 c(h.x, -h.y, h.z), d(-h.x, -h.y, h.z);
			Vector3 top(-h.x, h.y, -h.z);
			push_quad(vertices, indices, a, b, c, d);
			push_tri(vertices, indices, a, top, b);
			push_tri(vertices, indices, b, top, c);
			push_tri(vertices, indices, c, top, d);
			push_tri(vertices, indices, d, top, a);
			break;
		}
		case SHAPE_TRUSS: {
			float rail = std::max(0.05f, std::min({s.x, s.y, s.z}) * 0.12f);
			append_box(vertices, indices, Vector3(-h.x + rail * 0.5f, 0, -h.z + rail * 0.5f), Vector3(rail, s.y, rail));
			append_box(vertices, indices, Vector3(h.x - rail * 0.5f, 0, -h.z + rail * 0.5f), Vector3(rail, s.y, rail));
			append_box(vertices, indices, Vector3(-h.x + rail * 0.5f, 0, h.z - rail * 0.5f), Vector3(rail, s.y, rail));
			append_box(vertices, indices, Vector3(h.x - rail * 0.5f, 0, h.z - rail * 0.5f), Vector3(rail, s.y, rail));
			int bars = std::max(2, static_cast<int>(std::round(s.y / std::max(rail * 4.0f, 0.5f))));
			for (int i = 0; i <= bars; ++i) {
				float y = -h.y + s.y * static_cast<float>(i) / static_cast<float>(bars);
				append_box(vertices, indices, Vector3(0, y, -h.z + rail * 0.5f), Vector3(s.x, rail, rail));
				append_box(vertices, indices, Vector3(0, y, h.z - rail * 0.5f), Vector3(s.x, rail, rail));
			}
			break;
		}
		case SHAPE_BLOCK:
		default:
			append_box(vertices, indices, Vector3(), s);
			break;
	}

	return commit_mesh(vertices, indices);
}

void Roblox2016Part3D::rebuild_geometry() {
	_ensure_children();
	Ref<ArrayMesh> mesh = _build_mesh();
	_mesh_instance->set_mesh(mesh);
	if (mesh.is_valid()) {
		_collision_shape->set_shape(mesh->create_convex_shape(true, false));
	}
	_update_mass();
	set_can_collide(_can_collide);
}

void Roblox2016Part3D::_update_mass() {
	set_mass(static_cast<float>(std::max(0.001, _density * get_approx_volume())));
}

void Roblox2016Part3D::set_shape_type(int p_shape) {
	_shape_type = std::clamp(p_shape, static_cast<int>(SHAPE_BALL), static_cast<int>(SHAPE_TRUSS));
	if (is_inside_tree()) rebuild_geometry();
}
int Roblox2016Part3D::get_shape_type() const { return _shape_type; }

void Roblox2016Part3D::set_part_size(const Vector3 &p_size) {
	_part_size = safe_size(p_size);
	if (is_inside_tree()) rebuild_geometry();
}
Vector3 Roblox2016Part3D::get_part_size() const { return _part_size; }

void Roblox2016Part3D::set_anchored(bool p_anchored) {
	_anchored = p_anchored;
	set_freeze_enabled(_anchored);
}
bool Roblox2016Part3D::get_anchored() const { return _anchored; }

void Roblox2016Part3D::set_can_collide(bool p_can_collide) {
	_can_collide = p_can_collide;
	if (_collision_shape) _collision_shape->set_disabled(!_can_collide);
}
bool Roblox2016Part3D::get_can_collide() const { return _can_collide; }

void Roblox2016Part3D::set_density(double p_density) {
	_density = std::clamp(p_density, 0.001, 1000.0);
	_update_mass();
}
double Roblox2016Part3D::get_density() const { return _density; }

void Roblox2016Part3D::set_specific_gravity(double p_specific_gravity) {
	_specific_gravity = std::clamp(p_specific_gravity, 0.01, 1000.0);
}
double Roblox2016Part3D::get_specific_gravity() const { return _specific_gravity; }

double Roblox2016Part3D::get_approx_volume() const {
	Vector3 s = safe_size(_part_size);
	double box = static_cast<double>(s.x) * s.y * s.z;
	switch (_shape_type) {
		case SHAPE_BALL: return (4.0 / 3.0) * PI_D * (s.x * 0.5) * (s.y * 0.5) * (s.z * 0.5);
		case SHAPE_CYLINDER: return PI_D * (s.y * 0.5) * (s.z * 0.5) * s.x;
		case SHAPE_WEDGE: return box * 0.5;
		case SHAPE_CORNER_WEDGE: return box / 3.0;
		case SHAPE_TRUSS: return box * 0.30;
		case SHAPE_BLOCK:
		default: return box;
	}
}

void Roblox2016Part3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shape_type", "shape"), &Roblox2016Part3D::set_shape_type);
	ClassDB::bind_method(D_METHOD("get_shape_type"), &Roblox2016Part3D::get_shape_type);
	ClassDB::bind_method(D_METHOD("set_part_size", "size"), &Roblox2016Part3D::set_part_size);
	ClassDB::bind_method(D_METHOD("get_part_size"), &Roblox2016Part3D::get_part_size);
	ClassDB::bind_method(D_METHOD("set_anchored", "anchored"), &Roblox2016Part3D::set_anchored);
	ClassDB::bind_method(D_METHOD("get_anchored"), &Roblox2016Part3D::get_anchored);
	ClassDB::bind_method(D_METHOD("set_can_collide", "can_collide"), &Roblox2016Part3D::set_can_collide);
	ClassDB::bind_method(D_METHOD("get_can_collide"), &Roblox2016Part3D::get_can_collide);
	ClassDB::bind_method(D_METHOD("set_density", "density"), &Roblox2016Part3D::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &Roblox2016Part3D::get_density);
	ClassDB::bind_method(D_METHOD("set_specific_gravity", "specific_gravity"), &Roblox2016Part3D::set_specific_gravity);
	ClassDB::bind_method(D_METHOD("get_specific_gravity"), &Roblox2016Part3D::get_specific_gravity);
	ClassDB::bind_method(D_METHOD("get_approx_volume"), &Roblox2016Part3D::get_approx_volume);
	ClassDB::bind_method(D_METHOD("rebuild_geometry"), &Roblox2016Part3D::rebuild_geometry);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape_type", PROPERTY_HINT_ENUM, "Ball:0,Block:1,Cylinder:2,Wedge:3,CornerWedge:4,Truss:5"), "set_shape_type", "get_shape_type");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "part_size"), "set_part_size", "get_part_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "anchored"), "set_anchored", "get_anchored");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "can_collide"), "set_can_collide", "get_can_collide");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "density", PROPERTY_HINT_RANGE, "0.001,100,0.001"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specific_gravity", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_specific_gravity", "get_specific_gravity");

	BIND_ENUM_CONSTANT(SHAPE_BALL);
	BIND_ENUM_CONSTANT(SHAPE_BLOCK);
	BIND_ENUM_CONSTANT(SHAPE_CYLINDER);
	BIND_ENUM_CONSTANT(SHAPE_WEDGE);
	BIND_ENUM_CONSTANT(SHAPE_CORNER_WEDGE);
	BIND_ENUM_CONSTANT(SHAPE_TRUSS);
}

