#include "terrain_data.h"

#include "core/variant/variant.h"
#include "core/object/class_db.h"
#include "core/string/print_string.h"

#include <algorithm>
#include <cmath>
#include <limits>


// PackedByteArray exposes byte storage in both external-binding and built-in
// module builds, but the binding convenience encode/decode helpers are not
// part of Godot's core PackedByteArray API. Keep the serialized terrain format
// explicitly little-endian so both build paths share the same bytes.
static void write_i16_le(PackedByteArray &p_bytes, int p_offset, int16_t p_value) {
	const uint16_t bits = static_cast<uint16_t>(p_value);
	p_bytes.set(p_offset, static_cast<uint8_t>(bits & 0xffu));
	p_bytes.set(p_offset + 1, static_cast<uint8_t>((bits >> 8) & 0xffu));
}

static int16_t read_i16_le(const PackedByteArray &p_bytes, int p_offset) {
	const uint16_t bits = static_cast<uint16_t>(p_bytes[p_offset]) |
			(static_cast<uint16_t>(p_bytes[p_offset + 1]) << 8);
	return static_cast<int16_t>(bits);
}

static uint32_t read_u32_le(const PackedByteArray &p_bytes, int p_offset) {
	return static_cast<uint32_t>(p_bytes[p_offset]) |
			(static_cast<uint32_t>(p_bytes[p_offset + 1]) << 8) |
			(static_cast<uint32_t>(p_bytes[p_offset + 2]) << 16) |
			(static_cast<uint32_t>(p_bytes[p_offset + 3]) << 24);
}

TerrainData::TerrainData() {}
TerrainData::~TerrainData() {}

void TerrainData::_bump_revision() {
	_revision++;
	if (_revision == 0) {
		_revision = 1;
		_dirty_history.clear();
	}
}

void TerrainData::_begin_edit() {
	_edit_depth++;
}

void TerrainData::_end_edit() {
	if (_edit_depth <= 0) return;
	_edit_depth--;
	if (_edit_depth == 0) _commit_pending_dirty();
}

void TerrainData::_mark_dirty(const Vector3i &p_minimum, const Vector3i &p_maximum) {
	const Vector3i minimum(
		std::min(p_minimum.x, p_maximum.x),
		std::min(p_minimum.y, p_maximum.y),
		std::min(p_minimum.z, p_maximum.z));
	const Vector3i maximum(
		std::max(p_minimum.x, p_maximum.x),
		std::max(p_minimum.y, p_maximum.y),
		std::max(p_minimum.z, p_maximum.z));

	if (!_pending_dirty) {
		_pending_minimum = minimum;
		_pending_maximum = maximum;
		_pending_dirty = true;
	} else {
		_pending_minimum = Vector3i(
			std::min(_pending_minimum.x, minimum.x),
			std::min(_pending_minimum.y, minimum.y),
			std::min(_pending_minimum.z, minimum.z));
		_pending_maximum = Vector3i(
			std::max(_pending_maximum.x, maximum.x),
			std::max(_pending_maximum.y, maximum.y),
			std::max(_pending_maximum.z, maximum.z));
	}
	if (_edit_depth == 0) _commit_pending_dirty();
}

void TerrainData::_mark_full_rebuild() {
	_pending_dirty = true;
	_pending_full_rebuild = true;
	if (_edit_depth == 0) _commit_pending_dirty();
}

void TerrainData::_commit_pending_dirty() {
	if (!_pending_dirty) return;

	_bump_revision();
	DirtyChange change;
	change.revision = _revision;
	change.minimum = _pending_minimum;
	change.maximum = _pending_maximum;
	change.full_rebuild = _pending_full_rebuild;
	_dirty_history.push_back(change);
	while (_dirty_history.size() > DIRTY_HISTORY_LIMIT)
		_dirty_history.pop_front();

	_pending_dirty = false;
	_pending_full_rebuild = false;
	_pending_minimum = Vector3i();
	_pending_maximum = Vector3i();
}

TerrainData::DirtyRegion TerrainData::query_dirty_region_since(uint64_t p_revision) const {
	DirtyRegion result;
	if (p_revision == _revision)
		return result;

	result.valid = true;
	if (p_revision > _revision || _dirty_history.empty()) {
		result.full_rebuild = true;
		return result;
	}

	const uint64_t oldest = _dirty_history.front().revision;
	if (oldest > 1 && p_revision < oldest - 1) {
		result.full_rebuild = true;
		return result;
	}

	bool found = false;
	for (const DirtyChange &change : _dirty_history) {
		if (change.revision <= p_revision) continue;
		if (change.full_rebuild) {
			result.full_rebuild = true;
			return result;
		}
		if (!found) {
			result.minimum = change.minimum;
			result.maximum = change.maximum;
			found = true;
		} else {
			result.minimum = Vector3i(
				std::min(result.minimum.x, change.minimum.x),
				std::min(result.minimum.y, change.minimum.y),
				std::min(result.minimum.z, change.minimum.z));
			result.maximum = Vector3i(
				std::max(result.maximum.x, change.maximum.x),
				std::max(result.maximum.y, change.maximum.y),
				std::max(result.maximum.z, change.maximum.z));
		}
	}

	if (!found)
		result.full_rebuild = true;
	return result;
}

Dictionary TerrainData::get_dirty_region_since(int64_t p_revision) const {
	const uint64_t revision = p_revision < 0 ? 0 : static_cast<uint64_t>(p_revision);
	const DirtyRegion region = query_dirty_region_since(revision);
	Dictionary result;
	result["valid"] = region.valid;
	result["full_rebuild"] = region.full_rebuild;
	result["minimum"] = region.minimum;
	result["maximum"] = region.maximum;
	result["revision"] = get_revision();
	return result;
}

int TerrainData::_index(int p_x, int p_y, int p_z) const {
	return (p_y * _chunk_size + p_z) * _chunk_size + p_x;
}

int TerrainData::_clamp(int p_val, int p_min, int p_max) const {
	if (p_val < p_min) return p_min;
	if (p_val > p_max) return p_max;
	return p_val;
}

void TerrainData::set_block(int p_x, int p_y, int p_z, uint32_t p_block) {
	set_cell(p_x, p_y, p_z, static_cast<int>(block_type(make_block(p_block))), static_cast<int>(block_occupancy(make_block(p_block))));
}

