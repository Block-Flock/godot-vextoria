#include "terrain_manager.h"

#include "core/config/engine.h"
#include "scene/resources/surface_tool.h"
#include "scene/resources/mesh.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "core/os/time.h"
#include "scene/main/viewport.h"
#include "core/string/print_string.h"
#include "core/math/vector3i.h"
#include "core/object/class_db.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <queue>


TerrainManager::TerrainManager() {
	_water_material = Ref<Material>();
	_solid_material = Ref<Material>();
}
TerrainManager::~TerrainManager() {}

void TerrainManager::set_terrain_data(const Ref<TerrainData> &p_data) { _data = p_data; clear(); }
Ref<TerrainData> TerrainManager::get_terrain_data() const { return _data; }
void TerrainManager::set_view_distance(double p_dist) { _view_distance = std::max(p_dist, 16.0); }
double TerrainManager::get_view_distance() const { return _view_distance; }
void TerrainManager::set_viewer_position(const Vector3 &p_pos) { _viewer_position = p_pos; }
Vector3 TerrainManager::get_viewer_position() const { return _viewer_position; }
void TerrainManager::set_water_material(const Ref<Material> &p_mat) { _water_material = p_mat; }
Ref<Material> TerrainManager::get_water_material() const { return _water_material; }
void TerrainManager::set_solid_material(const Ref<Material> &p_mat) { _solid_material = p_mat; }
Ref<Material> TerrainManager::get_solid_material() const { return _solid_material; }
void TerrainManager::set_collision_enabled(bool p_enabled) { _collision_enabled = p_enabled; }
bool TerrainManager::get_collision_enabled() const { return _collision_enabled; }
void TerrainManager::set_collision_distance(double p_distance) { _collision_distance = std::max(0.0, p_distance); }
double TerrainManager::get_collision_distance() const { return _collision_distance; }
void TerrainManager::set_mesh_build_budget_ms(double p_budget) { _mesh_build_budget_ms = std::clamp(p_budget, 1.0, 33.0); }
double TerrainManager::get_mesh_build_budget_ms() const { return _mesh_build_budget_ms; }

int TerrainManager::get_chunk_size() const { return _data.is_valid() ? _data->get_chunk_size() : DEFAULT_CHUNK_SIZE; }
int TerrainManager::get_chunk_height() const { return _data.is_valid() ? _data->get_chunk_height() : DEFAULT_CHUNK_HEIGHT; }

void TerrainManager::_init_water_material() {
	if (_water_material.is_valid()) return;
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_albedo(Color(0.24f, 0.50f, 0.81f, 0.55f));
	mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	mat->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
	mat->set_transparency(StandardMaterial3D::TRANSPARENCY_ALPHA);
	mat->set_metallic(0.0f);
	mat->set_roughness(0.18f);
	_water_material = mat;
}

void TerrainManager::_init_solid_material() {
	if (_solid_material.is_valid()) return;
	Ref<StandardMaterial3D> mat;
	mat.instantiate();
	mat->set_albedo(Color(1, 1, 1, 1));
	mat->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
	mat->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
	mat->set_roughness(0.75f);
	_solid_material = mat;
}

void TerrainManager::_vextoria_ready() {
	if (Engine::get_singleton()->is_editor_hint()) return;
	if (!_data.is_valid()) _data.instantiate();
	_init_water_material();
	_init_solid_material();
}

void TerrainManager::_vextoria_process(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) return;
	Viewport *viewport = get_viewport();
	if (viewport != nullptr) {
		Camera3D *camera = viewport->get_camera_3d();
		if (camera != nullptr) {
			_viewer_position = to_local(camera->get_global_position());
		}
	}
	_time_since_update += p_delta;
	if (_time_since_update >= 0.25) {
		_time_since_update = 0;
		flush_dirty_chunks();
		_update_chunks();
	}
}

static int floor_div_int(int value, int divisor) {
	return value >= 0 ? value / divisor : (value - divisor + 1) / divisor;
}

