#include "roblox_smooth_terrain_manager.h"
#include "roblox_smooth_material_reduce.h"
#include "terrain_types.h"

#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "core/config/engine.h"
#include "scene/resources/mesh.h"
#include "core/os/os.h"
#include "scene/resources/material.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/3d/physics/vextoria_collision_layers.h"
#include "core/os/time.h"
#include "scene/main/viewport.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>


// Mesher-local geometry types. They live outside the anonymous namespace so
// the async pipeline can pass them across thread boundaries through the
// manager's private method signatures.
using RbxCell = RobloxSmoothMipCell;

struct GridVertex {
	uint8_t tag = 0;
	uint8_t occupancy = 0;
};

struct BasicVertex {
	Vector3 position;
	uint8_t material = 0;
	uint32_t seed = 0;
	// Faces of the two-cell sampling apron touched by this vertex. Keeping this
	// per-face, rather than as one boolean, lets adjacent LOD leaves nominate a
	// single owner for a shared seam without submitting the whole apron twice.
	uint8_t border_mask = 0;
};

static constexpr uint8_t BORDER_X_NEG = 1u << 0;
static constexpr uint8_t BORDER_X_POS = 1u << 1;
static constexpr uint8_t BORDER_Y_NEG = 1u << 2;
static constexpr uint8_t BORDER_Y_POS = 1u << 3;
static constexpr uint8_t BORDER_Z_NEG = 1u << 4;
static constexpr uint8_t BORDER_Z_POS = 1u << 5;
static constexpr uint8_t BORDER_ALL = BORDER_X_NEG | BORDER_X_POS
		| BORDER_Y_NEG | BORDER_Y_POS | BORDER_Z_NEG | BORDER_Z_POS;

struct BasicMesh {
	std::vector<BasicVertex> vertices;
	std::vector<uint32_t> indices;
	// One tag per emitted triangle, recording the non-air volume on the side
	// that owns the face. Vertex material reduction cannot provide this at a
	// shoreline because shared vertices may also touch solid ground.
	std::vector<uint8_t> surface_tags;
	// One topological outward direction per emitted triangle. This is derived
	// directly from the tag transition that created the quad (higher tag toward
	// lower tag) and therefore survives smoothing, material deformation and the
	// Roblox->Godot coordinate reflection without relying on camera-facing
	// shader heuristics.
	std::vector<Vector3> outward_hints;
};

