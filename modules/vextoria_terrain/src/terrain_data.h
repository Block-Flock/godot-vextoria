#ifndef VEXTORIA_TERRAIN_DATA_H
#define VEXTORIA_TERRAIN_DATA_H

#include "terrain_types.h"

#include "core/io/resource.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "core/math/vector3i.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>


class RobloxSmoothMipCache;

class TerrainData : public Resource {
	GDCLASS(TerrainData, Resource)

public:
	struct DirtyRegion {
		bool valid = false;
		bool full_rebuild = false;
		Vector3i minimum;
		Vector3i maximum;
	};

	TerrainData();
	~TerrainData();

	void set_block(int p_x, int p_y, int p_z, uint32_t p_block);
	uint32_t get_block(int p_x, int p_y, int p_z) const;
	void set_cell(int p_x, int p_y, int p_z, int p_material, int p_occupancy);
	void set_sdf(int p_x, int p_y, int p_z, double p_sdf, int p_material);
	double get_sdf(int p_x, int p_y, int p_z) const;
	int get_material(int p_x, int p_y, int p_z) const;
	int get_occupancy(int p_x, int p_y, int p_z) const;
	double get_column_height(int p_x, int p_z) const;
	int get_column_material(int p_x, int p_z) const;

	void set_chunk_blocks(int p_cx, int p_cz, const PackedInt32Array &p_blocks);
	// Fast import path for VoxelBuffer's validated dense channels. The block is
	// addressed in the serialized 16^3 source grid; the native side converts it
	// directly into the 32^2 horizontal chunks used by the Roblox mesher.
	void set_voxel_block_channels(int p_bx, int p_by, int p_bz,
			const PackedByteArray &p_sdf, const PackedByteArray &p_render_material,
			const PackedByteArray &p_source_material, int p_source_min_y);
	// Import-facing bulk variant. Block positions are packed as [x,y,z] triples;
	// offsets are [sdf,render,source] triples per block plus one terminal triple
	// into the three concatenated channel arrays. Water samples use [x,y,z,material,
	// occupancy] with Y already relative to p_source_min_y. Solid and water are
	// committed in the same edit transaction so the shared shoreline is never
	// built from two revisions.
	void set_voxel_blocks_channels(const PackedInt32Array &p_block_positions,
			const PackedInt32Array &p_block_offsets, const PackedByteArray &p_sdf,
			const PackedByteArray &p_render_material, const PackedByteArray &p_source_material,
			const PackedInt32Array &p_water_samples, int p_source_min_y);
	// Import-facing sparse batch format: [x, y, z, material, occupancy]...
	// Solid and water samples are accepted together so one edit transaction
	// produces one shared SDF/material field and one dirty-region revision.
	void set_material_occupancy_samples(const PackedInt32Array &p_samples);
	PackedInt32Array get_chunk_blocks(int p_cx, int p_cz) const;

	bool has_chunk(int p_cx, int p_cz) const;
	TypedArray<Vector3i> get_chunk_positions() const;
	void clear();
	int get_chunk_count() const;
	int64_t get_revision() const { return static_cast<int64_t>(_revision); }
	DirtyRegion query_dirty_region_since(uint64_t p_revision) const;
	Dictionary get_dirty_region_since(int64_t p_revision) const;
	bool get_dirty_since(int64_t p_revision, Vector3i &r_min, Vector3i &r_max, bool &r_full) const;

	int get_chunk_size() const { return _chunk_size; }
	void set_chunk_size(int p_size);
	int get_chunk_height() const { return _chunk_height; }
	void set_chunk_height(int p_height);
	double get_cell_size() const { return _cell_size; }
	void set_cell_size(double p_size);
	double get_surface_level() const { return _surface_level; }
	void set_surface_level(double p_level);

	void generate_volume(int p_x, int p_y, int p_z, int p_width, int p_height, int p_depth,
			double p_base_height, double p_amplitude, double p_frequency, int p_material,
			int p_seed, double p_cave_strength);
	int apply_sdf_brush(double p_x, double p_y, double p_z,
			double p_radius_x, double p_radius_y, double p_radius_z,
			int p_shape, int p_operation, int p_material,
			double p_strength, double p_smoothness);
	int smooth_sdf_brush(double p_x, double p_y, double p_z,
			double p_radius_x, double p_radius_y, double p_radius_z,
			int p_shape, double p_strength);
	int paint_material_brush(double p_x, double p_y, double p_z,
			double p_radius_x, double p_radius_y, double p_radius_z,
			int p_shape, int p_material, double p_strength);

	Dictionary serialize() const;
	void deserialize(const Dictionary &p_data);

protected:
	static void _bind_methods();

private:
	friend class RobloxSmoothMipCache;
	int _chunk_size = DEFAULT_CHUNK_SIZE;
	int _chunk_height = DEFAULT_CHUNK_HEIGHT;
	double _cell_size = 1.0;
	double _surface_level = 0.5;
	uint64_t _revision = 0;

	struct Sample {
		int16_t sdf = SDF_OUTSIDE;
		uint8_t material = MATERIAL_AIR;
		uint8_t flags = 0;
	};

	struct Chunk {
		std::vector<Sample> samples;
		// Kept alongside the dense storage so native mip construction can visit
		// populated cells only. Editing is infrequent compared with streaming;
		// removals use swap-and-pop and remain bounded by the active cell count.
		std::vector<uint32_t> active_indices;
		int active_count = 0;
	};
	std::unordered_map<ChunkPosition, Chunk> _chunks;

	struct DirtyChange {
		uint64_t revision = 0;
		Vector3i minimum;
		Vector3i maximum;
		bool full_rebuild = false;
	};
	static constexpr size_t DIRTY_HISTORY_LIMIT = 128;
	std::deque<DirtyChange> _dirty_history;
	int _edit_depth = 0;
	bool _pending_dirty = false;
	bool _pending_full_rebuild = false;
	Vector3i _pending_minimum;
	Vector3i _pending_maximum;

	Sample _get_sample(int p_x, int p_y, int p_z) const;
	bool _set_sample(int p_x, int p_y, int p_z, const Sample &p_sample);
	void _bump_revision();
	void _begin_edit();
	void _end_edit();
	void _mark_dirty(const Vector3i &p_minimum, const Vector3i &p_maximum);
	void _mark_full_rebuild();
	void _commit_pending_dirty();
	bool _write_voxel_block_channels(int p_bx, int p_by, int p_bz,
			const PackedByteArray &p_sdf, int p_sdf_offset,
			const PackedByteArray &p_render_material, int p_render_material_offset,
			const PackedByteArray &p_source_material, int p_source_material_offset,
			int p_source_min_y);
	int _index(int p_x, int p_y, int p_z) const;
	int _clamp(int p_val, int p_min, int p_max) const;
};


#endif
