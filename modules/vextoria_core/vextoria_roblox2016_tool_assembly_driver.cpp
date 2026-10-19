// Copyright (c) 2026 Vextoria contributors.
// SPDX-License-Identifier: MIT
/**************************************************************************/
/*  vextoria_roblox2016_tool_assembly_driver.cpp                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                         VEXTORIA ENGINE                                */
/**************************************************************************/

#include "vextoria_roblox2016_tool_assembly_driver.h"

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/variant/dictionary.h"
#include "scene/3d/physics/physics_body_3d.h"
#include "vextoria_instance.h"
#include "vextoria_roblox2016_transform_math.h"
#include "vextoria_weld.h"

#include <unordered_map>
#include <unordered_set>

static void collect_tool_descendants(Node *p_parent, std::vector<Node *> &r_nodes) {
	for (int i = 0; i < p_parent->get_child_count(); ++i) {
		Node *child = p_parent->get_child(i);
		r_nodes.push_back(child);
		collect_tool_descendants(child, r_nodes);
	}
}

void VextoriaRoblox2016ToolAssemblyDriver::configure(const Array &p_members, const Array &p_handle_to_member) {
	clear();
	const int count = MIN(p_members.size(), p_handle_to_member.size());
	for (int i = 0; i < count; ++i) {
		if (p_members[i].get_type() != Variant::OBJECT || p_handle_to_member[i].get_type() != Variant::TRANSFORM3D) {
			continue;
		}
		Object *object = p_members[i];
		Node3D *node = Object::cast_to<Node3D>(object);
		if (node == nullptr) {
			continue;
		}
		const Transform3D relative = p_handle_to_member[i];
		members.push_back(node->get_instance_id());
		handle_to_member.push_back(VextoriaRoblox2016TransformMath::rigid_frame(relative));
	}
}

