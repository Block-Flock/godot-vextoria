/**************************************************************************/
/*  vextoria_instance.h                                                   */
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

#pragma once

#include "core/math/color.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/main/node.h"

class BoxMesh;
class BoxShape3D;
class ConvexPolygonShape3D;
class CollisionShape3D;
class MeshInstance3D;
class ArrayMesh;
class Mesh;
class Shape3D;
class StandardMaterial3D;
class Material;

// Non-spatial Roblox Instances (folders and scripts) belong in Godot's Node
// tree without acquiring a transform or a viewport gizmo.
class VextoriaInstance : public Node {
	GDCLASS(VextoriaInstance, Node);

	bool archivable = true;

protected:
	static void _bind_methods();

public:
	VextoriaInstance();
	bool is_archivable() const { return archivable; }
	void set_archivable(bool p_archivable) { archivable = p_archivable; }
	bool is_locked() const;
	void set_locked(bool p_locked);
	void reparent(RequiredParam<Node> p_parent, bool p_keep_global_transform = true) override;
};

// A Part is the physics body itself. Its visual and collision children are
// internal Godot implementation details, not additional Vextoria Instances.
class Part : public RigidBody3D {
	GDCLASS(Part, RigidBody3D);

	Vector3 size = Vector3(4, 1, 2);
	Color color = Color(0.639216, 0.635294, 0.647059);
	bool archivable = true;
	bool anchored = false;
	bool can_collide = true;
	MeshInstance3D *visual = nullptr;
	CollisionShape3D *collision = nullptr;
	Ref<BoxMesh> box_mesh;
	Ref<BoxShape3D> box_shape;
	Ref<ArrayMesh> wedge_mesh;
	Ref<ConvexPolygonShape3D> wedge_shape;
	Ref<StandardMaterial3D> material;
	Ref<Material> appearance_material;
	Ref<Material> applied_appearance;
	bool appearance_uses_color_uniform = false;
	void appearance_material_changed();
	void update_appearance_color();
	Ref<Mesh> geometry_mesh;
	Ref<Shape3D> geometry_collision;
	int shape_kind = 0;
	int material_kind = 0;
	bool builtin_wedge = false;
	bool external_geometry = false;
	void rebuild_wedge_geometry();
	void apply_external_geometry();

protected:
	static void _bind_methods();

public:
	Part();
	~Part();
	Vector3 get_size() const { return size; }
	void set_size(const Vector3 &p_size);
	Color get_color() const { return color; }
	void set_color(const Color &p_color);
	int get_shape_kind() const { return shape_kind; }
	void set_shape_kind(int p_kind);
	int get_material_kind() const { return material_kind; }
	void set_material_kind(int p_kind);
	bool is_archivable() const { return archivable; }
	void set_archivable(bool p_archivable) { archivable = p_archivable; }
	bool is_locked() const;
	void set_locked(bool p_locked);
	bool is_anchored() const { return anchored; }
	void set_anchored(bool p_anchored);
	bool get_can_collide() const { return can_collide; }
	void set_can_collide(bool p_can_collide);
	// Restore the native box geometry after a gameplay Part switches back from
	// an authored non-box shape. Size remains authoritative on this native node.
	void reset_builtin_geometry();
	void set_builtin_wedge_geometry();
	int get_builtin_geometry_kind() const { return external_geometry ? 2 : (builtin_wedge ? 1 : 0); }
	void set_builtin_geometry_kind(int p_kind);
	Ref<Mesh> get_geometry_mesh() const;
	void set_geometry_mesh(const Ref<Mesh> &p_mesh);
	Ref<Shape3D> get_geometry_collision() const;
	void set_geometry_collision(const Ref<Shape3D> &p_shape);
	Ref<Material> get_appearance_material() const;
	void set_appearance_material(const Ref<Material> &p_material);
};

// Roblox Model is a PVInstance: unlike Folder, it has a spatial frame.
class Model : public Node3D {
	GDCLASS(Model, Node3D);

	bool archivable = true;

protected:
	static void _bind_methods();

public:
	bool is_archivable() const { return archivable; }
	void set_archivable(bool p_archivable) { archivable = p_archivable; }
	bool is_locked() const;
	void set_locked(bool p_locked);
};

class Folder : public VextoriaInstance {
	GDCLASS(Folder, VextoriaInstance);

protected:
	static void _bind_methods() {}
};

class VextoriaScript : public VextoriaInstance {
	GDCLASS(VextoriaScript, VextoriaInstance);

	String source;
	bool disabled = false;

protected:
	static void _bind_methods();

public:
	String get_source() const { return source; }
	void set_source(const String &p_source) { source = p_source; }
	bool is_disabled() const { return disabled; }
	void set_disabled(bool p_disabled) { disabled = p_disabled; }
};
