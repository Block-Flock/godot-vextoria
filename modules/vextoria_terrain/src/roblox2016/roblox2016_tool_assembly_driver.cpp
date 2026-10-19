#include "roblox2016_tool_assembly_driver.h"
#include "roblox2016_transform_math.h"

#include "scene/3d/node_3d.h"
#include "core/object/class_db.h"
#include "core/variant/dictionary.h"
#include "core/string/print_string.h"

#include <unordered_map>
#include <unordered_set>


void Roblox2016ToolAssemblyDriver::configure(
		const Array &p_members,
		const Array &p_handle_to_member) {
	clear();
	const int count = MIN(p_members.size(), p_handle_to_member.size());
	for (int i = 0; i < count; ++i) {
		if (p_members[i].get_type() != Variant::OBJECT ||
				p_handle_to_member[i].get_type() != Variant::TRANSFORM3D) {
			continue;
		}
		Object *object = p_members[i];
		Node3D *node = Object::cast_to<Node3D>(object);
		if (node == nullptr) {
			continue;
		}
		const Transform3D relative = p_handle_to_member[i];
		_members.push_back(node->get_instance_id());
		_handle_to_member.push_back(Roblox2016TransformMath::rigid_frame(relative));
	}
}

int Roblox2016ToolAssemblyDriver::configure_graph(
		Node3D *p_handle, const Array &p_candidates, const Array &p_welds) {
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
		if (endpoint0.get_type() != Variant::OBJECT || endpoint1.get_type() != Variant::OBJECT ||
				frame0.get_type() != Variant::TRANSFORM3D || frame1.get_type() != Variant::TRANSFORM3D) {
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
		// Part0 * C0 == Part1 * C1. Keep both traversal directions authored;
		// candidate poses and hierarchy transforms are not weld-frame inputs.
		const Transform3D c0 = Roblox2016TransformMath::rigid_frame(frame0);
		const Transform3D c1 = Roblox2016TransformMath::rigid_frame(frame1);
		adjacency[id0].push_back({ id1, Roblox2016TransformMath::rigid_frame(c0 * c1.affine_inverse()) });
		adjacency[id1].push_back({ id0, Roblox2016TransformMath::rigid_frame(c1 * c0.affine_inverse()) });
	}

	std::unordered_set<uint64_t> visited;
	visited.insert(handle_id);
	_members.push_back(handle_id);
	_handle_to_member.push_back(Transform3D());
	for (size_t current = 0; current < _members.size(); ++current) {
		const auto found = adjacency.find(_members[current]);
		if (found == adjacency.end()) {
			continue;
		}
		for (const Edge &edge : found->second) {
			if (!visited.insert(edge.other).second) {
				continue;
			}
			const Transform3D relative = Roblox2016TransformMath::rigid_frame(_handle_to_member[current] * edge.relative);
			_members.push_back(edge.other);
			_handle_to_member.push_back(relative);
		}
	}
	return get_member_count();
}

Array Roblox2016ToolAssemblyDriver::get_members() const {
	Array result;
	for (uint64_t member_id : _members) {
		Object *object = ObjectDB::get_instance(ObjectID(member_id));
		result.push_back(Object::cast_to<Node3D>(object));
	}
	return result;
}

Transform3D Roblox2016ToolAssemblyDriver::get_member_frame(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, static_cast<int>(_handle_to_member.size()), Transform3D());
	return _handle_to_member[p_index];
}

int Roblox2016ToolAssemblyDriver::apply_target(
		const Transform3D &p_target_handle_world) {
	_last_write_count = 0;
	const Transform3D target =
		Roblox2016TransformMath::rigid_frame(p_target_handle_world);
	const int count = MIN(_members.size(), _handle_to_member.size());
	int applied = 0;

	for (int i = 0; i < count; ++i) {
		Object *object = ObjectDB::get_instance(ObjectID(_members[i]));
		Node3D *node = Object::cast_to<Node3D>(object);
		if (node == nullptr || !node->is_inside_tree()) {
			continue;
		}
		const Transform3D relative = _handle_to_member[i];
		const Transform3D desired =
			Roblox2016TransformMath::rigid_frame(target * relative);
		// Node3D::set_global_transform always walks descendants, dirties global
		// transform/interpolation caches, and schedules transform notifications
		// even when the frame is unchanged. Held R6 tools call this at physics
		// cadence, so skip that work for stationary members while still repairing
		// any member that another system moved independently.
		if (!node->get_global_transform().is_equal_approx(desired)) {
			node->set_global_transform(desired);
			_last_write_count++;
		}
		applied++;
	}

	return applied;
}

void Roblox2016ToolAssemblyDriver::clear() {
	_members.clear();
	_handle_to_member.clear();
	_last_write_count = 0;
}

int Roblox2016ToolAssemblyDriver::get_member_count() const {
	return MIN(_members.size(), _handle_to_member.size());
}

int Roblox2016ToolAssemblyDriver::get_last_write_count() const {
	return _last_write_count;
}

void Roblox2016ToolAssemblyDriver::_bind_methods() {
	ClassDB::bind_method(
		D_METHOD("configure_graph", "handle", "candidates", "welds"),
		&Roblox2016ToolAssemblyDriver::configure_graph);
	ClassDB::bind_method(D_METHOD("get_members"), &Roblox2016ToolAssemblyDriver::get_members);
	ClassDB::bind_method(D_METHOD("get_member_frame", "index"), &Roblox2016ToolAssemblyDriver::get_member_frame);
	ClassDB::bind_method(
		D_METHOD("configure", "members", "handle_to_member"),
		&Roblox2016ToolAssemblyDriver::configure);
	ClassDB::bind_method(
		D_METHOD("apply_target", "target_handle_world"),
		&Roblox2016ToolAssemblyDriver::apply_target);
	ClassDB::bind_method(
		D_METHOD("clear"),
		&Roblox2016ToolAssemblyDriver::clear);
	ClassDB::bind_method(
		D_METHOD("get_member_count"),
		&Roblox2016ToolAssemblyDriver::get_member_count);
	ClassDB::bind_method(
		D_METHOD("get_last_write_count"),
		&Roblox2016ToolAssemblyDriver::get_last_write_count);
}