void TerrainData::set_cell(int p_x, int p_y, int p_z, int p_material, int p_occupancy) {
	set_sdf(p_x, p_y, p_z, occupancy_to_sdf(static_cast<uint32_t>(std::clamp(p_occupancy, 0, 255))), p_material);
}

void TerrainData::set_sdf(int p_x, int p_y, int p_z, double p_sdf, int p_material) {
	if (p_y < 0 || p_y >= _chunk_height) return;
	if (!std::isfinite(p_sdf)) return;

	Sample sample;
	sample.sdf = quantize_sdf(static_cast<float>(p_sdf));
	sample.material = static_cast<uint8_t>(std::clamp(p_material, 0, static_cast<int>(MATERIAL_MAX)));
	if (sample.sdf >= SDF_OUTSIDE) {
		sample.sdf = SDF_OUTSIDE;
		sample.material = MATERIAL_AIR;
	} else if (sample.sdf < 0 && sample.material == MATERIAL_AIR) {
		sample.material = MATERIAL_GRASS;
	}
	if (_set_sample(p_x, p_y, p_z, sample))
		_mark_dirty(Vector3i(p_x, p_y, p_z), Vector3i(p_x, p_y, p_z));
}

bool TerrainData::_set_sample(int p_x, int p_y, int p_z, const Sample &p_sample) {
	if (p_y < 0 || p_y >= _chunk_height) return false;

	int cx = p_x >= 0 ? p_x / _chunk_size : (p_x - _chunk_size + 1) / _chunk_size;
	int cz = p_z >= 0 ? p_z / _chunk_size : (p_z - _chunk_size + 1) / _chunk_size;

	int lx = p_x - cx * _chunk_size;
	int lz = p_z - cz * _chunk_size;
	if (lx < 0 || lx >= _chunk_size || lz < 0 || lz >= _chunk_size) return false;

	ChunkPosition key(cx, cz);
	auto it = _chunks.find(key);
	bool is_default = p_sample.sdf == SDF_OUTSIDE && p_sample.material == MATERIAL_AIR;
	if (it == _chunks.end()) {
		if (is_default) return false;
		Chunk c;
		c.samples.resize(_chunk_size * _chunk_height * _chunk_size);
		const int index = _index(lx, p_y, lz);
		c.samples[index] = p_sample;
		c.active_indices.push_back(static_cast<uint32_t>(index));
		c.active_count = 1;
		_chunks[key] = std::move(c);
		return true;
	}

	int index = _index(lx, p_y, lz);
	const Sample old = it->second.samples[index];
	if (old.sdf == p_sample.sdf && old.material == p_sample.material && old.flags == p_sample.flags)
		return false;

	bool had_value = old.sdf != SDF_OUTSIDE || old.material != MATERIAL_AIR;
	it->second.samples[index] = p_sample;
	if (is_default) {
		if (had_value) {
			it->second.active_count--;
			auto active = std::find(it->second.active_indices.begin(), it->second.active_indices.end(), static_cast<uint32_t>(index));
			if (active != it->second.active_indices.end()) {
				*active = it->second.active_indices.back();
				it->second.active_indices.pop_back();
			}
		}
		if (it->second.active_count <= 0)
			_chunks.erase(it);
	} else if (!had_value) {
		it->second.active_count++;
		it->second.active_indices.push_back(static_cast<uint32_t>(index));
	}
	return true;
}

uint32_t TerrainData::get_block(int p_x, int p_y, int p_z) const {
	Sample sample = _get_sample(p_x, p_y, p_z);
	return make_cell(sample.material, sdf_to_occupancy(dequantize_sdf(sample.sdf)));
}

TerrainData::Sample TerrainData::_get_sample(int p_x, int p_y, int p_z) const {
	if (p_y < 0 || p_y >= _chunk_height) return Sample();

	int cx = p_x >= 0 ? p_x / _chunk_size : (p_x - _chunk_size + 1) / _chunk_size;
	int cz = p_z >= 0 ? p_z / _chunk_size : (p_z - _chunk_size + 1) / _chunk_size;

	int lx = p_x - cx * _chunk_size;
	int lz = p_z - cz * _chunk_size;
	if (lx < 0 || lx >= _chunk_size || lz < 0 || lz >= _chunk_size) return Sample();

	auto it = _chunks.find(ChunkPosition(cx, cz));
	if (it == _chunks.end()) return Sample();

	return it->second.samples[_index(lx, p_y, lz)];
}

double TerrainData::get_sdf(int p_x, int p_y, int p_z) const {
	return dequantize_sdf(_get_sample(p_x, p_y, p_z).sdf);
}

int TerrainData::get_material(int p_x, int p_y, int p_z) const {
	return static_cast<int>(_get_sample(p_x, p_y, p_z).material);
}

int TerrainData::get_occupancy(int p_x, int p_y, int p_z) const {
	return static_cast<int>(sdf_to_occupancy(static_cast<float>(get_sdf(p_x, p_y, p_z))));
}

double TerrainData::get_column_height(int p_x, int p_z) const {
	for (int y = _chunk_height - 1; y >= 0; y--) {
		double sdf = get_sdf(p_x, y, p_z);
		if (sdf <= 0.0) {
			double above = get_sdf(p_x, y + 1, p_z);
			double denom = above - sdf;
			double fraction = std::abs(denom) < 0.0001 ? 0.5 : std::clamp(-sdf / denom, 0.0, 1.0);
			return static_cast<double>(y) + fraction;
		}
	}
	return 0.0;
}

int TerrainData::get_column_material(int p_x, int p_z) const {
	for (int y = _chunk_height - 1; y >= 0; y--) {
		if (get_sdf(p_x, y, p_z) <= 0.0)
			return get_material(p_x, y, p_z);
	}
	return static_cast<int>(MATERIAL_AIR);
}