void TerrainManager::mark_region_dirty(int p_x, int p_z, int p_width, int p_depth) {
	if (!_data.is_valid()) return;
	int cs = _data->get_chunk_size();
	int min_x = p_x - 1;
	int max_x = p_x + std::max(1, p_width) + 1;
	int min_z = p_z - 1;
	int max_z = p_z + std::max(1, p_depth) + 1;
	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		int stride = CHUNK_SIZE[0] / CHUNK_SIZE[lod];
		int chunk_span = cs * stride;
		int cmin_x = floor_div_int(min_x, chunk_span);
		int cmax_x = floor_div_int(max_x, chunk_span);
		int cmin_z = floor_div_int(min_z, chunk_span);
		int cmax_z = floor_div_int(max_z, chunk_span);
		for (int cz = cmin_z; cz <= cmax_z; cz++) {
			for (int cx = cmin_x; cx <= cmax_x; cx++) {
				_dirty_chunks[lod].insert(ChunkPosition(cx, cz));
			}
		}
	}
}

void TerrainManager::flush_dirty_chunks() {
	if (!_data.is_valid()) return;
	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		std::vector<ChunkPosition> dirty(_dirty_chunks[lod].begin(), _dirty_chunks[lod].end());
		_dirty_chunks[lod].clear();
		for (const ChunkPosition &pos : dirty) {
			_clear_mesh_instance(pos.x, pos.z, lod);
			_meshes[lod].erase(pos);
			if (_chunk_in_view(pos.x, pos.z, lod)) {
				_generate_mesh(pos.x, pos.z, lod);
			}
		}
	}
}

void TerrainManager::rebuild_region(int p_x, int p_z, int p_width, int p_depth) {
	mark_region_dirty(p_x, p_z, p_width, p_depth);
	flush_dirty_chunks();
}

void TerrainManager::update_streaming() {
	flush_dirty_chunks();
	_update_chunks();
}

void TerrainManager::_update_chunks() {
	if (!_data.is_valid()) return;
	int cs = _data->get_chunk_size();
	double cell_size = _data->get_cell_size();
	double vx = _viewer_position.x;
	double vz = _viewer_position.z;
	std::vector<ChunkPosition> needed[LOD_LEVELS];

	auto min_distance_to_chunk = [&](int lod, int cx, int cz) {
		int stride = LOD_STRIDE[lod];
		double span = cs * stride * cell_size;
		double min_x = cx * span;
		double max_x = min_x + span;
		double min_z = cz * span;
		double max_z = min_z + span;
		double nearest_x = std::clamp(vx, min_x, max_x);
		double nearest_z = std::clamp(vz, min_z, max_z);
		double dx = nearest_x - vx;
		double dz = nearest_z - vz;
		return std::sqrt(dx * dx + dz * dz);
	};

	std::function<void(int, int, int)> select_chunk = [&](int lod, int cx, int cz) {
		double min_distance = min_distance_to_chunk(lod, cx, cz);
		if (min_distance > _view_distance) return;
		if (lod > 0 && min_distance < LOD_DISTANCE[lod]) {
			for (int child_z = 0; child_z < 2; child_z++) {
				for (int child_x = 0; child_x < 2; child_x++) {
					select_chunk(lod - 1, cx * 2 + child_x, cz * 2 + child_z);
				}
			}
			return;
		}
		needed[lod].emplace_back(cx, cz);
	};

	int root_lod = LOD_LEVELS - 1;
	double root_span = cs * LOD_STRIDE[root_lod] * cell_size;
	int root_radius = static_cast<int>(std::ceil(_view_distance / root_span)) + 1;
	int root_cx = static_cast<int>(std::floor(vx / root_span));
	int root_cz = static_cast<int>(std::floor(vz / root_span));
	for (int dz = -root_radius; dz <= root_radius; dz++) {
		for (int dx = -root_radius; dx <= root_radius; dx++) {
			select_chunk(root_lod, root_cx + dx, root_cz + dz);
		}
	}

	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		std::sort(needed[lod].begin(), needed[lod].end(), [&](const ChunkPosition &a, const ChunkPosition &b) {
			return _chunk_distance(a.x, a.z, lod) < _chunk_distance(b.x, b.z, lod);
		});
	}

	uint64_t build_started_ms = Time::get_singleton()->get_ticks_msec();
	const int max_chunk_builds_per_update = 4;
	int chunk_builds = 0;
	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		for (const ChunkPosition &pos : needed[lod]) {
			if (_meshes[lod].find(pos) == _meshes[lod].end()) {
				if (chunk_builds >= max_chunk_builds_per_update) continue;
				if (chunk_builds > 0 && static_cast<double>(Time::get_singleton()->get_ticks_msec() - build_started_ms) >= _mesh_build_budget_ms) {
					continue;
				}
				_generate_mesh(pos.x, pos.z, lod);
				chunk_builds++;
			} else {
				_meshes[lod][pos].last_used_time = Time::get_singleton()->get_unix_time_from_system();
			}
		}
	}

	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		std::vector<ChunkPosition> dead;
		for (auto &[pos, _] : _meshes[lod]) {
			bool found = false;
			for (const ChunkPosition &n : needed[lod]) if (n == pos) { found = true; break; }
			if (!found) dead.push_back(pos);
		}
		for (const ChunkPosition &pos : dead) {
			_clear_mesh_instance(pos.x, pos.z, lod);
			_meshes[lod].erase(pos);
		}
	}
	_clean_mesh_cache();
}

