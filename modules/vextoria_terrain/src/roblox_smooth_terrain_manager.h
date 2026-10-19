#ifndef VEXTORIA_ROBLOX_SMOOTH_TERRAIN_MANAGER_H
#define VEXTORIA_ROBLOX_SMOOTH_TERRAIN_MANAGER_H

#include "terrain_data.h"
#include "roblox_smooth_lod_octree.h"
#include "roblox_smooth_mip_cache.h"

#include "scene/resources/mesh.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "core/variant/dictionary.h"
#include "scene/resources/material.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "core/os/memory.h"
#include "core/os/thread.h"
#include "core/math/vector3.h"

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>


using Vector3int32 = Vector3i;

class RobloxSmoothTerrainManager : public Node3D {
	GDCLASS(RobloxSmoothTerrainManager, Node3D)

public:
	RobloxSmoothTerrainManager();
	~RobloxSmoothTerrainManager() override;

	void set_terrain_data(const Ref<TerrainData> &p_data);
	Ref<TerrainData> get_terrain_data() const;

	void set_view_distance(double p_distance);
	double get_view_distance() const;
	void set_collision_enabled(bool p_enabled);
	bool get_collision_enabled() const;
	void set_collision_distance(double p_distance);
	double get_collision_distance() const;
	void set_viewer_world_positions(const PackedVector3Array &p_positions);
	void set_mesh_build_budget_ms(double p_budget);
	double get_mesh_build_budget_ms() const;
	void set_maximum_lod_level(int p_level);
	int get_maximum_lod_level() const;
	void set_solid_material(const Ref<Material> &p_material);
	Ref<Material> get_solid_material() const;
	void set_water_material(const Ref<Material> &p_material);
	Ref<Material> get_water_material() const;

	void rebuild_all();
	void clear();
	int get_rendered_chunk_count() const;
	int get_rendered_water_chunk_count() const;
	int get_required_chunk_count() const;
	bool is_streaming_complete() const;
	bool is_collision_ready_at_world_position(const Vector3 &p_position) const;
	Dictionary get_runtime_statistics() const;

