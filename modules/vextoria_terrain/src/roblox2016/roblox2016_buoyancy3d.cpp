#include "roblox2016_buoyancy3d.h"
#include "roblox2016_part3d.h"
#include "../terrain_manager.h"

#include "core/config/project_settings.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "core/object/class_db.h"

#include <algorithm>
#include <array>
#include <cmath>


Roblox2016Buoyancy3D::Roblox2016Buoyancy3D() {
	set_physics_process(true);
}

void Roblox2016Buoyancy3D::_vextoria_ready() {
	set_physics_process(_enabled);
}

RigidBody3D *Roblox2016Buoyancy3D::_resolve_body() const {
	Node *candidate = nullptr;
	if (!_body_path.is_empty()) {
		candidate = get_node_or_null(_body_path);
	}
	if (!candidate) {
		candidate = get_parent();
	}
	return Object::cast_to<RigidBody3D>(candidate);
}

TerrainManager *Roblox2016Buoyancy3D::_resolve_terrain() const {
	if (_terrain_path.is_empty()) {
		return nullptr;
	}
	return Object::cast_to<TerrainManager>(get_node_or_null(_terrain_path));
}

double Roblox2016Buoyancy3D::_gravity_magnitude() const {
	Variant value = ProjectSettings::get_singleton()->get_setting("physics/3d/default_gravity", 9.8);
	return std::max(0.001, std::abs(static_cast<double>(value)));
}

