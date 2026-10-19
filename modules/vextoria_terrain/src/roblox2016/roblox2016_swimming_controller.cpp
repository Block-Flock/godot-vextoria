#include "roblox2016_swimming_controller.h"
#include "../terrain_manager.h"

#include "scene/3d/physics/character_body_3d.h"
#include "core/object/class_db.h"

#include <algorithm>
#include <cmath>


Roblox2016SwimmingController::Roblox2016SwimmingController() { set_physics_process(true); }
void Roblox2016SwimmingController::_vextoria_ready() { set_physics_process(true); }

CharacterBody3D *Roblox2016SwimmingController::_resolve_body() const {
	Node *candidate = nullptr;
	if (!_body_path.is_empty()) candidate = get_node_or_null(_body_path);
	if (!candidate) candidate = get_parent();
	return Object::cast_to<CharacterBody3D>(candidate);
}

TerrainManager *Roblox2016SwimmingController::_resolve_terrain() const {
	if (_terrain_path.is_empty()) return nullptr;
	return Object::cast_to<TerrainManager>(get_node_or_null(_terrain_path));
}

bool Roblox2016SwimmingController::_detect_water(CharacterBody3D *p_body, TerrainManager *p_terrain, double p_threshold) {
	if (!p_body || !p_terrain) { _last_water_density = 0.0; return false; }
	Vector3 point = p_body->get_global_transform().xform(_detection_offset);
	_last_water_density = p_terrain->sample_water_density(point);
	return _last_water_density >= p_threshold;
}

void Roblox2016SwimmingController::_set_swimming(bool p_swimming, const Vector3 &p_prior_velocity, bool p_was_freefall) {
	if (_is_swimming == p_swimming) return;
	_is_swimming = p_swimming;
	if (_is_swimming) {
		_swim_time = 0.0;
		_was_freefall_on_entry = p_was_freefall;
		_entry_vertical_velocity = p_prior_velocity.y;
		_reenable_timer = p_was_freefall ? 0.5 : 0.0;
		emit_signal("swimming_started");
	} else {
		_reenable_timer = 0.0;
		_was_freefall_on_entry = false;
		emit_signal("swimming_ended");
	}
	emit_signal("swimming_changed", _is_swimming);
}

Vector3 Roblox2016SwimmingController::compute_swim_velocity(const Vector3 &p_current_velocity,
		const Vector3 &p_move_direction, bool p_jump_requested, double p_delta) {
	if (!_is_swimming) return p_current_velocity;
	_swim_time += std::max(0.0, p_delta);
	_reenable_timer = std::max(0.0, _reenable_timer - std::max(0.0, p_delta));

	Vector3 move = p_move_direction;
	if (move.length_squared() > 1.0f) move = move.normalized();
	Vector3 target = move * static_cast<float>(_walk_speed * _character_velocity_influence);

	Vector3 result = p_current_velocity;
	result.x = target.x;
	result.z = target.z;

	if (p_jump_requested) {
		result.y = static_cast<float>(_jump_power * 0.5);
	} else if (_was_freefall_on_entry && _swim_time <= 0.75) {
		double carry = (_entry_vertical_velocity * 0.5) * std::exp(-0.2 * _swim_time);
		if (_entry_vertical_velocity > 0.0) carry += _entry_vertical_velocity * std::exp(-0.2 * _swim_time);
		result.y = static_cast<float>(carry);
	} else {
		result.y = target.y;
	}
	return result;
}

void Roblox2016SwimmingController::_vextoria_physics_process(double p_delta) {
	CharacterBody3D *body = _resolve_body();
	TerrainManager *terrain = _resolve_terrain();
	if (!body || !terrain) return;

	Vector3 prior = body->get_velocity();
	const double threshold = _is_swimming ? _water_exit_threshold : _water_enter_threshold;
	bool in_water = _detect_water(body, terrain, threshold);
	bool freefall = !body->is_on_floor() && !_is_swimming;

	if (in_water && !_is_swimming) {
		_set_swimming(true, prior, freefall);
	} else if (!in_water && _is_swimming) {
		_set_swimming(false, prior, false);
	}

	if (_is_swimming && _auto_apply_velocity) {
		body->set_velocity(compute_swim_velocity(prior, _move_direction, _jump_requested, p_delta));
	}
}