	void _vextoria_ready();
	void _vextoria_process(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	using RenderKey = RobloxSmoothLodKey;
	using RenderKeyHash = RobloxSmoothLodKeyHash;

	struct RenderEntry {
		int lod = -1;
		bool collision = false;
	bool has_collision_shape = false;
		uint8_t retained_border_mask = 0;
		uint64_t revision = 0;
		MeshInstance3D *solid = nullptr;
		MeshInstance3D *water = nullptr;
	};

	struct MeshPair {
		Ref<ArrayMesh> solid;
		// Roblox's graphics path may append a reversed shoreline copy of selected
		// solid triangles so the water/solid seam remains visible from both sides.
		// Physics must use the original solid-only triangles; feeding that reversed
		// graphics copy to ConcavePolygonShape3D creates an artificial inner shell.
		Ref<ArrayMesh> solid_collision;
		Ref<ArrayMesh> water;
		// Cooked trimesh collision, shared with the render cache so a cache-hit
		// install does not pay Bullet's shape cooking cost again.
		Ref<ConcavePolygonShape3D> collision_shape;
	};

	struct MeshCacheEntry {
		MeshPair meshes;
		uint8_t retained_border_mask = 0;
		uint64_t revision = 0;
		uint64_t last_used = 0;
	};

	// LOD leaves are frequently removed and then requested again as the player
	// walks back across a boundary. Keep a bounded cache of the already-built
	// ArrayMesh resources so that this is a visibility change, not another
	// SmoothGrid meshing pass. ArrayMesh is reference-counted by Godot, so the
	// cache shares the existing GPU resource rather than duplicating it.
	static constexpr size_t MESH_CACHE_CAPACITY = 1024;

	Ref<TerrainData> _data;
	uint64_t _data_revision = 0;
	// Configured fine-detail horizon. Occupied terrain outside this radius is
	// still rendered using coarser octree leaves; it is not hard-despawned.
	double _view_distance = 192.0;
	double _adaptive_view_distance = 192.0;
	float _performance_distance_scale = 1.0f;
	double _performance_sample_accumulator = 0.0;
	double _collision_distance = 48.0;
	double _mesh_build_budget_ms = 1.5;
	// L4 is a 256-cell leaf. Imported Roblox terrain can contain narrow rivers,
	// banks, and snow caps that are intentionally thinner than that footprint;
	// callers may retain L3 as the farthest visual leaf without reducing the
	// overall view distance.
	int _maximum_lod_level = RobloxSmoothLodOctreePolicy::CHUNK_LEVELS - 1;
	bool _collision_enabled = true;
	Vector3 _viewer_position;
	std::vector<Vector3> _viewer_positions;
	PackedVector3Array _viewer_world_positions;
	Vector3 _last_stream_position = Vector3(1e20f, 1e20f, 1e20f);
	float _viewer_speed = 0.0f;
	double _stream_accumulator = 0.0;
	bool _force_stream_update = true;
	// Initial terrain population may use the larger startup construction burst.
	// Once the first desired leaf set is complete, ordinary LOD transitions must
	// stay on the interactive budget even while they span multiple frames.
	bool _startup_streaming = true;
	bool _streaming_complete = false;
	int _required_chunk_count = 0;
	bool _lod_cache_dirty = true;
	Ref<Material> _solid_material;
	Ref<Material> _water_material;
	std::unordered_map<RenderKey, RenderEntry, RenderKeyHash> _rendered;
	std::unordered_set<RenderKey, RenderKeyHash> _dirty_render_keys;
	std::unordered_map<RenderKey, MeshCacheEntry, RenderKeyHash> _mesh_cache;
	uint64_t _mesh_cache_clock = 0;
	RobloxSmoothMipCache _mip_cache;
	RobloxSmoothLodOctreePolicy::Coverage _lod_coverage;

	void _initialize_materials();
	void _rebuild_lod_cache();
	void _consume_data_changes();
	void _mark_rendered_region_dirty(const Vector3i &p_min, const Vector3i &p_max);
	void _invalidate_mesh_cache_region(const Vector3i &p_min, const Vector3i &p_max);
	void _store_mesh_cache(const RenderKey &p_key, MeshPair p_meshes, uint8_t p_retained_border_mask);
	bool _render_key_intersects_region(const RenderKey &p_key, const Vector3i &p_min, const Vector3i &p_max) const;
	bool _replacement_region_ready(
			const RenderKey &p_stale_key,
			const std::unordered_set<RenderKey, RenderKeyHash> &p_needed) const;
	void _update_streaming(bool p_force);
	double _distance_to_chunk(const RenderKey &p_key) const;
	MeshPair _build_chunk_mesh(const RenderKey &p_key, uint8_t p_retained_border_mask);
	void _install_chunk(const RenderKey &p_key, bool p_collision, uint8_t p_retained_border_mask, MeshPair &p_meshes);
	struct AsyncChunkResult;
	struct AsyncChunkRequest;
	void _install_async_chunk(const RenderKey &p_key, bool p_collision, const AsyncChunkResult &p_result, MeshPair &p_meshes);
	void _remove_chunk(const RenderKey &p_key);
	String _chunk_name(const RenderKey &p_key, bool p_water) const;

	// Async mesh generation. The march + normal/material packing for a chunk is
	// pure input computation with no Godot scene-object lifetime requirements,
	// so it runs on a small worker pool while the main thread only instantiates
	// the finished ArrayMesh/physics shapes during a bounded per-tick commit
	// budget. That removes the 10-75 ms frame stalls that walking across LOD
	// boundaries caused when the whole mesher ran synchronously.
	struct AsyncChunkRequest {
		RenderKey key;
		bool collision = false;
		uint8_t retained_border_mask = 0;
		double distance = 0.0;
	};

	struct AsyncChunkResult {
		RenderKey key;
		bool collision = false;
		uint8_t retained_border_mask = 0;
		uint64_t revision = 0;
		// Meshes have raw packed arrays ready for add_surface_from_arrays; each
		// array is indexed by Mesh::ArrayType. Packed arrays are safe to build on
		// a worker thread as long as nothing else shares them yet.
		Array solid_arrays;
		Array solid_reverse_arrays;
		Array water_arrays;
		// Triangle-soup vertices for ConcavePolygonShape3D::set_faces().
		PackedVector3Array collision_faces;
		bool has_solid = false;
		bool has_solid_reverse = false;
		bool has_water = false;
	};

	std::deque<AsyncChunkRequest> _async_requests;
	std::vector<AsyncChunkResult> _async_results;
	std::unordered_set<RenderKey, RenderKeyHash> _async_in_flight;
	mutable std::mutex _async_mutex;
	std::condition_variable _async_work_cv;
	std::condition_variable _async_idle_cv;
	std::atomic<int> _async_active{ 0 };
	bool _async_shutdown = false;
	std::vector<Thread *> _async_workers;
	// Latest desired RenderKey set from the most recent streaming evaluation.
	// Async results for keys no longer needed are dropped to avoid rendering
	// geometry the player already walked away from.
	std::unordered_set<RenderKey, RenderKeyHash> _async_currently_needed;
	std::unordered_map<RenderKey, uint8_t, RenderKeyHash> _async_currently_needed_border_masks;
	std::unordered_set<RenderKey, RenderKeyHash> _last_needed;
	std::unordered_map<RenderKey, uint8_t, RenderKeyHash> _last_border_masks;
	bool _has_last_needed = false;

	// Counters are intentionally owned by the manager rather than inferred from
	// frame time. They make the native Roblox path auditable in the Starter Place
	// benchmark: worker work, stale work discarded after camera movement, cache
	// reuse, bounded main-thread installs, collision cooking, and LOD set churn.
	static constexpr size_t MAX_ASYNC_IN_FLIGHT = 32;
	std::atomic<uint64_t> _metric_meshes_generated{0};
	std::atomic<uint64_t> _metric_meshes_installed{0};
	std::atomic<uint64_t> _metric_stale_jobs_dropped{0};
	std::atomic<uint64_t> _metric_cache_hits{0};
	std::atomic<uint64_t> _metric_cache_misses{0};
	std::atomic<uint64_t> _metric_install_usec{0};
	std::atomic<uint64_t> _metric_collision_install_usec{0};
	std::atomic<uint64_t> _metric_collision_installs{0};
	std::atomic<uint64_t> _metric_lod_transitions{0};
	std::atomic<uint64_t> _metric_lod_splits{0};
	std::atomic<uint64_t> _metric_lod_merges{0};

	void _ensure_async_workers();
	static void _async_worker_entry(void *p_userdata);
	void _async_worker_main();
	void _enqueue_async_chunk(const AsyncChunkRequest &p_request);
	void _drain_async_results(bool p_startup_burst);
	void _shutdown_async_workers();

	struct ComputedGeometry; // local forward decls for pure helpers
	bool _compute_chunk_geometry(const RenderKey &p_key, uint8_t p_retained_border_mask, struct BasicMesh &r_basic,
			std::vector<Vector3> &r_soft_normals, std::vector<Vector3> &r_hard_normals,
			std::vector<uint32_t> &r_solid, std::vector<uint32_t> &r_water,
			std::vector<uint32_t> &r_reverse_solid) const;
	void _compute_chunk_async(const AsyncChunkRequest &p_request, AsyncChunkResult &r_result) const;
	MeshPair _instantiate_async_result(const AsyncChunkResult &p_result);
	void _async_flush_and_wait();
};


#endif
