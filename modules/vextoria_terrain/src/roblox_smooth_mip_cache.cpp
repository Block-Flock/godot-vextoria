#include "roblox_smooth_mip_cache.h"
#include "roblox_smooth_material_rules.h"
#include "terrain_types.h"

#include "core/variant/variant.h"
#include "core/variant/typed_array.h"
#include "core/math/vector3i.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>


namespace {

struct WeightedMaterial {
	uint8_t material = 0;
	uint32_t occupancy = 0;
};

// Keep the eight source children together while building one mip cell.  The
// Roblox reduction is deliberately tree-shaped (rather than associative), so
// collecting the exact eight inputs lets us remove the second pass of
// TerrainData::get_block() lookups without changing the material winner or
// occupancy rounding.
struct MipAccumulator {
	std::array<RobloxSmoothMipCell, 8> children{};
};

static int child_slot(const RobloxSmoothMipKey &key, const RobloxSmoothMipKey &parent) {
	const int local_x = key.x - parent.x * 2;
	const int local_y = key.y - parent.y * 2;
	const int local_z = key.z - parent.z * 2;
	return local_x | (local_y << 1) | (local_z << 2);
}

static WeightedMaterial merge_material(const WeightedMaterial &a, const WeightedMaterial &b) {
	if (a.material == b.material)
		return {a.material, a.occupancy + b.occupancy};
	if (a.occupancy != b.occupancy)
		return a.occupancy > b.occupancy ? a : b;

	const uint8_t material = roblox_smooth_material_rules::resolve_mip_equal_weight(a.material, b.material);
	return material == a.material ? a : b;
}

} // namespace

size_t RobloxSmoothMipKeyHash::operator()(const RobloxSmoothMipKey &key) const noexcept {
	uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(key.x)) * 73856093ull;
	h ^= static_cast<uint64_t>(static_cast<uint32_t>(key.y)) * 19349663ull;
	h ^= static_cast<uint64_t>(static_cast<uint32_t>(key.z)) * 83492791ull;
	return static_cast<size_t>(h);
}

void RobloxSmoothMipCache::clear() {
	for (LevelMap &level : _levels)
		level.clear();
	_lod_support_cache.clear();
	_source_data.unref();
}

int RobloxSmoothMipCache::floor_div(int value, int divisor) {
	if (divisor <= 0)
		return 0;
	return value >= 0 ? value / divisor : (value - divisor + 1) / divisor;
}

RobloxSmoothMipKey RobloxSmoothMipCache::parent_key(const RobloxSmoothMipKey &key) {
	return {
		floor_div(key.x, 2),
		floor_div(key.y, 2),
		floor_div(key.z, 2),
	};
}

RobloxSmoothMipCell RobloxSmoothMipCache::downsample(
		const RobloxSmoothMipCell &c000,
		const RobloxSmoothMipCell &c001,
		const RobloxSmoothMipCell &c010,
		const RobloxSmoothMipCell &c011,
		const RobloxSmoothMipCell &c100,
		const RobloxSmoothMipCell &c101,
		const RobloxSmoothMipCell &c110,
		const RobloxSmoothMipCell &c111) {
	const uint32_t occupancy_sum =
		static_cast<uint32_t>(c000.occupancy) + c001.occupancy + c010.occupancy + c011.occupancy
		+ c100.occupancy + c101.occupancy + c110.occupancy + c111.occupancy;
	const uint8_t occupancy = static_cast<uint8_t>((occupancy_sum + 7u) / 8u);

	// Match App/voxel2/Grid.cpp exactly. The source rows pair X first, then Y,
	// then Z; preserving this tree matters because the material reduction drops
	// the losing branch's weight and therefore is not generally associative.
	const WeightedMaterial m00 = merge_material({c000.material, c000.occupancy}, {c001.material, c001.occupancy});
	const WeightedMaterial m01 = merge_material({c010.material, c010.occupancy}, {c011.material, c011.occupancy});
	const WeightedMaterial m10 = merge_material({c100.material, c100.occupancy}, {c101.material, c101.occupancy});
	const WeightedMaterial m11 = merge_material({c110.material, c110.occupancy}, {c111.material, c111.occupancy});
	const WeightedMaterial m0 = merge_material(m00, m01);
	const WeightedMaterial m1 = merge_material(m10, m11);
	const WeightedMaterial result = merge_material(m0, m1);

	if (result.material == MATERIAL_AIR || occupancy == 0)
		return {};
	return {result.material, occupancy};
}