void Roblox2016SwimmingController::reset_swim_state() {
	if (_is_swimming) _set_swimming(false, Vector3(), false);
	_swim_time = 0.0;
	_reenable_timer = 0.0;
	_entry_vertical_velocity = 0.0;
	_last_water_density = 0.0;
}

void Roblox2016SwimmingController::set_body_path(const NodePath &p_path) { _body_path = p_path; }
NodePath Roblox2016SwimmingController::get_body_path() const { return _body_path; }
void Roblox2016SwimmingController::set_terrain_path(const NodePath &p_path) { _terrain_path = p_path; }
NodePath Roblox2016SwimmingController::get_terrain_path() const { return _terrain_path; }
void Roblox2016SwimmingController::set_move_direction(const Vector3 &p_direction) { _move_direction = p_direction; }
Vector3 Roblox2016SwimmingController::get_move_direction() const { return _move_direction; }
void Roblox2016SwimmingController::set_jump_requested(bool p_jump) { _jump_requested = p_jump; }
bool Roblox2016SwimmingController::get_jump_requested() const { return _jump_requested; }
void Roblox2016SwimmingController::set_walk_speed(double p_speed) { _walk_speed = std::max(0.0, p_speed); }
double Roblox2016SwimmingController::get_walk_speed() const { return _walk_speed; }
void Roblox2016SwimmingController::set_jump_power(double p_power) { _jump_power = std::max(0.0, p_power); }
double Roblox2016SwimmingController::get_jump_power() const { return _jump_power; }
void Roblox2016SwimmingController::set_character_velocity_influence(double p_value) { _character_velocity_influence = std::max(0.0, p_value); }
double Roblox2016SwimmingController::get_character_velocity_influence() const { return _character_velocity_influence; }
void Roblox2016SwimmingController::set_auto_apply_velocity(bool p_enabled) { _auto_apply_velocity = p_enabled; }
bool Roblox2016SwimmingController::get_auto_apply_velocity() const { return _auto_apply_velocity; }
void Roblox2016SwimmingController::set_detection_offset(const Vector3 &p_offset) { _detection_offset = p_offset; }
Vector3 Roblox2016SwimmingController::get_detection_offset() const { return _detection_offset; }
void Roblox2016SwimmingController::set_water_enter_threshold(double p_threshold) {
	_water_enter_threshold = std::clamp(p_threshold, 0.0, 1.0);
	if (_water_exit_threshold > _water_enter_threshold) _water_exit_threshold = _water_enter_threshold;
}
double Roblox2016SwimmingController::get_water_enter_threshold() const { return _water_enter_threshold; }
void Roblox2016SwimmingController::set_water_exit_threshold(double p_threshold) {
	_water_exit_threshold = std::clamp(p_threshold, 0.0, _water_enter_threshold);
}
double Roblox2016SwimmingController::get_water_exit_threshold() const { return _water_exit_threshold; }
bool Roblox2016SwimmingController::get_is_swimming() const { return _is_swimming; }
int Roblox2016SwimmingController::get_swim_state() const { return _is_swimming ? STATE_SWIMMING : STATE_NOT_SWIMMING; }
double Roblox2016SwimmingController::get_water_density_at_body() const { return _last_water_density; }

