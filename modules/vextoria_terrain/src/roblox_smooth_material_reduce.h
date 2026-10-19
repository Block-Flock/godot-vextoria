#ifndef VEXTORIA_ROBLOX_SMOOTH_MATERIAL_REDUCE_H
#define VEXTORIA_ROBLOX_SMOOTH_MATERIAL_REDUCE_H

#include <cstdint>


struct RobloxSmoothWeightedMaterial {
	uint8_t material = 0;
	uint32_t weight = 0;
};

constexpr RobloxSmoothWeightedMaterial roblox_smooth_reduce_mip_material(
		const RobloxSmoothWeightedMaterial &a,
		const RobloxSmoothWeightedMaterial &b) {
	if (a.material == b.material)
		return {a.material, a.weight + b.weight};
	if (a.weight != b.weight)
		return a.weight > b.weight ? a : b;
	// Voxel2::Grid: higher material wins an equal-occupancy mip tie.
	return a.material > b.material ? a : b;
}

constexpr RobloxSmoothWeightedMaterial roblox_smooth_reduce_surface_material(
		const RobloxSmoothWeightedMaterial &a,
		const RobloxSmoothWeightedMaterial &b) {
	if (a.material == b.material)
		return {a.material, a.weight + b.weight};
	if (a.weight != b.weight)
		return a.weight > b.weight ? a : b;
	// Voxel2::Mesher: lower material wins an equal-weight vertex tie.
	return a.material < b.material ? a : b;
}

static_assert(roblox_smooth_reduce_mip_material({2, 128}, {8, 128}).material == 8,
	"Roblox 2016 mip equal-weight tie must choose the higher material ID");
static_assert(roblox_smooth_reduce_surface_material({2, 128}, {8, 128}).material == 2,
	"Roblox 2016 surface equal-weight tie must choose the lower material ID");
static_assert(roblox_smooth_reduce_mip_material({2, 200}, {8, 128}).material == 2,
	"higher occupancy must win before the mip material-ID tie-break");
static_assert(roblox_smooth_reduce_surface_material({2, 200}, {8, 128}).material == 2,
	"higher surface weight must win before the material-ID tie-break");


#endif