void Roblox2016Buoyancy3D::_vextoria_physics_process(double p_delta) {
	if (!_enabled || p_delta <= 0.0) {
		return;
	}

	RigidBody3D *body = _resolve_body();
	TerrainManager *terrain = _resolve_terrain();
	if (!body || !terrain || body->is_freeze_enabled()) {
		return;
	}

	Vector3 sample_size(
		std::abs(_sample_size.x),
		std::abs(_sample_size.y),
		std::abs(_sample_size.z));
	double specific_gravity = _specific_gravity;
	double volume = static_cast<double>(sample_size.x) * sample_size.y * sample_size.z;
	int shape_type = Roblox2016Part3D::SHAPE_BLOCK;

	if (Roblox2016Part3D *part = Object::cast_to<Roblox2016Part3D>(body)) {
		sample_size = part->get_part_size();
		sample_size = Vector3(std::abs(sample_size.x), std::abs(sample_size.y), std::abs(sample_size.z));
		specific_gravity = part->get_specific_gravity();
		volume = part->get_approx_volume();
		shape_type = part->get_shape_type();
	}

	const Transform3D xf = body->get_global_transform();
	const Vector3 linear_velocity = body->get_linear_velocity();
	const double gravity = _gravity_magnitude();
	const double full_buoyancy = _force_model == FORCE_VOLUME_WATER_DENSITY
		? volume * gravity * _water_density
		: static_cast<double>(body->get_mass()) * gravity / std::max(0.01, specific_gravity);

	// Legacy Roblox used one connector for balls and distributed connectors for
	// box-like geometry. The box connector offsets were half of local extents,
	// which keeps the forces inside the hull and gives stable righting torque.
	Vector3 quarter = sample_size * 0.25f;
	// Match Extents::getCorner(i) ordering used by Roblox's buoyancy code:
	// X = bit 2, Y = bit 1, Z = bit 0.
	std::array<Vector3, 8> connectors;
	for (int i = 0; i < 8; ++i) {
		connectors[static_cast<size_t>(i)] = Vector3(
			(i & 4) ? quarter.x : -quarter.x,
			(i & 2) ? quarter.y : -quarter.y,
			(i & 1) ? quarter.z : -quarter.z);
	}

	const int connector_count = shape_type == Roblox2016Part3D::SHAPE_BALL ? 1 : 8;
	double shape_ratio = -1.0;
	double search_radius = std::max({
		static_cast<double>(sample_size.x),
		static_cast<double>(sample_size.y),
		static_cast<double>(sample_size.z),
		2.0});
	const double water_surface = terrain->find_water_surface_y(xf.origin, search_radius);

	if (std::isfinite(water_surface) && shape_type == Roblox2016Part3D::SHAPE_BALL) {
		// Exact sphere segment fraction used by the historical buoyancy path:
		// (2 + 3k - k^3) / 4 for normalized water depth k in [-1, 1].
		const double radius_y = std::max(0.025, static_cast<double>(sample_size.y) * 0.5);
		const double k = std::clamp((water_surface - xf.origin.y) / radius_y, -1.0, 1.0);
		shape_ratio = std::clamp((2.0 + 3.0 * k - k * k * k) * 0.25, 0.0, 1.0);
	} else if (std::isfinite(water_surface) && shape_type == Roblox2016Part3D::SHAPE_CYLINDER) {
		// Roblox cylinders run along local X. The old implementation blended a
		// linear vertical-cylinder fraction with a circular-segment fraction.
		const Vector3 axis = xf.basis.get_column(0).normalized();
		const double verticality = std::clamp(std::abs(static_cast<double>(axis.y)), 0.0, 1.0);
		const double radius_y = std::max(0.025, static_cast<double>(sample_size.y) * 0.5);
		const double k = std::clamp((water_surface - xf.origin.y) / radius_y, -1.0, 1.0);
		const double circular = 0.5 + (std::asin(k) + k * std::sqrt(std::max(0.0, 1.0 - k * k))) / 3.14159265358979323846;
		const double half_axis = std::max(0.025, static_cast<double>(sample_size.x) * 0.5);
		const double linear = std::clamp(0.5 + (water_surface - xf.origin.y) / (2.0 * half_axis), 0.0, 1.0);
		shape_ratio = std::clamp(verticality * linear + (1.0 - verticality) * circular, 0.0, 1.0);
	}

	Vector3 cross_area(
		std::abs(sample_size.y * sample_size.z),
		std::abs(sample_size.x * sample_size.z),
		std::abs(sample_size.x * sample_size.y));
	if (shape_type == Roblox2016Part3D::SHAPE_BALL) {
		const float radius = std::abs(sample_size.x) * 0.5f;
		const float circle = static_cast<float>(3.14159265358979323846) * radius * radius;
		cross_area = Vector3(circle, circle, circle);
	} else if (shape_type == Roblox2016Part3D::SHAPE_CYLINDER) {
		const float radius = std::abs(sample_size.y) * 0.5f;
		cross_area.x = static_cast<float>(3.14159265358979323846) * radius * radius;
	} else if (shape_type == Roblox2016Part3D::SHAPE_WEDGE) {
		cross_area.x *= 0.5f;
	} else if (shape_type == Roblox2016Part3D::SHAPE_CORNER_WEDGE) {
		cross_area.x *= 0.5f;
		cross_area.z *= 0.5f;
	}

	static constexpr double wedge_weights[8] =
		{1.0, 2.0, 0.0, 1.0, 1.0, 2.0, 0.0, 1.0};
	static constexpr double corner_wedge_weights[8] =
		{1.5, 1.0, 0.0, 0.0, 3.0, 1.5, 1.0, 0.0};

	const Basis orientation = xf.basis.orthonormalized();
	const Vector3 local_velocity = orientation.inverse().xform(linear_velocity);
	const double viscosity_k =
		-_water_viscosity / std::max(1.0e-6, p_delta) / connector_count;

	double ratio_sum = 0.0;
	for (int i = 0; i < connector_count; ++i) {
		const Vector3 local = connector_count == 1 ? Vector3() : connectors[static_cast<size_t>(i)];
		const Vector3 world = xf.xform(local);
		double ratio = shape_ratio >= 0.0 ? shape_ratio : terrain->sample_water_density(world);
		if (shape_type == Roblox2016Part3D::SHAPE_WEDGE) {
			ratio *= wedge_weights[i];
		} else if (shape_type == Roblox2016Part3D::SHAPE_CORNER_WEDGE) {
			ratio *= corner_wedge_weights[i];
		}
		ratio_sum += ratio;
		if (ratio <= 0.0001) {
			continue;
		}

		Vector3 connector_cross;
		if (connector_count == 1) {
			// BuoyancyBallContact::getCrossSections divides the spherical projected
			// area by eight even though the ball has one buoyancy connector.
			connector_cross = cross_area / 8.0f;
		} else {
			const bool positive_x = local_velocity.x > 0.0f;
			const bool positive_y = local_velocity.y > 0.0f;
			const bool positive_z = local_velocity.z > 0.0f;
			const bool corner_x = (i & 4) != 0;
			const bool corner_y = (i & 2) != 0;
			const bool corner_z = (i & 1) != 0;
			const Vector3 quarter_area = cross_area / 4.0f;
			connector_cross = Vector3(
				positive_x != corner_x ? quarter_area.x : 0.0f,
				positive_y != corner_y ? quarter_area.y : 0.0f,
				positive_z != corner_z ? quarter_area.z : 0.0f);
		}

		const Vector3 viscosity_object = local_velocity * connector_cross;
		const Vector3 drag_world =
			orientation.xform(viscosity_object) * static_cast<float>(viscosity_k);

		Vector3 force(
			0.0f,
			static_cast<float>((full_buoyancy / connector_count) * ratio),
			0.0f);
		force += drag_world * static_cast<float>(ratio);
		body->apply_force(force, orientation.xform(local));
	}

	const double new_ratio = ratio_sum / static_cast<double>(connector_count);

	// Match the old broadphase test: center + all eight hull corners. This makes
	// buoyancy begin as soon as the hull intersects water, even before the force
	// connectors themselves are submerged.
	bool new_touching = terrain->sample_water_density(xf.origin) > 0.001;
	const Vector3 half = sample_size * 0.5f;
	if (!new_touching) {
		for (int i = 0; i < 8; ++i) {
			Vector3 corner(
				(i & 1) ? half.x : -half.x,
				(i & 2) ? half.y : -half.y,
				(i & 4) ? half.z : -half.z);
			if (terrain->sample_water_density(xf.xform(corner)) > 0.001) {
				new_touching = true;
				break;
			}
		}
	}

	if (new_touching != _touching_water) {
		_touching_water = new_touching;
		emit_signal("buoyancy_changed", _touching_water);
	}
	_submerged_ratio = new_ratio;
}