void Roblox2016SwimmingController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_body_path", "path"), &Roblox2016SwimmingController::set_body_path);
	ClassDB::bind_method(D_METHOD("get_body_path"), &Roblox2016SwimmingController::get_body_path);
	ClassDB::bind_method(D_METHOD("set_terrain_path", "path"), &Roblox2016SwimmingController::set_terrain_path);
	ClassDB::bind_method(D_METHOD("get_terrain_path"), &Roblox2016SwimmingController::get_terrain_path);
	ClassDB::bind_method(D_METHOD("set_move_direction", "direction"), &Roblox2016SwimmingController::set_move_direction);
	ClassDB::bind_method(D_METHOD("get_move_direction"), &Roblox2016SwimmingController::get_move_direction);
	ClassDB::bind_method(D_METHOD("set_jump_requested", "jump"), &Roblox2016SwimmingController::set_jump_requested);
	ClassDB::bind_method(D_METHOD("get_jump_requested"), &Roblox2016SwimmingController::get_jump_requested);
	ClassDB::bind_method(D_METHOD("set_walk_speed", "speed"), &Roblox2016SwimmingController::set_walk_speed);
	ClassDB::bind_method(D_METHOD("get_walk_speed"), &Roblox2016SwimmingController::get_walk_speed);
	ClassDB::bind_method(D_METHOD("set_jump_power", "power"), &Roblox2016SwimmingController::set_jump_power);
	ClassDB::bind_method(D_METHOD("get_jump_power"), &Roblox2016SwimmingController::get_jump_power);
	ClassDB::bind_method(D_METHOD("set_character_velocity_influence", "value"), &Roblox2016SwimmingController::set_character_velocity_influence);
	ClassDB::bind_method(D_METHOD("get_character_velocity_influence"), &Roblox2016SwimmingController::get_character_velocity_influence);
	ClassDB::bind_method(D_METHOD("set_auto_apply_velocity", "enabled"), &Roblox2016SwimmingController::set_auto_apply_velocity);
	ClassDB::bind_method(D_METHOD("get_auto_apply_velocity"), &Roblox2016SwimmingController::get_auto_apply_velocity);
	ClassDB::bind_method(D_METHOD("set_detection_offset", "offset"), &Roblox2016SwimmingController::set_detection_offset);
	ClassDB::bind_method(D_METHOD("get_detection_offset"), &Roblox2016SwimmingController::get_detection_offset);
	ClassDB::bind_method(D_METHOD("set_water_enter_threshold", "threshold"), &Roblox2016SwimmingController::set_water_enter_threshold);
	ClassDB::bind_method(D_METHOD("get_water_enter_threshold"), &Roblox2016SwimmingController::get_water_enter_threshold);
	ClassDB::bind_method(D_METHOD("set_water_exit_threshold", "threshold"), &Roblox2016SwimmingController::set_water_exit_threshold);
	ClassDB::bind_method(D_METHOD("get_water_exit_threshold"), &Roblox2016SwimmingController::get_water_exit_threshold);
	ClassDB::bind_method(D_METHOD("get_is_swimming"), &Roblox2016SwimmingController::get_is_swimming);
	ClassDB::bind_method(D_METHOD("get_swim_state"), &Roblox2016SwimmingController::get_swim_state);
	ClassDB::bind_method(D_METHOD("get_water_density_at_body"), &Roblox2016SwimmingController::get_water_density_at_body);
	ClassDB::bind_method(D_METHOD("compute_swim_velocity", "current_velocity", "move_direction", "jump_requested", "delta"), &Roblox2016SwimmingController::compute_swim_velocity);
	ClassDB::bind_method(D_METHOD("reset_swim_state"), &Roblox2016SwimmingController::reset_swim_state);

	ADD_SIGNAL(MethodInfo("swimming_started"));
	ADD_SIGNAL(MethodInfo("swimming_ended"));
	ADD_SIGNAL(MethodInfo("swimming_changed", PropertyInfo(Variant::BOOL, "swimming")));
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "body_path"), "set_body_path", "get_body_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "terrain_path"), "set_terrain_path", "get_terrain_path");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "move_direction"), "set_move_direction", "get_move_direction");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "jump_requested"), "set_jump_requested", "get_jump_requested");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walk_speed", PROPERTY_HINT_RANGE, "0,100,0.1"), "set_walk_speed", "get_walk_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_power", PROPERTY_HINT_RANGE, "0,200,0.1"), "set_jump_power", "get_jump_power");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "character_velocity_influence", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_character_velocity_influence", "get_character_velocity_influence");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_apply_velocity"), "set_auto_apply_velocity", "get_auto_apply_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "detection_offset"), "set_detection_offset", "get_detection_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "water_enter_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_water_enter_threshold", "get_water_enter_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "water_exit_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_water_exit_threshold", "get_water_exit_threshold");
	BIND_ENUM_CONSTANT(STATE_NOT_SWIMMING);
	BIND_ENUM_CONSTANT(STATE_SWIMMING);
}

