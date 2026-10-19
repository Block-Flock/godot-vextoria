#ifndef VEXTORIA_TERRAIN_MANAGER_H
#define VEXTORIA_TERRAIN_MANAGER_H

#include "terrain_types.h"
#include "terrain_data.h"

#include "scene/3d/node_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/material.h"
#include "core/math/vector3.h"
#include "core/variant/variant.h"
#include "core/variant/variant.h"
#include "core/math/color.h"
#include "core/string/ustring.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>


struct ChunkMesh {
	Ref<ArrayMesh> mesh;
	Ref<ArrayMesh> water_mesh;
	int lod = 0;
	int64_t cx = 0;
	int64_t cz = 0;
	double last_used_time = 0;
};

class TerrainManager : public Node3D {
	GDCLASS(TerrainManager, Node3D)

public:
	TerrainManager();
	~TerrainManager();

	void set_terrain_data(const Ref<TerrainData> &p_data);
	Ref<TerrainData> get_terrain_data() const;

	void set_view_distance(double p_dist);
	double get_view_distance() const;

	void set_viewer_position(const Vector3 &p_pos);
	Vector3 get_viewer_position() const;

	void set_water_material(const Ref<Material> &p_mat);
	Ref<Material> get_water_material() const;
	void set_solid_material(const Ref<Material> &p_mat);
	Ref<Material> get_solid_material() const;
	void set_collision_enabled(bool p_enabled);
	bool get_collision_enabled() const;
	void set_collision_distance(double p_distance);
	double get_collision_distance() const;
	void set_mesh_build_budget_ms(double p_budget);
	double get_mesh_build_budget_ms() const;

	void rebuild_chunk(int p_cx, int p_cz);
	void rebuild_region(int p_x, int p_z, int p_width, int p_depth);
	void mark_region_dirty(int p_x, int p_z, int p_width, int p_depth);
	void flush_dirty_chunks();
	void update_streaming();
	void rebuild_all();
	void clear();

	int get_chunk_size() const;
	int get_chunk_height() const;

	// Terrain editing tools
	void elevate(int wx, int wz, int radius, int delta);
	void smooth(int wx, int wz, int radius);
	void flatten(int wx, int wz, int radius, int height);
	void paint_material(int wx, int wz, int radius, int material);
	void crater(int cx, int cz, int radius, int depth);
	int floodfill(int wx, int wy, int wz, int material);

	// Water detection. The continuous queries sample the same SDF convention as
	// the smooth water renderer so gameplay does not snap to integer voxel cells.
	bool is_water(int wx, int wy, int wz) const;
	bool is_in_water(float world_x, float world_y, float world_z) const;
	float water_level(int wx, int wz) const;
	double sample_water_field(const Vector3 &p_world_position) const;
	double sample_water_density(const Vector3 &p_world_position) const;
	double find_water_surface_y(const Vector3 &p_world_position, double p_search_radius = 8.0) const;

	void _vextoria_ready();
	void _vextoria_process(double p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

private:
	Ref<TerrainData> _data;
	double _view_distance = 128.0;
	Vector3 _viewer_position;
	double _time_since_update = 0;
	int _max_mesh_memory = 512;
	bool _collision_enabled = true;
	double _collision_distance = 96.0;
	double _mesh_build_budget_ms = 7.5;
	Ref<Material> _water_material;
	Ref<Material> _solid_material;

	void _update_chunks();
	void _generate_mesh(int p_cx, int p_cz, int p_lod);
	void _init_water_material();
	void _init_solid_material();

	std::unordered_map<ChunkPosition, ChunkMesh> _meshes[LOD_LEVELS];
	std::unordered_set<ChunkPosition> _dirty_chunks[LOD_LEVELS];

	Vector3 _get_chunk_world_pos(int cx, int cz, int lod) const;
	String _get_chunk_name(int cx, int cz, int lod) const;
	String _get_water_name(int cx, int cz, int lod) const;
	void _clear_mesh_instance(int p_cx, int p_cz, int p_lod);
	void _set_mesh_instance(int p_cx, int p_cz, int p_lod, const Ref<ArrayMesh> &p_mesh);
	void _set_water_instance(int p_cx, int p_cz, int p_lod, const Ref<ArrayMesh> &p_water);

	bool _chunk_in_view(int p_cx, int p_cz, int p_lod) const;
	int _get_lod_for_distance(double p_dist) const;
	double _chunk_distance(int p_cx, int p_cz, int p_lod) const;
	void _clean_mesh_cache();
};


#endif
