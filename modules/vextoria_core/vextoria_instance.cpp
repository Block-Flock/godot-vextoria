/**************************************************************************/
/*  vextoria_instance.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                        VEXTORIA ENGINE                                  */
/*                         https://godotengine.org                        */
/**************************************************************************/
/* Copyright (c) 2026 Vextoria contributors.                              */
/* Copyright (c) 2014-present Godot Engine contributors.                   */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,     */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "vextoria_instance.h"

#include "core/object/class_db.h"

void VextoriaInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_archivable"), &VextoriaInstance::is_archivable);
	ClassDB::bind_method(D_METHOD("set_archivable", "archivable"), &VextoriaInstance::set_archivable);
	ClassDB::bind_method(D_METHOD("is_locked"), &VextoriaInstance::is_locked);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &VextoriaInstance::set_locked);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Archivable"), "set_archivable", "is_archivable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Locked"), "set_locked", "is_locked");
}

void Part::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_size"), &Part::get_size);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Part::set_size);
	ClassDB::bind_method(D_METHOD("get_color"), &Part::get_color);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &Part::set_color);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "Size"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "Color"), "set_color", "get_color");
}

void VextoriaScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_source"), &VextoriaScript::get_source);
	ClassDB::bind_method(D_METHOD("set_source", "source"), &VextoriaScript::set_source);
	ClassDB::bind_method(D_METHOD("is_disabled"), &VextoriaScript::is_disabled);
	ClassDB::bind_method(D_METHOD("set_disabled", "disabled"), &VextoriaScript::set_disabled);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "Source", PROPERTY_HINT_MULTILINE_TEXT), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "Disabled"), "set_disabled", "is_disabled");
}
