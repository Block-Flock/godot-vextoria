#ifndef VEXTORIA_ROBLOX2016_SWIMMING_CONTROLLER_H
#define VEXTORIA_ROBLOX2016_SWIMMING_CONTROLLER_H

#include "scene/main/node.h"
#include "core/string/node_path.h"
#include "core/math/vector3.h"


class CharacterBody3D;
class TerrainManager;

class Roblox2016SwimmingController : public Node {
	GDCLASS(Roblox2016SwimmingController, Node)

public:
	enum SwimState {
		STATE_NOT_SWIMMING = 0,
		STATE_SWIMMING = 1,
	};

	Roblox2016SwimmingController();
	~Roblox2016SwimmingController() override = default;

	void set_body_path(const NodePath &p_path);
	NodePath get_body_path() const;
	void set_terrain_path(const NodePath &p_path);
	NodePath get_terrain_path() const;
	void set_move_direction(const Vector3 &p_direction);
	Vector3 get_move_direction() const;
	void set_jump_requested(bool p_jump);
	bool get_jump_requested() const;
	void set_walk_speed(double p_speed);
	double get_walk_speed() const;
	void set_jump_power(double p_power);
	double get_jump_power() const;
	void set_character_velocity_influence(double p_value);
	double get_character_velocity_influence() const;
	void set_auto_apply_velocity(bool p_enabled);
	bool get_auto_apply_velocity() const;
	void set_detection_offset(const Vector3 &p_offset);
	Vector3 get_detection_offset() const;
	void set_water_enter_threshold(double p_threshold);
	double get_water_enter_threshold() const;
	void set_water_exit_threshold(double p_threshold);
	double get_water_exit_threshold() const;
	bool get_is_swimming() const;
	int get_swim_state() const;
	double get_water_density_at_body() const;

	Vector3 compute_swim_velocity(const Vector3 &p_current_velocity, const Vector3 &p_move_direction,
			bool p_jump_requested, double p_delta);
	void reset_swim_state();
	void _vextoria_ready();
	void _vextoria_physics_process(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	NodePath _body_path;
	NodePath _terrain_path;
	Vector3 _move_direction;
	Vector3 _detection_offset = Vector3(0, 0.5, 0);
	double _walk_speed = 16.0;
	double _jump_power = 50.0;
	double _character_velocity_influence = 1.0;
	double _water_enter_threshold = 0.05;
	double _water_exit_threshold = 0.02;
	bool _jump_requested = false;
	bool _auto_apply_velocity = true;
	bool _is_swimming = false;
	bool _was_freefall_on_entry = false;
	double _entry_vertical_velocity = 0.0;
	double _swim_time = 0.0;
	double _reenable_timer = 0.0;
	double _last_water_density = 0.0;

	CharacterBody3D *_resolve_body() const;
	TerrainManager *_resolve_terrain() const;
	bool _detect_water(CharacterBody3D *p_body, TerrainManager *p_terrain, double p_threshold);
	void _set_swimming(bool p_swimming, const Vector3 &p_prior_velocity, bool p_was_freefall);
};


VARIANT_ENUM_CAST(Roblox2016SwimmingController::SwimState);

#endif
