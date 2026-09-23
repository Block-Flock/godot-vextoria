/**************************************************************************/
/*  vextoria_instance.h                                                   */
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

#pragma once

#include "core/math/color.h"
#include "scene/3d/node_3d.h"

class VextoriaInstance : public Node3D {
	GDCLASS(VextoriaInstance, Node3D);

	bool archivable = true;
	bool locked = false;

protected:
	static void _bind_methods();

public:
	bool is_archivable() const { return archivable; }
	void set_archivable(bool p_archivable) { archivable = p_archivable; }
	bool is_locked() const { return locked; }
	void set_locked(bool p_locked) { locked = p_locked; }
};

class Part : public VextoriaInstance {
	GDCLASS(Part, VextoriaInstance);

	Vector3 size = Vector3(4, 1, 2);
	Color color = Color(0.639216, 0.635294, 0.647059);

protected:
	static void _bind_methods();

public:
	Vector3 get_size() const { return size; }
	void set_size(const Vector3 &p_size) { size = p_size; }
	Color get_color() const { return color; }
	void set_color(const Color &p_color) { color = p_color; }
};

class Model : public VextoriaInstance {
	GDCLASS(Model, VextoriaInstance);

protected:
	static void _bind_methods() {}
};

class Folder : public VextoriaInstance {
	GDCLASS(Folder, VextoriaInstance);

protected:
	static void _bind_methods() {}
};

class VextoriaScript : public VextoriaInstance {
	GDCLASS(VextoriaScript, VextoriaInstance);

	String source;
	bool disabled = false;

protected:
	static void _bind_methods();

public:
	String get_source() const { return source; }
	void set_source(const String &p_source) { source = p_source; }
	bool is_disabled() const { return disabled; }
	void set_disabled(bool p_disabled) { disabled = p_disabled; }
};
