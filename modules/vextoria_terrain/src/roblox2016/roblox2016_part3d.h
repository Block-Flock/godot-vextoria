#ifndef VEXTORIA_ROBLOX2016_PART3D_H
#define VEXTORIA_ROBLOX2016_PART3D_H

#include "scene/3d/physics/rigid_body_3d.h"
#include "core/math/vector3.h"


class MeshInstance3D;
class CollisionShape3D;
class ArrayMesh;

class Roblox2016Part3D : public RigidBody3D {
	GDCLASS(Roblox2016Part3D, RigidBody3D)

public:
	enum ShapeType {
		SHAPE_BALL = 0,
		SHAPE_BLOCK = 1,
		SHAPE_CYLINDER = 2,
		SHAPE_WEDGE = 3,
		SHAPE_CORNER_WEDGE = 4,
		SHAPE_TRUSS = 5,
	};

	Roblox2016Part3D();
	~Roblox2016Part3D() override = default;

	void set_shape_type(int p_shape);
	int get_shape_type() const;

	void set_part_size(const Vector3 &p_size);
	Vector3 get_part_size() const;

	void set_anchored(bool p_anchored);
	bool get_anchored() const;

	void set_can_collide(bool p_can_collide);
	bool get_can_collide() const;

	void set_density(double p_density);
	double get_density() const;

	void set_specific_gravity(double p_specific_gravity);
	double get_specific_gravity() const;

	double get_approx_volume() const;
	void rebuild_geometry();
	void _vextoria_ready();

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	int _shape_type = SHAPE_BLOCK;
	Vector3 _part_size = Vector3(4.0, 1.2, 2.0);
	bool _anchored = false;
	bool _can_collide = true;
	double _density = 0.7;
	double _specific_gravity = 0.7;
	MeshInstance3D *_mesh_instance = nullptr;
	CollisionShape3D *_collision_shape = nullptr;

	Ref<ArrayMesh> _build_mesh() const;
	void _ensure_children();
	void _update_mass();
};


VARIANT_ENUM_CAST(Roblox2016Part3D::ShapeType);

#endif