bool TerrainManager::_chunk_in_view(int p_cx, int p_cz, int p_lod) const {
	return _chunk_distance(p_cx, p_cz, p_lod) <= _view_distance;
}

double TerrainManager::_chunk_distance(int p_cx, int p_cz, int p_lod) const {
	int cs = _data.is_valid() ? _data->get_chunk_size() : DEFAULT_CHUNK_SIZE;
	int stride = CHUNK_SIZE[0] / CHUNK_SIZE[p_lod];
	double cell_size = _data.is_valid() ? _data->get_cell_size() : 1.0;
	double wx = (p_cx + 0.5) * cs * stride * cell_size;
	double wz = (p_cz + 0.5) * cs * stride * cell_size;
	double dx = wx - _viewer_position.x;
	double dz = wz - _viewer_position.z;
	return std::sqrt(dx * dx + dz * dz);
}

int TerrainManager::_get_lod_for_distance(double p_dist) const {
	for (int i = LOD_LEVELS - 1; i >= 0; i--) if (p_dist >= LOD_DISTANCE[i]) return i;
	return 0;
}

struct TerrainSample {
	float sdf = SDF_TRUNCATION;
	uint32_t material = MATERIAL_AIR;
};

static constexpr int CUBE_CORNERS[8][3] = {
	{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1},
	{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}
};

static constexpr int TETRAHEDRA[6][4] = {
	{0, 5, 1, 6},
	{0, 1, 2, 6},
	{0, 2, 3, 6},
	{0, 3, 7, 6},
	{0, 7, 4, 6},
	{0, 4, 5, 6}
};

static TerrainSample sample_field(const Ref<TerrainData> &data, int wx, int wy, int wz) {
	return TerrainSample{
		static_cast<float>(data->get_sdf(wx, wy, wz)),
		static_cast<uint32_t>(data->get_material(wx, wy, wz))
	};
}

static uint32_t choose_surface_material(const std::array<TerrainSample, 4> &samples) {
	uint32_t material = MATERIAL_GRASS;
	float closest_surface = 999.0f;
	for (const TerrainSample &sample : samples) {
		if (sample.sdf >= 0.0f || sample.material == MATERIAL_AIR) continue;
		float distance = std::abs(sample.sdf);
		if (distance < closest_surface) {
			closest_surface = distance;
			material = sample.material;
		}
	}
	return material;
}

static Vector3 interpolate_surface(const Vector3 &a, const Vector3 &b, float da, float db) {
	float denom = da - db;
	float t = std::abs(denom) < 0.0001f ? 0.5f : da / denom;
	t = std::max(0.05f, std::min(0.95f, t));
	return a + (b - a) * t;
}