void TerrainData::set_chunk_blocks(int p_cx, int p_cz, const PackedInt32Array &p_blocks) {
	int expected = _chunk_size * _chunk_height * _chunk_size;
	if (p_blocks.size() != expected) return;

	Chunk c;
	c.samples.resize(expected);
	for (int i = 0; i < expected; i++) {
		uint32_t block = make_block(static_cast<uint32_t>(p_blocks[i]));
		c.samples[i].sdf = quantize_sdf(occupancy_to_sdf(block_occupancy(block)));
		c.samples[i].material = static_cast<uint8_t>(block_type(block));
		if (c.samples[i].sdf != SDF_OUTSIDE || c.samples[i].material != MATERIAL_AIR) {
			c.active_count++;
			c.active_indices.push_back(static_cast<uint32_t>(i));
		}
	}

	if (c.active_count == 0)
		_chunks.erase(ChunkPosition(p_cx, p_cz));
	else
		_chunks[ChunkPosition(p_cx, p_cz)] = std::move(c);

	const int source_x = p_cx * _chunk_size;
	const int source_z = p_cz * _chunk_size;
	_mark_dirty(
		Vector3i(source_x, 0, source_z),
		Vector3i(source_x + _chunk_size - 1, _chunk_height - 1, source_z + _chunk_size - 1));
}

bool TerrainData::_write_voxel_block_channels(int p_bx, int p_by, int p_bz,
		const PackedByteArray &p_sdf, int p_sdf_offset,
		const PackedByteArray &p_render_material, int p_render_material_offset,
		const PackedByteArray &p_source_material, int p_source_material_offset,
		int p_source_min_y) {
	constexpr int source_size = 16;
	constexpr int source_cell_count = source_size * source_size * source_size;
	if (p_sdf_offset < 0 || p_render_material_offset < 0 || p_source_material_offset < 0
			|| p_sdf_offset + source_cell_count * 2 > p_sdf.size()
			|| p_render_material_offset + source_cell_count > p_render_material.size()
			|| p_source_material_offset + source_cell_count > p_source_material.size())
		return false;

	// VoxelBuffer's dense channels are [z][x][y], with Y contiguous. Keep the
	// conversion here so the JSON-published world does not call into C# once per
	// voxel. The arithmetic mirrors DecodeQuantizedSdf/SdfToOccupancy in
	// RobloxSmoothNativeBackend.cs.
	bool changed = false;
	for (int z = 0; z < source_size; z++) {
		for (int x = 0; x < source_size; x++) {
			for (int y = 0; y < source_size; y++) {
				const int source_index = y + source_size * (x + source_size * z);
				const int byte_index = p_sdf_offset + source_index * 2;
				const uint16_t raw_bits = static_cast<uint16_t>(p_sdf[byte_index])
					| (static_cast<uint16_t>(p_sdf[byte_index + 1]) << 8);
				const int16_t raw_sdf = static_cast<int16_t>(raw_bits);
				const float sdf = static_cast<float>(raw_sdf) / 32767.0f * 500.0f;
				const int occupancy = std::clamp(static_cast<int>(std::nearbyint((0.5f - sdf) * 255.0f)), 0, 255);
				if (occupancy == 0) continue;

				const int source_material = p_source_material[p_source_material_offset + source_index];
				const int render_material = p_render_material[p_render_material_offset + source_index];
				int material = source_material > 1 ? source_material : 0;
				if (material == 0) {
					switch (render_material) {
						case 0: material = 2; break; // Grass
						case 1: material = 14; break; // Ground
						case 2: material = 11; break; // Sandstone
						case 3: material = 6; break; // Sand
						case 4: material = 10; break; // Snow
						default: material = 8; break; // Rock
					}
				}
				if (material <= static_cast<int>(MATERIAL_AIR)) continue;

				const int world_x = p_bx * source_size + x;
				const int world_y = p_by * source_size + y - p_source_min_y;
				const int world_z = p_bz * source_size + z;
				if (world_y < 0 || world_y >= _chunk_height) continue;

				const int cx = world_x >= 0 ? world_x / _chunk_size : (world_x - _chunk_size + 1) / _chunk_size;
				const int cz = world_z >= 0 ? world_z / _chunk_size : (world_z - _chunk_size + 1) / _chunk_size;
				const int lx = world_x - cx * _chunk_size;
				const int lz = world_z - cz * _chunk_size;
				if (lx < 0 || lx >= _chunk_size || lz < 0 || lz >= _chunk_size) continue;

				const ChunkPosition key(cx, cz);
				auto it = _chunks.find(key);
				if (it == _chunks.end()) {
					Chunk chunk;
					chunk.samples.resize(_chunk_size * _chunk_height * _chunk_size);
					it = _chunks.emplace(key, std::move(chunk)).first;
				}
				Sample sample;
				sample.sdf = quantize_sdf(occupancy_to_sdf(static_cast<uint32_t>(occupancy)));
				sample.material = static_cast<uint8_t>(std::clamp(material, 0, static_cast<int>(MATERIAL_MAX)));
				Sample &destination = it->second.samples[_index(lx, world_y, lz)];
				const bool had_value = destination.sdf != SDF_OUTSIDE || destination.material != MATERIAL_AIR;
				if (destination.sdf == sample.sdf && destination.material == sample.material) continue;
				destination = sample;
				if (!had_value) {
					it->second.active_count++;
					it->second.active_indices.push_back(static_cast<uint32_t>(_index(lx, world_y, lz)));
				}
				changed = true;
			}
		}
	}
	return changed;
}

void TerrainData::set_voxel_block_channels(int p_bx, int p_by, int p_bz,
		const PackedByteArray &p_sdf, const PackedByteArray &p_render_material,
		const PackedByteArray &p_source_material, int p_source_min_y) {
	constexpr int source_size = 16;
	constexpr int source_cell_count = source_size * source_size * source_size;
	if (p_sdf.size() < source_cell_count * 2
			|| p_render_material.size() < source_cell_count
			|| p_source_material.size() < source_cell_count)
		return;

	_begin_edit();
	bool changed = _write_voxel_block_channels(
			p_bx, p_by, p_bz, p_sdf, 0, p_render_material, 0, p_source_material, 0, p_source_min_y);
	if (changed) {
		const int minimum_x = p_bx * source_size;
		const int minimum_y = std::max(0, p_by * source_size - p_source_min_y);
		const int minimum_z = p_bz * source_size;
		_mark_dirty(
			Vector3i(minimum_x, minimum_y, minimum_z),
			Vector3i(minimum_x + source_size - 1,
					std::min(_chunk_height - 1, minimum_y + source_size - 1),
					minimum_z + source_size - 1));
	}
	_end_edit();
}