void RobloxSmoothMipCache::rebuild(const Ref<TerrainData> &data, int p_max_level) {
	clear();
	if (data.is_null())
		return;
	p_max_level = std::clamp(p_max_level, 0, LEVELS - 1);
	_source_data = data;
	_max_level = p_max_level;

	if (data->get_chunk_size() <= 0 || data->get_chunk_height() <= 0)
		return;

	// Level 0 is already held densely by TerrainData.  Build level 1 by
	// accumulating each populated source sample directly into its parent.  The
	// old implementation first collected parent keys, then called get_block()
	// eight times for every parent; each call repeated a chunk hash lookup.  The
	// accumulator preserves the exact eight child slots while touching each
	// populated source sample only once.
	if (p_max_level < 1) return;

	size_t active_count = 0;
	for (const auto &[_, chunk] : data->_chunks) active_count += chunk.active_indices.size();
	std::unordered_map<RobloxSmoothMipKey, MipAccumulator, RobloxSmoothMipKeyHash> accumulators;
	accumulators.reserve((active_count + 1) / 2);
	for (const auto &[key, chunk] : data->_chunks) {
		const int source_x = key.x * data->_chunk_size;
		const int source_z = key.z * data->_chunk_size;
		for (uint32_t raw_index : chunk.active_indices) {
			const int index = static_cast<int>(raw_index);
			if (index < 0 || index >= static_cast<int>(chunk.samples.size())) continue;
			const TerrainData::Sample &sample = chunk.samples[index];
			const uint32_t occupancy = sdf_to_occupancy(dequantize_sdf(sample.sdf));
			if (sample.material == MATERIAL_AIR || occupancy == 0) continue;

			const int local_x = index % data->_chunk_size;
			const int plane = index / data->_chunk_size;
			const int local_z = plane % data->_chunk_size;
			const int source_y = plane / data->_chunk_size;
			const RobloxSmoothMipKey source_key{source_x + local_x, source_y, source_z + local_z};
			const RobloxSmoothMipKey parent = parent_key(source_key);
			accumulators[parent].children[child_slot(source_key, parent)] = {
				static_cast<uint8_t>(sample.material), static_cast<uint8_t>(occupancy)};
		}
	}

	LevelMap &level_one = _levels[1];
	level_one.reserve(accumulators.size());
	for (const auto &[key, accumulator] : accumulators) {
		const auto &c = accumulator.children;
		const RobloxSmoothMipCell value = downsample(
			c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
		if (!value.empty()) level_one.insert_or_assign(key, value);
	}

	// Coarser levels are built from the already-reduced previous level.  This
	// uses the same exact reduction order, but avoids eight sparse-map probes for
	// every destination cell by collecting the child slots in one pass.
	for (int level = 2; level <= p_max_level; ++level) {
		const LevelMap &source = _levels[level - 1];
		accumulators.clear();
		accumulators.reserve((source.size() + 1) / 2);
		for (const auto &[key, value] : source) {
			const RobloxSmoothMipKey parent = parent_key(key);
			accumulators[parent].children[child_slot(key, parent)] = value;
		}

		LevelMap &destination = _levels[level];
		destination.reserve(accumulators.size());
		for (const auto &[key, accumulator] : accumulators) {
			const auto &c = accumulator.children;
			const RobloxSmoothMipCell value = downsample(
				c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
			if (!value.empty()) destination.insert_or_assign(key, value);
		}
	}
}

void RobloxSmoothMipCache::update_region(const Ref<TerrainData> &data, const Vector3i &, const Vector3i &) {
	if (data.is_null())
		return;
	// Level 0 is owned by TerrainData, so rebuilding the small coarse pyramid
	// is the simplest correct update path after an edit. Initial imports never
	// enter this method; the full rebuild above remains sparse and bounded.
	rebuild(data, _max_level);
}

RobloxSmoothMipCell RobloxSmoothMipCache::get_coarse(int x, int y, int z, int level) const {
	level = std::clamp(level, 0, LEVELS - 1);
	if (level == 0 && _source_data.is_valid()) {
		const uint32_t block = _source_data->get_block(x, y, z);
		return {static_cast<uint8_t>(block_type(block)), static_cast<uint8_t>(block_occupancy(block))};
	}
	const LevelMap &cells = _levels[level];
	auto it = cells.find({x, y, z});
	return it == cells.end() ? RobloxSmoothMipCell{} : it->second;
}

RobloxSmoothMipCell RobloxSmoothMipCache::get_aligned_source(int source_x, int source_y, int source_z, int level) const {
	level = std::clamp(level, 0, LEVELS - 1);
	const int stride = 1 << level;
	return get_coarse(
		floor_div(source_x, stride),
		floor_div(source_y, stride),
		floor_div(source_z, stride),
		level);
}

bool RobloxSmoothMipCache::supports_lod_leaf(const RobloxSmoothLodKey &key) const {
	if (key.level <= 2)
		return true;
	if (key.level >= LEVELS)
		return false;

	auto cached = _lod_support_cache.find(key);
	if (cached != _lod_support_cache.end())
		return cached->second;

	// A level-N leaf covers exactly 16^3 cells in the level-N mip. A cell
	// whose occupancy is below the full-cell range is a surface sample; if a
	// leaf contains only such samples, downsampling it further collapses thin
	// walls, slabs, rivers, and cave roofs instead of preserving the source
	// surface. Require a meaningful full-cell interior before selecting L3/L4.
	const int begin_x = key.x * RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS;
	const int begin_y = key.y * RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS;
	const int begin_z = key.z * RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS;
	int full_cells = 0;
	int partial_cells = 0;
	const LevelMap &level = _levels[key.level];
	for (int y = 0; y < RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS; y++) {
		for (int z = 0; z < RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS; z++) {
			for (int x = 0; x < RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS; x++) {
				auto cell = level.find({begin_x + x, begin_y + y, begin_z + z});
				if (cell == level.end() || cell->second.empty())
					continue;
				if (cell->second.occupancy >= 240)
					full_cells++;
				else
					partial_cells++;
			}
		}
	}

	// Keep the fallback conservative. A solid interior must dominate the
	// surface samples; otherwise the next coarser march is not geometrically
	// representative enough to be used at an LOD transition.
	const bool supported = full_cells > 0 && partial_cells <= full_cells * 2;
	_lod_support_cache.insert_or_assign(key, supported);
	return supported;
}

const RobloxSmoothMipCache::LevelMap &RobloxSmoothMipCache::level_cells(int level) const {
	static const LevelMap empty;
	if (level < 0 || level >= LEVELS)
		return empty;
	return _levels[level];
}

RobloxSmoothLodOctreePolicy::KeySet RobloxSmoothMipCache::get_level0_render_regions() const {
	RobloxSmoothLodOctreePolicy::KeySet result;
	if (_source_data.is_valid()) {
		for (const auto &[key, chunk] : _source_data->_chunks) {
			const int source_x = key.x * _source_data->_chunk_size;
			const int source_z = key.z * _source_data->_chunk_size;
			for (uint32_t raw_index : chunk.active_indices) {
				const int index = static_cast<int>(raw_index);
				if (index < 0 || index >= static_cast<int>(chunk.samples.size())) continue;
				const int x = index % _source_data->_chunk_size;
				const int plane = index / _source_data->_chunk_size;
				const int z = plane % _source_data->_chunk_size;
				const int y = plane / _source_data->_chunk_size;
				result.insert({
					floor_div(source_x + x, RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS),
					floor_div(y, RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS),
					floor_div(source_z + z, RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS),
					0,
				});
			}
		}
		return result;
	}

	result.reserve((_levels[0].size() + RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS - 1)
		/ RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS);
	for (const auto &entry : _levels[0]) {
		const RobloxSmoothMipKey &cell = entry.first;
		result.insert({floor_div(cell.x, RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS),
			floor_div(cell.y, RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS),
			floor_div(cell.z, RobloxSmoothLodOctreePolicy::BASE_CHUNK_CELLS), 0});
	}
	return result;
}

