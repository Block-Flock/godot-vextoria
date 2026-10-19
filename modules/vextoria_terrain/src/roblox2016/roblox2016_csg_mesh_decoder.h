#ifndef VEXTORIA_ROBLOX2016_CSG_MESH_DECODER_H
#define VEXTORIA_ROBLOX2016_CSG_MESH_DECODER_H

#include "scene/resources/mesh.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"


/// Decoder for legacy Roblox CSGMDL render meshes used by PartOperationAsset.
/// Supports the v2 and v4 layouts relevant to classic/2016-era solid models.
class Roblox2016CSGMeshDecoder : public RefCounted {
	GDCLASS(Roblox2016CSGMeshDecoder, RefCounted)

public:
	Roblox2016CSGMeshDecoder() = default;
	~Roblox2016CSGMeshDecoder() override = default;

	int get_version(const PackedByteArray &p_data) const;
	bool can_decode(const PackedByteArray &p_data) const;
	Dictionary decode(const PackedByteArray &p_data) const;
	Ref<ArrayMesh> create_array_mesh(const PackedByteArray &p_data, bool p_use_part_color = false) const;

protected:
	static void _bind_methods();
};


#endif
