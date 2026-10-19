/**************************************************************************/
/*  vextoria_collision_layers.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         VEXTORIA ENGINE                                */
/**************************************************************************/

#ifndef VEXTORIA_COLLISION_LAYERS_H
#define VEXTORIA_COLLISION_LAYERS_H

#include "core/typedefs.h"

// Shared native collision routing for Roblox-compatible Parts and world
// geometry. Authored Roblox collision groups are translated at the Part
// boundary; engine-owned static world surfaces must participate in the same
// native channels instead of silently remaining on Godot's default layer.
namespace VextoriaCollisionLayers {
constexpr uint32_t ROBLOX_AUTHORED_DEFAULT = 1u;
constexpr uint32_t ROBLOX_CAMERA_CLIP = 1u << 5;
constexpr uint32_t ROBLOX_STATIC = 1u << 30;
constexpr uint32_t ROBLOX_DYNAMIC = 1u << 31;
} // namespace VextoriaCollisionLayers

#endif // VEXTORIA_COLLISION_LAYERS_H
