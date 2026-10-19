#include "terrain_data.h"


bool TerrainData::get_dirty_since(int64_t p_revision, Vector3i &r_min, Vector3i &r_max, bool &r_full) const {
	if (p_revision >= 0 && static_cast<uint64_t>(p_revision) >= _revision) {
		r_full = false;
		return false;
	}

	const uint64_t since = p_revision < 0 ? 0 : static_cast<uint64_t>(p_revision);
	const DirtyRegion region = query_dirty_region_since(since);
	if (!region.valid) {
		r_full = false;
		return false;
	}

	r_full = region.full_rebuild;
	r_min = region.minimum;
	r_max = region.maximum;
	return true;
}

