#include "src/terrain_manager.h"
#include "src/roblox_smooth_terrain_manager.h"
#include "src/roblox2016/roblox2016_part3d.h"
#include "src/roblox2016/roblox2016_buoyancy3d.h"
#include "src/roblox2016/roblox2016_swimming_controller.h"
#include "src/roblox2016/roblox2016_csg_compiler.h"

void TerrainManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			_vextoria_ready();
			break;
		case NOTIFICATION_PROCESS:
			_vextoria_process(get_process_delta_time());
			break;
	}
}

void RobloxSmoothTerrainManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			_vextoria_ready();
			break;
		case NOTIFICATION_PROCESS:
			_vextoria_process(get_process_delta_time());
			break;
	}
}

void Roblox2016Part3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			_vextoria_ready();
			break;
	}
}

void Roblox2016Buoyancy3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			_vextoria_ready();
			break;
		case NOTIFICATION_PHYSICS_PROCESS:
			_vextoria_physics_process(get_physics_process_delta_time());
			break;
	}
}

void Roblox2016SwimmingController::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			_vextoria_ready();
			break;
		case NOTIFICATION_PHYSICS_PROCESS:
			_vextoria_physics_process(get_physics_process_delta_time());
			break;
	}
}

void Roblox2016CSGCompiler::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS:
			_vextoria_process(get_process_delta_time());
			break;
	}
}
