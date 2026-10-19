#ifndef VEXTORIA_ROBLOX2016_CSG_COMPILER_H
#define VEXTORIA_ROBLOX2016_CSG_COMPILER_H

#include "scene/main/node.h"
#include "scene/resources/mesh.h"
#include "scene/resources/3d/shape_3d.h"
#include "core/variant/dictionary.h"
#include "core/math/transform_3d.h"


class CSGCombiner3D;
class CSGShape3D;

/// Import-time boolean compiler for Roblox 2016 PartOperation trees.
///
/// Godot's CSG nodes are used only as a temporary evaluator. Once the boolean
/// result has been generated, callers keep the baked Mesh/Shape resources and
/// discard the live CSG hierarchy. This avoids paying CSG recomputation cost
/// during gameplay while still giving UnionOperation/NegateOperation a native
/// boolean implementation.
class Roblox2016CSGCompiler : public Node {
	GDCLASS(Roblox2016CSGCompiler, Node)

public:
	enum Operation {
		OP_UNION = 0,
		OP_INTERSECTION = 1,
		OP_SUBTRACTION = 2,
	};

	enum PrimitiveShape {
		SHAPE_BALL = 0,
		SHAPE_BLOCK = 1,
		SHAPE_CYLINDER = 2,
		SHAPE_WEDGE = 3,
		SHAPE_CORNER_WEDGE = 4,
		SHAPE_TRUSS = 5,
	};

	enum CompileStatus {
		STATUS_EMPTY = 0,
		STATUS_READY = 1,
		STATUS_COMPILING = 2,
		STATUS_COMPLETE = 3,
		STATUS_FAILED = 4,
	};

	Roblox2016CSGCompiler();
	~Roblox2016CSGCompiler() override = default;

	/// Tree format:
	/// group:     { kind="group", operation=0..2, transform=Transform3D, children=Array }
	/// primitive: { kind="primitive", shape=0..5, operation=0..2, transform=Transform3D, size=Vector3 }
	/// mesh:      { kind="mesh", operation=0..2, transform=Transform3D, mesh=Mesh }
	bool set_operation_tree(const Dictionary &p_tree);
	void clear_operation_tree();
	bool begin_compile();
	void cancel_compile();

	int get_status() const;
	String get_last_error() const;
	Ref<Mesh> get_baked_mesh() const;
	Transform3D get_baked_transform() const;
	Ref<Shape3D> create_collision_shape(bool p_dynamic_body = false) const;
	int get_operand_count() const;

	void _vextoria_process(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	CSGCombiner3D *_root = nullptr;
	Ref<Mesh> _baked_mesh;
	Transform3D _baked_transform;
	String _last_error;
	int _status = STATUS_EMPTY;
	int _pending_frames = 0;
	int _compile_retries = 0;
	int _operand_count = 0;

	CSGShape3D *_build_node(const Dictionary &p_desc, bool p_is_root = false);
	CSGShape3D *_build_primitive(const Dictionary &p_desc);
	CSGShape3D *_build_mesh_operand(const Dictionary &p_desc);
	void _set_failure(const String &p_error);
	void _dispose_root();
};


VARIANT_ENUM_CAST(Roblox2016CSGCompiler::Operation);
VARIANT_ENUM_CAST(Roblox2016CSGCompiler::PrimitiveShape);
VARIANT_ENUM_CAST(Roblox2016CSGCompiler::CompileStatus);

#endif
