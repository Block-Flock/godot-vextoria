#include "terrain_manager.h"
#include "roblox_smooth_terrain_manager.h"

#include <algorithm>
#include <cmath>
#include <limits>

// Water samples retain their SDF; all other terrain is outside the fluid field.
static double sample_water_sdf_field(const Ref<TerrainData> &p_data, const Vector3 &p_position) {
	if (!p_data.is_valid())
		return SDF_TRUNCATION;

	const double cell_size = p_data->get_cell_size();
	if (cell_size <= 0.0)
		return SDF_TRUNCATION;

	const Vector3 gp = p_position / static_cast<float>(cell_size);
	const int x0 = static_cast<int>(std::floor(gp.x));
	const int y0 = static_cast<int>(std::floor(gp.y));
	const int z0 = static_cast<int>(std::floor(gp.z));
	const double tx = gp.x - static_cast<double>(x0);
	const double ty = gp.y - static_cast<double>(y0);
	const double tz = gp.z - static_cast<double>(z0);
	auto water_sdf = [&](int x, int y, int z) -> double {
		const double sdf = p_data->get_sdf(x, y, z);
		return p_data->get_material(x, y, z) == MATERIAL_WATER ? sdf : std::max(0.001, std::abs(sdf));
	};
	auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
	const double c000 = water_sdf(x0, y0, z0);
	const double c100 = water_sdf(x0 + 1, y0, z0);
	const double c010 = water_sdf(x0, y0 + 1, z0);
	const double c110 = water_sdf(x0 + 1, y0 + 1, z0);
	const double c001 = water_sdf(x0, y0, z0 + 1);
	const double c101 = water_sdf(x0 + 1, y0, z0 + 1);
	const double c011 = water_sdf(x0, y0 + 1, z0 + 1);
	const double c111 = water_sdf(x0 + 1, y0 + 1, z0 + 1);
	const double x00 = lerp(c000, c100, tx);
	const double x10 = lerp(c010, c110, tx);
	const double x01 = lerp(c001, c101, tx);
	const double x11 = lerp(c011, c111, tx);
	return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

static double find_water_surface_from_data(const Ref<TerrainData> &p_data, const Vector3 &p_position, double p_search_radius) {
	if (!p_data.is_valid())
		return std::numeric_limits<double>::quiet_NaN();

	const double cell_size = std::max(0.05, p_data->get_cell_size());
	const double radius = std::max(cell_size, std::abs(p_search_radius));
	const double step = std::max(0.05, cell_size * 0.5);
	const double start = p_position.y - radius;
	const double end = p_position.y + radius;
	double previous_y = start;
	double previous_f = sample_water_sdf_field(p_data, Vector3(p_position.x, static_cast<float>(start), p_position.z));
	double best = std::numeric_limits<double>::quiet_NaN();

	for (double y = start + step; y <= end + 1e-6; y += step) {
		const double f = sample_water_sdf_field(p_data, Vector3(p_position.x, static_cast<float>(y), p_position.z));
		if (previous_f < 0.0 && f >= 0.0) {
			double lo = previous_y;
			double hi = y;
			for (int iteration = 0; iteration < 12; ++iteration) {
				const double mid = (lo + hi) * 0.5;
				if (sample_water_sdf_field(p_data, Vector3(p_position.x, static_cast<float>(mid), p_position.z)) < 0.0)
					lo = mid;
				else
					hi = mid;
			}
			best = (lo + hi) * 0.5;
		}
		previous_y = y;
		previous_f = f;
	}
	return best;
}

double TerrainManager::sample_water_field(const Vector3 &p_world_position) const {
	return sample_water_sdf_field(_data, p_world_position);
}

double TerrainManager::sample_water_density(const Vector3 &p_world_position) const {
	return std::clamp(0.5 - sample_water_sdf_field(_data, p_world_position) * 0.5, 0.0, 1.0);
}

double TerrainManager::find_water_surface_y(const Vector3 &p_world_position, double p_search_radius) const {
	return find_water_surface_from_data(_data, p_world_position, p_search_radius);
}

double RobloxSmoothTerrainManager::sample_water_density(const Vector3 &p_local_position) const {
	return std::clamp(0.5 - sample_water_sdf_field(get_terrain_data(), p_local_position) * 0.5, 0.0, 1.0);
}

double RobloxSmoothTerrainManager::find_water_surface_y(const Vector3 &p_local_position, double p_search_radius) const {
	return find_water_surface_from_data(get_terrain_data(), p_local_position, p_search_radius);
}