void TerrainData::set_voxel_blocks_channels(const PackedInt32Array &p_block_positions,
		const PackedInt32Array &p_block_offsets, const PackedByteArray &p_sdf,
		const PackedByteArray &p_render_material, const PackedByteArray &p_source_material,
		const PackedInt32Array &p_water_samples, int p_source_min_y) {
	constexpr int source_size = 16;
	constexpr int source_cell_count = source_size * source_size * source_size;
	constexpr int sdf_bytes_per_block = source_cell_count * 2;
	constexpr int material_bytes_per_block = source_cell_count;
	if (p_block_positions.size() % 3 != 0)
		return;
	const int block_count = p_block_positions.size() / 3;
	if (p_block_offsets.size() != (block_count + 1) * 3)
		return;
	if (p_water_samples.size() % 5 != 0)
		return;
	for (int block = 0; block < block_count; block++) {
		const int sdf_begin = p_block_offsets[block * 3 + 0];
		const int render_begin = p_block_offsets[block * 3 + 1];
		const int source_begin = p_block_offsets[block * 3 + 2];
		const int sdf_end = p_block_offsets[(block + 1) * 3 + 0];
		const int render_end = p_block_offsets[(block + 1) * 3 + 1];
		const int source_end = p_block_offsets[(block + 1) * 3 + 2];
		if (sdf_begin < 0 || render_begin < 0 || source_begin < 0
				|| sdf_end - sdf_begin != sdf_bytes_per_block
				|| render_end - render_begin != material_bytes_per_block
				|| source_end - source_begin != material_bytes_per_block
				|| sdf_end > p_sdf.size() || render_end > p_render_material.size()
				|| source_end > p_source_material.size())
			return;
	}

	int minimum_x = std::numeric_limits<int>::max();
	int minimum_y = std::numeric_limits<int>::max();
	int minimum_z = std::numeric_limits<int>::max();
	int maximum_x = std::numeric_limits<int>::min();
	int maximum_y = std::numeric_limits<int>::min();
	int maximum_z = std::numeric_limits<int>::min();
	bool changed = false;
	_begin_edit();
	for (int block = 0; block < block_count; block++) {
		const int position_offset = block * 3;
		const int bx = p_block_positions[position_offset + 0];
		const int by = p_block_positions[position_offset + 1];
		const int bz = p_block_positions[position_offset + 2];
		const int sdf_begin = p_block_offsets[position_offset + 0];
		const int render_begin = p_block_offsets[position_offset + 1];
		const int source_begin = p_block_offsets[position_offset + 2];
		if (!_write_voxel_block_channels(bx, by, bz, p_sdf, sdf_begin,
				p_render_material, render_begin, p_source_material, source_begin, p_source_min_y))
			continue;
		const int block_minimum_y = std::max(0, by * source_size - p_source_min_y);
		minimum_x = std::min(minimum_x, bx * source_size);
		minimum_y = std::min(minimum_y, block_minimum_y);
		minimum_z = std::min(minimum_z, bz * source_size);
		maximum_x = std::max(maximum_x, bx * source_size + source_size - 1);
		maximum_y = std::max(maximum_y, std::min(_chunk_height - 1, block_minimum_y + source_size - 1));
		maximum_z = std::max(maximum_z, bz * source_size + source_size - 1);
		changed = true;
	}

	for (int sample_index = 0; sample_index < p_water_samples.size() / 5; sample_index++) {
		const int offset = sample_index * 5;
		const int x = p_water_samples[offset + 0];
		const int y = p_water_samples[offset + 1];
		const int z = p_water_samples[offset + 2];
		if (y < 0 || y >= _chunk_height)
			continue;
		const int material = std::clamp(p_water_samples[offset + 3], 0, static_cast<int>(MATERIAL_MAX));
		const int occupancy = std::clamp(p_water_samples[offset + 4], 0, static_cast<int>(OCCUPANCY_MAX));
		Sample sample;
		if (material != static_cast<int>(MATERIAL_AIR) && occupancy > 0) {
			sample.sdf = quantize_sdf(occupancy_to_sdf(static_cast<uint32_t>(occupancy)));
			sample.material = static_cast<uint8_t>(material);
		}
		if (!_set_sample(x, y, z, sample))
			continue;
		changed = true;
		minimum_x = std::min(minimum_x, x);
		minimum_y = std::min(minimum_y, y);
		minimum_z = std::min(minimum_z, z);
		maximum_x = std::max(maximum_x, x);
		maximum_y = std::max(maximum_y, y);
		maximum_z = std::max(maximum_z, z);
	}

	if (changed)
		_mark_dirty(Vector3i(minimum_x, minimum_y, minimum_z), Vector3i(maximum_x, maximum_y, maximum_z));
	_end_edit();
}

void TerrainData::set_material_occupancy_samples(const PackedInt32Array &p_samples) {
	if (p_samples.is_empty() || p_samples.size() % 5 != 0) return;

	const int sample_count = p_samples.size() / 5;
	const int max_material = static_cast<int>(MATERIAL_MAX);
	int minimum_x = std::numeric_limits<int>::max();
	int minimum_y = std::numeric_limits<int>::max();
	int minimum_z = std::numeric_limits<int>::max();
	int maximum_x = std::numeric_limits<int>::min();
	int maximum_y = std::numeric_limits<int>::min();
	int maximum_z = std::numeric_limits<int>::min();
	bool changed = false;

	_begin_edit();
	for (int i = 0; i < sample_count; i++) {
		const int offset = i * 5;
		const int x = p_samples[offset + 0];
		const int y = p_samples[offset + 1];
		const int z = p_samples[offset + 2];
		if (y < 0 || y >= _chunk_height) continue;

		const int material = std::clamp(p_samples[offset + 3], 0, max_material);
		const int occupancy = std::clamp(p_samples[offset + 4], 0, static_cast<int>(OCCUPANCY_MAX));
		Sample sample;
		if (material != static_cast<int>(MATERIAL_AIR) && occupancy > 0) {
			sample.sdf = quantize_sdf(occupancy_to_sdf(static_cast<uint32_t>(occupancy)));
			sample.material = static_cast<uint8_t>(material);
		}

		if (!_set_sample(x, y, z, sample)) continue;
		changed = true;
		minimum_x = std::min(minimum_x, x);
		minimum_y = std::min(minimum_y, y);
		minimum_z = std::min(minimum_z, z);
		maximum_x = std::max(maximum_x, x);
		maximum_y = std::max(maximum_y, y);
		maximum_z = std::max(maximum_z, z);
	}
	if (changed) {
		_mark_dirty(
			Vector3i(minimum_x, minimum_y, minimum_z),
			Vector3i(maximum_x, maximum_y, maximum_z));
	}
	_end_edit();
}

