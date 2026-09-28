/**************************************************************************/
/*  register_types.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "register_types.h"

#include "vextoria_explorer_tree.h"
#include "vextoria_instance.h"
#include "vextoria_part_renderer.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/part_gizmo_plugin.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"

static void initialize_vextoria_editor_gizmos() {
	Ref<VextoriaPartGizmoPlugin> part_gizmo;
	part_gizmo.instantiate();
	Node3DEditor::get_singleton()->add_gizmo_plugin(part_gizmo);
}
#endif

void initialize_vextoria_core_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorNode::add_init_callback(initialize_vextoria_editor_gizmos);
	}
#endif
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_ABSTRACT_CLASS(VextoriaInstance);
	GDREGISTER_CLASS(Part);
	// AppearanceMaterial is an optional authored override. The default Part
	// derives its material from the registry; do not retain that live GPU
	// resource in ClassDB's process-wide default-value cache.
	ClassDB::set_property_default_value("Part", "AppearanceMaterial", Variant());
	GDREGISTER_CLASS(Model);
	GDREGISTER_CLASS(Folder);
	GDREGISTER_CLASS(VextoriaScript);
	GDREGISTER_CLASS(VextoriaExplorerTree);
	GDREGISTER_CLASS(VextoriaPartRenderer);
}

void uninitialize_vextoria_core_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	Part::clear_shape_asset_cache();
}