static void add_colored_triangle(const Ref<SurfaceTool> &st, const Vector3 &a, const Vector3 &b, const Vector3 &c, uint32_t material, bool reverse) {
	uint32_t rgb = material_color(material);
	Color col(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
	auto push = [&](const Vector3 &v) {
		st->set_color(col);
		st->set_uv(Vector2(v.x * 0.0625f, v.z * 0.0625f));
		st->add_vertex(v);
	};
	if (!reverse) {
		push(a); push(b); push(c);
	} else {
		push(a); push(c); push(b);
	}
}

static int emit_tetra_surface(const Ref<SurfaceTool> &solid_st, const Ref<SurfaceTool> &water_st, const std::array<Vector3, 4> &pos, const std::array<TerrainSample, 4> &sample) {
	int inside_count = 0;
	bool inside[4] = {false, false, false, false};
	Vector3 inside_center;
	Vector3 outside_center;
	for (int i = 0; i < 4; i++) {
		inside[i] = sample[i].sdf < 0.0f;
		if (inside[i]) {
			inside_count++;
			inside_center += pos[i];
		} else {
			outside_center += pos[i];
		}
	}
	if (inside_count == 0 || inside_count == 4) return 0;
	inside_center /= static_cast<float>(inside_count);
	outside_center /= static_cast<float>(4 - inside_count);

	uint32_t material = choose_surface_material(sample);
	Ref<SurfaceTool> target = material == MATERIAL_WATER ? water_st : solid_st;
	std::vector<Vector3> verts;
	verts.reserve(4);

	for (int i = 0; i < 4; i++) {
		for (int j = i + 1; j < 4; j++) {
			if (inside[i] == inside[j]) continue;
			verts.push_back(interpolate_surface(pos[i], pos[j], sample[i].sdf, sample[j].sdf));
		}
	}

	if (verts.size() < 3) return 0;

	Vector3 outward = (outside_center - inside_center).normalized();
	Vector3 center;
	for (const Vector3 &v : verts) center += v;
	center /= static_cast<float>(verts.size());

	Vector3 tangent = std::abs(outward.y) < 0.9f
		? outward.cross(Vector3(0, 1, 0)).normalized()
		: outward.cross(Vector3(1, 0, 0)).normalized();
	Vector3 bitangent = outward.cross(tangent).normalized();
	std::sort(verts.begin(), verts.end(), [&](const Vector3 &a, const Vector3 &b) {
		Vector3 da = a - center;
		Vector3 db = b - center;
		return std::atan2(da.dot(bitangent), da.dot(tangent)) <
			std::atan2(db.dot(bitangent), db.dot(tangent));
	});

	int emitted = 0;
	for (size_t i = 1; i + 1 < verts.size(); i++) {
		// Godot considers clockwise triangles front-facing.
		bool reverse = (verts[i] - verts[0]).cross(verts[i + 1] - verts[0]).dot(outward) > 0.0f;
		add_colored_triangle(target, verts[0], verts[i], verts[i + 1], material, reverse);
		emitted++;
	}

	return emitted;
}

void TerrainManager::_generate_mesh(int p_cx, int p_cz, int p_lod) {
	if (!_data.is_valid()) return;
	int cs = _data->get_chunk_size();
	int ch = _data->get_chunk_height();
	int stride = CHUNK_SIZE[0] / CHUNK_SIZE[p_lod];
	int sx = cs;
	int sy = std::max(1, ch / stride);
	int sz = cs;
	int base_x = p_cx * cs * stride;
	int base_z = p_cz * cs * stride;
	float cell_size = static_cast<float>(_data->get_cell_size());
	Ref<SurfaceTool> solid_st;
	solid_st.instantiate();
	solid_st->begin(Mesh::PRIMITIVE_TRIANGLES);
	Ref<SurfaceTool> water_st;
	water_st.instantiate();
	water_st->begin(Mesh::PRIMITIVE_TRIANGLES);
	int solid_triangles = 0;
	int water_triangles = 0;

	for (int y = 0; y < sy; y++) {
		for (int z = 0; z < sz; z++) {
			for (int x = 0; x < sx; x++) {
				std::array<Vector3, 8> corner_pos;
				std::array<TerrainSample, 8> corner_sample;
				bool has_positive = false;
				bool has_negative = false;

				for (int c = 0; c < 8; c++) {
					int wx = base_x + (x + CUBE_CORNERS[c][0]) * stride;
					int wy = (y + CUBE_CORNERS[c][1]) * stride;
					int wz = base_z + (z + CUBE_CORNERS[c][2]) * stride;
					corner_pos[c] = Vector3(wx * cell_size, wy * cell_size, wz * cell_size);
					corner_sample[c] = sample_field(_data, wx, wy, wz);
					has_positive = has_positive || corner_sample[c].sdf >= 0.0f;
					has_negative = has_negative || corner_sample[c].sdf < 0.0f;
				}

				if (!has_positive || !has_negative) continue;

				for (const auto &tetra : TETRAHEDRA) {
					std::array<Vector3, 4> pos = {
						corner_pos[tetra[0]],
						corner_pos[tetra[1]],
						corner_pos[tetra[2]],
						corner_pos[tetra[3]]
					};
					std::array<TerrainSample, 4> sample = {
						corner_sample[tetra[0]],
						corner_sample[tetra[1]],
						corner_sample[tetra[2]],
						corner_sample[tetra[3]]
					};
					int emitted = emit_tetra_surface(solid_st, water_st, pos, sample);
					if (choose_surface_material(sample) == MATERIAL_WATER) {
						water_triangles += emitted;
					} else {
						solid_triangles += emitted;
					}
				}
			}
		}
	}

	Ref<ArrayMesh> solid_mesh;
	if (solid_triangles > 0) {
		solid_st->index();
		solid_st->generate_normals();
		solid_mesh = solid_st->commit();
	}
	Ref<ArrayMesh> water_mesh;
	if (water_triangles > 0) {
		water_st->index();
		water_st->generate_normals();
		water_mesh = water_st->commit();
	}
	ChunkMesh cm;
	cm.mesh = solid_mesh;
	cm.water_mesh = water_mesh;
	cm.lod = p_lod;
	cm.cx = p_cx;
	cm.cz = p_cz;
	cm.last_used_time = Time::get_singleton()->get_unix_time_from_system();
	_meshes[p_lod][ChunkPosition(p_cx, p_cz)] = cm;
	if (solid_mesh.is_null() && water_mesh.is_null()) return;
	_set_mesh_instance(p_cx, p_cz, p_lod, solid_mesh);
	_set_water_instance(p_cx, p_cz, p_lod, water_mesh);
}

void TerrainManager::elevate(int wx, int wz, int radius, int delta) {
	if (!_data.is_valid()) return;
	for (int dz = -radius; dz <= radius; dz++) for (int dx = -radius; dx <= radius; dx++) {
		if (dx * dx + dz * dz > radius * radius) continue;
		int cx = wx + dx, cz = wz + dz, top = -1;
		for (int y = _data->get_chunk_height() - 1; y >= 0; y--) if (_data->get_sdf(cx, y, cz) <= 0.0) { top = y; break; }
		if (delta > 0) {
			int material = top >= 0 ? _data->get_material(cx, top, cz) : (int)MATERIAL_GRASS;
			for (int y = top + 1; y <= std::min(top + delta, _data->get_chunk_height() - 1); y++) _data->set_cell(cx, y, cz, material, 255);
		} else {
			for (int y = top; y > std::max(-1, top + delta); y--) _data->set_cell(cx, y, cz, 0, 0);
		}
	}
	rebuild_region(wx - radius - 2, wz - radius - 2, radius * 2 + 4, radius * 2 + 4);
}

void TerrainManager::smooth(int wx, int wz, int radius) {
	if (!_data.is_valid()) return;
	struct Edit { int x, y, z, material; double sdf; };
	std::vector<Edit> edits;
	for (int dz = -radius; dz <= radius; dz++) for (int dx = -radius; dx <= radius; dx++) {
		if (dx * dx + dz * dz > radius * radius) continue;
		int x = wx + dx, z = wz + dz;
		for (int y = 0; y < _data->get_chunk_height(); y++) {
			double sum = 0.0;
			int count = 0;
			for (int oy = -1; oy <= 1; oy++) for (int oz = -1; oz <= 1; oz++) for (int ox = -1; ox <= 1; ox++) {
				int yy = y + oy;
				if (yy < 0 || yy >= _data->get_chunk_height()) continue;
				sum += _data->get_sdf(x + ox, yy, z + oz); count++;
			}
			double cur = _data->get_sdf(x, y, z);
			double avg = count > 0 ? sum / count : cur;
			double next = (cur + avg) * 0.5;
			int mat = _data->get_material(x, y, z);
			if (next < SDF_TRUNCATION || cur < SDF_TRUNCATION) edits.push_back({x, y, z, mat == 0 ? (int)MATERIAL_GRASS : mat, next});
		}
	}
	for (const Edit &e : edits) _data->set_sdf(e.x, e.y, e.z, e.sdf, e.material);
	rebuild_region(wx - radius - 2, wz - radius - 2, radius * 2 + 4, radius * 2 + 4);
}

void TerrainManager::flatten(int wx, int wz, int radius, int height) {
	if (!_data.is_valid()) return;
	height = std::max(0, std::min(height, _data->get_chunk_height() - 1));
	for (int dz = -radius; dz <= radius; dz++) for (int dx = -radius; dx <= radius; dx++) {
		if (dx * dx + dz * dz > radius * radius) continue;
		int x = wx + dx, z = wz + dz, top = -1;
		for (int y = _data->get_chunk_height() - 1; y >= 0; y--) if (_data->get_sdf(x, y, z) <= 0.0) { top = y; break; }
		int mat = top >= 0 ? _data->get_material(x, top, z) : (int)MATERIAL_GRASS;
		for (int y = top; y > height; y--) _data->set_cell(x, y, z, 0, 0);
		for (int y = std::max(0, top + 1); y <= height; y++) _data->set_cell(x, y, z, mat, 255);
	}
	rebuild_region(wx - radius - 2, wz - radius - 2, radius * 2 + 4, radius * 2 + 4);
}

void TerrainManager::paint_material(int wx, int wz, int radius, int material) {
	if (!_data.is_valid()) return;
	for (int dz = -radius; dz <= radius; dz++) for (int dx = -radius; dx <= radius; dx++) {
		if (dx * dx + dz * dz > radius * radius) continue;
		int x = wx + dx, z = wz + dz;
		for (int y = _data->get_chunk_height() - 1; y >= 0; y--) {
			double sdf = _data->get_sdf(x, y, z);
			if (sdf <= 0.0) { _data->set_sdf(x, y, z, sdf, material); break; }
		}
	}
	rebuild_region(wx - radius - 2, wz - radius - 2, radius * 2 + 4, radius * 2 + 4);
}

void TerrainManager::crater(int base_x, int base_z, int radius, int depth) {
	if (!_data.is_valid()) return;
	for (int dz = -radius; dz <= radius; dz++) for (int dx = -radius; dx <= radius; dx++) {
		double dist = std::sqrt((double)(dx * dx + dz * dz));
		if (dist > radius) continue;
		int x = base_x + dx, z = base_z + dz, top = -1;
		for (int y = _data->get_chunk_height() - 1; y >= 0; y--) if (_data->get_sdf(x, y, z) <= 0.0) { top = y; break; }
		if (top < 0) continue;
		double n = dist / std::max(1, radius);
		int carve = (int)std::round(depth * (1.0 - n * n));
		int rim = (n > 0.72 && n < 1.0) ? (int)std::round(depth * 0.35 * (1.0 - std::abs(n - 0.86) / 0.14)) : 0;
		int mat = _data->get_material(x, top, z);
		for (int y = top; y > top - carve && y >= 0; y--) _data->set_cell(x, y, z, 0, 0);
		for (int y = top + 1; y <= std::min(_data->get_chunk_height() - 1, top + rim); y++) _data->set_cell(x, y, z, mat, 255);
	}
	rebuild_region(base_x - radius - 2, base_z - radius - 2, radius * 2 + 4, radius * 2 + 4);
}

int TerrainManager::floodfill(int wx, int wy, int wz, int material) {
	if (!_data.is_valid()) return 0;
	double target_sdf = _data->get_sdf(wx, wy, wz);
	int target_material = _data->get_material(wx, wy, wz);
	if (target_sdf > 0.0 || target_material == material) return 0;
	int count = 0;
	std::queue<Vector3i> q; q.push(Vector3i(wx, wy, wz));
	std::unordered_set<int64_t> seen;
	const int MAX_FILL = 50000;
	int ch = _data->get_chunk_height();
	int dirs[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
	while (!q.empty() && count < MAX_FILL) {
		Vector3i p = q.front(); q.pop();
		if (p.y < 0 || p.y >= ch) continue;
		int64_t key = ((int64_t)p.x << 42) ^ ((int64_t)p.y << 21) ^ (int64_t)p.z;
		if (seen.find(key) != seen.end()) continue;
		seen.insert(key);
		double sdf = _data->get_sdf(p.x, p.y, p.z);
		if (sdf > 0.0 || _data->get_material(p.x, p.y, p.z) != target_material) continue;
		_data->set_sdf(p.x, p.y, p.z, sdf, material);
		count++;
		for (int d = 0; d < 6; d++) q.push(Vector3i(p.x + dirs[d][0], p.y + dirs[d][1], p.z + dirs[d][2]));
	}
	rebuild_region(wx - 64, wz - 64, 128, 128);
	return count;
}

bool TerrainManager::is_water(int wx, int wy, int wz) const { return _data.is_valid() && _data->get_material(wx, wy, wz) == MATERIAL_WATER; }
bool TerrainManager::is_in_water(float world_x, float world_y, float world_z) const {
	if (!_data.is_valid()) return false;
	float cell_size = static_cast<float>(_data->get_cell_size());
	return is_water(
			static_cast<int>(std::floor(world_x / cell_size)),
			static_cast<int>(std::floor(world_y / cell_size)),
			static_cast<int>(std::floor(world_z / cell_size)));
}
float TerrainManager::water_level(int wx, int wz) const {
	if (!_data.is_valid()) return -1.0f;
	for (int y = _data->get_chunk_height() - 1; y >= 0; y--) {
		if (_data->get_material(wx, y, wz) == MATERIAL_WATER) {
			return (static_cast<float>(y) + 1.0f) * static_cast<float>(_data->get_cell_size());
		}
	}
	return -1.0f;
}

void TerrainManager::rebuild_chunk(int p_cx, int p_cz) {
	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		_clear_mesh_instance(p_cx, p_cz, lod);
		_meshes[lod].erase(ChunkPosition(p_cx, p_cz));
		_generate_mesh(p_cx, p_cz, lod);
	}
}
void TerrainManager::rebuild_all() { clear(); if (_data.is_valid()) _update_chunks(); }
void TerrainManager::clear() {
	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		for (auto &[pos, _] : _meshes[lod]) _clear_mesh_instance(pos.x, pos.z, lod);
		_meshes[lod].clear(); _dirty_chunks[lod].clear();
	}
}