PackedInt32Array TerrainData::get_chunk_blocks(int p_cx, int p_cz) const {
	auto it = _chunks.find(ChunkPosition(p_cx, p_cz));
	if (it == _chunks.end()) return PackedInt32Array();

	int count = _chunk_size * _chunk_height * _chunk_size;
	PackedInt32Array result;
	// Godot's PackedInt32Array does not expose std::vector-style reserve().
	// Resize once so the native bridge remains compatible with Godot 4.7.
	result.resize(count);
	for (int i = 0; i < count; i++) {
		const Sample &sample = it->second.samples[i];
		result.set(i, static_cast<int32_t>(make_cell(sample.material, sdf_to_occupancy(dequantize_sdf(sample.sdf)))));
	}
	return result;
}

bool TerrainData::has_chunk(int p_cx, int p_cz) const {
	return _chunks.find(ChunkPosition(p_cx, p_cz)) != _chunks.end();
}

TypedArray<Vector3i> TerrainData::get_chunk_positions() const {
	TypedArray<Vector3i> result;
	result.resize(_chunks.size());
	int i = 0;
	for (auto &[key, _] : _chunks) {
		result[i] = Vector3i(static_cast<int>(key.x), 0, static_cast<int>(key.z));
		i++;
	}
	return result;
}

void TerrainData::clear() {
	if (_chunks.empty()) return;
	_chunks.clear();
	_mark_full_rebuild();
}

int TerrainData::get_chunk_count() const {
	return static_cast<int>(_chunks.size());
}

void TerrainData::set_chunk_size(int p_size) {
	if (p_size >= 4 && p_size <= 64 && (p_size & (p_size - 1)) == 0 && _chunk_size != p_size) {
		_chunk_size = p_size;
		_mark_full_rebuild();
	}
}

void TerrainData::set_chunk_height(int p_height) {
	if (p_height >= 4 && p_height <= 256 && _chunk_height != p_height) {
		_chunk_height = p_height;
		_mark_full_rebuild();
	}
}

void TerrainData::set_cell_size(double p_size) {
	if (std::isfinite(p_size) && p_size >= 0.125 && p_size <= 16.0 && _cell_size != p_size) {
		_cell_size = p_size;
		_mark_full_rebuild();
	}
}

void TerrainData::set_surface_level(double p_level) {
	if (std::isfinite(p_level)) {
		double next = std::clamp(p_level, 0.05, 0.95);
		if (_surface_level != next) {
			_surface_level = next;
			_mark_full_rebuild();
		}
	}
}

static uint32_t terrain_hash(int x, int y, int z, int seed) {
	uint32_t h = static_cast<uint32_t>(seed) ^ 0x9E3779B9u;
	h ^= static_cast<uint32_t>(x) * 0x85EBCA6Bu;
	h ^= static_cast<uint32_t>(y) * 0xC2B2AE35u;
	h ^= static_cast<uint32_t>(z) * 0x27D4EB2Fu;
	h ^= h >> 16;
	h *= 0x7FEB352Du;
	h ^= h >> 15;
	h *= 0x846CA68Bu;
	return h ^ (h >> 16);
}

static double terrain_hash_noise(int x, int y, int z, int seed) {
	return static_cast<double>(terrain_hash(x, y, z, seed) & 0x00FFFFFFu) / 8388607.5 - 1.0;
}

static double terrain_fade(double t) {
	return t * t * (3.0 - 2.0 * t);
}

static double terrain_lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

static double terrain_value_noise_3d(double x, double y, double z, int seed) {
	int x0 = static_cast<int>(std::floor(x));
	int y0 = static_cast<int>(std::floor(y));
	int z0 = static_cast<int>(std::floor(z));
	double tx = terrain_fade(x - x0);
	double ty = terrain_fade(y - y0);
	double tz = terrain_fade(z - z0);

	double c000 = terrain_hash_noise(x0, y0, z0, seed);
	double c100 = terrain_hash_noise(x0 + 1, y0, z0, seed);
	double c010 = terrain_hash_noise(x0, y0 + 1, z0, seed);
	double c110 = terrain_hash_noise(x0 + 1, y0 + 1, z0, seed);
	double c001 = terrain_hash_noise(x0, y0, z0 + 1, seed);
	double c101 = terrain_hash_noise(x0 + 1, y0, z0 + 1, seed);
	double c011 = terrain_hash_noise(x0, y0 + 1, z0 + 1, seed);
	double c111 = terrain_hash_noise(x0 + 1, y0 + 1, z0 + 1, seed);

	double x00 = terrain_lerp(c000, c100, tx);
	double x10 = terrain_lerp(c010, c110, tx);
	double x01 = terrain_lerp(c001, c101, tx);
	double x11 = terrain_lerp(c011, c111, tx);
	return terrain_lerp(terrain_lerp(x00, x10, ty), terrain_lerp(x01, x11, ty), tz);
}

static double terrain_fbm_3d(double x, double y, double z, int seed, int octaves) {
	double value = 0.0;
	double weight = 1.0;
	double total = 0.0;
	for (int octave = 0; octave < octaves; octave++) {
		value += terrain_value_noise_3d(x, y, z, seed + octave * 1013) * weight;
		total += weight;
		weight *= 0.5;
		x *= 2.0;
		y *= 2.0;
		z *= 2.0;
	}
	return total > 0.0 ? value / total : 0.0;
}

static double smooth_min(double a, double b, double radius) {
	if (radius <= 0.0001) return std::min(a, b);
	double h = std::clamp(0.5 + 0.5 * (b - a) / radius, 0.0, 1.0);
	return terrain_lerp(b, a, h) - radius * h * (1.0 - h);
}

