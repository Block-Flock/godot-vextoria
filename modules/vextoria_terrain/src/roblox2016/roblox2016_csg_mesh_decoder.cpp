#include "roblox2016_csg_mesh_decoder.h"

#include "scene/resources/mesh.h"
#include "scene/resources/mesh.h"
#include "core/object/class_db.h"
#include "core/variant/array.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>


namespace {

constexpr uint8_t XOR_CYCLE[31] = {
	86, 46, 110, 88, 49, 32, 48, 4, 52, 105, 12, 119, 12, 1, 94, 0,
	26, 96, 55, 105, 29, 82, 43, 7, 79, 36, 89, 101, 83, 4, 122,
};
constexpr uint32_t CLASSIC_VERTEX_STRIDE = 84;
constexpr uint32_t MAX_VERTEX_COUNT = 4u * 1024u * 1024u;
constexpr uint32_t MAX_INDEX_COUNT = 12u * 1024u * 1024u;

static bool has_bytes(const PackedByteArray &p_data, int64_t p_offset, int64_t p_count) {
	return p_offset >= 0 && p_count >= 0 && p_offset <= p_data.size() && p_count <= p_data.size() - p_offset;
}

static PackedByteArray deobfuscate(const PackedByteArray &p_data) {
	PackedByteArray out;
	out.resize(p_data.size());
	for (int64_t i = 0; i < p_data.size(); ++i) {
		out.set(i, p_data[i] ^ XOR_CYCLE[i % 31]);
	}
	return out;
}

static bool read_u32(const PackedByteArray &p_data, int64_t &r_offset, uint32_t &r_value) {
	if (!has_bytes(p_data, r_offset, 4)) return false;
	const uint8_t *src = p_data.ptr() + r_offset;
	r_value = static_cast<uint32_t>(src[0])
		| (static_cast<uint32_t>(src[1]) << 8)
		| (static_cast<uint32_t>(src[2]) << 16)
		| (static_cast<uint32_t>(src[3]) << 24);
	r_offset += 4;
	return true;
}

static bool read_f32(const PackedByteArray &p_data, int64_t &r_offset, float &r_value) {
	uint32_t raw = 0;
	if (!read_u32(p_data, r_offset, raw)) return false;
	std::memcpy(&r_value, &raw, sizeof(float));
	return true;
}

static bool read_vec2(const PackedByteArray &p_data, int64_t &r_offset, Vector2 &r_value) {
	float x = 0.0f, y = 0.0f;
	if (!read_f32(p_data, r_offset, x) || !read_f32(p_data, r_offset, y)) return false;
	r_value = Vector2(x, y);
	return true;
}

static bool read_vec3(const PackedByteArray &p_data, int64_t &r_offset, Vector3 &r_value) {
	float x = 0.0f, y = 0.0f, z = 0.0f;
	if (!read_f32(p_data, r_offset, x) || !read_f32(p_data, r_offset, y) || !read_f32(p_data, r_offset, z)) return false;
	r_value = Vector3(x, y, z);
	return true;
}

static bool has_magic(const PackedByteArray &p_data, const char *p_magic, int p_length) {
	if (!has_bytes(p_data, 0, p_length)) return false;
	return std::memcmp(p_data.ptr(), p_magic, static_cast<size_t>(p_length)) == 0;
}

static Dictionary failure(int p_version, const String &p_error) {
	Dictionary result;
	result["ok"] = false;
	result["version"] = p_version;
	result["error"] = p_error;
	return result;
}

static PackedVector3Array recalculate_normals_from_winding(
		const PackedVector3Array &p_vertices,
		const PackedInt32Array &p_indices,
		const PackedVector3Array &p_fallback_normals) {
	std::vector<Vector3> accumulated(static_cast<size_t>(p_vertices.size()), Vector3());
	std::vector<float> weights(static_cast<size_t>(p_vertices.size()), 0.0f);

	// Rebuild normals from the final triangle order that is sent to Godot.
	// CSGMDL's source normals belong to the pre-conversion winding; retaining
	// them after the index swap makes every Lantern face disagree with the
	// actual front-face geometry. Area-weighted accumulation also handles
	// shared vertices without applying a blind global sign.
	for (int64_t i = 0; i + 2 < p_indices.size(); i += 3) {
		const int32_t ia = p_indices[i];
		const int32_t ib = p_indices[i + 1];
		const int32_t ic = p_indices[i + 2];
		const Vector3 face = (p_vertices[ib] - p_vertices[ia]).cross(p_vertices[ic] - p_vertices[ia]);
		const float area = face.length();
		if (area <= 0.000001f) continue;

		accumulated[static_cast<size_t>(ia)] += face;
		accumulated[static_cast<size_t>(ib)] += face;
		accumulated[static_cast<size_t>(ic)] += face;
		weights[static_cast<size_t>(ia)] += area;
		weights[static_cast<size_t>(ib)] += area;
		weights[static_cast<size_t>(ic)] += area;
	}

	PackedVector3Array result;
	result.resize(p_vertices.size());
	for (int64_t i = 0; i < p_vertices.size(); ++i) {
		Vector3 normal = accumulated[static_cast<size_t>(i)];
		if (weights[static_cast<size_t>(i)] > 0.0f && normal.length_squared() > 0.000001f) {
			result.set(i, normal.normalized());
		} else if (i < p_fallback_normals.size() && p_fallback_normals[i].length_squared() > 0.000001f) {
			result.set(i, p_fallback_normals[i].normalized());
		} else {
			result.set(i, Vector3(0.0f, 1.0f, 0.0f));
		}
	}
	return result;
}

} // namespace

