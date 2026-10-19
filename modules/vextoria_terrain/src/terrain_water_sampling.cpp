#include "terrain_manager.h"

#include <algorithm>
#include <cmath>
#include <limits>


// Water is a separate scalar field: only Water samples contribute negative SDF.
// Rock/dirt samples are forced outside the fluid, preventing water physics from
// treating a solid interface as a free water surface.
double TerrainManager::sample_water_field(const Vector3 &p_world_position) const {
	if (!_data.is_valid()) {
		return SDF_TRUNCATION;
	}

	const double cell_size = _data->get_cell_size();
	if (cell_size <= 0.0) {
		return SDF_TRUNCATION;
	}

	const Vector3 gp = p_world_position / static_cast<float>(cell_size);
	const int x0 = static_cast<int>(std::floor(gp.x));
	const int y0 = static_cast<int>(std::floor(gp.y));
	const int z0 = static_cast<int>(std::floor(gp.z));
	const double tx = gp.x - static_cast<double>(x0);
	const double ty = gp.y - static_cast<double>(y0);
	const double tz = gp.z - static_cast<double>(z0);

	auto water_sdf = [&](int x, int y, int z) -> double {
		const double sdf = _data->get_sdf(x, y, z);
		if (_data->get_material(x, y, z) == MATERIAL_WATER) {
			return sdf;
		}
		return std::max(0.001, std::abs(sdf));
	};
	auto lerp = [](double a, double b, double t) {
		return a + (b - a) * t;
	};

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

double TerrainManager::sample_water_density(const Vector3 &p_world_position) const {
	const double field = sample_water_field(p_world_position);
	return std::clamp(0.5 - field * 0.5, 0.0, 1.0);
}

double TerrainManager::find_water_surface_y(const Vector3 &p_world_position, double p_search_radius) const {
	if (!_data.is_valid()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	const double cell_size = std::max(0.05, _data->get_cell_size());
	const double radius = std::max(cell_size, std::abs(p_search_radius));
	const double step = std::max(0.05, cell_size * 0.5);
	const double start = p_world_position.y - radius;
	const double end = p_world_position.y + radius;

	double previous_y = start;
	double previous_f = sample_water_field(Vector3(p_world_position.x, static_cast<float>(start), p_world_position.z));
	double best = std::numeric_limits<double>::quiet_NaN();

	for (double y = start + step; y <= end + 1e-6; y += step) {
		const double f = sample_water_field(Vector3(p_world_position.x, static_cast<float>(y), p_world_position.z));
		if (previous_f < 0.0 && f >= 0.0) {
			double lo = previous_y;
			double hi = y;
			for (int iteration = 0; iteration < 12; ++iteration) {
				const double mid = (lo + hi) * 0.5;
				const double mid_field = sample_water_field(Vector3(p_world_position.x, static_cast<float>(mid), p_world_position.z));
				if (mid_field < 0.0) {
					lo = mid;
				} else {
					hi = mid;
				}
			}
			best = (lo + hi) * 0.5;
		}
		previous_y = y;
		previous_f = f;
	}
	return best;
}

