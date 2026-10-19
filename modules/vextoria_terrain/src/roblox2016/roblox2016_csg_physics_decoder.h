#ifndef VEXTORIA_ROBLOX2016_CSG_PHYSICS_DECODER_H
#define VEXTORIA_ROBLOX2016_CSG_PHYSICS_DECODER_H

#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"


/// Decoder for legacy Roblox UnionOperation PhysicsData.
///
/// Roblox 2016-era CSGPHS v3/v5 stores one or more convex hull meshes in a
/// compact little-endian stream. The decoded hulls are suitable for ordinary
/// ConvexPolygonShape3D collision and do not require live CSG nodes at runtime.
class Roblox2016CSGPhysicsDecoder : public RefCounted {
	GDCLASS(Roblox2016CSGPhysicsDecoder, RefCounted)

public:
	Roblox2016CSGPhysicsDecoder() = default;
	~Roblox2016CSGPhysicsDecoder() override = default;

	int get_version(const PackedByteArray &p_data) const;
	bool can_decode(const PackedByteArray &p_data) const;
	Dictionary decode(const PackedByteArray &p_data) const;
	Array create_convex_shapes(const PackedByteArray &p_data) const;

protected:
	static void _bind_methods();
};


#endif