static double smooth_max(double a, double b, double radius) {
	return -smooth_min(-a, -b, radius);
}

static double brush_sdf(double x, double y, double z,
		double rx, double ry, double rz, int shape) {
	rx = std::max(0.125, rx);
	ry = std::max(0.125, ry);
	rz = std::max(0.125, rz);
	if (shape == 1) {
		double qx = std::abs(x) - rx;
		double qy = std::abs(y) - ry;
		double qz = std::abs(z) - rz;
		double outside = std::sqrt(std::max(qx, 0.0) * std::max(qx, 0.0)
				+ std::max(qy, 0.0) * std::max(qy, 0.0)
				+ std::max(qz, 0.0) * std::max(qz, 0.0));
		return outside + std::min(std::max(qx, std::max(qy, qz)), 0.0);
	}
	if (shape == 2) {
		double radial = std::sqrt((x / rx) * (x / rx) + (z / rz) * (z / rz)) - 1.0;
		double qx = radial * std::min(rx, rz);
		double qy = std::abs(y) - ry;
		double outside = std::sqrt(std::max(qx, 0.0) * std::max(qx, 0.0)
				+ std::max(qy, 0.0) * std::max(qy, 0.0));
		return outside + std::min(std::max(qx, qy), 0.0);
	}
	double normalized = std::sqrt((x / rx) * (x / rx) + (y / ry) * (y / ry) + (z / rz) * (z / rz));
	return (normalized - 1.0) * std::min(rx, std::min(ry, rz));
}

void TerrainData::generate_volume(int p_x, int p_y, int p_z, int p_width, int p_height, int p_depth,
		double p_base_height, double p_amplitude, double p_frequency, int p_material,
		int p_seed, double p_cave_strength) {
	p_width = std::clamp(p_width, 1, 1024);
	p_height = std::clamp(p_height, 1, _chunk_height);
	p_depth = std::clamp(p_depth, 1, 1024);
	p_frequency = std::max(1.0, p_frequency);
	p_cave_strength = std::clamp(p_cave_strength, 0.0, 1.0);
	p_material = std::clamp(p_material, static_cast<int>(MATERIAL_GRASS), static_cast<int>(MATERIAL_MAX));

	_begin_edit();
	const double surface_band = 2.25;
	for (int z = 0; z < p_depth; z++) {
		for (int x = 0; x < p_width; x++) {
			int wx = p_x + x;
			int wz = p_z + z;
			double nx = static_cast<double>(wx) / p_frequency;
			double nz = static_cast<double>(wz) / p_frequency;
			double broad = terrain_fbm_3d(nx, 0.0, nz, p_seed, 4);
			double ridges = 1.0 - std::abs(terrain_fbm_3d(nx * 0.55, 7.0, nz * 0.55, p_seed + 4099, 3));
			double surface_y = p_base_height + broad * p_amplitude + (ridges - 0.5) * p_amplitude * 0.35;

			for (int y = 0; y < p_height; y++) {
				int wy = p_y + y;
				if (wy < 0 || wy >= _chunk_height) continue;

				double sdf = (static_cast<double>(wy) - surface_y) / surface_band;
				if (p_cave_strength > 0.0) {
					double cave = std::abs(terrain_fbm_3d(
							static_cast<double>(wx) / (p_frequency * 0.72),
							static_cast<double>(wy) / (p_frequency * 0.52),
							static_cast<double>(wz) / (p_frequency * 0.72),
							p_seed + 7919, 3));
					double cave_mask = std::clamp((0.55 - cave) * 4.5, 0.0, 1.0);
					double depth_mask = std::clamp((surface_y - wy - 2.0) / 10.0, 0.0, 1.0);
					sdf += cave_mask * depth_mask * p_cave_strength * 4.4;
				}
				if (sdf >= SDF_TRUNCATION) continue;

				int material = p_material;
				double depth = surface_y - wy;
				if (p_material == static_cast<int>(MATERIAL_GRASS)) {
					material = depth > 4.0 ? static_cast<int>(MATERIAL_ROCK)
							: depth > 1.5 ? static_cast<int>(MATERIAL_DIRT)
							: static_cast<int>(MATERIAL_GRASS);
				}
				set_sdf(wx, wy, wz, sdf, material);
			}
		}
	}
	_end_edit();
}

int TerrainData::apply_sdf_brush(double p_x, double p_y, double p_z,
		double p_radius_x, double p_radius_y, double p_radius_z,
		int p_shape, int p_operation, int p_material,
		double p_strength, double p_smoothness) {
	p_radius_x = std::max(0.125, p_radius_x);
	p_radius_y = std::max(0.125, p_radius_y);
	p_radius_z = std::max(0.125, p_radius_z);
	p_shape = std::clamp(p_shape, 0, 2);
	p_operation = std::clamp(p_operation, 0, 1);
	p_material = std::clamp(p_material, static_cast<int>(MATERIAL_GRASS), static_cast<int>(MATERIAL_MAX));
	p_strength = std::clamp(p_strength, 0.0, 1.0);
	p_smoothness = std::clamp(p_smoothness, 0.0, 16.0);

	double padding = SDF_TRUNCATION + p_smoothness;
	int min_x = static_cast<int>(std::floor(p_x - p_radius_x - padding));
	int max_x = static_cast<int>(std::ceil(p_x + p_radius_x + padding));
	int min_y = std::max(0, static_cast<int>(std::floor(p_y - p_radius_y - padding)));
	int max_y = std::min(_chunk_height - 1, static_cast<int>(std::ceil(p_y + p_radius_y + padding)));
	int min_z = static_cast<int>(std::floor(p_z - p_radius_z - padding));
	int max_z = static_cast<int>(std::ceil(p_z + p_radius_z + padding));
	int changed = 0;

	_begin_edit();
	for (int z = min_z; z <= max_z; z++) {
		for (int y = min_y; y <= max_y; y++) {
			for (int x = min_x; x <= max_x; x++) {
				double shape = brush_sdf(x - p_x, y - p_y, z - p_z,
						p_radius_x, p_radius_y, p_radius_z, p_shape);
				double current = get_sdf(x, y, z);
				double combined = p_operation == 0
						? smooth_min(current, shape, p_smoothness)
						: smooth_max(current, -shape, p_smoothness);
				double next = terrain_lerp(current, combined, p_strength);
				if (std::abs(next - current) < 1.0 / SDF_QUANTIZATION) continue;
				int material = get_material(x, y, z);
				if (p_operation == 0 && shape <= current + p_smoothness) material = p_material;
				if (material == MATERIAL_AIR && next < 0.0) material = p_material;
				set_sdf(x, y, z, next, material);
				changed++;
			}
		}
	}
	_end_edit();
	return changed;
}

