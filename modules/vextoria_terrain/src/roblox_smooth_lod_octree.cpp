#include "roblox_smooth_lod_octree.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>


size_t RobloxSmoothLodKeyHash::operator()(const RobloxSmoothLodKey &key) const noexcept {
	uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(key.x)) * 73856093ull;
	h ^= static_cast<uint64_t>(static_cast<uint32_t>(key.y)) * 19349663ull;
	h ^= static_cast<uint64_t>(static_cast<uint32_t>(key.z)) * 83492791ull;
	h ^= static_cast<uint64_t>(static_cast<uint32_t>(key.level)) * 2654435761ull;
	return static_cast<size_t>(h);
}

int RobloxSmoothLodOctreePolicy::cell_extent(int level) {
	level = std::clamp(level, 0, CHUNK_LEVELS - 1);
	return BASE_CHUNK_CELLS << level;
}

int RobloxSmoothLodOctreePolicy::lod_stride(int level) {
	level = std::clamp(level, 0, CHUNK_LEVELS - 1);
	return 1 << level;
}

int RobloxSmoothLodOctreePolicy::mesher_interior_samples() {
	return BASE_CHUNK_CELLS;
}

int RobloxSmoothLodOctreePolicy::mesher_grid_samples() {
	// SmoothClusterLOD expands the logical region by 2 << lod on each side;
	// after Grid::read(..., lod), that is two coarse samples per side.
	return BASE_CHUNK_CELLS + MESH_BORDER_CELLS * 2;
}

int RobloxSmoothLodOctreePolicy::floor_div2(int value) {
	return value >= 0 ? value / 2 : (value - 1) / 2;
}

RobloxSmoothLodKey RobloxSmoothLodOctreePolicy::parent(const RobloxSmoothLodKey &key) {
	return {
		floor_div2(key.x),
		floor_div2(key.y),
		floor_div2(key.z),
		std::min(key.level + 1, CHUNK_LEVELS - 1),
	};
}

RobloxSmoothLodKey RobloxSmoothLodOctreePolicy::child(const RobloxSmoothLodKey &key, int x, int y, int z) {
	return {
		key.x * 2 + (x != 0 ? 1 : 0),
		key.y * 2 + (y != 0 ? 1 : 0),
		key.z * 2 + (z != 0 ? 1 : 0),
		std::max(key.level - 1, 0),
	};
}

Vector3 RobloxSmoothLodOctreePolicy::minimum(const RobloxSmoothLodKey &key, double cell_size) {
	const double size = static_cast<double>(cell_extent(key.level)) * cell_size;
	return Vector3(
		static_cast<double>(key.x) * size,
		static_cast<double>(key.y) * size,
		static_cast<double>(key.z) * size);
}

Vector3 RobloxSmoothLodOctreePolicy::maximum(const RobloxSmoothLodKey &key, double cell_size) {
	const double size = static_cast<double>(cell_extent(key.level)) * cell_size;
	return minimum(key, cell_size) + Vector3(size, size, size);
}

Vector3 RobloxSmoothLodOctreePolicy::center(const RobloxSmoothLodKey &key, double cell_size) {
	const double half = half_extent(key, cell_size);
	return minimum(key, cell_size) + Vector3(half, half, half);
}

double RobloxSmoothLodOctreePolicy::half_extent(const RobloxSmoothLodKey &key, double cell_size) {
	return static_cast<double>(cell_extent(key.level)) * cell_size * 0.5;
}

double RobloxSmoothLodOctreePolicy::distance_to_bounds(const RobloxSmoothLodKey &key, const Vector3 &viewer, double cell_size) {
	return distance_to_bounds(key, std::vector<Vector3>{viewer}, cell_size);
}

double RobloxSmoothLodOctreePolicy::distance_to_bounds(const RobloxSmoothLodKey &key, const std::vector<Vector3> &viewers, double cell_size) {
	if (viewers.empty()) return std::numeric_limits<double>::infinity();
	const Vector3 min = minimum(key, cell_size);
	const Vector3 max = maximum(key, cell_size);
	double nearest_distance = std::numeric_limits<double>::infinity();
	for (const Vector3 &viewer : viewers) {
		const Vector3 nearest(
				std::clamp(viewer.x, min.x, max.x),
				std::clamp(viewer.y, min.y, max.y),
				std::clamp(viewer.z, min.z, max.z));
		nearest_distance = std::min(nearest_distance, static_cast<double>(nearest.distance_to(viewer)));
	}
	return nearest_distance;
}

double RobloxSmoothLodOctreePolicy::center_ratio(const RobloxSmoothLodKey &key, const Vector3 &viewer, double cell_size) {
	return center_ratio(key, std::vector<Vector3>{viewer}, cell_size);
}

double RobloxSmoothLodOctreePolicy::center_ratio(const RobloxSmoothLodKey &key, const std::vector<Vector3> &viewers, double cell_size) {
	if (viewers.empty()) return std::numeric_limits<double>::infinity();
	const double radius = half_extent(key, cell_size);
	if (radius <= 0.0)
		return 0.0;
	const Vector3 key_center = center(key, cell_size);
	double nearest_ratio = std::numeric_limits<double>::infinity();
	for (const Vector3 &viewer : viewers)
		nearest_ratio = std::min(nearest_ratio, key_center.distance_to(viewer) / radius);
	return nearest_ratio;
}