int Roblox2016CSGMeshDecoder::get_version(const PackedByteArray &p_data) const {
	if (p_data.size() < 10) return -1;
	PackedByteArray decoded = deobfuscate(p_data);
	if (!has_magic(decoded, "CSGMDL", 6)) return -1;
	int64_t offset = 6;
	uint32_t version = 0;
	if (!read_u32(decoded, offset, version) || version > static_cast<uint32_t>(std::numeric_limits<int>::max())) return -1;
	return static_cast<int>(version);
}

bool Roblox2016CSGMeshDecoder::can_decode(const PackedByteArray &p_data) const {
	const int version = get_version(p_data);
	return version == 2 || version == 4;
}

Dictionary Roblox2016CSGMeshDecoder::decode(const PackedByteArray &p_data) const {
	PackedByteArray data = deobfuscate(p_data);
	if (!has_magic(data, "CSGMDL", 6)) return failure(-1, "MeshData does not decode to a CSGMDL header.");

	int64_t offset = 6;
	uint32_t version_u32 = 0;
	if (!read_u32(data, offset, version_u32)) return failure(-1, "Truncated CSGMDL version.");
	const int version = static_cast<int>(version_u32);
	if (version != 2 && version != 4) return failure(version, "Unsupported CSGMDL version; classic decoder supports v2/v4.");

	// 16-byte content hash + 16 bytes of legacy salt/unknown data.
	if (!has_bytes(data, offset, 32)) return failure(version, "Truncated CSGMDL hash block.");
	offset += 32;

	uint32_t vertex_count = 0;
	uint32_t vertex_stride = 0;
	if (!read_u32(data, offset, vertex_count) || !read_u32(data, offset, vertex_stride))
		return failure(version, "Truncated CSGMDL vertex header.");
	if (vertex_stride != CLASSIC_VERTEX_STRIDE)
		return failure(version, "Unexpected CSGMDL vertex stride; expected classic 84-byte vertices.");
	if (vertex_count == 0 || vertex_count > MAX_VERTEX_COUNT)
		return failure(version, "Invalid CSGMDL vertex count.");
	if (!has_bytes(data, offset, static_cast<int64_t>(vertex_count) * CLASSIC_VERTEX_STRIDE))
		return failure(version, "Truncated CSGMDL vertex payload.");

	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedVector2Array uvs;
	vertices.resize(vertex_count);
	normals.resize(vertex_count);
	colors.resize(vertex_count);
	uvs.resize(vertex_count);

	for (uint32_t i = 0; i < vertex_count; ++i) {
		const int64_t vertex_start = offset;
		Vector3 position, normal, tangent;
		Vector2 uv;
		if (!read_vec3(data, offset, position) || !read_vec3(data, offset, normal))
			return failure(version, "Truncated CSGMDL vertex position/normal.");
		if (!has_bytes(data, offset, 4)) return failure(version, "Truncated CSGMDL vertex color.");
		const uint8_t r = data[offset + 0];
		const uint8_t g = data[offset + 1];
		const uint8_t b = data[offset + 2];
		const uint8_t a = data[offset + 3];
		offset += 4;

		uint32_t normal_id = 0;
		if (!read_u32(data, offset, normal_id) || !read_vec2(data, offset, uv))
			return failure(version, "Truncated CSGMDL vertex UV metadata.");
		(void)normal_id;

		// Legacy v2/v4 vertex layout contains 16 reserved zero bytes, a tangent
		// vector, and another 16-byte reserved block.
		if (!has_bytes(data, offset, 16)) return failure(version, "Truncated CSGMDL vertex reserved block.");
		offset += 16;
		if (!read_vec3(data, offset, tangent)) return failure(version, "Truncated CSGMDL tangent.");
		if (!has_bytes(data, offset, 16)) return failure(version, "Truncated CSGMDL vertex tail.");
		offset += 16;
		(void)tangent;

		if (offset - vertex_start != CLASSIC_VERTEX_STRIDE)
			return failure(version, "CSGMDL vertex layout did not consume 84 bytes.");
		vertices.set(i, position);
		normals.set(i, normal.normalized());
		colors.set(i, Color(
			static_cast<float>(r) / 255.0f,
			static_cast<float>(g) / 255.0f,
			static_cast<float>(b) / 255.0f,
			static_cast<float>(a) / 255.0f));
		uvs.set(i, uv);
	}

	uint32_t index_count = 0;
	if (!read_u32(data, offset, index_count)) return failure(version, "Truncated CSGMDL index header.");
	if (index_count == 0 || index_count % 3 != 0 || index_count > MAX_INDEX_COUNT)
		return failure(version, "Invalid CSGMDL triangle index count.");
	if (!has_bytes(data, offset, static_cast<int64_t>(index_count) * 4))
		return failure(version, "Truncated CSGMDL index payload.");

	PackedInt32Array indices;
	indices.resize(index_count);
	for (uint32_t i = 0; i < index_count; ++i) {
		uint32_t index = 0;
		if (!read_u32(data, offset, index)) return failure(version, "Truncated CSGMDL index.");
		if (index >= vertex_count || index > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
			return failure(version, "CSGMDL triangle references a vertex outside the vertex array.");
		indices.set(i, static_cast<int32_t>(index));
	}
	// Roblox's classic mesh winding is opposite Godot's front-face convention.
	// Keep the source vertex frame and swap only the index order. Normals are
	// recalculated below from this final order; the source normal stream is only
	// a fallback for degenerate vertices and must not be carried across the
	// winding conversion.
	for (uint32_t i = 0; i < index_count; i += 3) {
		const int32_t second = indices[i + 1];
		indices.set(i + 1, indices[i + 2]);
		indices.set(i + 2, second);
	}
	normals = recalculate_normals_from_winding(vertices, indices, normals);

	if (version == 4) {
		uint32_t trailing_count = 0;
		if (!read_u32(data, offset, trailing_count)) return failure(version, "Truncated CSGMDL v4 trailing table.");
		if (trailing_count > MAX_INDEX_COUNT || !has_bytes(data, offset, static_cast<int64_t>(trailing_count) * 4))
			return failure(version, "Invalid CSGMDL v4 trailing table.");
		offset += static_cast<int64_t>(trailing_count) * 4;
	}

	Dictionary result;
	result["ok"] = true;
	result["version"] = version;
	result["error"] = String();
	result["vertices"] = vertices;
	result["normals"] = normals;
	result["colors"] = colors;
	result["uvs"] = uvs;
	result["indices"] = indices;
	result["vertex_count"] = static_cast<int64_t>(vertex_count);
	result["triangle_count"] = static_cast<int64_t>(index_count / 3);
	result["bytes_consumed"] = offset;
	result["bytes_total"] = data.size();
	return result;
}

Ref<ArrayMesh> Roblox2016CSGMeshDecoder::create_array_mesh(const PackedByteArray &p_data, bool p_use_part_color) const {
	Dictionary decoded = decode(p_data);
	if (!static_cast<bool>(decoded.get("ok", false))) return Ref<ArrayMesh>();
	PackedColorArray colors = decoded.get("colors", PackedColorArray());
	if (p_use_part_color) {
		// GeometryGenerator.cpp keeps the authored part color when
		// UnionOperation.UsePartColor is true. The serialized CSG vertex colors
		// still exist, but Roblox does not multiply them into this surface.
		// Keep a neutral COLOR array so Vextoria's shared part shader can apply
		// the uniform part color without tinting it a second time.
		PackedColorArray neutral_colors;
		neutral_colors.resize(colors.size());
		for (int64_t i = 0; i < neutral_colors.size(); ++i)
			neutral_colors.set(i, Color(1.0f, 1.0f, 1.0f, 1.0f));
		colors = neutral_colors;
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = decoded.get("vertices", PackedVector3Array());
	arrays[Mesh::ARRAY_NORMAL] = decoded.get("normals", PackedVector3Array());
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_TEX_UV] = decoded.get("uvs", PackedVector2Array());
	arrays[Mesh::ARRAY_INDEX] = decoded.get("indices", PackedInt32Array());

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

void Roblox2016CSGMeshDecoder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_version", "data"), &Roblox2016CSGMeshDecoder::get_version);
	ClassDB::bind_method(D_METHOD("can_decode", "data"), &Roblox2016CSGMeshDecoder::can_decode);
	ClassDB::bind_method(D_METHOD("decode", "data"), &Roblox2016CSGMeshDecoder::decode);
	ClassDB::bind_method(
		D_METHOD("create_array_mesh", "data", "use_part_color"),
		&Roblox2016CSGMeshDecoder::create_array_mesh,
		DEFVAL(false));
}