int TerrainData::smooth_sdf_brush(double p_x, double p_y, double p_z,
		double p_radius_x, double p_radius_y, double p_radius_z,
		int p_shape, double p_strength) {
	p_strength = std::clamp(p_strength, 0.0, 1.0);
	struct Edit { int x; int y; int z; double sdf; int material; };
	std::vector<Edit> edits;
	int min_x = static_cast<int>(std::floor(p_x - p_radius_x));
	int max_x = static_cast<int>(std::ceil(p_x + p_radius_x));
	int min_y = std::max(0, static_cast<int>(std::floor(p_y - p_radius_y)));
	int max_y = std::min(_chunk_height - 1, static_cast<int>(std::ceil(p_y + p_radius_y)));
	int min_z = static_cast<int>(std::floor(p_z - p_radius_z));
	int max_z = static_cast<int>(std::ceil(p_z + p_radius_z));
	for (int z = min_z; z <= max_z; z++) {
		for (int y = min_y; y <= max_y; y++) {
			for (int x = min_x; x <= max_x; x++) {
				double shape = brush_sdf(x - p_x, y - p_y, z - p_z,
						p_radius_x, p_radius_y, p_radius_z, p_shape);
				if (shape > 0.0) continue;
				double sum = 0.0;
				for (int oz = -1; oz <= 1; oz++)
					for (int oy = -1; oy <= 1; oy++)
						for (int ox = -1; ox <= 1; ox++)
							sum += get_sdf(x + ox, y + oy, z + oz);
				double current = get_sdf(x, y, z);
				double weight = std::clamp(-shape / std::max(0.125, std::min(p_radius_x, std::min(p_radius_y, p_radius_z))), 0.0, 1.0);
				double next = terrain_lerp(current, sum / 27.0, p_strength * weight);
				edits.push_back({x, y, z, next, get_material(x, y, z)});
			}
		}
	}

	_begin_edit();
	for (const Edit &edit : edits)
		set_sdf(edit.x, edit.y, edit.z, edit.sdf, edit.material);
	_end_edit();
	return static_cast<int>(edits.size());
}

int TerrainData::paint_material_brush(double p_x, double p_y, double p_z,
		double p_radius_x, double p_radius_y, double p_radius_z,
		int p_shape, int p_material, double p_strength) {
	p_radius_x = std::max(0.125, p_radius_x);
	p_radius_y = std::max(0.125, p_radius_y);
	p_radius_z = std::max(0.125, p_radius_z);
	p_shape = std::clamp(p_shape, 0, 2);
	p_material = std::clamp(p_material, static_cast<int>(MATERIAL_GRASS), static_cast<int>(MATERIAL_MAX));
	p_strength = std::clamp(p_strength, 0.0, 1.0);
	int changed = 0;

	_begin_edit();
	int min_x = static_cast<int>(std::floor(p_x - p_radius_x));
	int max_x = static_cast<int>(std::ceil(p_x + p_radius_x));
	int min_y = std::max(0, static_cast<int>(std::floor(p_y - p_radius_y)));
	int max_y = std::min(_chunk_height - 1, static_cast<int>(std::ceil(p_y + p_radius_y)));
	int min_z = static_cast<int>(std::floor(p_z - p_radius_z));
	int max_z = static_cast<int>(std::ceil(p_z + p_radius_z));
	for (int z = min_z; z <= max_z; z++) {
		for (int y = min_y; y <= max_y; y++) {
			for (int x = min_x; x <= max_x; x++) {
				double shape = brush_sdf(x - p_x, y - p_y, z - p_z,
						p_radius_x, p_radius_y, p_radius_z, p_shape);
				if (shape > 0.0) continue;
				double current = get_sdf(x, y, z);
				if (current > 0.0) continue;
				int current_material = get_material(x, y, z);
				if (current_material == p_material) continue;
				if (p_strength >= 0.999 || (terrain_hash(x, y, z, 17) & 0xFFFFu) / 65535.0 <= p_strength) {
					set_sdf(x, y, z, current, p_material);
					changed++;
				}
			}
		}
	}
	_end_edit();
	return changed;
}

Dictionary TerrainData::serialize() const {
	Dictionary result;
	result["format_version"] = 3;
	result["chunk_size"] = _chunk_size;
	result["chunk_height"] = _chunk_height;
	result["cell_size"] = _cell_size;
	result["surface_level"] = _surface_level;

	Dictionary chunks_dict;
	for (auto &[key, chunk] : _chunks) {
		String key_str = itos(key.x) + "," + itos(key.z);

		PackedByteArray bytes;
		int count = _chunk_size * _chunk_height * _chunk_size;
		bytes.resize(count * 4);
		for (int i = 0; i < count; i++) {
			const Sample &sample = chunk.samples[i];
			write_i16_le(bytes, i * 4, sample.sdf);
			bytes.set(i * 4 + 2, sample.material);
			bytes.set(i * 4 + 3, sample.flags);
		}
		chunks_dict[key_str] = bytes;
	}
	result["chunks"] = chunks_dict;
	return result;
}