RobloxSmoothLodOctreePolicy::Coverage RobloxSmoothLodOctreePolicy::build_coverage(const KeySet &level0) {
	Coverage coverage;
	coverage[0].reserve(level0.size());
	for (const RobloxSmoothLodKey &source : level0) {
		coverage[0].insert({source.x, source.y, source.z, 0});
	}

	for (int level = 1; level < CHUNK_LEVELS; ++level) {
		coverage[level].reserve((coverage[level - 1].size() + 7) / 8);
		for (const RobloxSmoothLodKey &key : coverage[level - 1])
			coverage[level].insert(parent(key));
	}

	return coverage;
}

bool RobloxSmoothLodOctreePolicy::children_are_merge_ready(
		const RobloxSmoothLodKey &key,
		const Coverage &coverage,
		const std::vector<Vector3> &viewers,
		double cell_size) {
	if (key.level <= 0)
		return false;

	bool has_covered_child = false;
	for (int x = 0; x < 2; ++x)
		for (int y = 0; y < 2; ++y)
			for (int z = 0; z < 2; ++z) {
				const RobloxSmoothLodKey c = child(key, x, y, z);
				if (coverage[c.level].find(c) == coverage[c.level].end())
					continue;
				has_covered_child = true;
				if (center_ratio(c, viewers, cell_size) <= MERGE_CHILD_RATIO)
					return false;
			}

	return has_covered_child;
}

void RobloxSmoothLodOctreePolicy::select_recursive(
		const RobloxSmoothLodKey &key,
		const Coverage &coverage,
		const KeySet &currently_rendered,
		const std::vector<Vector3> &viewers,
		double view_distance,
		double cell_size,
		int maximum_level,
		std::vector<RobloxSmoothLodKey> &out) {
	if (key.level < 0 || key.level >= CHUNK_LEVELS)
		return;
	if (coverage[key.level].find(key) == coverage[key.level].end())
		return;

	const double distance = distance_to_bounds(key, viewers, cell_size);

	// Vextoria keeps authored terrain resident instead of treating the visual
	// distance as a hard deletion radius. Once a covered region leaves the
	// fine-detail horizon, collapse it to the coarsest allowed octree leaf and
	// keep that leaf alive. This matches the expected "LOD farther, don't pop
	// out" behavior and prevents a visible terrain wall from following the
	// camera around large imported places.
	if (distance > view_distance) {
		if (key.level <= maximum_level) {
			out.push_back(key);
			return;
		}
		for (int x = 0; x < 2; ++x)
			for (int y = 0; y < 2; ++y)
				for (int z = 0; z < 2; ++z)
					select_recursive(child(key, x, y, z), coverage, currently_rendered, viewers,
							view_distance, cell_size, maximum_level, out);
		return;
	}

	if (key.level == 0 || key.level > maximum_level) {
		if (key.level > maximum_level) {
			for (int x = 0; x < 2; ++x)
				for (int y = 0; y < 2; ++y)
					for (int z = 0; z < 2; ++z)
						select_recursive(child(key, x, y, z), coverage, currently_rendered, viewers,
								view_distance, cell_size, maximum_level, out);
			return;
		}
		out.push_back(key);
		return;
	}

	const bool parent_is_rendered = currently_rendered.find(key) != currently_rendered.end();
	const double ratio = center_ratio(key, viewers, cell_size);

	// Original SmoothClusterLOD behavior:
	//   rendered parent splits when parent d/r < 4;
	//   children merge when each existing sibling has child d/r > 8.
	// Selecting a desired leaf set top-down reproduces those two conditions
	// without making chunk identity depend on traversal order.
	const bool keep_parent = parent_is_rendered
		? ratio >= SPLIT_RATIO
		: children_are_merge_ready(key, coverage, viewers, cell_size);

	if (keep_parent) {
		out.push_back(key);
		return;
	}

	for (int x = 0; x < 2; ++x)
		for (int y = 0; y < 2; ++y)
			for (int z = 0; z < 2; ++z)
				select_recursive(child(key, x, y, z), coverage, currently_rendered, viewers,
						view_distance, cell_size, maximum_level, out);
}

std::vector<RobloxSmoothLodKey> RobloxSmoothLodOctreePolicy::select_leaves(
		const Coverage &coverage,
		const KeySet &currently_rendered,
		const Vector3 &viewer,
		double view_distance,
		double cell_size,
		int maximum_level) {
	return select_leaves(coverage, currently_rendered, std::vector<Vector3>{viewer}, view_distance, cell_size, maximum_level);
}

std::vector<RobloxSmoothLodKey> RobloxSmoothLodOctreePolicy::select_leaves(
		const Coverage &coverage,
		const KeySet &currently_rendered,
		const std::vector<Vector3> &viewers,
		double view_distance,
		double cell_size,
		int maximum_level) {
	std::vector<RobloxSmoothLodKey> result;
	if (view_distance <= 0.0 || cell_size <= 0.0 || viewers.empty())
		return result;
	maximum_level = std::clamp(maximum_level, 0, CHUNK_LEVELS - 1);

	for (const RobloxSmoothLodKey &root : coverage[CHUNK_LEVELS - 1])
		select_recursive(root, coverage, currently_rendered, viewers, view_distance, cell_size,
				maximum_level, result);

	std::sort(result.begin(), result.end(), [](const RobloxSmoothLodKey &a, const RobloxSmoothLodKey &b) {
		if (a.level != b.level) return a.level < b.level;
		if (a.x != b.x) return a.x < b.x;
		if (a.y != b.y) return a.y < b.y;
		return a.z < b.z;
	});
	return result;
}

