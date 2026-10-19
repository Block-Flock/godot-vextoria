#ifndef VEXTORIA_ROBLOX_SMOOTH_LOD_OCTREE_H
#define VEXTORIA_ROBLOX_SMOOTH_LOD_OCTREE_H

#include "core/math/vector3.h"

#include <array>
#include <cstddef>
#include <unordered_set>
#include <vector>


// Rendering key used by the Roblox-2016 SmoothCluster-style terrain octree.
// Coordinates are level-local: level 0 addresses 16-cell cubes; every higher
// level doubles the covered source-cell extent on all three axes.
struct RobloxSmoothLodKey {
	int x = 0;
	int y = 0;
	int z = 0;
	int level = 0;

	bool operator==(const RobloxSmoothLodKey &other) const {
		return x == other.x && y == other.y && z == other.z && level == other.level;
	}
};

struct RobloxSmoothLodKeyHash {
	size_t operator()(const RobloxSmoothLodKey &key) const noexcept;
};

class RobloxSmoothLodOctreePolicy {
public:
	// Recovered from Roblox 2016 Rendering/GfxRender/SmoothCluster.cpp.
	static constexpr int BASE_CHUNK_LOG2 = 4;
	static constexpr int BASE_CHUNK_CELLS = 1 << BASE_CHUNK_LOG2; // 16
	// Match the four cached Roblox Voxel2 chunk mips. The fine level preserves
	// narrow rivers and cliffs; the three coarser levels keep the outer map from
	// turning into hundreds of level-0 meshes during the initial join.
	// Roblox-main's SmoothClusterLOD keeps five selectable levels: 16, 32,
	// 64, 128, and 256 source cells. The coarsest level is important for a
	// large imported map: without it, neighboring leaves stop at level 3 and
	// their two-cell aprons can overlap instead of merging at the same edge.
	static constexpr int CHUNK_LEVELS = 5;
	// The imported SmoothGrid sample has a one-cell voxel boundary on both sides
	// of the authored chunk region; in this representation that requires two
	// sample cells in the mesher apron. Border triangles are still excluded, so
	// neighboring leaves meet on the authored boundary instead of exposing the
	// sampling apron.
	static constexpr int MESH_BORDER_CELLS = 2;
	static constexpr double SPLIT_RATIO = 4.0;
	static constexpr double MERGE_CHILD_RATIO = 8.0;

	using KeySet = std::unordered_set<RobloxSmoothLodKey, RobloxSmoothLodKeyHash>;
	using Coverage = std::array<KeySet, CHUNK_LEVELS>;

	static int cell_extent(int level);
	static int lod_stride(int level);
	static int mesher_interior_samples();
	static int mesher_grid_samples();

	static RobloxSmoothLodKey parent(const RobloxSmoothLodKey &key);
	static RobloxSmoothLodKey child(const RobloxSmoothLodKey &key, int x, int y, int z);

	static Vector3 minimum(const RobloxSmoothLodKey &key, double cell_size = 1.0);
	static Vector3 maximum(const RobloxSmoothLodKey &key, double cell_size = 1.0);
	static Vector3 center(const RobloxSmoothLodKey &key, double cell_size = 1.0);
	static double half_extent(const RobloxSmoothLodKey &key, double cell_size = 1.0);
	static double distance_to_bounds(const RobloxSmoothLodKey &key, const Vector3 &viewer, double cell_size = 1.0);
	static double distance_to_bounds(const RobloxSmoothLodKey &key, const std::vector<Vector3> &viewers, double cell_size = 1.0);
	static double center_ratio(const RobloxSmoothLodKey &key, const Vector3 &viewer, double cell_size = 1.0);
	static double center_ratio(const RobloxSmoothLodKey &key, const std::vector<Vector3> &viewers, double cell_size = 1.0);

	// Build all ancestor coverage sets from level-0 regions that may contain
	// terrain. Missing siblings are intentionally allowed; Roblox's merge path
	// also ignored siblings that did not exist.
	static Coverage build_coverage(const KeySet &level0);

	// Select a non-overlapping octree leaf set. A currently rendered parent is
	// retained until d/r < 4. A parent that is not currently rendered is only
	// selected when all covered immediate children satisfy d/r > 8, matching
	// the original sibling-coalescing merge criterion as closely as possible in
	// a deterministic desired-state pass.
	static std::vector<RobloxSmoothLodKey> select_leaves(
			const Coverage &coverage,
			const KeySet &currently_rendered,
			const Vector3 &viewer,
			double view_distance,
			double cell_size = 1.0,
			int maximum_level = CHUNK_LEVELS - 1);
	static std::vector<RobloxSmoothLodKey> select_leaves(
			const Coverage &coverage,
			const KeySet &currently_rendered,
			const std::vector<Vector3> &viewers,
			double view_distance,
			double cell_size = 1.0,
			int maximum_level = CHUNK_LEVELS - 1);

private:
	static int floor_div2(int value);
	static bool children_are_merge_ready(
			const RobloxSmoothLodKey &key,
			const Coverage &coverage,
			const std::vector<Vector3> &viewers,
			double cell_size);
	static void select_recursive(
			const RobloxSmoothLodKey &key,
			const Coverage &coverage,
			const KeySet &currently_rendered,
			const std::vector<Vector3> &viewers,
			double view_distance,
			double cell_size,
			int maximum_level,
			std::vector<RobloxSmoothLodKey> &out);
};


#endif