namespace {

static constexpr int CORNERS[8][3] = {
	{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
	{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
};

static constexpr int EDGES[12][2] = {
	{0, 1}, {1, 2}, {2, 3}, {3, 0},
	{4, 5}, {5, 6}, {6, 7}, {7, 4},
	{0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static size_t hash_combine_size(size_t seed, int value) {
	size_t v = static_cast<size_t>(value);
	return seed ^ (v + static_cast<size_t>(0x9e3779b9u) + (seed << 6) + (seed >> 2));
}

static size_t avalanche_size(size_t v) {
	v += ~(v << 15);
	v ^= v >> 10;
	v += v << 3;
	v ^= v >> 6;
	v += ~(v << 11);
	v ^= v >> 16;
	return v;
}

static uint32_t compute_seed(int x, int y, int z) {
	size_t seed = 0;
	seed = hash_combine_size(seed, x);
	seed = hash_combine_size(seed, y);
	seed = hash_combine_size(seed, z);
	return static_cast<uint32_t>(avalanche_size(seed));
}

static Vector3 round_components(const Vector3 &v) {
	auto round_one = [](float f) {
		return f < 0.0f ? static_cast<float>(static_cast<int>(f - 0.5f)) : static_cast<float>(static_cast<int>(f + 0.5f));
	};
	return Vector3(round_one(v.x), round_one(v.y), round_one(v.z));
}

static Vector3 deform_point(const Vector3 &smooth, float cell_size, uint8_t material, uint32_t seed) {
	Vector3 center(cell_size * 0.5f, cell_size * 0.5f, cell_size * 0.5f);
	float shift = 0.0f;
	if (material == 3) shift = 0.1f;
	else if (material == 8) shift = 0.3f;
	else if (material == 13) shift = 0.2f;
	else if (material == 15) shift = 0.1f;

	if (shift > 0.0f) {
		Vector3 random(
			static_cast<float>(seed & 255u) / 255.0f,
			static_cast<float>((seed >> 8) & 255u) / 255.0f,
			static_cast<float>((seed >> 16) & 255u) / 255.0f);
		return smooth + (random * 2.0f - Vector3(1, 1, 1)) * (shift * cell_size);
	}
	if (material == 5 || material == 7)
		return center;
	if (material == 4) {
		constexpr float quantum = 0.5f;
		return round_components((smooth - center) / quantum) * quantum + center;
	}
	if (material == 1)
		return Vector3(smooth.x, smooth.y - 0.5f, smooth.z);
	return smooth;
}

static int material_normal_type(uint8_t material) {
	if (material == 4 || material == 5 || material == 7) return 1;
	// Roblox-main materials.json is zero-based from Air in the table, so the
	// hard-soft materials are Rock=8 and Basalt=13. Ground=14 is soft.
	if (material == 8 || material == 13) return 2;
	return 0;
}

static Vector3 resolved_material_normal(
		const Vector3 &hard,
		const Vector3 &soft,
		uint8_t material) {
	int normal_type = material_normal_type(material);
	if (normal_type == 1) return hard;
	if (normal_type == 2) return hard.dot(soft) > 0.75f ? soft : hard;
	return soft;
}

static Vector3 source_vertex_normal(
		const Vector3 &hard,
		const Vector3 &soft,
		uint8_t material) {
	// Roblox-main's computeNormals returns a pair: the first vector is the
	// lighting normal and the second is only for terrain-atlas orientation.
	// Hard-soft materials therefore keep the hard face normal for lighting even
	// when the atlas is allowed to use the agreeing soft normal. Conflating these
	// two outputs makes Rock/Basalt shading differ from the source and can make
	// a recalculated normal appear inverted at a material boundary.
	return material_normal_type(material) == 0 ? soft : hard;
}

static Vector3 godot_to_roblox_frame(const Vector3 &normal) {
	// SmoothGrid and imported object CFrames now share the source XYZ frame.
	// Do not reflect the normal before choosing the Roblox atlas projection:
	// that would make material direction disagree with the visible terrain.
	return normal;
}

static int normal_segment_default(const Vector3 &normal) {
	if (normal.y > 0.9f) return 8;
	if (normal.y > 0.4f) {
		// Keep the upward-sloped segment range distinct from the side range.
		// Roblox-main's getNormalSegmentDefault returns 9..12 here; using 0..3
		// selects a side projection for sloped terrain and makes both the atlas
		// lighting and material direction visibly wrong.
		if (std::abs(normal.x) > std::abs(normal.z)) return 9 + (normal.x < 0.0f ? 1 : 0);
		return 9 + 2 + (normal.z < 0.0f ? 1 : 0);
	}
	if (normal.y < -0.8f) return 13;
	if (normal.y < -0.6f) {
		if (std::abs(normal.x) > std::abs(normal.z)) return 14 + (normal.x < 0.0f ? 1 : 0);
		return 16 + (normal.z < 0.0f ? 1 : 0);
	}
	const float ax = std::abs(normal.x);
	const float az = std::abs(normal.z);
	if (ax > az * 2.0f) return normal.x < 0.0f ? 1 : 0;
	if (az > ax * 2.0f) return 2 + (normal.z < 0.0f ? 1 : 0);
	return 4 + 2 * (normal.x < 0.0f ? 1 : 0) + (normal.z < 0.0f ? 1 : 0);
}

static int normal_segment_cube(const Vector3 &normal) {
	const float ax = std::abs(normal.x);
	const float az = std::abs(normal.z);
	if (normal.y > ax && normal.y > az) return 8;
	if (normal.y < -ax && normal.y < -az) return 13;
	if (ax > az) return normal.x < 0.0f ? 1 : 0;
	return 2 + (normal.z < 0.0f ? 1 : 0);
}

static int source_material_segment(uint8_t material, const Vector3 &normal) {
	return (material == 5 || material == 7) ? normal_segment_cube(normal) : normal_segment_default(normal);
}

static int source_material_layer(uint8_t material, const Vector3 &normal) {
	const int segment = source_material_segment(material, normal);
	// These layer indices and tiling groups are the fixed order in
	// ROBLOX-main/PlatformContent/pc/terrain/materials.json. Keeping the layer
	// selection in the mesh lets the shader sample the original atlas without
	// interpolating a raw material enum across a triangle.
	switch (material) {
		case 2: return segment >= 13 ? 2 : segment >= 8 ? 0 : 1; // Grass
		case 3: return 3; // Slate has no authored layer; use the shared stone tile.
		case 4: return segment >= 8 ? 4 : 5; // Concrete
		case 5: return 6; // Brick
		// These are the canonical atlas layer indices from MaterialTable.cpp.
		// Sand owns separate top/bottom and side layers, so every material after
		// it is shifted by one compared with the old table.
		case 6: return segment >= 8 ? 7 : 8; // Sand top/bottom, side
		case 7: return 9; // WoodPlanks
		case 8: return 10; // Rock
		case 9: return segment >= 13 ? 13 : segment >= 8 ? 11 : 12; // Glacier
		case 10: return 14; // Snow
		case 11: return segment >= 13 ? 17 : segment >= 8 ? 15 : 16; // Sandstone
		case 12: return 18; // Mud
		case 13: return 19; // Basalt
		case 14: return 20; // Ground
		case 15: return 21; // CrackedLava
		default: return 3;
	}
}

static Color material_color_rbx(uint8_t material) {
	switch (material) {
		case 1: return Color(0.24f, 0.50f, 0.81f, 0.55f);
		case 2: return Color(0.25f, 0.56f, 0.23f);
		case 3: return Color(0.36f, 0.39f, 0.42f);
		case 4: return Color(0.51f, 0.50f, 0.48f);
		case 5: return Color(0.55f, 0.25f, 0.18f);
		case 6: return Color(0.78f, 0.70f, 0.45f);
		case 7: return Color(0.47f, 0.31f, 0.18f);
		case 8: return Color(0.36f, 0.37f, 0.38f);
		case 9: return Color(0.70f, 0.83f, 0.88f);
		case 10: return Color(0.90f, 0.93f, 0.95f);
		case 11: return Color(0.64f, 0.48f, 0.31f);
		case 12: return Color(0.31f, 0.25f, 0.17f);
		case 13: return Color(0.20f, 0.21f, 0.22f);
		case 14: return Color(0.37f, 0.30f, 0.21f);
		case 15: return Color(0.41f, 0.16f, 0.09f);
		default: return Color(0.45f, 0.45f, 0.45f);
	}
}

// Solid terrain carries the Roblox material id in vertex color so the native
// mesh can use the same authored texture set as the compatibility voxel path.
// Water keeps its real translucent color and is rendered with the separate
// water material override below.
static Color material_index_color(uint8_t material) {
	return Color(std::clamp(static_cast<float>(material) / 15.0f, 0.0f, 1.0f), 0.0f, 0.0f, 1.0f);
}

static uint8_t tag_for_cell(const RbxCell &cell) {
	if (cell.material == 0 || cell.occupancy == 0) return 0;
	return cell.material == 1 ? 1 : 2;
}

static uint16_t edge_mask_for_cube(const std::array<GridVertex, 8> &corners) {
	uint16_t mask = 0;
	for (int i = 0; i < 12; i++) {
		if (corners[EDGES[i][0]].tag != corners[EDGES[i][1]].tag)
			mask |= static_cast<uint16_t>(1u << i);
	}
	return mask;
}

static std::pair<uint32_t, uint8_t> reduce_vertex_material(
		const std::pair<uint32_t, uint8_t> &a,
		const std::pair<uint32_t, uint8_t> &b) {
	const RobloxSmoothWeightedMaterial reduced = roblox_smooth_reduce_surface_material(
		{a.second, a.first}, {b.second, b.first});
	return {reduced.weight, reduced.material};
}

static BasicVertex make_vertex(
		const std::array<GridVertex, 8> &grid,
		const std::array<RbxCell, 8> &cells,
		uint16_t edge_mask,
		const Vector3 &corner,
		float cell_size,
		int local_x,
		int local_y,
		int local_z,
		int seed_x,
		int seed_y,
		int seed_z,
		int grid_size) {
	Vector3 average;
	int edge_count = 0;
	constexpr float occ_scale = 1.0f / 256.0f;
	for (int i = 0; i < 12; i++) {
		if ((edge_mask & (1u << i)) == 0) continue;
		int a = EDGES[i][0];
		int b = EDGES[i][1];
		float t = grid[a].tag > grid[b].tag
			? (static_cast<float>(grid[a].occupancy) + 1.0f) * occ_scale
			: 1.0f - (static_cast<float>(grid[b].occupancy) + 1.0f) * occ_scale;
		Vector3 pa(CORNERS[a][0] * cell_size, CORNERS[a][1] * cell_size, CORNERS[a][2] * cell_size);
		Vector3 pb(CORNERS[b][0] * cell_size, CORNERS[b][1] * cell_size, CORNERS[b][2] * cell_size);
		average += pa.lerp(pb, t);
		edge_count++;
	}
	if (edge_count > 0) average /= static_cast<float>(edge_count);

	// Match Mesher::generateVertex's fixed reduction tree. The zero-filled
	// entries are intentional: reduceMaterials treats them as empty material
	// candidates, and changing this to a sequential fold changes equal-weight
	// surface ties and can move a material boundary by a full voxel.
	std::array<std::pair<uint32_t, uint8_t>, 8> mats{};
	int mat_count = 0;
	for (int i = 0; i < 8; i++) {
		if (cells[i].material > 1) {
			mats[mat_count++] = {static_cast<uint32_t>(cells[i].occupancy) + 1u, cells[i].material};
		}
	}

	uint8_t material = 1;
	if (mat_count > 0) {
		std::pair<uint32_t, uint8_t> reduced = reduce_vertex_material(
			reduce_vertex_material(mats[0], mats[1]),
			reduce_vertex_material(mats[2], mats[3]));
		if (mat_count > 4) {
			reduced = reduce_vertex_material(
				reduced,
				reduce_vertex_material(
					reduce_vertex_material(mats[4], mats[5]),
					reduce_vertex_material(mats[6], mats[7])));
		}
		material = reduced.second;
	}

	uint32_t seed = compute_seed(seed_x, seed_y, seed_z);
	Vector3 point = deform_point(average, cell_size, material, seed);
	point.x = std::clamp(point.x, 0.0f, cell_size);
	point.y = std::clamp(point.y, 0.0f, cell_size);
	point.z = std::clamp(point.z, 0.0f, cell_size);
	uint8_t border_mask = 0;
	if (local_x == 0) border_mask |= BORDER_X_NEG;
	if (local_x == grid_size - 2) border_mask |= BORDER_X_POS;
	if (local_y == 0) border_mask |= BORDER_Y_NEG;
	if (local_y == grid_size - 2) border_mask |= BORDER_Y_POS;
	if (local_z == 0) border_mask |= BORDER_Z_NEG;
	if (local_z == grid_size - 2) border_mask |= BORDER_Z_POS;
	// TerrainData is already in the reflected Godot cell frame: the SmoothGrid
	// importer performs the Roblox->Godot X reflection before the native backend
	// is populated. Emitting directly from that frame avoids reflecting terrain
	// a second time and keeps geometry, streaming keys, and object CFrames aligned.
	return {corner + point + Vector3(0.5f, 0.5f, 0.5f), material, seed, border_mask};
}

static void push_quad(
	BasicMesh &mesh,
	uint32_t v0,
	uint32_t v1,
	uint32_t v2,
	uint32_t v3,
	bool flip,
	uint8_t surface_tag,
	const Vector3 &outward_hint) {
	if (v0 >= mesh.vertices.size() || v1 >= mesh.vertices.size() || v2 >= mesh.vertices.size() || v3 >= mesh.vertices.size())
		return;

	// Match Roblox 2016 App/voxel2/Mesher.cpp::pushQuad. The diagonal is
	// selected from the four generated vertex materials to minimize material
	// interpolation artifacts; `flip` is independently determined by which
	// tagged volume owns the face. Conflating these two decisions twists mixed-
	// material slopes and makes the atlas blend look scrambled.
	const uint8_t m0 = mesh.vertices[v0].material;
	const uint8_t m1 = mesh.vertices[v1].material;
	const uint8_t m2 = mesh.vertices[v2].material;
	const uint8_t m3 = mesh.vertices[v3].material;
	const bool flip_diagonal = (m1 == m3) && (m1 == m2 || m1 == m0);

	const uint32_t v[4] = {v0, v1, v2, v3};
	static constexpr int order[2][2][6] = {
		{{1, 0, 2, 2, 0, 3}, {1, 2, 0, 0, 2, 3}},
		{{0, 3, 1, 1, 3, 2}, {0, 1, 3, 3, 1, 2}},
	};
	const int *o = order[flip_diagonal ? 1 : 0][flip ? 1 : 0];
	for (int i = 0; i < 6; i += 3) {
		mesh.indices.push_back(v[o[i]]);
		mesh.indices.push_back(v[o[i + 1]]);
		mesh.indices.push_back(v[o[i + 2]]);
		mesh.surface_tags.push_back(surface_tag);
		mesh.outward_hints.push_back(outward_hint);
	}
}

static bool triangle_has_water(const BasicMesh &basic, uint32_t tri) {
	if (tri * 3u + 2u >= basic.indices.size()) return false;
	for (int corner = 0; corner < 3; corner++) {
		const uint32_t vertex = basic.indices[tri * 3u + static_cast<uint32_t>(corner)];
		if (vertex < basic.vertices.size() && basic.vertices[vertex].material == 1) return true;
	}
	return false;
}

static uint8_t triangle_border_mask(const BasicMesh &basic, uint32_t tri) {
	if (tri * 3u + 2u >= basic.indices.size()) return BORDER_ALL;
	return static_cast<uint8_t>(basic.vertices[basic.indices[tri * 3u]].border_mask
		| basic.vertices[basic.indices[tri * 3u + 1u]].border_mask
		| basic.vertices[basic.indices[tri * 3u + 2u]].border_mask);
}

static void lod_key_bounds(const RobloxSmoothLodKey &key, int64_t r_min[3], int64_t r_max[3]) {
	const int64_t extent = RobloxSmoothLodOctreePolicy::cell_extent(key.level);
	r_min[0] = static_cast<int64_t>(key.x) * extent;
	r_min[1] = static_cast<int64_t>(key.y) * extent;
	r_min[2] = static_cast<int64_t>(key.z) * extent;
	r_max[0] = r_min[0] + extent;
	r_max[1] = r_min[1] + extent;
	r_max[2] = r_min[2] + extent;
}

static bool lod_leaf_touches_face(
		const RobloxSmoothLodKey &key,
		const RobloxSmoothLodKey &other,
		int axis,
		bool positive) {
	if (key == other) return false;
	int64_t key_min[3], key_max[3], other_min[3], other_max[3];
	lod_key_bounds(key, key_min, key_max);
	lod_key_bounds(other, other_min, other_max);
	if (positive ? other_min[axis] != key_max[axis] : other_max[axis] != key_min[axis])
		return false;
	for (int other_axis = 0; other_axis < 3; ++other_axis) {
		if (other_axis == axis) continue;
		// A face neighbor must overlap by area, not merely touch at an edge or
		// corner. This also handles a coarse leaf abutting several finer leaves.
		if (other_max[other_axis] <= key_min[other_axis]
				|| other_min[other_axis] >= key_max[other_axis])
			return false;
	}
	return true;
}

static bool has_lod_face_neighbor(
		const RobloxSmoothLodKey &key,
		int axis,
		bool positive,
		const std::unordered_set<RobloxSmoothLodKey, RobloxSmoothLodKeyHash> &needed) {
	for (const RobloxSmoothLodKey &other : needed) {
		if (lod_leaf_touches_face(key, other, axis, positive)) return true;
	}
	return false;
}

static uint8_t retained_border_mask_for_key(
		const RobloxSmoothLodKey &key,
		const std::unordered_set<RobloxSmoothLodKey, RobloxSmoothLodKeyHash> &needed) {
	uint8_t retained = 0;
	// If a neighboring leaf owns the other side of a seam, nominate the leaf
	// with the lower source coordinate as the sole border owner. That produces
	// one converged surface instead of either a gap (both shells culled) or
	// z-fighting (both shells submitted). Outer/world edges retain their cap.
	if (!has_lod_face_neighbor(key, 0, false, needed)) retained |= BORDER_X_NEG;
	retained |= BORDER_X_POS;
	if (!has_lod_face_neighbor(key, 1, false, needed)) retained |= BORDER_Y_NEG;
	retained |= BORDER_Y_POS;
	if (!has_lod_face_neighbor(key, 2, false, needed)) retained |= BORDER_Z_NEG;
	retained |= BORDER_Z_POS;
	return retained;
}

static BitField<Mesh::ArrayFormat> terrain_array_format() {
	uint64_t format = static_cast<uint64_t>(Mesh::ARRAY_FORMAT_VERTEX)
			| static_cast<uint64_t>(Mesh::ARRAY_FORMAT_NORMAL)
			| static_cast<uint64_t>(Mesh::ARRAY_FORMAT_COLOR)
			| static_cast<uint64_t>(Mesh::ARRAY_FORMAT_TEX_UV)
			| static_cast<uint64_t>(Mesh::ARRAY_FORMAT_CUSTOM0)
			| static_cast<uint64_t>(Mesh::ARRAY_FORMAT_CUSTOM1)
			| static_cast<uint64_t>(Mesh::ARRAY_FORMAT_CUSTOM2)
			| static_cast<uint64_t>(Mesh::ARRAY_FORMAT_CUSTOM3);
	format |= static_cast<uint64_t>(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
	format |= static_cast<uint64_t>(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT;
	format |= static_cast<uint64_t>(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM2_SHIFT;
	format |= static_cast<uint64_t>(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM3_SHIFT;
	return (BitField<Mesh::ArrayFormat>)format;
}

static void append_custom_rgba(PackedByteArray &array, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
	array.push_back(r);
	array.push_back(g);
	array.push_back(b);
	array.push_back(a);
}

// SurfaceTool is convenient but performs a Variant-heavy attribute update for
// every vertex. Imported SmoothGrid chunks have no shared vertex attributes
// (the barycentric weights and material samples are triangle-local), so the
// source-compatible representation is already a flat packed triangle stream.
// Build the same stream directly into ArrayMesh arrays; this keeps the exact
// normals, atlas selectors, seeds, and culling semantics while removing the
// per-vertex SurfaceTool dispatch overhead from every LOD chunk.
static Array emit_packed_arrays(
		const BasicMesh &basic,
		const std::vector<Vector3> &soft_normals,
		const std::vector<Vector3> &hard_normals,
		const std::vector<uint32_t> &triangles,
		bool water,
		bool reverse,
		uint8_t retained_border_mask) {
	if (triangles.empty()) return Array();
	// A shared border is retained by exactly one desired leaf; the other leaf
	// still samples the apron for stable vertices and normals but does not submit
	// duplicate faces. This avoids both the old gap and the full-apron z-fight.
	int64_t kept = 0;
	for (uint32_t tri : triangles) {
		if (tri * 3u + 2u >= basic.indices.size()) continue;
		if ((triangle_border_mask(basic, tri) & static_cast<uint8_t>(~retained_border_mask)) != 0) continue;
		kept++;
	}
	const int64_t vertex_count = kept * 3;
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colors;
	PackedVector2Array uvs;
	PackedByteArray custom0;
	PackedByteArray custom1;
	PackedByteArray custom2;
	PackedByteArray custom3;
	vertices.resize(vertex_count);
	normals.resize(vertex_count);
	colors.resize(vertex_count);
	uvs.resize(vertex_count);
	const Color vertex_color = water ? material_color_rbx(1) : Color(1, 1, 1, 1);
	int64_t output_vertex = 0;
	for (uint32_t tri : triangles) {
		if (tri * 3u + 2u >= basic.indices.size()) continue;
		if ((triangle_border_mask(basic, tri) & static_cast<uint8_t>(~retained_border_mask)) != 0)
			continue;
		const uint32_t first = basic.indices[tri * 3u];
		// Roblox's SmoothCluster indices are authored in the opposite winding
		// from Godot's front-face convention. The source solid/water triangle
		// therefore needs one winding conversion before it reaches ArrayMesh.
		// `reverse` is the intentional second copy Roblox adds at a water seam;
		// keep that copy opposite the primary copy after conversion.
		const bool flip_for_godot = !reverse;
		const uint32_t second = basic.indices[tri * 3u + (flip_for_godot ? 2u : 1u)];
		const uint32_t third = basic.indices[tri * 3u + (flip_for_godot ? 1u : 2u)];
		const uint32_t ids[3] = {first, second, third};
		int triangle_layers[3];
		int triangle_segments[3];
		Vector3 triangle_normals[3];
		for (int corner = 0; corner < 3; corner++) {
			const BasicVertex &v = basic.vertices[ids[corner]];
			const Vector3 texture_normal = godot_to_roblox_frame(
					resolved_material_normal(hard_normals[tri], soft_normals[ids[corner]], v.material));
			triangle_segments[corner] = source_material_segment(v.material, texture_normal);
			triangle_layers[corner] = source_material_layer(v.material, texture_normal);
			triangle_normals[corner] = source_vertex_normal(hard_normals[tri], soft_normals[ids[corner]], v.material);
		}
		for (int corner = 0; corner < 3; corner++, output_vertex++) {
			const BasicVertex &v = basic.vertices[ids[corner]];
			vertices.set(output_vertex, v.position);
			normals.set(output_vertex, triangle_normals[corner].normalized());
			colors.set(output_vertex, vertex_color);
			uvs.set(output_vertex, Vector2(v.position.x * 0.0625f, v.position.z * 0.0625f));
			append_custom_rgba(custom0, static_cast<uint8_t>(triangle_layers[0]),
					static_cast<uint8_t>(triangle_layers[1]), static_cast<uint8_t>(triangle_layers[2]));
			append_custom_rgba(custom1, corner == 0 ? 255 : 0, corner == 1 ? 255 : 0, corner == 2 ? 255 : 0);
			append_custom_rgba(custom2, static_cast<uint8_t>(triangle_segments[0]),
					static_cast<uint8_t>(triangle_segments[1]), static_cast<uint8_t>(triangle_segments[2]));
			append_custom_rgba(custom3, static_cast<uint8_t>(basic.vertices[ids[0]].seed & 0xffu),
					static_cast<uint8_t>(basic.vertices[ids[1]].seed & 0xffu),
					static_cast<uint8_t>(basic.vertices[ids[2]].seed & 0xffu));
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_CUSTOM0] = custom0;
	arrays[Mesh::ARRAY_CUSTOM1] = custom1;
	arrays[Mesh::ARRAY_CUSTOM2] = custom2;
	arrays[Mesh::ARRAY_CUSTOM3] = custom3;
	return arrays;
}

static Ref<ArrayMesh> emit_packed_surface(
		const BasicMesh &basic,
		const std::vector<Vector3> &soft_normals,
		const std::vector<Vector3> &hard_normals,
		const std::vector<uint32_t> &triangles,
		bool water,
		bool reverse,
		uint8_t retained_border_mask) {
	Array arrays = emit_packed_arrays(basic, soft_normals, hard_normals, triangles, water, reverse, retained_border_mask);
	if (arrays.is_empty()) return Ref<ArrayMesh>();
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), terrain_array_format());
	return mesh;
}

} // namespace

RobloxSmoothTerrainManager::RobloxSmoothTerrainManager() {
	set_process(true);
}

RobloxSmoothTerrainManager::~RobloxSmoothTerrainManager() {
	_shutdown_async_workers();
}

void RobloxSmoothTerrainManager::_ensure_async_workers() {
	if (!_async_workers.empty()) return;
	const unsigned int hw = static_cast<unsigned int>(std::max(1, OS::get_singleton()->get_processor_count()));
	// Built-in Vextoria uses Godot's platform thread abstraction so Android,
	// Linux and Windows can actually lower terrain-worker priority. This keeps
	// streaming from preempting rendering/physics on On5-class quad-core CPUs.
	const unsigned int count = hw <= 4 ? 1u : (hw <= 8 ? 2u : 4u);
	Thread::Settings settings;
	settings.priority = Thread::PRIORITY_LOW;
	for (unsigned int i = 0; i < count; i++) {
		Thread *worker = memnew(Thread);
		worker->start(&RobloxSmoothTerrainManager::_async_worker_entry, this, settings);
		_async_workers.push_back(worker);
	}
}

void RobloxSmoothTerrainManager::_shutdown_async_workers() {
	{
		std::lock_guard<std::mutex> guard(_async_mutex);
		_async_shutdown = true;
	}
	_async_work_cv.notify_all();
	for (Thread *worker : _async_workers) {
		if (worker == nullptr) continue;
		if (worker->is_started()) worker->wait_to_finish();
		memdelete(worker);
	}
	_async_workers.clear();
	std::lock_guard<std::mutex> guard(_async_mutex);
	_async_requests.clear();
	_async_results.clear();
	_async_in_flight.clear();
	_async_currently_needed.clear();
	_async_currently_needed_border_masks.clear();
	_async_shutdown = false;
}

void RobloxSmoothTerrainManager::_async_flush_and_wait() {
	// Data ownership transfer: pending requests would read the old TerrainData
	// (or die mid-write when we rebuild the mip cache). Drop queued work, let
	// any in-flight request finish, and discard results keyed to old revisions.
	std::unique_lock<std::mutex> lock(_async_mutex);
	_async_requests.clear();
	while (_async_active.load() != 0)
		_async_idle_cv.wait(lock);
	_async_results.clear();
}

void RobloxSmoothTerrainManager::_enqueue_async_chunk(const AsyncChunkRequest &p_request) {
	{
		std::lock_guard<std::mutex> guard(_async_mutex);
		if (_async_in_flight.find(p_request.key) != _async_in_flight.end())
			return;
		// Keep the producer close to the bounded main-thread install budget. Without
		// backpressure a large camera jump lets the worker pool finish hundreds of
		// meshes that then sit in _async_results while Godot uploads/cooks only a
		// few per frame, creating a delayed memory and frame-time spike.
		if (_async_in_flight.size() >= MAX_ASYNC_IN_FLIGHT)
			return;
		if (!_async_in_flight.insert(p_request.key).second)
			return;
		// Keep nearer work draining first without re-sorting the complete queue
		// for every chunk discovered during a streaming pass. On large worlds the
		// old push+stable_sort path turned camera movement into repeated O(n log n)
		// main-thread work before the worker pool could even start marching.
		auto insert_at = std::upper_bound(
				_async_requests.begin(), _async_requests.end(), p_request.distance,
				[](double distance, const AsyncChunkRequest &queued) {
					return distance < queued.distance;
				});
		_async_requests.insert(insert_at, p_request);
	}
	_async_work_cv.notify_one();
}

void RobloxSmoothTerrainManager::_async_worker_entry(void *p_userdata) {
	Thread::set_name("VextoriaTerrain");
	static_cast<RobloxSmoothTerrainManager *>(p_userdata)->_async_worker_main();
}

void RobloxSmoothTerrainManager::_async_worker_main() {
	for (;;) {
		AsyncChunkRequest request;
		{
			std::unique_lock<std::mutex> lock(_async_mutex);
			_async_work_cv.wait(lock, [&] { return _async_shutdown || !_async_requests.empty(); });
			if (_async_shutdown) return;
			if (_async_requests.empty()) continue;
			request = _async_requests.front();
			_async_requests.pop_front();
			_async_active.fetch_add(1);
		}

		AsyncChunkResult result;
		_compute_chunk_async(request, result);
		if (result.has_solid || result.has_water)
			_metric_meshes_generated.fetch_add(1, std::memory_order_relaxed);

		{
			std::lock_guard<std::mutex> guard(_async_mutex);
			_async_results.push_back(std::move(result));
		}
		_async_active.fetch_sub(1);
		_async_idle_cv.notify_all();
	}
}

RobloxSmoothTerrainManager::MeshPair RobloxSmoothTerrainManager::_instantiate_async_result(const AsyncChunkResult &p_result) {
	MeshPair pair;
	if (p_result.has_solid && !p_result.solid_arrays.is_empty()) {
		pair.solid.instantiate();
		pair.solid->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, p_result.solid_arrays, Array(), Dictionary(), terrain_array_format());
		if (p_result.has_solid_reverse && !p_result.solid_reverse_arrays.is_empty())
			pair.solid->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, p_result.solid_reverse_arrays, Array(), Dictionary(), terrain_array_format());
	}
	if (p_result.has_water && !p_result.water_arrays.is_empty()) {
		pair.water.instantiate();
		pair.water->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, p_result.water_arrays, Array(), Dictionary(), terrain_array_format());
	}
	return pair;
}

void RobloxSmoothTerrainManager::_drain_async_results(bool p_startup_burst) {
	// Roblox 2016 SmoothClusterLOD performs at most eight chunk updates in one
	// steady update pass. Do not inflate this to chase a backlog: spending 16 ms
	// or installing 32 leaves on the render thread can consume an entire 60 Hz
	// frame before gameplay, physics, scripts, or drawing run. Workers may stay
	// ahead in the background; old/new LOD coverage overlaps until the bounded
	// main-thread commit catches up.
	// Even startup is interactive on low-end hardware. Never let terrain commits
	// consume an entire 60 Hz frame just because a loading overlay is visible.
	// The worker pool may get arbitrarily far ahead; scene/GPU/physics ownership
	// crosses to the main thread in small slices only.
	const double budget_ms = p_startup_burst
		? std::clamp(_mesh_build_budget_ms * 2.0, 2.0, 4.0)
		: _mesh_build_budget_ms;
	const int install_count_limit = p_startup_burst ? 12 : 6;
	const uint64_t started_usec = Time::get_singleton()->get_ticks_usec();
	const uint64_t budget_usec = static_cast<uint64_t>(budget_ms * 1000.0);
	int installed = 0;
	// Bullet cooks the concave shape synchronously on install; that cost dwarfs
	// the mesh upload for near chunks, so cap physics installs per frame and
	// let visual-only chunks flow freely. Roblox avoided this by driving
	// chunk-local shapes lazily instead of per-frame Bullet cooking.
	int collision_installed = 0;
	// Collision shape creation is the most expensive remaining main-thread
	// operation. During motion admit only one new collision-bearing leaf per
	// pass; startup can populate two while the loading screen is still active.
	const int collision_install_limit = p_startup_burst ? 2 : 1;

	for (;;) {
		if (installed >= install_count_limit) break;
		if (installed > 0 && Time::get_singleton()->get_ticks_usec() - started_usec >= budget_usec) break;

		AsyncChunkResult result;
		{
			std::lock_guard<std::mutex> guard(_async_mutex);
			// Prefer visual-only results when the physics quota is spent so we
			// do not starve the visible world behind Bullet cooking.
			auto pick = std::find_if(_async_results.begin(), _async_results.end(),
					[&](const AsyncChunkResult &r) { return collision_installed < collision_install_limit || !r.collision; });
			if (pick == _async_results.end()) break;
			result = std::move(*pick);
			_async_results.erase(pick);
			_async_in_flight.erase(result.key);
		}

		if (result.revision != _data_revision) {
			_metric_stale_jobs_dropped.fetch_add(1, std::memory_order_relaxed);
			continue;
		}

		bool still_needed;
		bool seam_state_current;
		{
			std::lock_guard<std::mutex> guard(_async_mutex);
			still_needed = _async_currently_needed.find(result.key) != _async_currently_needed.end();
			auto mask = _async_currently_needed_border_masks.find(result.key);
			seam_state_current = mask != _async_currently_needed_border_masks.end()
					&& mask->second == result.retained_border_mask;
		}
		if (!seam_state_current) {
			_metric_stale_jobs_dropped.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		MeshPair meshes = _instantiate_async_result(result);
		// We hold meshes in the cache regardless of current visibility so a
		// player stepping back over the boundary does not recompute the march.
		_store_mesh_cache(result.key, meshes, result.retained_border_mask);
		if (!still_needed) {
			_metric_stale_jobs_dropped.fetch_add(1, std::memory_order_relaxed);
			continue;
		}

		if (_rendered.find(result.key) != _rendered.end())
			_remove_chunk(result.key);
		const uint64_t install_started_usec = Time::get_singleton()->get_ticks_usec();
		_install_async_chunk(result.key, result.collision, result, meshes);
		const uint64_t install_elapsed_usec = Time::get_singleton()->get_ticks_usec() - install_started_usec;
		_metric_install_usec.fetch_add(install_elapsed_usec, std::memory_order_relaxed);
		if (result.collision) {
			_metric_collision_installs.fetch_add(1, std::memory_order_relaxed);
			_metric_collision_install_usec.fetch_add(install_elapsed_usec, std::memory_order_relaxed);
		}
		_metric_meshes_installed.fetch_add(1, std::memory_order_relaxed);
		_dirty_render_keys.erase(result.key);
		installed++;
		if (result.collision)
			collision_installed++;
	}

	if (OS::get_singleton()->has_environment("VEXTORIA_STREAM_TRACE") && installed > 0) {
		int in_flight_count = 0;
		{
			std::lock_guard<std::mutex> guard(_async_mutex);
			in_flight_count = static_cast<int>(_async_in_flight.size());
		}
		print_line(vformat("[StreamDrain] installed=%d elapsedMs=%.3f startup=%d inFlight=%d",
				installed,
				(double)(Time::get_singleton()->get_ticks_usec() - started_usec) / 1000.0,
				p_startup_burst ? 1 : 0,
				in_flight_count));
	}
}

void RobloxSmoothTerrainManager::set_terrain_data(const Ref<TerrainData> &data) {
	_async_flush_and_wait();
	_data = data;
	// The managed importer populates TerrainData completely before assigning it
	// to the manager.  Retain that revision so the first process tick does not
	// treat the handoff as a second edit and schedule another full LOD-cache
	// rebuild before streaming can begin.
	_data_revision = _data.is_valid() ? static_cast<uint64_t>(_data->get_revision()) : 0;
	_lod_cache_dirty = true;
	_mesh_cache.clear();
	_mesh_cache_clock = 0;
	_startup_streaming = true;
	_streaming_complete = false;
	_force_stream_update = true;
	clear();
}

Ref<TerrainData> RobloxSmoothTerrainManager::get_terrain_data() const { return _data; }
void RobloxSmoothTerrainManager::set_view_distance(double distance) {
	_view_distance = std::max(32.0, distance);
	_adaptive_view_distance = _view_distance * _performance_distance_scale;
	_force_stream_update = true;
}
double RobloxSmoothTerrainManager::get_view_distance() const { return _view_distance; }
void RobloxSmoothTerrainManager::set_collision_enabled(bool enabled) { _collision_enabled = enabled; _force_stream_update = true; }
bool RobloxSmoothTerrainManager::get_collision_enabled() const { return _collision_enabled; }
void RobloxSmoothTerrainManager::set_collision_distance(double distance) { _collision_distance = std::max(0.0, distance); _force_stream_update = true; }
double RobloxSmoothTerrainManager::get_collision_distance() const { return _collision_distance; }
void RobloxSmoothTerrainManager::set_viewer_world_positions(const PackedVector3Array &positions) {
	bool changed = positions.size() != _viewer_world_positions.size();
	if (!changed) {
		for (int64_t i = 0; i < positions.size(); ++i) {
			if (positions[i].distance_squared_to(_viewer_world_positions[i]) >= 16.0f) {
				changed = true;
				break;
			}
		}
	}
	if (changed) {
		_viewer_world_positions = positions;
		_force_stream_update = true;
	}
}
void RobloxSmoothTerrainManager::set_mesh_build_budget_ms(double budget) { _mesh_build_budget_ms = std::clamp(budget, 0.25, 8.0); }
double RobloxSmoothTerrainManager::get_mesh_build_budget_ms() const { return _mesh_build_budget_ms; }
void RobloxSmoothTerrainManager::set_maximum_lod_level(int level) {
	_maximum_lod_level = std::clamp(level, 0, RobloxSmoothLodOctreePolicy::CHUNK_LEVELS - 1);
	_force_stream_update = true;
}
int RobloxSmoothTerrainManager::get_maximum_lod_level() const { return _maximum_lod_level; }
void RobloxSmoothTerrainManager::set_solid_material(const Ref<Material> &material) { _solid_material = material; }
Ref<Material> RobloxSmoothTerrainManager::get_solid_material() const { return _solid_material; }
void RobloxSmoothTerrainManager::set_water_material(const Ref<Material> &material) { _water_material = material; }
Ref<Material> RobloxSmoothTerrainManager::get_water_material() const { return _water_material; }

void RobloxSmoothTerrainManager::_initialize_materials() {
	if (_solid_material.is_null()) {
		Ref<StandardMaterial3D> material;
		material.instantiate();
		material->set_albedo(Color(1, 1, 1, 1));
		material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
		material->set_roughness(0.82f);
		_solid_material = material;
	}
	if (_water_material.is_null()) {
		Ref<StandardMaterial3D> material;
		material.instantiate();
		material->set_albedo(Color(1, 1, 1, 1));
		material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
		material->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
		material->set_roughness(0.12f);
		_water_material = material;
	}
}

void RobloxSmoothTerrainManager::_vextoria_ready() {
	_initialize_materials();
	_force_stream_update = true;
	// The camera is assigned by the runtime immediately before this node is
	// attached, but its local position is only authoritative once the first
	// process tick has run. Building here would select a second, origin-centered
	// terrain set and then throw it away on the next frame. Let _process sample
	// the real viewer first so the initial burst constructs only the requested
	// leaves.
}

void RobloxSmoothTerrainManager::_vextoria_process(double delta) {
	if (Engine::get_singleton()->is_editor_hint() || _data.is_null()) return;
	_consume_data_changes();
	_viewer_positions.clear();
	Viewport *viewport = get_viewport();
	// The client supplies authoritative player/spawn viewers. The viewport
	// camera is only a bootstrap fallback: including it alongside explicit
	// viewers creates a second, often origin-centered LOD volume and duplicates
	// mesh/collision work for terrain that no player is using.
	if (_viewer_world_positions.is_empty() && viewport != nullptr) {
		Camera3D *camera = viewport->get_camera_3d();
		if (camera != nullptr)
			// SmoothGrid cells are reflected into Godot space before TerrainData is
			// populated, so the camera, octree keys, and generated mesh all share the
			// same local frame. A second X reflection here inverts the LOD field and
			// makes distant chunks split while the visible side coarsens.
			_viewer_positions.push_back(to_local(camera->get_global_position()));
	}
	for (int64_t i = 0; i < _viewer_world_positions.size(); ++i)
		_viewer_positions.push_back(to_local(_viewer_world_positions[i]));
	if (_viewer_positions.empty())
		_viewer_positions.push_back(_viewer_position);
	_viewer_position = _viewer_positions.front();
	_stream_accumulator += delta;
	_performance_sample_accumulator += delta;

	// Treat the configured view distance as a quality target, not a hard
	// visibility cutoff. Stronger machines can carry the detailed LOD ring
	// farther out; slower machines reduce only that detailed ring while the far
	// terrain remains resident at coarse LOD.
	if (_performance_sample_accumulator >= 1.0) {
		_performance_sample_accumulator = 0.0;
		const int fps = Engine::get_singleton()->get_frames_per_second();
		float target_scale = 1.0f;
		if (fps >= 100) target_scale = 1.75f;
		else if (fps >= 75) target_scale = 1.5f;
		else if (fps >= 58) target_scale = 1.25f;
		else if (fps < 42) target_scale = 0.8f;

		// Hysteresis avoids rebuilding the octree selection because of tiny FPS
		// oscillations around a threshold.
		if (Math::abs(target_scale - _performance_distance_scale) >= 0.2f) {
			_performance_distance_scale = target_scale;
			_adaptive_view_distance = std::max(32.0, _view_distance * (double)_performance_distance_scale);
			_force_stream_update = true;
		}
	}

	// Estimate viewer speed between streaming decisions so the trace can
	// tie LOD churn/FPS drops to actual motion.
	if (_last_stream_position == _viewer_position) {
		_viewer_speed = _viewer_speed * 0.95f;
	} else {
		const float dt = (float)std::max(1e-4, _stream_accumulator);
		_viewer_speed = Math::lerp(_viewer_speed,
				(float)(_last_stream_position.distance_to(_viewer_position) / dt), 0.5f);
	}
	const float movement_distance_squared = _last_stream_position.distance_squared_to(_viewer_position);
	bool moved = movement_distance_squared >= 16.0f;
	// Camera transforms arrive every render frame, but SmoothCluster streaming
	// does not need to rebuild its leaf set at that cadence. Re-entering the
	// selector for every small movement causes avoidable LOD split/merge churn,
	// especially on software/OpenGL and low-end mobile renderers. Keep the old
	// coverage visible while a player moves through the short interval, then
	// refresh often enough that a normal walk does not outrun the requested LOD.
	const bool movement_refresh_due = moved
			&& (_stream_accumulator >= 0.12 || movement_distance_squared >= 1024.0f);
	// During the first native handoff, keep consuming the bounded mesh budget on
	// every frame. Waiting for the steady-state 350 ms streaming tick here turns
	// a finite import into a minutes-long construction stall. Once the desired
	// set is complete, movement and the normal periodic refresh use the Roblox-like
	// streaming cadence again.
	if (_force_stream_update || !_streaming_complete || movement_refresh_due || _stream_accumulator >= 0.35) {
		_stream_accumulator = 0.0;
		_update_streaming(_force_stream_update);
		_force_stream_update = false;
		_last_stream_position = _viewer_position;
	}
}

void RobloxSmoothTerrainManager::rebuild_all() {
	clear();
	_lod_cache_dirty = true;
	_startup_streaming = true;
	_streaming_complete = false;
	_force_stream_update = true;
	_update_streaming(true);
}

void RobloxSmoothTerrainManager::clear() {
	std::vector<RenderKey> keys;
	keys.reserve(_rendered.size());
	for (const auto &entry : _rendered) keys.push_back(entry.first);
	for (const RenderKey &key : keys) _remove_chunk(key);
	_rendered.clear();
	_dirty_render_keys.clear();
	_last_border_masks.clear();
}

int RobloxSmoothTerrainManager::get_rendered_chunk_count() const {
	return static_cast<int>(_rendered.size());
}

int RobloxSmoothTerrainManager::get_rendered_water_chunk_count() const {
	int count = 0;
	for (const auto &entry : _rendered) {
		if (entry.second.water != nullptr) ++count;
	}
	return count;
}

int RobloxSmoothTerrainManager::get_required_chunk_count() const {
	return _required_chunk_count;
}

bool RobloxSmoothTerrainManager::is_streaming_complete() const {
	return _streaming_complete;
}

bool RobloxSmoothTerrainManager::is_collision_ready_at_world_position(const Vector3 &position) const {
	if (!_collision_enabled || _data.is_null()) return false;
	const Vector3 local = to_local(position);
	const double chunk_size = RobloxSmoothLodOctreePolicy::cell_extent(0) * _data->get_cell_size();
	if (chunk_size <= 0.0) return false;
	const int center_x = static_cast<int>(std::floor(local.x / chunk_size));
	const int center_y = static_cast<int>(std::floor(local.y / chunk_size));
	const int center_z = static_cast<int>(std::floor(local.z / chunk_size));
	for (int y = center_y; y >= center_y - 4; --y) {
		for (int x = center_x - 1; x <= center_x + 1; ++x) {
			for (int z = center_z - 1; z <= center_z + 1; ++z) {
				auto entry = _rendered.find({x, y, z, 0});
				if (entry != _rendered.end() && entry->second.has_collision_shape)
					return true;
			}
		}
	}
	return false;
}

Dictionary RobloxSmoothTerrainManager::get_runtime_statistics() const {
	Dictionary statistics;
	int queued_jobs = 0;
	int completed_jobs = 0;
	int reserved_jobs = 0;
	int active_jobs = _async_active.load(std::memory_order_relaxed);
	{
		std::lock_guard<std::mutex> guard(_async_mutex);
		queued_jobs = static_cast<int>(_async_requests.size());
		completed_jobs = static_cast<int>(_async_results.size());
		reserved_jobs = static_cast<int>(_async_in_flight.size());
	}
	statistics["required_chunks"] = _required_chunk_count;
	statistics["rendered_chunks"] = static_cast<int>(_rendered.size());
	statistics["rendered_water_chunks"] = get_rendered_water_chunk_count();
	statistics["cached_chunks"] = static_cast<int>(_mesh_cache.size());
	statistics["queued_worker_jobs"] = queued_jobs;
	statistics["active_worker_jobs"] = active_jobs;
	statistics["completed_waiting_jobs"] = completed_jobs;
	statistics["reserved_worker_jobs"] = reserved_jobs;
	statistics["streaming_complete"] = _streaming_complete;
	statistics["startup_streaming"] = _startup_streaming;
	statistics["stale_jobs_dropped"] = static_cast<int64_t>(_metric_stale_jobs_dropped.load(std::memory_order_relaxed));
	statistics["meshes_generated"] = static_cast<int64_t>(_metric_meshes_generated.load(std::memory_order_relaxed));
	statistics["meshes_installed"] = static_cast<int64_t>(_metric_meshes_installed.load(std::memory_order_relaxed));
	statistics["cache_hits"] = static_cast<int64_t>(_metric_cache_hits.load(std::memory_order_relaxed));
	statistics["cache_misses"] = static_cast<int64_t>(_metric_cache_misses.load(std::memory_order_relaxed));
	statistics["main_thread_install_ms"] = static_cast<double>(_metric_install_usec.load(std::memory_order_relaxed)) / 1000.0;
	statistics["collision_installs"] = static_cast<int64_t>(_metric_collision_installs.load(std::memory_order_relaxed));
	statistics["collision_install_ms"] = static_cast<double>(_metric_collision_install_usec.load(std::memory_order_relaxed)) / 1000.0;
	statistics["lod_transition_count"] = static_cast<int64_t>(_metric_lod_transitions.load(std::memory_order_relaxed));
	statistics["lod_split_count"] = static_cast<int64_t>(_metric_lod_splits.load(std::memory_order_relaxed));
	statistics["lod_merge_count"] = static_cast<int64_t>(_metric_lod_merges.load(std::memory_order_relaxed));
	statistics["maximum_lod_level"] = _maximum_lod_level;
	statistics["view_distance"] = _view_distance;
	statistics["adaptive_view_distance"] = _adaptive_view_distance;
	statistics["performance_distance_scale"] = _performance_distance_scale;
	statistics["collision_distance"] = _collision_distance;
	statistics["viewer_count"] = static_cast<int64_t>(_viewer_positions.size());
	return statistics;
}

void RobloxSmoothTerrainManager::_rebuild_lod_cache() {
	_async_flush_and_wait();
	_mip_cache.rebuild(_data, _maximum_lod_level);
	_lod_coverage = RobloxSmoothLodOctreePolicy::build_coverage(_mip_cache.get_level0_render_regions());
	_data_revision = _data.is_valid() ? static_cast<uint64_t>(_data->get_revision()) : 0;
	_lod_cache_dirty = false;
	// A full mip rebuild means every cached leaf may now read different source
	// samples. Do not reuse a mesh across that revision boundary.
	_mesh_cache.clear();
	_mesh_cache_clock = 0;
	_last_needed.clear();
	_last_border_masks.clear();
	_has_last_needed = false;
	_metric_meshes_generated.store(0, std::memory_order_relaxed);
	_metric_meshes_installed.store(0, std::memory_order_relaxed);
	_metric_stale_jobs_dropped.store(0, std::memory_order_relaxed);
	_metric_cache_hits.store(0, std::memory_order_relaxed);
	_metric_cache_misses.store(0, std::memory_order_relaxed);
	_metric_install_usec.store(0, std::memory_order_relaxed);
	_metric_collision_install_usec.store(0, std::memory_order_relaxed);
	_metric_collision_installs.store(0, std::memory_order_relaxed);
	_metric_lod_transitions.store(0, std::memory_order_relaxed);
	_metric_lod_splits.store(0, std::memory_order_relaxed);
	_metric_lod_merges.store(0, std::memory_order_relaxed);
}

void RobloxSmoothTerrainManager::_consume_data_changes() {
	if (_data.is_null()) return;
	const uint64_t current_revision = static_cast<uint64_t>(_data->get_revision());
	if (current_revision != _data_revision)
		_async_flush_and_wait();
	if (current_revision == _data_revision) return;

	Vector3i dirty_min;
	Vector3i dirty_max;
	bool full = false;
	if (_lod_cache_dirty || !_data->get_dirty_since(static_cast<int64_t>(_data_revision), dirty_min, dirty_max, full) || full) {
		for (const auto &entry : _rendered)
			_dirty_render_keys.insert(entry.first);
		_lod_cache_dirty = true;
		_mesh_cache.clear();
		_mesh_cache_clock = 0;
		_stream_accumulator = 0.35;
		return;
	}

	_mip_cache.update_region(_data, dirty_min, dirty_max);
	_lod_coverage = RobloxSmoothLodOctreePolicy::build_coverage(_mip_cache.get_level0_render_regions());
	_data_revision = current_revision;
	_mark_rendered_region_dirty(dirty_min, dirty_max);
	_invalidate_mesh_cache_region(dirty_min, dirty_max);
	_stream_accumulator = 0.35;
}

bool RobloxSmoothTerrainManager::_render_key_intersects_region(
		const RenderKey &key, const Vector3i &p_min, const Vector3i &p_max) const {
	const int lod = std::clamp(key.level, 0, RobloxSmoothLodOctreePolicy::CHUNK_LEVELS - 1);
	const int stride = RobloxSmoothLodOctreePolicy::lod_stride(lod);
	const int extent = RobloxSmoothLodOctreePolicy::cell_extent(lod);
	const int apron = RobloxSmoothLodOctreePolicy::MESH_BORDER_CELLS * stride;
	const int sample_footprint = stride - 1;
	const Vector3i key_min(
		key.x * extent - apron,
		key.y * extent - apron,
		key.z * extent - apron);
	const Vector3i key_max(
		(key.x + 1) * extent + apron + sample_footprint,
		(key.y + 1) * extent + apron + sample_footprint,
		(key.z + 1) * extent + apron + sample_footprint);
	return key_min.x <= p_max.x && key_max.x >= p_min.x
		&& key_min.y <= p_max.y && key_max.y >= p_min.y
		&& key_min.z <= p_max.z && key_max.z >= p_min.z;
}

void RobloxSmoothTerrainManager::_mark_rendered_region_dirty(const Vector3i &p_min, const Vector3i &p_max) {
	for (const auto &entry : _rendered) {
		if (_render_key_intersects_region(entry.first, p_min, p_max))
			_dirty_render_keys.insert(entry.first);
	}
}

void RobloxSmoothTerrainManager::_invalidate_mesh_cache_region(
		const Vector3i &p_min, const Vector3i &p_max) {
	std::vector<RenderKey> invalidated;
	invalidated.reserve(_mesh_cache.size());
	for (const auto &entry : _mesh_cache) {
		if (_render_key_intersects_region(entry.first, p_min, p_max))
			invalidated.push_back(entry.first);
	}
	for (const RenderKey &key : invalidated)
		_mesh_cache.erase(key);
	// TerrainData revisions are global, but a partial edit only invalidates the
	// leaves whose apron samples intersect the edited region. Retag the other
	// cached leaves so they remain reusable after the revision counter advances.
	for (auto &entry : _mesh_cache)
		entry.second.revision = _data_revision;
}

bool RobloxSmoothTerrainManager::_replacement_region_ready(
		const RenderKey &stale_key,
		const std::unordered_set<RenderKey, RenderKeyHash> &needed) const {
	const int stale_extent = RobloxSmoothLodOctreePolicy::cell_extent(stale_key.level);
	const Vector3i stale_min(stale_key.x * stale_extent, stale_key.y * stale_extent, stale_key.z * stale_extent);
	const Vector3i stale_max((stale_key.x + 1) * stale_extent, (stale_key.y + 1) * stale_extent, (stale_key.z + 1) * stale_extent);

	for (const RenderKey &desired : needed) {
		const int desired_extent = RobloxSmoothLodOctreePolicy::cell_extent(desired.level);
		const Vector3i desired_min(desired.x * desired_extent, desired.y * desired_extent, desired.z * desired_extent);
		const Vector3i desired_max((desired.x + 1) * desired_extent, (desired.y + 1) * desired_extent, (desired.z + 1) * desired_extent);
		const bool overlaps =
			stale_min.x < desired_max.x && desired_min.x < stale_max.x
			&& stale_min.y < desired_max.y && desired_min.y < stale_max.y
			&& stale_min.z < desired_max.z && desired_min.z < stale_max.z;
		if (!overlaps)
			continue;
		if (_rendered.find(desired) == _rendered.end())
			return false;
	}
	return true;
}

void RobloxSmoothTerrainManager::_store_mesh_cache(const RenderKey &key, MeshPair meshes, uint8_t retained_border_mask) {
	if (_data.is_null())
		return;

	MeshCacheEntry entry;
	entry.meshes = std::move(meshes);
	entry.retained_border_mask = retained_border_mask;
	entry.revision = _data_revision;
	entry.last_used = ++_mesh_cache_clock;
	_mesh_cache.insert_or_assign(key, std::move(entry));

	// Mobile-class targets have a much tighter memory budget than desktop. Keep
	// enough recently visited leaves to prevent split/merge remeshing without
	// retaining a four-digit set of GPU meshes on a 1.5 GB-class device.
	const size_t cache_capacity = OS::get_singleton()->get_name() == "Android"
		? std::min<size_t>(MESH_CACHE_CAPACITY, 384)
		: MESH_CACHE_CAPACITY;
	while (_mesh_cache.size() > cache_capacity) {
		auto oldest = _mesh_cache.end();
		// Prefer evicting leaves that are not currently visible. A rendered leaf
		// is exactly the mesh most likely to be requested again when the viewer
		// crosses back over the same split/merge threshold. Only allow visible
		// entries to be evicted if the cache grows to twice its normal target.
		const bool hard_limit = _mesh_cache.size() > cache_capacity * 2;
		for (auto it = _mesh_cache.begin(); it != _mesh_cache.end(); ++it) {
			if (!hard_limit && _rendered.find(it->first) != _rendered.end())
				continue;
			if (oldest == _mesh_cache.end() || it->second.last_used < oldest->second.last_used)
				oldest = it;
		}
		if (oldest == _mesh_cache.end())
			break;
		_mesh_cache.erase(oldest);
	}
}

double RobloxSmoothTerrainManager::_distance_to_chunk(const RenderKey &key) const {
	if (_data.is_null()) return std::numeric_limits<double>::infinity();
	return RobloxSmoothLodOctreePolicy::distance_to_bounds(key, _viewer_positions, _data->get_cell_size());
}

void RobloxSmoothTerrainManager::_update_streaming(bool force) {
	if (_data.is_null()) {
		_streaming_complete = false;
		_required_chunk_count = 0;
		return;
	}
	if (_lod_cache_dirty) _rebuild_lod_cache();

	RobloxSmoothLodOctreePolicy::KeySet currently_rendered;
	currently_rendered.reserve(_rendered.size());
	for (const auto &entry : _rendered)
		currently_rendered.insert(entry.first);

	std::vector<RenderKey> selected = RobloxSmoothLodOctreePolicy::select_leaves(
			_lod_coverage,
			currently_rendered,
			_viewer_positions,
			_adaptive_view_distance,
			_data->get_cell_size(),
			_maximum_lod_level);
	// SmoothCluster's occupancy mip is intentionally preserved, but a very thin
	// source feature can still become a collapsed pair of coarse surfaces when
	// marched at L3/L4. Demote only those selected leaves to covered children;
	// broad terrain with a real coarse interior continues through the normal
	// five-level Roblox-style octree path.
	std::unordered_set<RenderKey, RenderKeyHash> selected_set(selected.begin(), selected.end());
	std::unordered_set<RenderKey, RenderKeyHash> unsafe_macro_regions;
	for (const RenderKey &key : selected) {
		if (key.level < 3 || _mip_cache.supports_lod_leaf(key))
			continue;
		RenderKey macro = key;
		while (macro.level < RobloxSmoothLodOctreePolicy::CHUNK_LEVELS - 1)
			macro = RobloxSmoothLodOctreePolicy::parent(macro);
		unsafe_macro_regions.insert(macro);
	}
	std::deque<RenderKey> demotion_queue(selected.begin(), selected.end());
	while (!demotion_queue.empty()) {
		const RenderKey key = demotion_queue.front();
		demotion_queue.pop_front();
		RenderKey macro = key;
		while (macro.level < RobloxSmoothLodOctreePolicy::CHUNK_LEVELS - 1)
			macro = RobloxSmoothLodOctreePolicy::parent(macro);
		const bool macro_is_unsafe = key.level >= 3 && unsafe_macro_regions.find(macro) != unsafe_macro_regions.end();
		if (key.level <= 0 || (!macro_is_unsafe && _mip_cache.supports_lod_leaf(key)))
			continue;
		selected_set.erase(key);
		for (int x = 0; x < 2; ++x) {
			for (int y = 0; y < 2; ++y) {
				for (int z = 0; z < 2; ++z) {
					const RenderKey child = RobloxSmoothLodOctreePolicy::child(key, x, y, z);
					if (_lod_coverage[child.level].find(child) == _lod_coverage[child.level].end())
						continue;
					if (selected_set.insert(child).second)
						demotion_queue.push_back(child);
				}
			}
		}
	}
	selected.assign(selected_set.begin(), selected_set.end());
	std::sort(selected.begin(), selected.end(), [](const RenderKey &a, const RenderKey &b) {
		if (a.level != b.level) return a.level < b.level;
		if (a.x != b.x) return a.x < b.x;
		if (a.y != b.y) return a.y < b.y;
		return a.z < b.z;
	});
	_required_chunk_count = static_cast<int>(selected.size());
	_streaming_complete = false;

	std::unordered_set<RenderKey, RenderKeyHash> needed;
	needed.reserve(selected.size());
	struct Candidate { RenderKey key; bool collision; double distance; uint8_t retained_border_mask; };
	std::vector<Candidate> candidates;
	candidates.reserve(selected.size());

	for (const RenderKey &key : selected) {
		const double distance = _distance_to_chunk(key);
		const bool collision = _collision_enabled && key.level == 0 && distance <= _collision_distance;
		needed.insert(key);
		auto existing = _rendered.find(key);
		const bool dirty = _dirty_render_keys.find(key) != _dirty_render_keys.end();
		if (force || existing == _rendered.end() || existing->second.collision != collision || dirty)
			candidates.push_back({key, collision, distance, 0});
	}
	std::unordered_map<RenderKey, uint8_t, RenderKeyHash> needed_border_masks;
	needed_border_masks.reserve(needed.size());
	if (_has_last_needed && needed == _last_needed && _last_border_masks.size() == needed.size()) {
		needed_border_masks = _last_border_masks;
	} else {
		for (const RenderKey &key : needed)
			needed_border_masks.emplace(key, retained_border_mask_for_key(key, needed));
		_last_border_masks = needed_border_masks;
	}
	for (Candidate &candidate : candidates)
		candidate.retained_border_mask = needed_border_masks.at(candidate.key);
	for (const RenderKey &key : needed) {
		const uint8_t retained_border_mask = needed_border_masks.at(key);
		auto existing = _rendered.find(key);
		if (existing != _rendered.end() && existing->second.retained_border_mask != retained_border_mask) {
			if (std::find_if(candidates.begin(), candidates.end(), [&](const Candidate &candidate) {
					return candidate.key == key;
				}) == candidates.end()) {
				const double distance = _distance_to_chunk(key);
				candidates.push_back({key, existing->second.collision, distance, retained_border_mask});
			}
		}
	}
	if (_has_last_needed && needed != _last_needed) {
		size_t added = 0;
		size_t removed = 0;
		for (const RenderKey &key : needed)
			if (_last_needed.find(key) == _last_needed.end()) ++added;
		for (const RenderKey &key : _last_needed)
			if (needed.find(key) == needed.end()) ++removed;
		_metric_lod_transitions.fetch_add(1, std::memory_order_relaxed);
		if (added > removed)
			_metric_lod_splits.fetch_add(1, std::memory_order_relaxed);
		else if (removed > added)
			_metric_lod_merges.fetch_add(1, std::memory_order_relaxed);
	}
	_last_needed = needed;
	_has_last_needed = true;

	std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
		if (a.distance != b.distance) return a.distance < b.distance;
		return a.key.level < b.key.level;
	});

	// Meshes are computed on the worker pool; the main thread only expenses this
	// part of the frame on cache hits + draining finished installs. Nothing in
	// the steady-state walking path pays a synchronous march/emit anymore.
	uint64_t started = Time::get_singleton()->get_ticks_msec();
	_ensure_async_workers();

	std::unordered_set<RenderKey, RenderKeyHash> needed_set = needed;
	{
	std::lock_guard<std::mutex> guard(_async_mutex);
		_async_currently_needed = std::move(needed_set);
		_async_currently_needed_border_masks = needed_border_masks;
		// Drop queued work for chunks the viewer abandoned; marching terrain
		// that is no longer needed puts fresh work behind useless work.
		for (auto it = _async_requests.begin(); it != _async_requests.end();) {
			if (_async_currently_needed.find(it->key) == _async_currently_needed.end()) {
				_async_in_flight.erase(it->key);
				it = _async_requests.erase(it);
			} else {
				++it;
			}
		}
		// Completed worker results are still ownership-transfer work, not a free
		// cache. A camera move can make a result obsolete before the main thread
		// installs it; retaining those meshes until the next drain both inflates
		// memory and lets readiness report true while relevant work is still queued.
		// Drop obsolete results at the same desired-set boundary used for requests.
		for (auto it = _async_results.begin(); it != _async_results.end();) {
			auto mask = _async_currently_needed_border_masks.find(it->key);
			const bool still_needed = _async_currently_needed.find(it->key) != _async_currently_needed.end();
			const bool seam_state_current = mask != _async_currently_needed_border_masks.end()
					&& mask->second == it->retained_border_mask;
			if (it->revision != _data_revision || !still_needed || !seam_state_current) {
				_metric_stale_jobs_dropped.fetch_add(1, std::memory_order_relaxed);
				// The reservation covers queued, active and completed work. This
				// result is now discarded, so release its reservation just as the
				// normal drain does. Otherwise a later request for the same leaf
				// is suppressed forever despite an idle worker/install queue.
				_async_in_flight.erase(it->key);
				it = _async_results.erase(it);
			} else {
				++it;
			}
		}
	}

	int built = 0;
	int cached_collision_installed = 0;
	const uint64_t cache_commit_started_usec = Time::get_singleton()->get_ticks_usec();
	const uint64_t cache_commit_budget_usec = static_cast<uint64_t>(
		std::max(0.25, _mesh_build_budget_ms * 0.65) * 1000.0);
	const int cache_install_limit = _startup_streaming ? 8 : 4;
	for (const Candidate &candidate : candidates) {
		auto cached = _mesh_cache.find(candidate.key);
		if (cached != _mesh_cache.end() && cached->second.revision == _data_revision
				&& cached->second.retained_border_mask == candidate.retained_border_mask) {
			_metric_cache_hits.fetch_add(1, std::memory_order_relaxed);
			// A cache hit avoids meshing, but scene-node creation, GPU upload state,
			// and especially collision shape installation are still main-thread work.
			// Bound cache re-entry exactly like freshly completed worker results.
			const bool cache_budget_spent = built >= cache_install_limit
				|| (built > 0 && Time::get_singleton()->get_ticks_usec() - cache_commit_started_usec >= cache_commit_budget_usec)
				|| (candidate.collision && cached_collision_installed >= 1);
			if (cache_budget_spent)
				continue;
			cached->second.last_used = ++_mesh_cache_clock;
			if (_rendered.find(candidate.key) != _rendered.end())
				_remove_chunk(candidate.key);
			const uint64_t install_started_usec = Time::get_singleton()->get_ticks_usec();
			_install_chunk(candidate.key, candidate.collision, candidate.retained_border_mask, cached->second.meshes);
			const uint64_t install_elapsed_usec = Time::get_singleton()->get_ticks_usec() - install_started_usec;
			_metric_install_usec.fetch_add(install_elapsed_usec, std::memory_order_relaxed);
			_metric_meshes_installed.fetch_add(1, std::memory_order_relaxed);
			if (candidate.collision) {
				_metric_collision_installs.fetch_add(1, std::memory_order_relaxed);
				_metric_collision_install_usec.fetch_add(install_elapsed_usec, std::memory_order_relaxed);
			}
			_dirty_render_keys.erase(candidate.key);
			built++;
			if (candidate.collision)
				cached_collision_installed++;
			continue;
		}
		_metric_cache_misses.fetch_add(1, std::memory_order_relaxed);
		if (cached != _mesh_cache.end())
			_mesh_cache.erase(cached);
		_enqueue_async_chunk({ candidate.key, candidate.collision, candidate.retained_border_mask, candidate.distance });
	}

	_drain_async_results(_startup_streaming);
	built += 0; // drain count reported inside

	bool desired_ready = true;
	for (const RenderKey &key : needed) {
		if (_rendered.find(key) == _rendered.end()) {
			desired_ready = false;
			break;
		}
	}
	// Only work for the current desired set blocks readiness. A worker may still
	// be finishing a chunk from the camera's previous position; forcing the new
	// view to wait for abandoned work makes fast flight look artificially stuck.
	if (desired_ready) {
		std::lock_guard<std::mutex> guard(_async_mutex);
		for (const RenderKey &key : _async_in_flight) {
			if (needed.find(key) != needed.end()) {
				desired_ready = false;
				break;
			}
		}
		if (desired_ready) {
			for (const AsyncChunkResult &result : _async_results) {
				if (needed.find(result.key) != needed.end()) {
					desired_ready = false;
					break;
				}
			}
		}
	}

	std::vector<RenderKey> dead;
	for (const auto &entry : _rendered) {
		if (needed.find(entry.first) != needed.end()) continue;
		// Retire stale parent/child leaves only after replacement coverage is
		// ready. Distance itself is no longer a deletion condition: outside the
		// detailed ring the selector requests a coarse resident leaf instead.
		if (_replacement_region_ready(entry.first, needed))
			dead.push_back(entry.first);
	}
	for (const RenderKey &key : dead) _remove_chunk(key);
	if (_startup_streaming && desired_ready)
		_startup_streaming = false;
	_streaming_complete = desired_ready;

	if (OS::get_singleton()->has_environment("VEXTORIA_STREAM_TRACE")) {
		const uint64_t elapsed = Time::get_singleton()->get_ticks_msec() - started;
		if (built > 0 || elapsed >= 2) {
			print_line(vformat("[StreamTrace] built=%d removed=%d candidates=%d rendered=%d elapsedMs=%d startup=%d speed=%.1f fps=%d",
					built, dead.size(), (int)candidates.size(), (int)_rendered.size(), (int)elapsed, _startup_streaming ? 1 : 0,
					_viewer_speed, Engine::get_singleton()->get_frames_per_second()));
		}
	}
}

RobloxSmoothTerrainManager::MeshPair RobloxSmoothTerrainManager::_build_chunk_mesh(
		const RenderKey &key, uint8_t retained_border_mask) {
	auto cached = _mesh_cache.find(key);
	if (cached != _mesh_cache.end() && cached->second.revision == _data_revision
			&& cached->second.retained_border_mask == retained_border_mask) {
		cached->second.last_used = ++_mesh_cache_clock;
		return cached->second.meshes;
	}
	if (cached != _mesh_cache.end())
		_mesh_cache.erase(cached);

	BasicMesh basic;
	std::vector<Vector3> soft_normals;
	std::vector<Vector3> hard_normals;
	std::vector<uint32_t> solid_triangles;
	std::vector<uint32_t> water_triangles;
	std::vector<uint32_t> reverse_solid_triangles;
	MeshPair result;
	if (!_compute_chunk_geometry(key, retained_border_mask, basic, soft_normals, hard_normals, solid_triangles, water_triangles, reverse_solid_triangles)) {
		_store_mesh_cache(key, result, retained_border_mask);
		return result;
	}
	result.solid = emit_packed_surface(basic, soft_normals, hard_normals, solid_triangles, false, false, retained_border_mask);
	result.solid_collision = result.solid;
	if (!reverse_solid_triangles.empty()) {
		Ref<ArrayMesh> reverse = emit_packed_surface(basic, soft_normals, hard_normals, reverse_solid_triangles, false, true, retained_border_mask);
		if (result.solid.is_null()) result.solid = reverse;
		else if (reverse.is_valid() && reverse->get_surface_count() > 0) {
			Array arrays = reverse->surface_get_arrays(0);
			result.solid->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		}
	}
	result.water = emit_packed_surface(basic, soft_normals, hard_normals, water_triangles, true, false, retained_border_mask);
	_store_mesh_cache(key, result, retained_border_mask);
	return result;
}

void RobloxSmoothTerrainManager::_compute_chunk_async(const AsyncChunkRequest &p_request, AsyncChunkResult &r_result) const {
	r_result.key = p_request.key;
	r_result.collision = p_request.collision;
	r_result.retained_border_mask = p_request.retained_border_mask;
	r_result.revision = _data_revision;

	BasicMesh basic;
	std::vector<Vector3> soft_normals;
	std::vector<Vector3> hard_normals;
	std::vector<uint32_t> solid_triangles;
	std::vector<uint32_t> water_triangles;
	std::vector<uint32_t> reverse_solid_triangles;
	if (!_compute_chunk_geometry(p_request.key, p_request.retained_border_mask, basic, soft_normals, hard_normals, solid_triangles, water_triangles, reverse_solid_triangles))
		return;

	r_result.has_solid = !solid_triangles.empty();
	if (r_result.has_solid)
		r_result.solid_arrays = emit_packed_arrays(basic, soft_normals, hard_normals, solid_triangles, false, false, p_request.retained_border_mask);
	r_result.has_solid_reverse = !reverse_solid_triangles.empty();
	if (r_result.has_solid_reverse)
		r_result.solid_reverse_arrays = emit_packed_arrays(basic, soft_normals, hard_normals, reverse_solid_triangles, false, true, p_request.retained_border_mask);
	r_result.has_water = !water_triangles.empty();
	if (r_result.has_water)
		r_result.water_arrays = emit_packed_arrays(basic, soft_normals, hard_normals, water_triangles, true, false, p_request.retained_border_mask);
	if (p_request.collision && r_result.has_solid)
		r_result.collision_faces = r_result.solid_arrays[Mesh::ARRAY_VERTEX].operator PackedVector3Array();
}

bool RobloxSmoothTerrainManager::_compute_chunk_geometry(
		const RenderKey &key,
		uint8_t retained_border_mask,
		BasicMesh &basic,
		std::vector<Vector3> &soft_normals,
		std::vector<Vector3> &hard_normals,
		std::vector<uint32_t> &solid_triangles,
		std::vector<uint32_t> &water_triangles,
		std::vector<uint32_t> &reverse_solid_triangles) const
{
	if (_data.is_null()) return false;
	const int lod = std::clamp(key.level, 0, RobloxSmoothLodOctreePolicy::CHUNK_LEVELS - 1);
	const int stride = RobloxSmoothLodOctreePolicy::lod_stride(lod);
	const int interior = RobloxSmoothLodOctreePolicy::mesher_interior_samples();
	const int border = RobloxSmoothLodOctreePolicy::MESH_BORDER_CELLS;
	const int grid_size = RobloxSmoothLodOctreePolicy::mesher_grid_samples();
	const int cube_size = grid_size - 1;
	const int grid_xz = grid_size * grid_size;
	const int cube_xz = cube_size * cube_size;
	const int source_extent = RobloxSmoothLodOctreePolicy::cell_extent(lod);
	const float cell_size = static_cast<float>(stride) * static_cast<float>(_data->get_cell_size());
	const Vector3int32 origin(
		key.x * source_extent - border * stride,
		key.y * source_extent - border * stride,
		key.z * source_extent - border * stride);

	static_cast<void>(interior);

	std::vector<RbxCell> cells(static_cast<size_t>(grid_size) * grid_size * grid_size);
	std::vector<GridVertex> grid(cells.size());
	auto gi = [grid_size, grid_xz](int x, int y, int z) { return x + grid_xz * y + grid_size * z; };
	for (int y = 0; y < grid_size; y++) {
		for (int z = 0; z < grid_size; z++) {
			for (int x = 0; x < grid_size; x++) {
				RbxCell cell = _mip_cache.get_aligned_source(
					origin.x + x * stride,
					origin.y + y * stride,
					origin.z + z * stride,
					lod);
				int index = gi(x, y, z);
				cells[index] = cell;
				grid[index] = {tag_for_cell(cell), cell.occupancy};
			}
		}
	}
	std::vector<uint32_t> points(static_cast<size_t>(cube_size) * cube_size * cube_size, std::numeric_limits<uint32_t>::max());
	auto pi = [cube_size, cube_xz](int x, int y, int z) { return x + cube_xz * y + cube_size * z; };
	for (int y = 0; y < cube_size; y++) {
		for (int z = 0; z < cube_size; z++) {
			for (int x = 0; x < cube_size; x++) {
				std::array<GridVertex, 8> corner_grid;
				std::array<RbxCell, 8> corner_cells;
				for (int c = 0; c < 8; c++) {
					int index = gi(x + CORNERS[c][0], y + CORNERS[c][1], z + CORNERS[c][2]);
					corner_grid[c] = grid[index];
					corner_cells[c] = cells[index];
				}
				uint16_t mask = edge_mask_for_cube(corner_grid);
				if (mask == 0) continue;
				Vector3 corner(
					(origin.x + x * stride) * _data->get_cell_size(),
					(origin.y + y * stride) * _data->get_cell_size(),
					(origin.z + z * stride) * _data->get_cell_size());
				int seed_x = x + (origin.x >> lod);
				int seed_y = y + (origin.y >> lod);
				int seed_z = z + (origin.z >> lod);
				BasicVertex vertex = make_vertex(corner_grid, corner_cells, mask, corner, cell_size,
					x, y, z, seed_x, seed_y, seed_z, grid_size);
				points[pi(x, y, z)] = static_cast<uint32_t>(basic.vertices.size());
				basic.vertices.push_back(vertex);
			}
		}
	}
	for (int y = 1; y + 1 < grid_size; y++) {
		for (int z = 1; z + 1 < grid_size; z++) {
			for (int x = 1; x + 1 < grid_size; x++) {
				const GridVertex &v000 = grid[gi(x, y, z)];
				const GridVertex &v100 = grid[gi(x + 1, y, z)];
				const GridVertex &v010 = grid[gi(x, y + 1, z)];
				const GridVertex &v001 = grid[gi(x, y, z + 1)];
				if (v000.tag != v100.tag) {
					push_quad(basic,
						points[pi(x, y, z)], points[pi(x, y - 1, z)], points[pi(x, y - 1, z - 1)], points[pi(x, y, z - 1)],
						v000.tag > v100.tag, std::max(v000.tag, v100.tag),
						v000.tag > v100.tag ? Vector3(1, 0, 0) : Vector3(-1, 0, 0));
				}
				if (v000.tag != v010.tag) {
					push_quad(basic,
						points[pi(x, y, z)], points[pi(x - 1, y, z)], points[pi(x - 1, y, z - 1)], points[pi(x, y, z - 1)],
						v000.tag < v010.tag, std::max(v000.tag, v010.tag),
						v000.tag > v010.tag ? Vector3(0, 1, 0) : Vector3(0, -1, 0));
				}
				if (v000.tag != v001.tag) {
					push_quad(basic,
						points[pi(x, y, z)], points[pi(x - 1, y, z)], points[pi(x - 1, y - 1, z)], points[pi(x, y - 1, z)],
						v000.tag > v001.tag, std::max(v000.tag, v001.tag),
						v000.tag > v001.tag ? Vector3(0, 0, 1) : Vector3(0, 0, -1));
				}
			}
		}
	}
	if (basic.indices.empty())
		return false;
	size_t triangle_count = basic.indices.size() / 3;
	soft_normals.resize(basic.vertices.size());
	std::vector<Vector3> soft_outward_hints(basic.vertices.size());
	hard_normals.resize(triangle_count);
	std::vector<bool> water_flags(triangle_count, false);
	for (size_t tri = 0; tri < triangle_count; tri++) {
		const uint32_t i0 = basic.indices[tri * 3];
		const uint32_t i1 = basic.indices[tri * 3 + 1];
		const uint32_t i2 = basic.indices[tri * 3 + 2];
		Vector3 face = (basic.vertices[i1].position - basic.vertices[i0].position).cross(basic.vertices[i2].position - basic.vertices[i0].position);
		const Vector3 outward = tri < basic.outward_hints.size() ? basic.outward_hints[tri] : Vector3();
		// Preserve Roblox-main's generated winding. The source mesher emits
		// faces in the coordinate convention used by SmoothCluster; the single
		// Roblox-to-Godot winding conversion happens in emit_packed_arrays(),
		// after the source hard/soft normals are calculated. Reordering here
		// against a Godot-facing outward hint would double-invert the source face
		// on the axes whose Roblox winding is intentionally opposite.
		soft_normals[i0] += face;
		soft_normals[i1] += face;
		soft_normals[i2] += face;
		soft_outward_hints[i0] += outward;
		soft_outward_hints[i1] += outward;
		soft_outward_hints[i2] += outward;
		if (face.length_squared() > 1e-12f)
			hard_normals[tri] = face.normalized();
		else if (outward.length_squared() > 1e-12f)
			hard_normals[tri] = outward.normalized();
		else
			hard_normals[tri] = Vector3(0, 1, 0);
		// Roblox-main's BasicMesh::isWater() uses the generated triangle vertices,
		// not the coarse cell transition tag. Mixed solid/water shoreline triangles
		// therefore belong to the water surface when any vertex is water.
		water_flags[tri] = triangle_has_water(basic, static_cast<uint32_t>(tri));
	}
	for (size_t i = 0; i < soft_normals.size(); i++) {
		Vector3 &normal = soft_normals[i];
		const Vector3 &outward = soft_outward_hints[i];
		if (normal.length_squared() > 1e-12f) {
			normal = normal.normalized();
			if (outward.length_squared() > 1e-12f && normal.dot(outward) < 0.0f)
				normal = -normal;
		} else if (outward.length_squared() > 1e-12f) {
			normal = outward.normalized();
		} else {
			normal = Vector3(0, 1, 0);
		}
	}
	solid_triangles.clear(); water_triangles.clear(); reverse_solid_triangles.clear();
	for (uint32_t tri = 0; tri < triangle_count; tri++) {
		// SmoothClusterLOD reads two coarse cells beyond the logical leaf so the
		// mesher has enough neighborhood information for stable vertices/normals.
		// The apron is submitted only on its deterministic seam-owning faces;
		// neighboring leaves still converge from the same shared samples without
		// leaving both copies to fight in the depth buffer.
		if ((triangle_border_mask(basic, tri) & static_cast<uint8_t>(~retained_border_mask)) != 0)
			continue;
		if (water_flags[tri]) water_triangles.push_back(tri);
		else {
			solid_triangles.push_back(tri);
			uint32_t pair = tri ^ 1u;
			if (pair < triangle_count && water_flags[pair]) reverse_solid_triangles.push_back(tri);
		}
	}
	return true;
}

void RobloxSmoothTerrainManager::_install_async_chunk(const RenderKey &key, bool collision, const AsyncChunkResult &p_result, MeshPair &meshes) {
	RenderEntry entry;
	entry.lod = key.level;
	entry.collision = collision;
	entry.retained_border_mask = p_result.retained_border_mask;
	entry.revision = _data_revision;
	if (meshes.solid.is_valid()) {
		MeshInstance3D *mesh = memnew(MeshInstance3D);
		mesh->set_mesh(meshes.solid);
		mesh->set_material_override(_solid_material);
		add_child(mesh);
		mesh->set_name(_chunk_name(key, false));
		entry.solid = mesh;
		if (collision && !p_result.collision_faces.is_empty()) {
			if (meshes.collision_shape.is_null()) {
				Ref<ConcavePolygonShape3D> shape;
				shape.instantiate();
				shape->set_faces(p_result.collision_faces);
				meshes.collision_shape = shape;
			}
			StaticBody3D *body = memnew(StaticBody3D);
			body->set_collision_layer(VextoriaCollisionLayers::ROBLOX_STATIC);
			body->set_collision_mask(VextoriaCollisionLayers::ROBLOX_DYNAMIC);
			CollisionShape3D *collision_shape = memnew(CollisionShape3D);
			collision_shape->set_shape(meshes.collision_shape);
			body->add_child(collision_shape);
			mesh->add_child(body);
			entry.has_collision_shape = true;
		}
	}
	if (meshes.water.is_valid()) {
		MeshInstance3D *mesh = memnew(MeshInstance3D);
		mesh->set_mesh(meshes.water);
		mesh->set_material_override(_water_material);
		add_child(mesh);
		mesh->set_name(_chunk_name(key, true));
		entry.water = mesh;
	}
	_rendered[key] = entry;
}

void RobloxSmoothTerrainManager::_install_chunk(
		const RenderKey &key, bool collision, uint8_t retained_border_mask, MeshPair &meshes) {
	RenderEntry entry;
	entry.lod = key.level;
	entry.collision = collision;
	entry.retained_border_mask = retained_border_mask;
	entry.revision = _data_revision;
	if (meshes.solid.is_valid()) {
		MeshInstance3D *mesh = memnew(MeshInstance3D);
		mesh->set_mesh(meshes.solid);
		mesh->set_material_override(_solid_material);
		add_child(mesh);
		mesh->set_name(_chunk_name(key, false));
		entry.solid = mesh;
		if (collision) {
			if (meshes.collision_shape.is_null()) {
				Ref<ArrayMesh> collision_mesh = meshes.solid_collision.is_valid()
					? meshes.solid_collision
					: meshes.solid;
				meshes.collision_shape = collision_mesh->create_trimesh_shape();
			}
			if (meshes.collision_shape.is_valid()) {
				StaticBody3D *body = memnew(StaticBody3D);
				body->set_collision_layer(VextoriaCollisionLayers::ROBLOX_STATIC);
				body->set_collision_mask(VextoriaCollisionLayers::ROBLOX_DYNAMIC);
				CollisionShape3D *collision_shape = memnew(CollisionShape3D);
				collision_shape->set_shape(meshes.collision_shape);
				body->add_child(collision_shape);
				mesh->add_child(body);
				entry.has_collision_shape = true;
			}
		}
	}
	if (meshes.water.is_valid()) {
		MeshInstance3D *mesh = memnew(MeshInstance3D);
		mesh->set_mesh(meshes.water);
		mesh->set_material_override(_water_material);
		add_child(mesh);
		mesh->set_name(_chunk_name(key, true));
		entry.water = mesh;
	}
	_rendered[key] = entry;
}

void RobloxSmoothTerrainManager::_remove_chunk(const RenderKey &key) {
	auto it = _rendered.find(key);
	if (it == _rendered.end()) {
		_dirty_render_keys.erase(key);
		return;
	}
	if (it->second.solid != nullptr) it->second.solid->queue_free();
	if (it->second.water != nullptr) it->second.water->queue_free();
	_rendered.erase(it);
	_dirty_render_keys.erase(key);
}

String RobloxSmoothTerrainManager::_chunk_name(const RenderKey &key, bool water) const {
	return String(water ? "Water_" : "Solid_")
		+ "L" + String::num_int64(key.level) + "_"
		+ String::num_int64(key.x) + "_" + String::num_int64(key.y) + "_" + String::num_int64(key.z);
}

void RobloxSmoothTerrainManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_terrain_data", "data"), &RobloxSmoothTerrainManager::set_terrain_data);
	ClassDB::bind_method(D_METHOD("get_terrain_data"), &RobloxSmoothTerrainManager::get_terrain_data);
	ClassDB::bind_method(D_METHOD("set_view_distance", "distance"), &RobloxSmoothTerrainManager::set_view_distance);
	ClassDB::bind_method(D_METHOD("get_view_distance"), &RobloxSmoothTerrainManager::get_view_distance);
	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enabled"), &RobloxSmoothTerrainManager::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("get_collision_enabled"), &RobloxSmoothTerrainManager::get_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_distance", "distance"), &RobloxSmoothTerrainManager::set_collision_distance);
	ClassDB::bind_method(D_METHOD("get_collision_distance"), &RobloxSmoothTerrainManager::get_collision_distance);
	ClassDB::bind_method(D_METHOD("set_viewer_world_positions", "positions"), &RobloxSmoothTerrainManager::set_viewer_world_positions);
	ClassDB::bind_method(D_METHOD("set_mesh_build_budget_ms", "budget"), &RobloxSmoothTerrainManager::set_mesh_build_budget_ms);
	ClassDB::bind_method(D_METHOD("get_mesh_build_budget_ms"), &RobloxSmoothTerrainManager::get_mesh_build_budget_ms);
	ClassDB::bind_method(D_METHOD("set_maximum_lod_level", "level"), &RobloxSmoothTerrainManager::set_maximum_lod_level);
	ClassDB::bind_method(D_METHOD("get_maximum_lod_level"), &RobloxSmoothTerrainManager::get_maximum_lod_level);
	ClassDB::bind_method(D_METHOD("set_solid_material", "material"), &RobloxSmoothTerrainManager::set_solid_material);
	ClassDB::bind_method(D_METHOD("get_solid_material"), &RobloxSmoothTerrainManager::get_solid_material);
	ClassDB::bind_method(D_METHOD("set_water_material", "material"), &RobloxSmoothTerrainManager::set_water_material);
	ClassDB::bind_method(D_METHOD("get_water_material"), &RobloxSmoothTerrainManager::get_water_material);
	ClassDB::bind_method(D_METHOD("rebuild_all"), &RobloxSmoothTerrainManager::rebuild_all);
	ClassDB::bind_method(D_METHOD("clear"), &RobloxSmoothTerrainManager::clear);
	ClassDB::bind_method(D_METHOD("get_rendered_chunk_count"), &RobloxSmoothTerrainManager::get_rendered_chunk_count);
	ClassDB::bind_method(D_METHOD("get_rendered_water_chunk_count"), &RobloxSmoothTerrainManager::get_rendered_water_chunk_count);
	ClassDB::bind_method(D_METHOD("get_required_chunk_count"), &RobloxSmoothTerrainManager::get_required_chunk_count);
	ClassDB::bind_method(D_METHOD("is_streaming_complete"), &RobloxSmoothTerrainManager::is_streaming_complete);
	ClassDB::bind_method(D_METHOD("is_collision_ready_at_world_position", "position"), &RobloxSmoothTerrainManager::is_collision_ready_at_world_position);
	ClassDB::bind_method(D_METHOD("get_runtime_statistics"), &RobloxSmoothTerrainManager::get_runtime_statistics);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "terrain_data", PROPERTY_HINT_RESOURCE_TYPE, "TerrainData"), "set_terrain_data", "get_terrain_data");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_distance", PROPERTY_HINT_RANGE, "32,2048,1"), "set_view_distance", "get_view_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_collision_enabled", "get_collision_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_distance", PROPERTY_HINT_RANGE, "0,512,1"), "set_collision_distance", "get_collision_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mesh_build_budget_ms", PROPERTY_HINT_RANGE, "1,33,0.5"), "set_mesh_build_budget_ms", "get_mesh_build_budget_ms");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "maximum_lod_level", PROPERTY_HINT_RANGE, "0,4,1"), "set_maximum_lod_level", "get_maximum_lod_level");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "solid_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_solid_material", "get_solid_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "water_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_water_material", "get_water_material");
}

