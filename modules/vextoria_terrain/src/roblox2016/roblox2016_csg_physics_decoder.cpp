#include "roblox2016_csg_physics_decoder.h"

#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"

#include <cstdint>
#include <cstring>
#include <limits>


namespace {

constexpr uint8_t CSGPHS_MAGIC[6] = { 'C', 'S', 'G', 'P', 'H', 'S' };
constexpr uint8_t CSGPHS3_MESH_MAGIC[40] = {
	0x10, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
};

constexpr uint32_t MAX_COMPONENT_COUNT = 12u * 1024u * 1024u;

static bool has_bytes(const PackedByteArray &p_data, int64_t p_offset, int64_t p_count) {
	if (p_offset < 0 || p_count < 0) return false;
	if (p_offset > p_data.size()) return false;
	return p_count <= p_data.size() - p_offset;
}

static bool matches(const PackedByteArray &p_data, int64_t p_offset, const uint8_t *p_expected, int64_t p_count) {
	if (!has_bytes(p_data, p_offset, p_count)) return false;
	const uint8_t *src = p_data.ptr() + p_offset;
	return std::memcmp(src, p_expected, static_cast<size_t>(p_count)) == 0;
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
	static_assert(sizeof(float) == sizeof(uint32_t), "Expected IEEE-754 32-bit float.");
	std::memcpy(&r_value, &raw, sizeof(float));
	return true;
}

static Dictionary fail_result(int p_version, const String &p_error) {
	Dictionary result;
	result["ok"] = false;
	result["version"] = p_version;
	result["error"] = p_error;
	result["hulls"] = Array();
	return result;
}

} // namespace

int Roblox2016CSGPhysicsDecoder::get_version(const PackedByteArray &p_data) const {
	if (!matches(p_data, 0, CSGPHS_MAGIC, 6) || p_data.size() < 10) return -1;
	int64_t offset = 6;
	uint32_t version = 0;
	if (!read_u32(p_data, offset, version)) return -1;
	if (version > static_cast<uint32_t>(std::numeric_limits<int>::max())) return -1;
	return static_cast<int>(version);
}

bool Roblox2016CSGPhysicsDecoder::can_decode(const PackedByteArray &p_data) const {
	const int version = get_version(p_data);
	return version == 3 || version == 5;
}

Dictionary Roblox2016CSGPhysicsDecoder::decode(const PackedByteArray &p_data) const {
	const int version = get_version(p_data);
	if (version < 0) {
		return fail_result(-1, "PhysicsData does not begin with a valid CSGPHS header.");
	}
	if (version != 3 && version != 5) {
		return fail_result(version, "Unsupported CSGPHS version. Roblox 2016 compatibility currently decodes v3/v5 hull streams.");
	}

	int64_t offset = 10;
	Array hulls;
	while (offset < p_data.size()) {
		if (!matches(p_data, offset, CSGPHS3_MESH_MAGIC, 40)) {
			return fail_result(version, "Malformed CSGPHS v3/v5 mesh preamble.");
		}
		offset += 40;

		uint32_t pos_component_count = 0;
		uint32_t vertex_width = 0;
		if (!read_u32(p_data, offset, pos_component_count) || !read_u32(p_data, offset, vertex_width)) {
			return fail_result(version, "Truncated CSGPHS vertex header.");
		}
		if (vertex_width != 4) {
			return fail_result(version, "Unsupported CSGPHS vertex scalar width.");
		}
		if (pos_component_count == 0 || pos_component_count % 3 != 0 || pos_component_count > MAX_COMPONENT_COUNT) {
			return fail_result(version, "Invalid CSGPHS position component count.");
		}

		const uint32_t vertex_count = pos_component_count / 3;
		const int64_t vertex_bytes = static_cast<int64_t>(vertex_count) * 12;
		if (!has_bytes(p_data, offset, vertex_bytes)) {
			return fail_result(version, "Truncated CSGPHS vertex payload.");
		}

		PackedVector3Array positions;
		positions.resize(vertex_count);
		for (uint32_t i = 0; i < vertex_count; ++i) {
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
			if (!read_f32(p_data, offset, x) || !read_f32(p_data, offset, y) || !read_f32(p_data, offset, z)) {
				return fail_result(version, "Truncated CSGPHS vertex.");
			}
			positions.set(i, Vector3(x, y, z));
		}

		uint32_t face_component_count = 0;
		if (!read_u32(p_data, offset, face_component_count)) {
			return fail_result(version, "Truncated CSGPHS face header.");
		}
		if (face_component_count == 0 || face_component_count % 3 != 0 || face_component_count > MAX_COMPONENT_COUNT) {
			return fail_result(version, "Invalid CSGPHS face component count.");
		}
		const uint32_t triangle_count = face_component_count / 3;
		const int64_t index_bytes = static_cast<int64_t>(face_component_count) * 4;
		if (!has_bytes(p_data, offset, index_bytes)) {
			return fail_result(version, "Truncated CSGPHS face payload.");
		}

		PackedInt32Array indices;
		indices.resize(face_component_count);
		for (uint32_t i = 0; i < face_component_count; ++i) {
			uint32_t index = 0;
			if (!read_u32(p_data, offset, index)) {
				return fail_result(version, "Truncated CSGPHS face index.");
			}
			if (index >= vertex_count || index > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
				return fail_result(version, "CSGPHS face index is outside the hull vertex array.");
			}
			indices.set(i, static_cast<int32_t>(index));
		}

		Dictionary hull;
		hull["positions"] = positions;
		hull["indices"] = indices;
		hull["vertex_count"] = static_cast<int64_t>(vertex_count);
		hull["triangle_count"] = static_cast<int64_t>(triangle_count);
		hulls.push_back(hull);
	}

	if (hulls.is_empty()) {
		return fail_result(version, "CSGPHS stream contains no convex hulls.");
	}

	Dictionary result;
	result["ok"] = true;
	result["version"] = version;
	result["error"] = String();
	result["hulls"] = hulls;
	result["hull_count"] = hulls.size();
	return result;
}

Array Roblox2016CSGPhysicsDecoder::create_convex_shapes(const PackedByteArray &p_data) const {
	Array shapes;
	Dictionary decoded = decode(p_data);
	if (!static_cast<bool>(decoded.get("ok", false))) return shapes;

	const Array hulls = decoded.get("hulls", Array());
	for (int64_t i = 0; i < hulls.size(); ++i) {
		if (hulls[i].get_type() != Variant::DICTIONARY) continue;
		const Dictionary hull = hulls[i];
		const PackedVector3Array positions = hull.get("positions", PackedVector3Array());
		if (positions.size() < 4) continue;

		Ref<ConvexPolygonShape3D> shape;
		shape.instantiate();
		shape->set_points(positions);
		shapes.push_back(shape);
	}
	return shapes;
}

void Roblox2016CSGPhysicsDecoder::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_version", "data"), &Roblox2016CSGPhysicsDecoder::get_version);
	ClassDB::bind_method(D_METHOD("can_decode", "data"), &Roblox2016CSGPhysicsDecoder::can_decode);
	ClassDB::bind_method(D_METHOD("decode", "data"), &Roblox2016CSGPhysicsDecoder::decode);
	ClassDB::bind_method(D_METHOD("create_convex_shapes", "data"), &Roblox2016CSGPhysicsDecoder::create_convex_shapes);
}

