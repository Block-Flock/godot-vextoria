#ifndef VEXTORIA_ROBLOX2016_BUOYANCY3D_H
#define VEXTORIA_ROBLOX2016_BUOYANCY3D_H

#include "scene/3d/node_3d.h"
#include "core/string/node_path.h"
#include "core/math/vector3.h"


class RigidBody3D;
class TerrainManager;

class Roblox2016Buoyancy3D : public Node3D {
	GDCLASS(Roblox2016Buoyancy3D, Node3D)

public:
	enum ForceModel {
		FORCE_LEGACY_SPECIFIC_GRAVITY = 0,
		FORCE_VOLUME_WATER_DENSITY = 1,
	};

	Roblox2016Buoyancy3D();
	~Roblox2016Buoyancy3D() override = default;

	void set_body_path(const NodePath &p_path);
	NodePath get_body_path() const;
	void set_terrain_path(const NodePath &p_path);
	NodePath get_terrain_path() const;
	void set_sample_size(const Vector3 &p_size);
	Vector3 get_sample_size() const;
	void set_specific_gravity(double p_value);
	double get_specific_gravity() const;
	void set_water_density(double p_value);
	double get_water_density() const;
	void set_water_viscosity(double p_value);
	double get_water_viscosity() const;
	void set_force_model(int p_model);
	int get_force_model() const;
	void set_enabled(bool p_enabled);
	bool get_enabled() const;
	double get_submerged_ratio() const;
	bool get_touching_water() const;

	void _vextoria_ready();
	void _vextoria_physics_process(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	NodePath _body_path;
	NodePath _terrain_path;
	Vector3 _sample_size = Vector3(2, 2, 2);
	double _specific_gravity = 0.7;
	double _water_density = 1.0;
	double _water_viscosity = 0.02;
	int _force_model = FORCE_LEGACY_SPECIFIC_GRAVITY;
	bool _enabled = true;
	double _submerged_ratio = 0.0;
	bool _touching_water = false;

	RigidBody3D *_resolve_body() const;
	TerrainManager *_resolve_terrain() const;
	double _gravity_magnitude() const;
};


VARIANT_ENUM_CAST(Roblox2016Buoyancy3D::ForceModel);

#endif