String TerrainManager::_get_chunk_name(int cx, int cz, int lod) const { return "ChunkMesh_" + itos(cx) + "_" + itos(cz) + "_L" + itos(lod); }
String TerrainManager::_get_water_name(int cx, int cz, int lod) const { return "WaterMesh_" + itos(cx) + "_" + itos(cz) + "_L" + itos(lod); }
void TerrainManager::_clear_mesh_instance(int cx, int cz, int lod) {
	Node *n = find_child(_get_chunk_name(cx, cz, lod), true, false); if (n) { remove_child(n); n->queue_free(); }
	Node *wn = find_child(_get_water_name(cx, cz, lod), true, false); if (wn) { remove_child(wn); wn->queue_free(); }
}
void TerrainManager::_set_mesh_instance(int cx, int cz, int lod, const Ref<ArrayMesh> &p_mesh) {
	Node *existing = find_child(_get_chunk_name(cx, cz, lod), true, false); if (existing) { remove_child(existing); existing->queue_free(); }
	if (p_mesh.is_null()) return;
	MeshInstance3D *mi = memnew(MeshInstance3D); mi->set_name(_get_chunk_name(cx, cz, lod)); mi->set_mesh(p_mesh); if (_solid_material.is_valid()) mi->set_material_override(_solid_material); add_child(mi); mi->set_owner(this);
	if (_collision_enabled && lod == 0 && _chunk_distance(cx, cz, lod) <= _collision_distance) {
		Ref<ConcavePolygonShape3D> shape = p_mesh->create_trimesh_shape();
		if (shape.is_valid()) {
			StaticBody3D *body = memnew(StaticBody3D);
			body->set_name("Collision");
			body->set_collision_layer(1);
			body->set_collision_mask(1);
			CollisionShape3D *collider = memnew(CollisionShape3D);
			collider->set_name("CollisionShape3D");
			collider->set_shape(shape);
			mi->add_child(body);
			body->add_child(collider);
			body->set_owner(this);
			collider->set_owner(this);
		}
	}
}
void TerrainManager::_set_water_instance(int cx, int cz, int lod, const Ref<ArrayMesh> &p_water) {
	Node *existing = find_child(_get_water_name(cx, cz, lod), true, false); if (existing) { remove_child(existing); existing->queue_free(); }
	if (p_water.is_null()) return;
	MeshInstance3D *mi = memnew(MeshInstance3D); mi->set_name(_get_water_name(cx, cz, lod)); mi->set_mesh(p_water); if (_water_material.is_valid()) mi->set_material_override(_water_material); add_child(mi); mi->set_owner(this);
}
void TerrainManager::_clean_mesh_cache() {
	double now = Time::get_singleton()->get_unix_time_from_system();
	for (int lod = 0; lod < LOD_LEVELS; lod++) {
		std::vector<ChunkPosition> dead;
		for (auto &[pos, info] : _meshes[lod]) if (now - info.last_used_time > 30.0 && !_chunk_in_view(pos.x, pos.z, lod)) dead.push_back(pos);
		for (auto &pos : dead) { _clear_mesh_instance(pos.x, pos.z, lod); _meshes[lod].erase(pos); }
	}
}

void TerrainManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_terrain_data", "data"), &TerrainManager::set_terrain_data);
	ClassDB::bind_method(D_METHOD("get_terrain_data"), &TerrainManager::get_terrain_data);
	ClassDB::bind_method(D_METHOD("set_view_distance", "distance"), &TerrainManager::set_view_distance);
	ClassDB::bind_method(D_METHOD("get_view_distance"), &TerrainManager::get_view_distance);
	ClassDB::bind_method(D_METHOD("set_viewer_position", "position"), &TerrainManager::set_viewer_position);
	ClassDB::bind_method(D_METHOD("get_viewer_position"), &TerrainManager::get_viewer_position);
	ClassDB::bind_method(D_METHOD("set_water_material", "material"), &TerrainManager::set_water_material);
	ClassDB::bind_method(D_METHOD("get_water_material"), &TerrainManager::get_water_material);
	ClassDB::bind_method(D_METHOD("set_solid_material", "material"), &TerrainManager::set_solid_material);
	ClassDB::bind_method(D_METHOD("get_solid_material"), &TerrainManager::get_solid_material);
	ClassDB::bind_method(D_METHOD("set_collision_enabled", "enabled"), &TerrainManager::set_collision_enabled);
	ClassDB::bind_method(D_METHOD("get_collision_enabled"), &TerrainManager::get_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_collision_distance", "distance"), &TerrainManager::set_collision_distance);
	ClassDB::bind_method(D_METHOD("get_collision_distance"), &TerrainManager::get_collision_distance);
	ClassDB::bind_method(D_METHOD("set_mesh_build_budget_ms", "budget_ms"), &TerrainManager::set_mesh_build_budget_ms);
	ClassDB::bind_method(D_METHOD("get_mesh_build_budget_ms"), &TerrainManager::get_mesh_build_budget_ms);
	ClassDB::bind_method(D_METHOD("rebuild_chunk", "cx", "cz"), &TerrainManager::rebuild_chunk);
	ClassDB::bind_method(D_METHOD("rebuild_region", "x", "z", "width", "depth"), &TerrainManager::rebuild_region);
	ClassDB::bind_method(D_METHOD("mark_region_dirty", "x", "z", "width", "depth"), &TerrainManager::mark_region_dirty);
	ClassDB::bind_method(D_METHOD("flush_dirty_chunks"), &TerrainManager::flush_dirty_chunks);
	ClassDB::bind_method(D_METHOD("update_streaming"), &TerrainManager::update_streaming);
	ClassDB::bind_method(D_METHOD("rebuild_all"), &TerrainManager::rebuild_all);
	ClassDB::bind_method(D_METHOD("clear"), &TerrainManager::clear);
	ClassDB::bind_method(D_METHOD("get_chunk_size"), &TerrainManager::get_chunk_size);
	ClassDB::bind_method(D_METHOD("get_chunk_height"), &TerrainManager::get_chunk_height);
	ClassDB::bind_method(D_METHOD("elevate", "wx", "wz", "radius", "delta"), &TerrainManager::elevate);
	ClassDB::bind_method(D_METHOD("smooth", "wx", "wz", "radius"), &TerrainManager::smooth);
	ClassDB::bind_method(D_METHOD("flatten", "wx", "wz", "radius", "height"), &TerrainManager::flatten);
	ClassDB::bind_method(D_METHOD("paint_material", "wx", "wz", "radius", "material"), &TerrainManager::paint_material);
	ClassDB::bind_method(D_METHOD("crater", "cx", "cz", "radius", "depth"), &TerrainManager::crater);
	ClassDB::bind_method(D_METHOD("floodfill", "wx", "wy", "wz", "material"), &TerrainManager::floodfill);
	ClassDB::bind_method(D_METHOD("is_water", "wx", "wy", "wz"), &TerrainManager::is_water);
	ClassDB::bind_method(D_METHOD("is_in_water", "world_x", "world_y", "world_z"), &TerrainManager::is_in_water);
	ClassDB::bind_method(D_METHOD("water_level", "wx", "wz"), &TerrainManager::water_level);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "terrain_data", PROPERTY_HINT_RESOURCE_TYPE, "TerrainData"), "set_terrain_data", "get_terrain_data");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_distance"), "set_view_distance", "get_view_distance");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "viewer_position"), "set_viewer_position", "get_viewer_position");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "water_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_water_material", "get_water_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "solid_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_solid_material", "get_solid_material");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collision_enabled"), "set_collision_enabled", "get_collision_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_distance", PROPERTY_HINT_RANGE, "0,1024,1"), "set_collision_distance", "get_collision_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mesh_build_budget_ms", PROPERTY_HINT_RANGE, "1,33,0.5"), "set_mesh_build_budget_ms", "get_mesh_build_budget_ms");
}

