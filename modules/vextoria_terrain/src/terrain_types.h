#ifndef VEXTORIA_TERRAIN_TYPES_H
#define VEXTORIA_TERRAIN_TYPES_H

#include <cstdint>
#include <functional>

// Roblox terrain is exposed as material + occupancy cells. Internally this
// extension now uses Roblox-style 32^3 chunk sizing and four mesh LOD tiers;
// the current TerrainData class still keeps vertical chunks for compatibility,
// but the constants and APIs are ready for voxel2 parity work.
constexpr int DEFAULT_CHUNK_SIZE = 32;
constexpr int DEFAULT_WORLD_HEIGHT = 256;
constexpr int DEFAULT_CHUNK_HEIGHT = DEFAULT_WORLD_HEIGHT;
constexpr int LOD_LEVELS = 4;
constexpr int CHUNK_MIPS = 4;
constexpr int CHUNK_SIZE[LOD_LEVELS] = { 32, 16, 8, 4 };
constexpr int LOD_STRIDE[LOD_LEVELS] = { 1, 2, 4, 8 };
constexpr double LOD_DISTANCE[LOD_LEVELS] = { 0.0, 96.0, 192.0, 384.0 };

constexpr uint32_t BLOCK_AIR = 0;
constexpr uint32_t MATERIAL_AIR = 0;
constexpr uint32_t MATERIAL_WATER = 1;
constexpr uint32_t MATERIAL_GRASS = 2;
constexpr uint32_t MATERIAL_DIRT = 3;
constexpr uint32_t MATERIAL_SAND = 4;
constexpr uint32_t MATERIAL_ROCK = 5;
constexpr uint32_t MATERIAL_SNOW = 6;
constexpr uint32_t MATERIAL_MAX = 63;
constexpr uint32_t OCCUPANCY_MAX = 255;
constexpr uint32_t OCCUPANCY_SURFACE = 128;
constexpr float SDF_TRUNCATION = 8.0f;
constexpr float SDF_QUANTIZATION = 256.0f;
constexpr int16_t SDF_OUTSIDE = static_cast<int16_t>(SDF_TRUNCATION * SDF_QUANTIZATION);
constexpr int16_t SDF_INSIDE = static_cast<int16_t>(-SDF_TRUNCATION * SDF_QUANTIZATION);

inline int16_t quantize_sdf(float sdf) {
	if (sdf <= -SDF_TRUNCATION) return SDF_INSIDE;
	if (sdf >= SDF_TRUNCATION) return SDF_OUTSIDE;
	return static_cast<int16_t>(sdf * SDF_QUANTIZATION);
}

inline float dequantize_sdf(int16_t sdf) {
	return static_cast<float>(sdf) / SDF_QUANTIZATION;
}

inline float occupancy_to_sdf(uint32_t occupancy) {
	float normalized = static_cast<float>(occupancy) / static_cast<float>(OCCUPANCY_MAX);
	return (0.5f - normalized) * 2.0f;
}

inline uint32_t sdf_to_occupancy(float sdf) {
	float normalized = 0.5f - sdf * 0.5f;
	if (normalized <= 0.0f) return 0;
	if (normalized >= 1.0f) return OCCUPANCY_MAX;
	return static_cast<uint32_t>(normalized * static_cast<float>(OCCUPANCY_MAX) + 0.5f);
}

inline uint32_t make_cell(uint32_t material, uint32_t occupancy = OCCUPANCY_MAX) {
	material &= MATERIAL_MAX;
	occupancy = occupancy > OCCUPANCY_MAX ? OCCUPANCY_MAX : occupancy;
	if (material == MATERIAL_AIR || occupancy == 0) return BLOCK_AIR;
	return material | (occupancy << 8);
}

inline uint32_t make_block(uint32_t type) {
	if (type == BLOCK_AIR) return BLOCK_AIR;
	if ((type & 0xFF00) != 0) return make_cell(type & MATERIAL_MAX, (type >> 8) & 0xFF);
	return make_cell(type & MATERIAL_MAX, OCCUPANCY_MAX);
}

inline uint32_t block_type(uint32_t block) { return block & MATERIAL_MAX; }
inline bool is_air(uint32_t block) { return (block & MATERIAL_MAX) == MATERIAL_AIR || ((block >> 8) & 0xFF) == 0; }
inline bool is_water_cell(uint32_t block) { return !is_air(block) && block_type(block) == MATERIAL_WATER; }
inline uint32_t block_occupancy(uint32_t block) { return is_air(block) ? 0 : ((block >> 8) & 0xFF); }
inline bool is_solid_cell(uint32_t block) {
	return !is_air(block) && block_type(block) != MATERIAL_WATER && block_occupancy(block) >= OCCUPANCY_SURFACE;
}
inline float occupancy_density(uint32_t block) {
	return static_cast<float>(block_occupancy(block)) / static_cast<float>(OCCUPANCY_MAX) * 2.0f - 1.0f;
}

inline uint32_t merge_mip_cells(uint32_t a, uint32_t b) {
	if (is_air(a)) return b;
	if (is_air(b)) return a;

	uint32_t ma = block_type(a);
	uint32_t mb = block_type(b);
	uint32_t oa = block_occupancy(a);
	uint32_t ob = block_occupancy(b);

	if (ma == mb) {
		uint32_t occ = oa + ob;
		return make_cell(ma, occ > OCCUPANCY_MAX ? OCCUPANCY_MAX : occ);
	}

	if (oa != ob) return oa > ob ? a : b;
	return ma > mb ? a : b;
}

inline uint32_t material_color(uint32_t material) {
	switch (material) {
		case MATERIAL_WATER: return 0x3D7FCF;
		case MATERIAL_GRASS: return 0x58B858;
		case MATERIAL_DIRT: return 0x725337;
		case MATERIAL_SAND: return 0xCDBE6D;
		case MATERIAL_ROCK: return 0x898989;
		case MATERIAL_SNOW: return 0xE6F1F4;
		default: return 0x58B858;
	}
}

struct ChunkPosition {
	int64_t x = 0;
	int64_t y = 0;
	int64_t z = 0;

	ChunkPosition() = default;
	ChunkPosition(int64_t p_x, int64_t p_z) : x(p_x), y(0), z(p_z) {}
	ChunkPosition(int64_t p_x, int64_t p_y, int64_t p_z) : x(p_x), y(p_y), z(p_z) {}

	bool operator==(const ChunkPosition &o) const { return x == o.x && y == o.y && z == o.z; }
	bool operator<(const ChunkPosition &o) const {
		if (x != o.x) return x < o.x;
		if (y != o.y) return y < o.y;
		return z < o.z;
	}
};

namespace std {
template <>
struct hash<ChunkPosition> {
	size_t operator()(const ChunkPosition &k) const noexcept {
		uint64_t h = static_cast<uint64_t>(k.x) * 73856093ULL;
		h ^= static_cast<uint64_t>(k.y) * 19349663ULL;
		h ^= static_cast<uint64_t>(k.z) * 83492791ULL;
		return static_cast<size_t>(h);
	}
};
} // namespace std

#endif