void TerrainData::deserialize(const Dictionary &p_data) {
	_begin_edit();
	_chunks.clear();

	int format_version = static_cast<int>(p_data.get("format_version", 2));
	_chunk_size = static_cast<int>(p_data.get("chunk_size", DEFAULT_CHUNK_SIZE));
	_chunk_height = static_cast<int>(p_data.get("chunk_height", DEFAULT_CHUNK_HEIGHT));
	set_cell_size(static_cast<double>(p_data.get("cell_size", 1.0)));
	set_surface_level(static_cast<double>(p_data.get("surface_level", 0.5)));

	Dictionary chunks_dict = p_data.get("chunks", Dictionary());
	Array keys = chunks_dict.keys();
	for (int i = 0; i < keys.size(); i++) {
		String key_str = keys[i];
		PackedByteArray bytes = chunks_dict[key_str];

		PackedStringArray parts = key_str.split(",");
		if (parts.size() < 2) continue;
		int64_t cx = parts[0].to_int();
		int64_t cz = parts[1].to_int();

		int count = _chunk_size * _chunk_height * _chunk_size;
		Chunk c;
		c.samples.resize(count);
		for (int j = 0; j < count && j * 4 + 4 <= bytes.size(); j++) {
			if (format_version >= 3) {
				c.samples[j].sdf = read_i16_le(bytes, j * 4);
				c.samples[j].material = bytes[j * 4 + 2];
				c.samples[j].flags = bytes[j * 4 + 3];
			} else {
				uint32_t block = make_block(read_u32_le(bytes, j * 4));
				c.samples[j].sdf = quantize_sdf(occupancy_to_sdf(block_occupancy(block)));
				c.samples[j].material = static_cast<uint8_t>(block_type(block));
			}
			if (c.samples[j].sdf != SDF_OUTSIDE || c.samples[j].material != MATERIAL_AIR) {
				c.active_count++;
				c.active_indices.push_back(static_cast<uint32_t>(j));
			}
		}
		if (c.active_count > 0)
			_chunks[ChunkPosition(cx, cz)] = std::move(c);
	}
	_mark_full_rebuild();
	_end_edit();
}

void TerrainData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_block", "x", "y", "z", "block"), &TerrainData::set_block);
	ClassDB::bind_method(D_METHOD("get_block", "x", "y", "z"), &TerrainData::get_block);
	ClassDB::bind_method(D_METHOD("set_cell", "x", "y", "z", "material", "occupancy"), &TerrainData::set_cell);
	ClassDB::bind_method(D_METHOD("set_sdf", "x", "y", "z", "sdf", "material"), &TerrainData::set_sdf);
	ClassDB::bind_method(D_METHOD("get_sdf", "x", "y", "z"), &TerrainData::get_sdf);
	ClassDB::bind_method(D_METHOD("get_material", "x", "y", "z"), &TerrainData::get_material);
	ClassDB::bind_method(D_METHOD("get_occupancy", "x", "y", "z"), &TerrainData::get_occupancy);
	ClassDB::bind_method(D_METHOD("get_column_height", "x", "z"), &TerrainData::get_column_height);
	ClassDB::bind_method(D_METHOD("get_column_material", "x", "z"), &TerrainData::get_column_material);

	ClassDB::bind_method(D_METHOD("set_chunk_blocks", "cx", "cz", "blocks"), &TerrainData::set_chunk_blocks);
	ClassDB::bind_method(D_METHOD("set_voxel_block_channels", "bx", "by", "bz", "sdf", "render_material", "source_material", "source_min_y"), &TerrainData::set_voxel_block_channels);
	ClassDB::bind_method(D_METHOD("set_voxel_blocks_channels", "block_positions", "block_offsets", "sdf", "render_material", "source_material", "water_samples", "source_min_y"), &TerrainData::set_voxel_blocks_channels);
	ClassDB::bind_method(D_METHOD("set_material_occupancy_samples", "samples"), &TerrainData::set_material_occupancy_samples);
	ClassDB::bind_method(D_METHOD("get_chunk_blocks", "cx", "cz"), &TerrainData::get_chunk_blocks);
	ClassDB::bind_method(D_METHOD("has_chunk", "cx", "cz"), &TerrainData::has_chunk);
	ClassDB::bind_method(D_METHOD("get_chunk_positions"), &TerrainData::get_chunk_positions);
	ClassDB::bind_method(D_METHOD("get_chunk_count"), &TerrainData::get_chunk_count);
	ClassDB::bind_method(D_METHOD("get_revision"), &TerrainData::get_revision);
	ClassDB::bind_method(D_METHOD("get_dirty_region_since", "revision"), &TerrainData::get_dirty_region_since);
	ClassDB::bind_method(D_METHOD("clear"), &TerrainData::clear);

	ClassDB::bind_method(D_METHOD("get_chunk_size"), &TerrainData::get_chunk_size);
	ClassDB::bind_method(D_METHOD("set_chunk_size", "size"), &TerrainData::set_chunk_size);
	ClassDB::bind_method(D_METHOD("get_chunk_height"), &TerrainData::get_chunk_height);
	ClassDB::bind_method(D_METHOD("set_chunk_height", "height"), &TerrainData::set_chunk_height);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &TerrainData::get_cell_size);
	ClassDB::bind_method(D_METHOD("set_cell_size", "size"), &TerrainData::set_cell_size);
	ClassDB::bind_method(D_METHOD("get_surface_level"), &TerrainData::get_surface_level);
	ClassDB::bind_method(D_METHOD("set_surface_level", "level"), &TerrainData::set_surface_level);
	ClassDB::bind_method(D_METHOD("generate_volume", "x", "y", "z", "width", "height", "depth",
			"base_height", "amplitude", "frequency", "material", "seed", "cave_strength"), &TerrainData::generate_volume);
	ClassDB::bind_method(D_METHOD("apply_sdf_brush", "x", "y", "z", "radius_x", "radius_y", "radius_z",
			"shape", "operation", "material", "strength", "smoothness"), &TerrainData::apply_sdf_brush);
	ClassDB::bind_method(D_METHOD("smooth_sdf_brush", "x", "y", "z", "radius_x", "radius_y", "radius_z",
			"shape", "strength"), &TerrainData::smooth_sdf_brush);
	ClassDB::bind_method(D_METHOD("paint_material_brush", "x", "y", "z", "radius_x", "radius_y", "radius_z",
			"shape", "material", "strength"), &TerrainData::paint_material_brush);

	ClassDB::bind_method(D_METHOD("serialize"), &TerrainData::serialize);
	ClassDB::bind_method(D_METHOD("deserialize", "data"), &TerrainData::deserialize);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_size"), "set_chunk_size", "get_chunk_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_height"), "set_chunk_height", "get_chunk_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cell_size", PROPERTY_HINT_RANGE, "0.125,16.0,0.125"), "set_cell_size", "get_cell_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "surface_level", PROPERTY_HINT_RANGE, "0.05,0.95,0.01"), "set_surface_level", "get_surface_level");
}

