#include "register_types.h"
#include "core/object/class_db.h"
#include "src/terrain_manager.h"
#include "src/terrain_data.h"
#include "src/roblox_smooth_terrain_manager.h"
#include "src/roblox2016/roblox2016_part3d.h"
#include "src/roblox2016/roblox2016_buoyancy3d.h"
#include "src/roblox2016/roblox2016_swimming_controller.h"
#include "src/roblox2016/roblox2016_csg_compiler.h"
#include "src/roblox2016/roblox2016_csg_physics_decoder.h"
#include "src/roblox2016/roblox2016_csg_mesh_decoder.h"
#include "src/roblox2016/roblox2016_tool_assembly.h"
#include "src/roblox2016/roblox2016_tool_assembly_driver.h"
#include "src/roblox2016/roblox2016_transform_math.h"

void initialize_vextoria_terrain_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(TerrainManager);
	GDREGISTER_CLASS(TerrainData);
	GDREGISTER_CLASS(RobloxSmoothTerrainManager);
	GDREGISTER_CLASS(Roblox2016Part3D);
	GDREGISTER_CLASS(Roblox2016Buoyancy3D);
	GDREGISTER_CLASS(Roblox2016SwimmingController);
	GDREGISTER_CLASS(Roblox2016CSGCompiler);
	GDREGISTER_CLASS(Roblox2016CSGPhysicsDecoder);
	GDREGISTER_CLASS(Roblox2016CSGMeshDecoder);
	GDREGISTER_CLASS(Roblox2016ToolAssembly);
	GDREGISTER_CLASS(Roblox2016ToolAssemblyDriver);
	GDREGISTER_CLASS(Roblox2016TransformMath);
}

void uninitialize_vextoria_terrain_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
