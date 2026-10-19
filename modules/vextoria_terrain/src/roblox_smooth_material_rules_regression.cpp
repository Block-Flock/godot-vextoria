#include "roblox_smooth_material_rules.h"


namespace {

using namespace roblox_smooth_material_rules;

static_assert(resolve_mip_equal_weight(2, 8) == 8,
	"Roblox 2016 mip/downsample equal-weight ties must prefer the higher material ID");
static_assert(resolve_mip_equal_weight(8, 2) == 8,
	"Mip tie resolution must be independent of argument ordering");
static_assert(resolve_surface_equal_weight(2, 8) == 2,
	"Roblox 2016 surface-vertex equal-weight ties must prefer the lower material ID");
static_assert(resolve_surface_equal_weight(8, 2) == 2,
	"Surface tie resolution must be independent of argument ordering");
static_assert(resolve_mip_equal_weight(2, 8) != resolve_surface_equal_weight(2, 8),
	"Mip and final surface material tie rules are intentionally different");

} // namespace