void Roblox2016Buoyancy3D::set_body_path(const NodePath &p_path) { _body_path = p_path; }
NodePath Roblox2016Buoyancy3D::get_body_path() const { return _body_path; }
void Roblox2016Buoyancy3D::set_terrain_path(const NodePath &p_path) { _terrain_path = p_path; }
NodePath Roblox2016Buoyancy3D::get_terrain_path() const { return _terrain_path; }
void Roblox2016Buoyancy3D::set_sample_size(const Vector3 &p_size) { _sample_size = Vector3(std::abs(p_size.x), std::abs(p_size.y), std::abs(p_size.z)); }
Vector3 Roblox2016Buoyancy3D::get_sample_size() const { return _sample_size; }
void Roblox2016Buoyancy3D::set_specific_gravity(double p_value) { _specific_gravity = std::clamp(p_value, 0.01, 1000.0); }
double Roblox2016Buoyancy3D::get_specific_gravity() const { return _specific_gravity; }
void Roblox2016Buoyancy3D::set_water_density(double p_value) { _water_density = std::clamp(p_value, 0.001, 1000.0); }
double Roblox2016Buoyancy3D::get_water_density() const { return _water_density; }
void Roblox2016Buoyancy3D::set_water_viscosity(double p_value) { _water_viscosity = std::clamp(p_value, 0.0, 100.0); }
double Roblox2016Buoyancy3D::get_water_viscosity() const { return _water_viscosity; }
void Roblox2016Buoyancy3D::set_force_model(int p_model) { _force_model = std::clamp(p_model, 0, 1); }
int Roblox2016Buoyancy3D::get_force_model() const { return _force_model; }
void Roblox2016Buoyancy3D::set_enabled(bool p_enabled) { _enabled = p_enabled; set_physics_process(_enabled); }
bool Roblox2016Buoyancy3D::get_enabled() const { return _enabled; }
double Roblox2016Buoyancy3D::get_submerged_ratio() const { return _submerged_ratio; }
bool Roblox2016Buoyancy3D::get_touching_water() const { return _touching_water; }

