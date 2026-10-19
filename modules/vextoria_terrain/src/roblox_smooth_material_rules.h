#ifndef VEXTORIA_ROBLOX_SMOOTH_MATERIAL_RULES_H
#define VEXTORIA_ROBLOX_SMOOTH_MATERIAL_RULES_H

#include <cstdint>

namespace roblox_smooth_material_rules {

// App/voxel2/Grid.cpp: equal-occupancy mip/downsample ties prefer the higher
// material ID. This is important because solid material IDs beat Air (0).
constexpr uint8_t resolve_mip_equal_weight(uint8_t a, uint8_t b) {
	return a > b ? a : b;
}

// App/voxel2/Mesher.cpp: final surface-vertex equal-weight ties deliberately
// use the opposite ordering and prefer the lower material ID.
constexpr uint8_t resolve_surface_equal_weight(uint8_t a, uint8_t b) {
	return a < b ? a : b;
}

} // namespace roblox_smooth_material_rules

#endif
