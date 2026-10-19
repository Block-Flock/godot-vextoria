#ifndef VEXTORIA_ROBLOX_SMOOTH_MIP_CACHE_H
#define VEXTORIA_ROBLOX_SMOOTH_MIP_CACHE_H

#include "terrain_data.h"
#include "roblox_smooth_lod_octree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>


struct RobloxSmoothMipCell {
	uint8_t material = 0;
	uint8_t occupancy = 0;

	bool empty() const { return material == 0 || occupancy == 0; }
};

struct RobloxSmoothMipKey {
	int x = 0;
	int y = 0;
	int z = 0;

	bool operator==(const RobloxSmoothMipKey &other) const {
		return x == other.x && y == other.y && z == other.z;
	}
};

struct RobloxSmoothMipKeyHash {
	size_t operator()(const RobloxSmoothMipKey &key) const noexcept;
};

// Sparse equivalent of Roblox 2016 Voxel2::Grid's per-chunk mip pyramid.
// Air is implicit, so only non-empty cells are stored at each level. Level 4
// is generated too even though Roblox storage cached only levels 0..3; the
// original Grid::read generated higher requested LODs by downsampling the last
// cached mip on demand.
class RobloxSmoothMipCache {
public:
	static constexpr int LEVELS = RobloxSmoothLodOctreePolicy::CHUNK_LEVELS;
	using LevelMap = std::unordered_map<RobloxSmoothMipKey, RobloxSmoothMipCell, RobloxSmoothMipKeyHash>;

	void clear();
	void rebuild(const Ref<TerrainData> &data, int p_max_level = LEVELS - 1);
	void update_region(const Ref<TerrainData> &data, const Vector3i &p_min, const Vector3i &p_max);

	RobloxSmoothMipCell get_coarse(int x, int y, int z, int level) const;
	RobloxSmoothMipCell get_aligned_source(int source_x, int source_y, int source_z, int level) const;
	// A coarse SmoothCluster sample can collapse a thin or carved feature into
	// a pair of surfaces that no longer covers the source feature. Keep those
	// leaves at a finer level while retaining coarse LOD for cells with a real
	// interior. This is evaluated from the same cached mip data used by the
	// Roblox-compatible mesher, not from camera-dependent geometry heuristics.
	bool supports_lod_leaf(const RobloxSmoothLodKey &key) const;

	const LevelMap &level_cells(int level) const;
	RobloxSmoothLodOctreePolicy::KeySet get_level0_render_regions() const;

private:
	std::array<LevelMap, LEVELS> _levels;
	mutable std::unordered_map<RobloxSmoothLodKey, bool, RobloxSmoothLodKeyHash> _lod_support_cache;
	Ref<TerrainData> _source_data;
	int _max_level = LEVELS - 1;

	static int floor_div(int value, int divisor);
	static RobloxSmoothMipKey parent_key(const RobloxSmoothMipKey &key);
	static RobloxSmoothMipCell downsample(
			const RobloxSmoothMipCell &c000,
			const RobloxSmoothMipCell &c001,
			const RobloxSmoothMipCell &c010,
			const RobloxSmoothMipCell &c011,
			const RobloxSmoothMipCell &c100,
			const RobloxSmoothMipCell &c101,
			const RobloxSmoothMipCell &c110,
			const RobloxSmoothMipCell &c111);
};


#endif