void Roblox2016Buoyancy3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_body_path", "path"), &Roblox2016Buoyancy3D::set_body_path);
	ClassDB::bind_method(D_METHOD("get_body_path"), &Roblox2016Buoyancy3D::get_body_path);
	ClassDB::bind_method(D_METHOD("set_terrain_path", "path"), &Roblox2016Buoyancy3D::set_terrain_path);
	ClassDB::bind_method(D_METHOD("get_terrain_path"), &Roblox2016Buoyancy3D::get_terrain_path);
	ClassDB::bind_method(D_METHOD("set_sample_size", "size"), &Roblox2016Buoyancy3D::set_sample_size);
	ClassDB::bind_method(D_METHOD("get_sample_size"), &Roblox2016Buoyancy3D::get_sample_size);
	ClassDB::bind_method(D_METHOD("set_specific_gravity", "value"), &Roblox2016Buoyancy3D::set_specific_gravity);
	ClassDB::bind_method(D_METHOD("get_specific_gravity"), &Roblox2016Buoyancy3D::get_specific_gravity);
	ClassDB::bind_method(D_METHOD("set_water_density", "value"), &Roblox2016Buoyancy3D::set_water_density);
	ClassDB::bind_method(D_METHOD("get_water_density"), &Roblox2016Buoyancy3D::get_water_density);
	ClassDB::bind_method(D_METHOD("set_water_viscosity", "value"), &Roblox2016Buoyancy3D::set_water_viscosity);
	ClassDB::bind_method(D_METHOD("get_water_viscosity"), &Roblox2016Buoyancy3D::get_water_viscosity);
	ClassDB::bind_method(D_METHOD("set_force_model", "model"), &Roblox2016Buoyancy3D::set_force_model);
	ClassDB::bind_method(D_METHOD("get_force_model"), &Roblox2016Buoyancy3D::get_force_model);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &Roblox2016Buoyancy3D::set_enabled);
	ClassDB::bind_method(D_METHOD("get_enabled"), &Roblox2016Buoyancy3D::get_enabled);
	ClassDB::bind_method(D_METHOD("get_submerged_ratio"), &Roblox2016Buoyancy3D::get_submerged_ratio);
	ClassDB::bind_method(D_METHOD("get_touching_water"), &Roblox2016Buoyancy3D::get_touching_water);

	ADD_SIGNAL(MethodInfo("buoyancy_changed", PropertyInfo(Variant::BOOL, "touching_water")));
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "body_path"), "set_body_path", "get_body_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "terrain_path"), "set_terrain_path", "get_terrain_path");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "sample_size"), "set_sample_size", "get_sample_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "specific_gravity", PROPERTY_HINT_RANGE, "0.01,100,0.01"), "set_specific_gravity", "get_specific_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "water_density", PROPERTY_HINT_RANGE, "0.001,100,0.001"), "set_water_density", "get_water_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "water_viscosity", PROPERTY_HINT_RANGE, "0,10,0.001"), "set_water_viscosity", "get_water_viscosity");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "force_model", PROPERTY_HINT_ENUM, "LegacySpecificGravity:0,VolumeWaterDensity:1"), "set_force_model", "get_force_model");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");

	BIND_ENUM_CONSTANT(FORCE_LEGACY_SPECIFIC_GRAVITY);
	BIND_ENUM_CONSTANT(FORCE_VOLUME_WATER_DENSITY);
}