int VextoriaRoblox2016ToolAssemblyDriver::configure_graph(Node3D *p_handle, const Array &p_candidates, const Array &p_welds) {
	clear();
	if (p_handle == nullptr) {
		return 0;
	}
	std::unordered_set<uint64_t> candidates;
	for (int i = 0; i < p_candidates.size(); ++i) {
		if (p_candidates[i].get_type() != Variant::OBJECT) {
			continue;
		}
		Object *object = p_candidates[i];
		Node3D *node = Object::cast_to<Node3D>(object);
		if (node != nullptr) {
			candidates.insert(node->get_instance_id());
		}
	}
	const uint64_t handle_id = p_handle->get_instance_id();
	if (candidates.count(handle_id) == 0) {
		return 0;
	}
	struct Edge {
		uint64_t other;
		Transform3D relative;
	};
	std::unordered_map<uint64_t, std::vector<Edge>> adjacency;
	for (int i = 0; i < p_welds.size(); ++i) {
		if (p_welds[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary weld = p_welds[i];
		const Variant endpoint0 = weld.get("part0", Variant());
		const Variant endpoint1 = weld.get("part1", Variant());
		const Variant frame0 = weld.get("c0", Variant());
		const Variant frame1 = weld.get("c1", Variant());
		if (endpoint0.get_type() != Variant::OBJECT || endpoint1.get_type() != Variant::OBJECT || frame0.get_type() != Variant::TRANSFORM3D || frame1.get_type() != Variant::TRANSFORM3D) {
			continue;
		}
		Object *object0 = endpoint0;
		Object *object1 = endpoint1;
		Node3D *part0 = Object::cast_to<Node3D>(object0);
		Node3D *part1 = Object::cast_to<Node3D>(object1);
		if (part0 == nullptr || part1 == nullptr) {
			continue;
		}
		const uint64_t id0 = part0->get_instance_id();
		const uint64_t id1 = part1->get_instance_id();
		if (id0 == id1 || candidates.count(id0) == 0 || candidates.count(id1) == 0) {
			continue;
		}
		const Transform3D c0 = VextoriaRoblox2016TransformMath::rigid_frame(frame0);
		const Transform3D c1 = VextoriaRoblox2016TransformMath::rigid_frame(frame1);
		adjacency[id0].push_back({ id1, VextoriaRoblox2016TransformMath::rigid_frame(c0 * c1.affine_inverse()) });
		adjacency[id1].push_back({ id0, VextoriaRoblox2016TransformMath::rigid_frame(c1 * c0.affine_inverse()) });
	}
	std::unordered_set<uint64_t> visited;
	visited.insert(handle_id);
	members.push_back(handle_id);
	handle_to_member.push_back(Transform3D());
	for (size_t current = 0; current < members.size(); ++current) {
		const auto found = adjacency.find(members[current]);
		if (found == adjacency.end()) {
			continue;
		}
		for (const Edge &edge : found->second) {
			if (!visited.insert(edge.other).second) {
				continue;
			}
			const Transform3D relative = VextoriaRoblox2016TransformMath::rigid_frame(handle_to_member[current] * edge.relative);
			members.push_back(edge.other);
			handle_to_member.push_back(relative);
		}
	}
	return get_member_count();
}

int VextoriaRoblox2016ToolAssemblyDriver::configure_tool_graph(Node3D *p_handle, Node *p_tool_root) {
	clear();
	if (p_handle == nullptr || p_tool_root == nullptr || !p_handle->is_inside_tree() || !p_tool_root->is_inside_tree()) {
		return 0;
	}

	std::vector<Node *> descendants;
	collect_tool_descendants(p_tool_root, descendants);
	Array candidates;
	std::unordered_set<uint64_t> candidate_ids;
	for (Node *node : descendants) {
		PhysicsBody3D *body = Object::cast_to<PhysicsBody3D>(node);
		if (body == nullptr || !body->is_vextoria_instance_native()) {
			continue;
		}
		const uint64_t id = body->get_instance_id();
		if (candidate_ids.insert(id).second) {
			candidates.push_back(body);
		}
	}
	if (candidate_ids.count(p_handle->get_instance_id()) == 0) {
		return 0;
	}

	Array welds;
	for (Node *node : descendants) {
		Weld *weld = Object::cast_to<Weld>(node);
		if (weld == nullptr || !weld->is_enabled()) {
			continue;
		}
		const NodePath path0 = weld->get_node_a();
		const NodePath path1 = weld->get_node_b();
		if (path0.is_empty() || path1.is_empty()) {
			continue;
		}
		Node *part0 = weld->get_node_or_null(path0);
		Node *part1 = weld->get_node_or_null(path1);
		if (part0 == nullptr || part1 == nullptr
				|| candidate_ids.count(part0->get_instance_id()) == 0
				|| candidate_ids.count(part1->get_instance_id()) == 0) {
			continue;
		}
		Dictionary descriptor;
		descriptor["part0"] = part0;
		descriptor["part1"] = part1;
		descriptor["c0"] = weld->get_c0();
		descriptor["c1"] = weld->get_c1();
		welds.push_back(descriptor);
	}
	return configure_graph(p_handle, candidates, welds);
}

Array VextoriaRoblox2016ToolAssemblyDriver::get_members() const {
	Array result;
	for (uint64_t member_id : members) {
		Object *object = ObjectDB::get_instance(ObjectID(member_id));
		result.push_back(Object::cast_to<Node3D>(object));
	}
	return result;
}

Transform3D VextoriaRoblox2016ToolAssemblyDriver::get_member_frame(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, static_cast<int>(handle_to_member.size()), Transform3D());
	return handle_to_member[p_index];
}

int VextoriaRoblox2016ToolAssemblyDriver::apply_target(const Transform3D &p_target_handle_world) {
	last_write_count = 0;
	const Transform3D target = VextoriaRoblox2016TransformMath::rigid_frame(p_target_handle_world);
	const int count = MIN(members.size(), handle_to_member.size());
	int applied = 0;
	for (int i = 0; i < count; ++i) {
		Object *object = ObjectDB::get_instance(ObjectID(members[i]));
		Node3D *node = Object::cast_to<Node3D>(object);
		if (node == nullptr || !node->is_inside_tree()) {
			continue;
		}
		const Transform3D desired = VextoriaRoblox2016TransformMath::rigid_frame(target * handle_to_member[i]);
		if (!node->get_global_transform().is_equal_approx(desired)) {
			node->set_global_transform(desired);
			last_write_count++;
		}
		++applied;
	}
	return applied;
}

void VextoriaRoblox2016ToolAssemblyDriver::clear() {
	members.clear();
	handle_to_member.clear();
	last_write_count = 0;
}

int VextoriaRoblox2016ToolAssemblyDriver::get_member_count() const {
	return MIN(members.size(), handle_to_member.size());
}

int VextoriaRoblox2016ToolAssemblyDriver::get_last_write_count() const {
	return last_write_count;
}

void VextoriaRoblox2016ToolAssemblyDriver::_bind_methods() {
	ClassDB::bind_method(D_METHOD("configure_graph", "handle", "candidates", "welds"), &VextoriaRoblox2016ToolAssemblyDriver::configure_graph);
	ClassDB::bind_method(D_METHOD("configure_tool_graph", "handle", "tool_root"), &VextoriaRoblox2016ToolAssemblyDriver::configure_tool_graph);
	ClassDB::bind_method(D_METHOD("get_members"), &VextoriaRoblox2016ToolAssemblyDriver::get_members);
	ClassDB::bind_method(D_METHOD("get_member_frame", "index"), &VextoriaRoblox2016ToolAssemblyDriver::get_member_frame);
	ClassDB::bind_method(D_METHOD("configure", "members", "handle_to_member"), &VextoriaRoblox2016ToolAssemblyDriver::configure);
	ClassDB::bind_method(D_METHOD("apply_target", "target_handle_world"), &VextoriaRoblox2016ToolAssemblyDriver::apply_target);
	ClassDB::bind_method(D_METHOD("clear"), &VextoriaRoblox2016ToolAssemblyDriver::clear);
	ClassDB::bind_method(D_METHOD("get_member_count"), &VextoriaRoblox2016ToolAssemblyDriver::get_member_count);
	ClassDB::bind_method(D_METHOD("get_last_write_count"), &VextoriaRoblox2016ToolAssemblyDriver::get_last_write_count);
}
