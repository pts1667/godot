/**************************************************************************/
/*  source_anim_player.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "source_anim_player.h"

#include "core/error/error_macros.h"
#include "core/math/math_funcs.h"
#include "core/math/transform_3d.h"
#include "core/object/class_db.h"
#include "scene/3d/fabr_ik_3d.h"
#include "scene/3d/skeleton_modifier_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/3d/two_bone_ik_3d.h"

#include <mdlpp/mdlpp.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int STUDIO_LOOPING = 0x0001;
constexpr int STUDIO_AUTOPLAY = 0x0008;
constexpr int STUDIO_DELTA = 0x0004;
constexpr int STUDIO_LOCAL = 0x0200;
constexpr int STUDIO_POST = 0x0010;

constexpr int STUDIO_AL_POST = 0x0010;
constexpr int STUDIO_AL_SPLINE = 0x0040;
constexpr int STUDIO_AL_XFADE = 0x0080;
constexpr int STUDIO_AL_NOBLEND = 0x0200;
constexpr int STUDIO_AL_LOCAL = 0x1000;
constexpr int STUDIO_AL_POSE = 0x4000;

constexpr int MAX_SEQUENCE_DEPTH = 16;

Vector3 _to_vector3(const sourcepp::math::Vec3f &p_vector) {
	return Vector3(p_vector[0], p_vector[1], p_vector[2]);
}

Quaternion _to_quaternion(const sourcepp::math::Quat &p_quaternion) {
	return Quaternion(p_quaternion[0], p_quaternion[1], p_quaternion[2], p_quaternion[3]);
}

String _from_utf8(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

String _resolve_anim_block_path(const String &p_model_path, const mdlpp::MDL::MDL &p_mdl) {
	if (p_mdl.animBlocks.size() <= 1 || p_mdl.animBlockName.empty()) {
		return String();
	}

	const String anim_block_name = String::utf8(p_mdl.animBlockName.c_str()).replace("\\", "/");
	if (anim_block_name.is_empty()) {
		return String();
	}
	if (FileAccess::exists(anim_block_name)) {
		return anim_block_name;
	}

	const String model_dir = p_model_path.get_base_dir();
	const String local_candidate = model_dir.path_join(anim_block_name.get_file());
	if (FileAccess::exists(local_candidate)) {
		return local_candidate;
	}

	const String relative_candidate = model_dir.path_join(anim_block_name);
	if (FileAccess::exists(relative_candidate)) {
		return relative_candidate;
	}

	const String fallback_candidate = p_model_path.get_basename() + ".ani";
	if (FileAccess::exists(fallback_candidate)) {
		return fallback_candidate;
	}

	return String();
}

float _simple_spline(float p_value) {
	const float clamped = CLAMP(p_value, 0.0f, 1.0f);
	return clamped * clamped * (3.0f - (2.0f * clamped));
}

float _get_axis_value(const mdlpp::MDL::SequenceDesc &p_sequence_desc, const Vector2 &p_blend_values, int p_pose_parameter) {
	if (p_sequence_desc.paramIndex[0] == p_pose_parameter) {
		return p_blend_values.x;
	}
	if (p_sequence_desc.paramIndex[1] == p_pose_parameter) {
		return p_blend_values.y;
	}
	return 0.0f;
}

void _resolve_axis_weights(const mdlpp::MDL::SequenceDesc &p_sequence_desc, int p_axis, float p_value, int &r_index_a, int &r_index_b, float &r_weight) {
	const int axis_size = MAX(p_sequence_desc.groupSize[p_axis], 1);
	r_index_a = 0;
	r_index_b = 0;
	r_weight = 0.0f;
	if (axis_size <= 1) {
		return;
	}

	const std::vector<float> &pose_keys = p_sequence_desc.poseKeys[p_axis];
	if (static_cast<int>(pose_keys.size()) >= axis_size) {
		const bool ascending = pose_keys.front() <= pose_keys.back();
		if ((ascending && p_value <= pose_keys.front()) || (!ascending && p_value >= pose_keys.front())) {
			return;
		}
		if ((ascending && p_value >= pose_keys[axis_size - 1]) || (!ascending && p_value <= pose_keys[axis_size - 1])) {
			r_index_a = axis_size - 1;
			r_index_b = axis_size - 1;
			return;
		}

		for (int i = 0; i < axis_size - 1; i++) {
			const float start = pose_keys[static_cast<size_t>(i)];
			const float end = pose_keys[static_cast<size_t>(i + 1)];
			const bool in_range = ascending ? (p_value >= start && p_value <= end) : (p_value <= start && p_value >= end);
			if (!in_range) {
				continue;
			}

			r_index_a = i;
			r_index_b = i + 1;
			const float denominator = end - start;
			r_weight = Math::is_zero_approx(denominator) ? 0.0f : CLAMP((p_value - start) / denominator, 0.0f, 1.0f);
			return;
		}
	}

	const float start = p_sequence_desc.paramStart[p_axis];
	const float end = p_sequence_desc.paramEnd[p_axis];
	const float denominator = end - start;
	if (Math::is_zero_approx(denominator)) {
		return;
	}

	const float coordinate = CLAMP(((p_value - start) / denominator) * static_cast<float>(axis_size - 1), 0.0f, static_cast<float>(axis_size - 1));
	r_index_a = static_cast<int>(Math::floor(coordinate));
	r_index_b = MIN(r_index_a + 1, axis_size - 1);
	r_weight = coordinate - static_cast<float>(r_index_a);
	if (r_index_a == r_index_b) {
		r_weight = 0.0f;
	}
}

float _get_sequence_bone_weight(const mdlpp::MDL::SequenceDesc &p_sequence_desc, int p_bone) {
	if (p_bone >= 0 && p_bone < static_cast<int>(p_sequence_desc.boneWeights.size())) {
		return CLAMP(p_sequence_desc.boneWeights[static_cast<size_t>(p_bone)], 0.0f, 1.0f);
	}
	return 1.0f;
}

Quaternion _apply_delta_rotation(const Quaternion &p_base, const Quaternion &p_delta, float p_weight, bool p_post) {
	Quaternion scaled_delta = Quaternion().slerp(p_delta, CLAMP(p_weight, 0.0f, 1.0f));
	return p_post ? (p_base * scaled_delta).normalized() : (scaled_delta * p_base).normalized();
}

bool _sequence_loops(const mdlpp::MDL::SequenceDesc &p_sequence_desc) {
	return (static_cast<int>(p_sequence_desc.flags) & STUDIO_LOOPING) != 0;
}

bool _sequence_is_delta(const mdlpp::MDL::SequenceDesc &p_sequence_desc) {
	return (static_cast<int>(p_sequence_desc.flags) & STUDIO_DELTA) != 0;
}

bool _sequence_is_local(const mdlpp::MDL::SequenceDesc &p_sequence_desc) {
	return (static_cast<int>(p_sequence_desc.flags) & STUDIO_LOCAL) != 0;
}

Transform3D _make_transform(const Vector3 &p_position, const Quaternion &p_rotation) {
	return Transform3D(Basis(p_rotation), p_position);
}

}

SourceAnimPlayer::SourceAnimPlayer() = default;

SourceAnimPlayer::~SourceAnimPlayer() = default;

std::string SourceAnimPlayer::_to_utf8(const String &p_string) {
	CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data());
}

std::vector<std::byte> SourceAnimPlayer::_to_byte_vector(const Vector<uint8_t> &p_data) {
	std::vector<std::byte> out;
	out.resize(static_cast<size_t>(p_data.size()));
	if (!out.empty()) {
		std::memcpy(out.data(), p_data.ptr(), out.size());
	}
	return out;
}

String SourceAnimPlayer::_derive_companion_path(const String &p_model_path, const PackedStringArray &p_candidates) {
	const String base = p_model_path.get_basename();
	for (const String &suffix : p_candidates) {
		const String candidate = base + suffix;
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	return String();
}

Error SourceAnimPlayer::_open_bytes(const Vector<uint8_t> &p_mdl_data, const Vector<uint8_t> &p_vtx_data, const Vector<uint8_t> &p_vvd_data, const Vector<uint8_t> &p_anim_block_data) {
	std::vector<std::byte> mdl_data = _to_byte_vector(p_mdl_data);
	std::vector<std::byte> vtx_data = _to_byte_vector(p_vtx_data);
	std::vector<std::byte> vvd_data = _to_byte_vector(p_vvd_data);

	auto loaded = std::make_unique<mdlpp::StudioModel>();
	if (!loaded->open(mdl_data, vtx_data, vvd_data)) {
		return ERR_FILE_CORRUPT;
	}
	if (!p_anim_block_data.is_empty()) {
		loaded->setAnimBlockData(_to_byte_vector(p_anim_block_data));
	}

	model = std::move(loaded);
	sampled_animation_cache.clear();
	_refresh_bone_map();
	_rebuild_ik_runtime();
	return OK;
}

void SourceAnimPlayer::_clear_ik_runtime() {
	for (const IKRuntimeChain &runtime_chain : ik_runtime_chains) {
		Node *modifier = ObjectDB::get_instance<Node>(runtime_chain.modifier);
		if (modifier == nullptr) {
			continue;
		}
		if (Node *parent = modifier->get_parent()) {
			parent->remove_child(modifier);
		}
		memdelete(modifier);
	}
	ik_runtime_chains.clear();
}

void SourceAnimPlayer::_rebuild_ik_runtime() {
	_clear_ik_runtime();
	Skeleton3D *skeleton = _get_skeleton();
	if (model == nullptr || skeleton == nullptr || model_to_skeleton_bones.size() != static_cast<int>(model->mdl.bones.size())) {
		return;
	}

	for (int chain_index = 0; chain_index < static_cast<int>(model->mdl.ikChains.size()); chain_index++) {
		const mdlpp::MDL::IKChain &ik_chain = model->mdl.ikChains[static_cast<size_t>(chain_index)];
		if (ik_chain.links.size() < 3) {
			continue;
		}

		const int root_model_bone = ik_chain.links.front().bone;
		const int end_model_bone = ik_chain.links.back().bone;
		ERR_CONTINUE(root_model_bone < 0 || root_model_bone >= model_to_skeleton_bones.size());
		ERR_CONTINUE(end_model_bone < 0 || end_model_bone >= model_to_skeleton_bones.size());

		const int root_bone = model_to_skeleton_bones[root_model_bone];
		const int end_bone = model_to_skeleton_bones[end_model_bone];
		if (root_bone < 0 || end_bone < 0) {
			continue;
		}

		IKRuntimeChain runtime_chain;
		runtime_chain.chain_index = chain_index;
		runtime_chain.root_bone = root_bone;
		runtime_chain.end_bone = end_bone;

		float pole_distance = 0.0f;
		for (size_t link_index = 1; link_index < ik_chain.links.size(); link_index++) {
			const int model_bone = ik_chain.links[link_index].bone;
			if (model_bone >= 0 && model_bone < static_cast<int>(model->mdl.bones.size())) {
				pole_distance += MAX(_to_vector3(model->mdl.bones[static_cast<size_t>(model_bone)].position).length(), 0.0f);
			}
		}
		runtime_chain.pole_distance = MAX(pole_distance, 0.1f);

		Node3D *target = memnew(Node3D);
		target->set_name("target");

		Node3D *pole = nullptr;
		SkeletonModifier3D *modifier = nullptr;
		if (ik_chain.links.size() == 3) {
			const int middle_model_bone = ik_chain.links[1].bone;
			ERR_CONTINUE(middle_model_bone < 0 || middle_model_bone >= model_to_skeleton_bones.size());
			const int middle_bone = model_to_skeleton_bones[middle_model_bone];
			if (middle_bone < 0) {
				memdelete(target);
				continue;
			}

			TwoBoneIK3D *two_bone = memnew(TwoBoneIK3D);
			two_bone->set_name(vformat("_source_anim_player_ik_%d_%d", get_instance_id(), chain_index));
			two_bone->set_setting_count(1);
			pole = memnew(Node3D);
			pole->set_name("pole");
			two_bone->add_child(target, false, Node::INTERNAL_MODE_BACK);
			two_bone->add_child(pole, false, Node::INTERNAL_MODE_BACK);
			two_bone->set_root_bone(0, root_bone);
			two_bone->set_middle_bone(0, middle_bone);
			two_bone->set_end_bone(0, end_bone);
			two_bone->set_target_node(0, NodePath("target"));
			two_bone->set_pole_node(0, NodePath("pole"));
			two_bone->set_pole_direction(0, SkeletonModifier3D::SECONDARY_DIRECTION_NONE);
			runtime_chain.uses_two_bone = true;
			runtime_chain.middle_bone = middle_bone;
			modifier = two_bone;
		} else {
			FABRIK3D *fabrik = memnew(FABRIK3D);
			fabrik->set_name(vformat("_source_anim_player_ik_%d_%d", get_instance_id(), chain_index));
			fabrik->set_setting_count(1);
			fabrik->add_child(target, false, Node::INTERNAL_MODE_BACK);
			fabrik->set_root_bone(0, root_bone);
			fabrik->set_end_bone(0, end_bone);
			fabrik->set_target_node(0, NodePath("target"));
			fabrik->set_max_iterations(8);
			modifier = fabrik;
		}

		ERR_CONTINUE(modifier == nullptr);
		modifier->set_active(true);
		modifier->set_influence(1.0f);
		skeleton->add_child(modifier, false, Node::INTERNAL_MODE_BACK);

		runtime_chain.modifier = modifier->get_instance_id();
		runtime_chain.target = target->get_instance_id();
		runtime_chain.pole = pole != nullptr ? pole->get_instance_id() : ObjectID();
		ik_runtime_chains.push_back(runtime_chain);
	}
}

void SourceAnimPlayer::_update_skeleton_cache() {
	_clear_ik_runtime();
	skeleton_cache = ObjectID();
	if (!is_inside_tree()) {
		return;
	}

	Node *node = nullptr;
	if (!skeleton_path.is_empty() && has_node(skeleton_path)) {
		node = get_node(skeleton_path);
	} else {
		node = get_parent();
	}

	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(node);
	if (skeleton != nullptr) {
		skeleton_cache = skeleton->get_instance_id();
	}
	_refresh_bone_map();
	_rebuild_ik_runtime();
}

Skeleton3D *SourceAnimPlayer::_get_skeleton() const {
	if (skeleton_cache.is_valid()) {
		if (Skeleton3D *skeleton = ObjectDB::get_instance<Skeleton3D>(skeleton_cache)) {
			return skeleton;
		}
	}
	return nullptr;
}

void SourceAnimPlayer::_refresh_bone_map() {
	model_to_skeleton_bones.clear();
	const Skeleton3D *skeleton = _get_skeleton();
	if (model == nullptr || skeleton == nullptr) {
		return;
	}

	model_to_skeleton_bones.resize(static_cast<int>(model->mdl.bones.size()));
	for (int i = 0; i < model_to_skeleton_bones.size(); i++) {
		model_to_skeleton_bones.write[i] = -1;
	}

	for (int bone_index = 0; bone_index < static_cast<int>(model->mdl.bones.size()); bone_index++) {
		const String bone_name = _from_utf8(model->mdl.bones[static_cast<size_t>(bone_index)].name);
		model_to_skeleton_bones.write[bone_index] = skeleton->find_bone(bone_name);
	}
	update_configuration_warnings();
}

void SourceAnimPlayer::_reset_mapped_bone_poses() {
	Skeleton3D *skeleton = _get_skeleton();
	if (skeleton == nullptr) {
		return;
	}

	for (int i = 0; i < model_to_skeleton_bones.size(); i++) {
		const int skeleton_bone = model_to_skeleton_bones[i];
		if (skeleton_bone >= 0) {
			skeleton->reset_bone_pose(skeleton_bone);
		}
	}
	skeleton->force_update_deferred();
}

void SourceAnimPlayer::_set_processing_enabled(bool p_enabled) {
	set_process(p_enabled);
	set_physics_process(false);
}

void SourceAnimPlayer::_initialize_pose_buffer(PoseBuffer &r_pose, bool p_delta) const {
	ERR_FAIL_COND(model == nullptr);
	r_pose.positions.resize(static_cast<int>(model->mdl.bones.size()));
	r_pose.rotations.resize(static_cast<int>(model->mdl.bones.size()));

	for (int bone_index = 0; bone_index < static_cast<int>(model->mdl.bones.size()); bone_index++) {
		if (p_delta) {
			r_pose.positions.write[bone_index] = Vector3();
			r_pose.rotations.write[bone_index] = Quaternion();
		} else {
			const mdlpp::MDL::Bone &bone = model->mdl.bones[static_cast<size_t>(bone_index)];
			r_pose.positions.write[bone_index] = _to_vector3(bone.position);
			r_pose.rotations.write[bone_index] = _to_quaternion(bone.rotationQuat);
		}
	}
}

bool SourceAnimPlayer::_ensure_sampled_animation(int p_animation_descriptor) const {
	if (model == nullptr || p_animation_descriptor < 0) {
		return false;
	}

	if (sampled_animation_cache.contains(p_animation_descriptor)) {
		return sampled_animation_cache.at(p_animation_descriptor) != nullptr;
	}

	auto sampled = std::make_unique<mdlpp::SampledAnimation>();
	if (!model->sampleAnimation(p_animation_descriptor, *sampled)) {
		sampled_animation_cache.emplace(p_animation_descriptor, nullptr);
		return false;
	}

	sampled_animation_cache.emplace(p_animation_descriptor, std::move(sampled));
	return true;
}

const mdlpp::SampledAnimation *SourceAnimPlayer::_get_sampled_animation(int p_animation_descriptor) const {
	if (!_ensure_sampled_animation(p_animation_descriptor)) {
		return nullptr;
	}
	const auto it = sampled_animation_cache.find(p_animation_descriptor);
	if (it == sampled_animation_cache.end()) {
		return nullptr;
	}
	return it->second.get();
}

double SourceAnimPlayer::_get_sequence_cycles_per_second(int p_sequence_descriptor) const {
	if (model == nullptr || p_sequence_descriptor < 0 || p_sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size())) {
		return 0.0;
	}

	const mdlpp::MDL::SequenceDesc &sequence_desc = model->mdl.sequenceDescs[static_cast<size_t>(p_sequence_descriptor)];
	const int group_x = MAX(sequence_desc.groupSize[0], 1);
	const int group_y = MAX(sequence_desc.groupSize[1], 1);

	int x0 = 0;
	int x1 = 0;
	float x_weight = 0.0f;
	_resolve_axis_weights(sequence_desc, 0, blend_values.x, x0, x1, x_weight);

	int y0 = 0;
	int y1 = 0;
	float y_weight = 0.0f;
	_resolve_axis_weights(sequence_desc, 1, blend_values.y, y0, y1, y_weight);

	struct BlendCell {
		int index;
		float weight;
	};
	const BlendCell blend_cells[4] = {
		{ y0 * group_x + x0, (1.0f - x_weight) * (1.0f - y_weight) },
		{ y0 * group_x + x1, x_weight * (1.0f - y_weight) },
		{ y1 * group_x + x0, (1.0f - x_weight) * y_weight },
		{ y1 * group_x + x1, x_weight * y_weight },
	};

	double cycles_per_second = 0.0;
	for (const BlendCell &cell : blend_cells) {
		if (cell.weight <= 0.0f || cell.index < 0 || cell.index >= static_cast<int>(sequence_desc.animationIndices.size())) {
			continue;
		}

		const mdlpp::SampledAnimation *sampled = _get_sampled_animation(sequence_desc.animationIndices[static_cast<size_t>(cell.index)]);
		if (sampled == nullptr || sampled->fps <= 0.0f || sampled->frameCount <= 1) {
			continue;
		}

		cycles_per_second += (static_cast<double>(sampled->fps) / static_cast<double>(sampled->frameCount - 1)) * static_cast<double>(cell.weight);
	}

	return cycles_per_second;
}

double SourceAnimPlayer::_get_sequence_length(int p_sequence_descriptor) const {
	const double cycles_per_second = _get_sequence_cycles_per_second(p_sequence_descriptor);
	if (cycles_per_second <= 0.0) {
		return 0.0;
	}
	return 1.0 / cycles_per_second;
}

float SourceAnimPlayer::_get_normalized_cycle(int p_sequence_descriptor, double p_time) const {
	const double length = _get_sequence_length(p_sequence_descriptor);
	if (length <= 0.0) {
		return 0.0f;
	}

	const mdlpp::MDL::SequenceDesc &sequence_desc = model->mdl.sequenceDescs[static_cast<size_t>(p_sequence_descriptor)];
	if (_sequence_loops(sequence_desc)) {
		return static_cast<float>(Math::fposmod(p_time, length) / length);
	}
	return static_cast<float>(CLAMP(p_time / length, 0.0, 1.0));
}

void SourceAnimPlayer::_sample_animation_track(const mdlpp::SampledAnimation &p_animation, int p_bone, float p_cycle, bool p_looping, Vector3 &r_position, Quaternion &r_rotation) const {
	if (p_bone < 0 || p_bone >= static_cast<int>(p_animation.tracks.size()) || p_animation.frameCount <= 0) {
		r_position = Vector3();
		r_rotation = Quaternion();
		return;
	}

	const mdlpp::SampledAnimationTrack &track = p_animation.tracks[static_cast<size_t>(p_bone)];
	const float frame = CLAMP(p_cycle, 0.0f, 1.0f) * static_cast<float>(MAX(p_animation.frameCount - 1, 0));
	const int frame_a = CLAMP(static_cast<int>(Math::floor(frame)), 0, p_animation.frameCount - 1);
	int frame_b = frame_a + 1;
	if (frame_b >= p_animation.frameCount) {
		frame_b = p_looping ? 0 : p_animation.frameCount - 1;
	}
	const float weight = frame - static_cast<float>(frame_a);

	const Vector3 pos_a = _to_vector3(track.positions[static_cast<size_t>(frame_a)]);
	const Vector3 pos_b = _to_vector3(track.positions[static_cast<size_t>(frame_b)]);
	const Quaternion rot_a = _to_quaternion(track.rotations[static_cast<size_t>(frame_a)]);
	const Quaternion rot_b = _to_quaternion(track.rotations[static_cast<size_t>(frame_b)]);

	r_position = pos_a.lerp(pos_b, weight);
	r_rotation = rot_a.slerp(rot_b, weight).normalized();
}

void SourceAnimPlayer::_capture_ik_locks(const PoseBuffer &p_pose, const std::vector<mdlpp::MDL::IKLock> &p_ik_locks, int p_depth, Vector<PendingIKLock> &r_pending_locks) const {
	if (model == nullptr || p_ik_locks.empty() || model->mdl.ikChains.empty()) {
		return;
	}

	Vector<Transform3D> global_transforms;
	global_transforms.resize(static_cast<int>(model->mdl.bones.size()));
	Vector<uint8_t> computed;
	computed.resize(global_transforms.size());
	for (int i = 0; i < computed.size(); i++) {
		computed.write[i] = 0;
	}

	auto compute_global_transform = [&](auto &&self, int p_bone) -> Transform3D {
		if (p_bone < 0 || p_bone >= global_transforms.size()) {
			return Transform3D();
		}
		if (computed[p_bone] != 0) {
			return global_transforms[p_bone];
		}

		Transform3D transform = _make_transform(p_pose.positions[p_bone], p_pose.rotations[p_bone]);
		const int parent = model->mdl.bones[static_cast<size_t>(p_bone)].parent;
		if (parent >= 0) {
			transform = self(self, parent) * transform;
		}
		global_transforms.write[p_bone] = transform;
		computed.write[p_bone] = 1;
		return transform;
	};

	for (const mdlpp::MDL::IKLock &ik_lock : p_ik_locks) {
		if (ik_lock.chain < 0 || ik_lock.chain >= static_cast<int>(model->mdl.ikChains.size())) {
			continue;
		}

		const mdlpp::MDL::IKChain &ik_chain = model->mdl.ikChains[static_cast<size_t>(ik_lock.chain)];
		if (ik_chain.links.empty()) {
			continue;
		}

		const int end_bone = ik_chain.links.back().bone;
		if (end_bone < 0 || end_bone >= static_cast<int>(model->mdl.bones.size())) {
			continue;
		}

		const Transform3D end_transform = compute_global_transform(compute_global_transform, end_bone);
		PendingIKLock pending_lock;
		pending_lock.chain_index = ik_lock.chain;
		pending_lock.depth = p_depth;
		pending_lock.position_weight = CLAMP(ik_lock.positionWeight, 0.0f, 1.0f);
		pending_lock.local_rotation_weight = CLAMP(ik_lock.localQuaternionWeight, 0.0f, 1.0f);
		pending_lock.target_position = end_transform.origin;
		pending_lock.target_rotation = end_transform.basis.get_rotation_quaternion();

		if (ik_chain.links.size() >= 3) {
			const mdlpp::MDL::IKLink &root_link = ik_chain.links.front();
			const int knee_bone = ik_chain.links[1].bone;
			const Vector3 knee_dir = _to_vector3(root_link.kneeDir);
			if (!knee_dir.is_zero_approx()) {
				const Transform3D root_transform = compute_global_transform(compute_global_transform, root_link.bone);
				pending_lock.knee_direction = root_transform.basis.xform(knee_dir).normalized();
			}
			if (knee_bone >= 0 && knee_bone < static_cast<int>(model->mdl.bones.size())) {
				pending_lock.knee_position = compute_global_transform(compute_global_transform, knee_bone).origin;
			}
		}

		r_pending_locks.push_back(pending_lock);
	}
}

void SourceAnimPlayer::_capture_sequence_locks(const PoseBuffer &p_pose, int p_sequence_descriptor, int p_depth, Vector<PendingIKLock> &r_pending_locks) const {
	if (model == nullptr || p_sequence_descriptor < 0 || p_sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size())) {
		return;
	}

	const mdlpp::MDL::SequenceDesc &sequence_desc = model->mdl.sequenceDescs[static_cast<size_t>(p_sequence_descriptor)];
	_capture_ik_locks(p_pose, sequence_desc.ikLocks, p_depth, r_pending_locks);
}

void SourceAnimPlayer::_apply_pending_ik_locks(const Vector<PendingIKLock> &p_pending_locks) {
	if (!ik_enabled || p_pending_locks.is_empty()) {
		return;
	}

	Skeleton3D *skeleton = _get_skeleton();
	if (skeleton == nullptr) {
		return;
	}

	skeleton->force_update_all_bone_transforms();
	for (int lock_index = p_pending_locks.size() - 1; lock_index >= 0; lock_index--) {
		const PendingIKLock &pending_lock = p_pending_locks[lock_index];
		const IKRuntimeChain *runtime_chain = nullptr;
		for (int chain_index = 0; chain_index < ik_runtime_chains.size(); chain_index++) {
			if (ik_runtime_chains[chain_index].chain_index == pending_lock.chain_index) {
				runtime_chain = &ik_runtime_chains[chain_index];
				break;
			}
		}
		if (runtime_chain == nullptr) {
			continue;
		}

		SkeletonModifier3D *modifier = ObjectDB::get_instance<SkeletonModifier3D>(runtime_chain->modifier);
		Node3D *target = ObjectDB::get_instance<Node3D>(runtime_chain->target);
		if (modifier == nullptr || target == nullptr || runtime_chain->end_bone < 0) {
			continue;
		}

		const Transform3D current_end_transform = skeleton->get_bone_global_pose(runtime_chain->end_bone);
		const Quaternion previous_local_rotation = skeleton->get_bone_pose_rotation(runtime_chain->end_bone);
		const Vector3 target_position = current_end_transform.origin.lerp(pending_lock.target_position, pending_lock.position_weight);
		target->set_global_transform(Transform3D(Basis(pending_lock.target_rotation), target_position));

		Node3D *pole = ObjectDB::get_instance<Node3D>(runtime_chain->pole);
		if (runtime_chain->uses_two_bone && pole != nullptr) {
			Vector3 pole_position = pending_lock.knee_position;
			if (!pending_lock.knee_direction.is_zero_approx()) {
				pole_position += pending_lock.knee_direction.normalized() * runtime_chain->pole_distance;
			} else if (runtime_chain->middle_bone >= 0) {
				pole_position = skeleton->get_bone_global_pose(runtime_chain->middle_bone).origin;
			}
			pole->set_global_transform(Transform3D(pole->get_global_transform().basis, pole_position));
		}

		modifier->process_modification(0.0);
		if (pending_lock.local_rotation_weight > 0.0f) {
			const Quaternion solved_rotation = skeleton->get_bone_pose_rotation(runtime_chain->end_bone);
			skeleton->set_bone_pose_rotation(runtime_chain->end_bone, solved_rotation.slerp(previous_local_rotation, pending_lock.local_rotation_weight).normalized());
		}
		skeleton->force_update_all_bone_transforms();
	}
}

void SourceAnimPlayer::_blend_pose(PoseBuffer &r_pose, const PoseBuffer &p_sample, int p_sequence_descriptor, float p_weight) const {
	if (model == nullptr || p_sequence_descriptor < 0 || p_sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size()) || p_weight <= 0.0f) {
		return;
	}

	const mdlpp::MDL::SequenceDesc &sequence_desc = model->mdl.sequenceDescs[static_cast<size_t>(p_sequence_descriptor)];
	const bool is_delta = _sequence_is_delta(sequence_desc);
	const bool post_delta = (static_cast<int>(sequence_desc.flags) & STUDIO_POST) != 0;

	for (int bone_index = 0; bone_index < r_pose.positions.size(); bone_index++) {
		const float bone_weight = CLAMP(p_weight * _get_sequence_bone_weight(sequence_desc, bone_index), 0.0f, 1.0f);
		if (bone_weight <= 0.0f) {
			continue;
		}

		if (is_delta) {
			r_pose.positions.write[bone_index] += p_sample.positions[bone_index] * bone_weight;
			r_pose.rotations.write[bone_index] = _apply_delta_rotation(r_pose.rotations[bone_index], p_sample.rotations[bone_index], bone_weight, post_delta);
		} else {
			r_pose.positions.write[bone_index] = r_pose.positions[bone_index].lerp(p_sample.positions[bone_index], bone_weight);
			r_pose.rotations.write[bone_index] = r_pose.rotations[bone_index].slerp(p_sample.rotations[bone_index], bone_weight).normalized();
		}
	}
}

bool SourceAnimPlayer::_evaluate_sequence_pose(int p_sequence_descriptor, float p_cycle, PoseBuffer &r_pose, Vector<PendingIKLock> *r_pending_locks, int p_depth) const {
	if (model == nullptr || p_sequence_descriptor < 0 || p_sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size()) || p_depth > MAX_SEQUENCE_DEPTH) {
		return false;
	}

	const mdlpp::MDL::SequenceDesc &sequence_desc = model->mdl.sequenceDescs[static_cast<size_t>(p_sequence_descriptor)];
	const bool is_delta = _sequence_is_delta(sequence_desc);
	_initialize_pose_buffer(r_pose, is_delta);

	const int group_x = MAX(sequence_desc.groupSize[0], 1);
	const int group_y = MAX(sequence_desc.groupSize[1], 1);

	int x0 = 0;
	int x1 = 0;
	float x_weight = 0.0f;
	_resolve_axis_weights(sequence_desc, 0, blend_values.x, x0, x1, x_weight);

	int y0 = 0;
	int y1 = 0;
	float y_weight = 0.0f;
	_resolve_axis_weights(sequence_desc, 1, blend_values.y, y0, y1, y_weight);

	struct BlendCell {
		int index;
		float weight;
	};
	BlendCell blend_cells[4] = {
		{ y0 * group_x + x0, (1.0f - x_weight) * (1.0f - y_weight) },
		{ y0 * group_x + x1, x_weight * (1.0f - y_weight) },
		{ y1 * group_x + x0, (1.0f - x_weight) * y_weight },
		{ y1 * group_x + x1, x_weight * y_weight },
	};

	bool contributed = false;
	for (const BlendCell &cell : blend_cells) {
		if (cell.weight <= 0.0f || cell.index < 0 || cell.index >= static_cast<int>(sequence_desc.animationIndices.size())) {
			continue;
		}

		const int animation_descriptor = sequence_desc.animationIndices[static_cast<size_t>(cell.index)];
		const mdlpp::SampledAnimation *sampled = _get_sampled_animation(animation_descriptor);
		if (sampled == nullptr) {
			continue;
		}

		PoseBuffer sample_pose;
		_initialize_pose_buffer(sample_pose, is_delta);
		for (int bone_index = 0; bone_index < static_cast<int>(sampled->tracks.size()); bone_index++) {
			Vector3 position;
			Quaternion rotation;
			_sample_animation_track(*sampled, bone_index, p_cycle, _sequence_loops(sequence_desc), position, rotation);
			sample_pose.positions.write[bone_index] = position;
			sample_pose.rotations.write[bone_index] = rotation;
		}

		if (is_delta) {
			const bool post_delta = (static_cast<int>(sequence_desc.flags) & STUDIO_POST) != 0;
			for (int bone_index = 0; bone_index < r_pose.positions.size(); bone_index++) {
				r_pose.positions.write[bone_index] += sample_pose.positions[bone_index] * cell.weight;
				r_pose.rotations.write[bone_index] = _apply_delta_rotation(r_pose.rotations[bone_index], sample_pose.rotations[bone_index], cell.weight, post_delta);
			}
		} else {
			for (int bone_index = 0; bone_index < r_pose.positions.size(); bone_index++) {
				r_pose.positions.write[bone_index] = r_pose.positions[bone_index].lerp(sample_pose.positions[bone_index], cell.weight);
				r_pose.rotations.write[bone_index] = r_pose.rotations[bone_index].slerp(sample_pose.rotations[bone_index], cell.weight).normalized();
			}
		}
		contributed = true;
	}

	if (_sequence_is_local(sequence_desc)) {
		for (const mdlpp::MDL::AutoLayer &layer : sequence_desc.autoLayers) {
			if ((layer.flags & STUDIO_AL_LOCAL) == 0) {
				continue;
			}

			float layer_cycle = p_cycle;
			float layer_weight = 1.0f;
			if (layer.start != layer.end) {
				float index = p_cycle;
				if ((layer.flags & STUDIO_AL_POSE) != 0) {
					index = _get_axis_value(sequence_desc, blend_values, layer.pose);
				}

				if (index < layer.start || index >= layer.end) {
					continue;
				}

				float s = 1.0f;
				if (index < layer.peak && layer.start != layer.peak) {
					s = (index - layer.start) / (layer.peak - layer.start);
				} else if (index > layer.tail && layer.end != layer.tail) {
					s = (layer.end - index) / (layer.end - layer.tail);
				}
				if ((layer.flags & STUDIO_AL_SPLINE) != 0) {
					s = _simple_spline(s);
				}
				if ((layer.flags & STUDIO_AL_XFADE) != 0 && index > layer.tail) {
					layer_weight = s;
				} else if ((layer.flags & STUDIO_AL_NOBLEND) != 0) {
					layer_weight = s;
				} else {
					layer_weight = s;
				}
				layer_cycle = (p_cycle - layer.start) / (layer.end - layer.start);
			}

			contributed = _accumulate_sequence(r_pose, layer.sequence, CLAMP(layer_cycle, 0.0f, 1.0f), layer_weight, r_pending_locks, p_depth + 1) || contributed;
		}
	}

	return contributed;
}

bool SourceAnimPlayer::_accumulate_sequence(PoseBuffer &r_pose, int p_sequence_descriptor, float p_cycle, float p_weight, Vector<PendingIKLock> *r_pending_locks, int p_depth) const {
	if (model == nullptr || p_sequence_descriptor < 0 || p_sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size()) || p_weight <= 0.0f || p_depth > MAX_SEQUENCE_DEPTH) {
		return false;
	}

	if (r_pending_locks != nullptr) {
		_capture_sequence_locks(r_pose, p_sequence_descriptor, p_depth, *r_pending_locks);
	}

	PoseBuffer sequence_pose;
	if (!_evaluate_sequence_pose(p_sequence_descriptor, p_cycle, sequence_pose, r_pending_locks, p_depth)) {
		return false;
	}

	_blend_pose(r_pose, sequence_pose, p_sequence_descriptor, p_weight);

	const mdlpp::MDL::SequenceDesc &sequence_desc = model->mdl.sequenceDescs[static_cast<size_t>(p_sequence_descriptor)];
	bool contributed = true;
	for (const mdlpp::MDL::AutoLayer &layer : sequence_desc.autoLayers) {
		if ((layer.flags & STUDIO_AL_LOCAL) != 0) {
			continue;
		}

		float layer_cycle = p_cycle;
		float layer_weight = p_weight;
		if (layer.start != layer.end) {
			float index = p_cycle;
			if ((layer.flags & STUDIO_AL_POSE) != 0) {
				index = _get_axis_value(sequence_desc, blend_values, layer.pose);
			}

			if (index < layer.start || index >= layer.end) {
				continue;
			}

			float s = 1.0f;
			if (index < layer.peak && layer.start != layer.peak) {
				s = (index - layer.start) / (layer.peak - layer.start);
			} else if (index > layer.tail && layer.end != layer.tail) {
				s = (layer.end - index) / (layer.end - layer.tail);
			}
			if ((layer.flags & STUDIO_AL_SPLINE) != 0) {
				s = _simple_spline(s);
			}
			if ((layer.flags & STUDIO_AL_XFADE) != 0 && index > layer.tail) {
				const float denominator = 1.0f - p_weight + s * p_weight;
				layer_weight = Math::is_zero_approx(denominator) ? 0.0f : (s * p_weight) / denominator;
			} else if ((layer.flags & STUDIO_AL_NOBLEND) != 0) {
				layer_weight = s;
			} else {
				layer_weight = p_weight * s;
			}

			if ((layer.flags & STUDIO_AL_POSE) == 0) {
				layer_cycle = (p_cycle - layer.start) / (layer.end - layer.start);
			}
		}

		contributed = _accumulate_sequence(r_pose, layer.sequence, CLAMP(layer_cycle, 0.0f, 1.0f), layer_weight, r_pending_locks, p_depth + 1) || contributed;
	}

	return contributed;
}

void SourceAnimPlayer::_accumulate_autoplay_sequences(PoseBuffer &r_pose, Vector<PendingIKLock> *r_pending_locks) const {
	if (model == nullptr) {
		return;
	}

	if (r_pending_locks != nullptr) {
		_capture_ik_locks(r_pose, model->mdl.ikAutoplayLocks, MAX_SEQUENCE_DEPTH + 1, *r_pending_locks);
	}

	for (int autoplay_sequence = 0; autoplay_sequence < static_cast<int>(model->mdl.sequenceDescs.size()); autoplay_sequence++) {
		const mdlpp::MDL::SequenceDesc &sequence_desc = model->mdl.sequenceDescs[static_cast<size_t>(autoplay_sequence)];
		if ((static_cast<int>(sequence_desc.flags) & STUDIO_AUTOPLAY) == 0) {
			continue;
		}

		const double cycles_per_second = _get_sequence_cycles_per_second(autoplay_sequence);
		if (cycles_per_second <= 0.0) {
			continue;
		}

		const float autoplay_cycle = static_cast<float>(Math::fposmod(playback_time * cycles_per_second, 1.0));
		_accumulate_sequence(r_pose, autoplay_sequence, autoplay_cycle, 1.0f, r_pending_locks);
	}
}

void SourceAnimPlayer::_emit_sequence_events(double p_previous_time, double p_current_time, bool p_looped) {
	if (model == nullptr || sequence_descriptor < 0 || sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size())) {
		return;
	}

	const double length = _get_sequence_length(sequence_descriptor);
	if (length <= 0.0) {
		return;
	}

	const mdlpp::MDL::SequenceDesc &sequence_desc_ref = model->mdl.sequenceDescs[static_cast<size_t>(sequence_descriptor)];
	auto emit_in_range = [&](double p_start_cycle, double p_end_cycle) {
		for (int event_index = 0; event_index < static_cast<int>(sequence_desc_ref.events.size()); event_index++) {
			const mdlpp::MDL::Event &event = sequence_desc_ref.events[static_cast<size_t>(event_index)];
			if (event.cycle >= p_start_cycle && event.cycle < p_end_cycle) {
				emit_signal(SNAME("sequence_event"), sequence_descriptor, event_index, _from_utf8(event.name), event.event, _from_utf8(event.options));
			}
		}
	};

	const double previous_cycle = CLAMP(p_previous_time / length, 0.0, 1.0);
	const double current_cycle = CLAMP(p_current_time / length, 0.0, 1.0);
	if (p_looped) {
		emit_in_range(previous_cycle, 1.0);
		emit_in_range(0.0, current_cycle);
	} else if (current_cycle >= previous_cycle) {
		emit_in_range(previous_cycle, current_cycle);
	}
}

void SourceAnimPlayer::_apply_pose() {
	if (model == nullptr || sequence_descriptor < 0 || sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size())) {
		return;
	}

	Skeleton3D *skeleton = _get_skeleton();
	if (skeleton == nullptr) {
		return;
	}

	if (model_to_skeleton_bones.size() != static_cast<int>(model->mdl.bones.size())) {
		_refresh_bone_map();
	}

	PoseBuffer final_pose;
	Vector<PendingIKLock> pending_locks;
	_initialize_pose_buffer(final_pose, false);
	const float cycle = _get_normalized_cycle(sequence_descriptor, playback_time);
	if (!_accumulate_sequence(final_pose, sequence_descriptor, cycle, 1.0f, &pending_locks)) {
		return;
	}
	_accumulate_autoplay_sequences(final_pose, &pending_locks);

	for (int bone_index = 0; bone_index < model_to_skeleton_bones.size(); bone_index++) {
		const int skeleton_bone = model_to_skeleton_bones[bone_index];
		if (skeleton_bone < 0) {
			continue;
		}

		skeleton->set_bone_pose_position(skeleton_bone, final_pose.positions[bone_index]);
		skeleton->set_bone_pose_rotation(skeleton_bone, final_pose.rotations[bone_index]);
	}
	skeleton->force_update_all_bone_transforms();
	_apply_pending_ik_locks(pending_locks);
	skeleton->force_update_deferred();
}

void SourceAnimPlayer::set_skeleton_path(const NodePath &p_path) {
	if (skeleton_path == p_path) {
		return;
	}
	skeleton_path = p_path;
	_update_skeleton_cache();
	if (model != nullptr && sequence_descriptor >= 0) {
		_apply_pose();
	}
}

NodePath SourceAnimPlayer::get_skeleton_path() const {
	return skeleton_path;
}

void SourceAnimPlayer::set_sequence_descriptor(int p_sequence_descriptor) {
	if (model == nullptr) {
		sequence_descriptor = p_sequence_descriptor;
		return;
	}
	ERR_FAIL_COND_MSG(p_sequence_descriptor < -1 || p_sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size()), "Requested sequence descriptor is out of range.");
	sequence_descriptor = p_sequence_descriptor;
	playback_time = 0.0;
	if (model != nullptr && sequence_descriptor >= 0) {
		_apply_pose();
	}
}

int SourceAnimPlayer::get_sequence_descriptor() const {
	return sequence_descriptor;
}

void SourceAnimPlayer::set_blend_values(const Vector2 &p_blend_values) {
	blend_values = p_blend_values;
	if (model != nullptr && sequence_descriptor >= 0) {
		_apply_pose();
	}
}

Vector2 SourceAnimPlayer::get_blend_values() const {
	return blend_values;
}

void SourceAnimPlayer::set_ik_enabled(bool p_enabled) {
	ik_enabled = p_enabled;
	if (model != nullptr && sequence_descriptor >= 0) {
		_apply_pose();
	}
}

bool SourceAnimPlayer::is_ik_enabled() const {
	return ik_enabled;
}

void SourceAnimPlayer::set_speed_scale(float p_speed_scale) {
	speed_scale = MAX(p_speed_scale, 0.0f);
}

float SourceAnimPlayer::get_speed_scale() const {
	return speed_scale;
}

void SourceAnimPlayer::set_current_time(double p_time) {
	seek(p_time, true);
}

double SourceAnimPlayer::get_current_time() const {
	return playback_time;
}

PackedStringArray SourceAnimPlayer::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (_get_skeleton() == nullptr) {
		warnings.push_back(RTR("SourceAnimPlayer needs a Skeleton3D target. Set skeleton_path or parent it to a Skeleton3D node."));
	}
	if (model == nullptr) {
		warnings.push_back(RTR("SourceAnimPlayer does not have an MDL model loaded."));
	} else if (sequence_descriptor < 0 || sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size())) {
		warnings.push_back(RTR("SourceAnimPlayer does not have a valid sequence selected."));
	} else if (ik_enabled && !model->mdl.ikChains.empty() && ik_runtime_chains.is_empty()) {
		warnings.push_back(RTR("SourceAnimPlayer found MDL IK chains but could not bind them onto the target Skeleton3D."));
	} else if (model_to_skeleton_bones.is_empty()) {
		warnings.push_back(RTR("SourceAnimPlayer could not map any MDL bones onto the target Skeleton3D."));
	}
	return warnings;
}

Error SourceAnimPlayer::open(const String &p_mdl_path, const String &p_vtx_path, const String &p_vvd_path) {
	close();
	ERR_FAIL_COND_V_MSG(p_mdl_path.is_empty(), ERR_INVALID_PARAMETER, "MDL path must not be empty.");

	const String resolved_vtx_path = p_vtx_path.is_empty() ? _derive_companion_path(p_mdl_path, PackedStringArray{ ".dx90.vtx", ".vtx", ".dx80.vtx", ".sw.vtx", ".dx11.vtx" }) : p_vtx_path;
	const String resolved_vvd_path = p_vvd_path.is_empty() ? _derive_companion_path(p_mdl_path, PackedStringArray{ ".vvd" }) : p_vvd_path;
	ERR_FAIL_COND_V_MSG(resolved_vtx_path.is_empty(), ERR_FILE_NOT_FOUND, "Could not resolve a companion VTX file for the requested MDL.");
	ERR_FAIL_COND_V_MSG(resolved_vvd_path.is_empty(), ERR_FILE_NOT_FOUND, "Could not resolve a companion VVD file for the requested MDL.");

	Error mdl_error = OK;
	Error vtx_error = OK;
	Error vvd_error = OK;
	const Vector<uint8_t> mdl_data = FileAccess::get_file_as_bytes(p_mdl_path, &mdl_error);
	const Vector<uint8_t> vtx_data = FileAccess::get_file_as_bytes(resolved_vtx_path, &vtx_error);
	const Vector<uint8_t> vvd_data = FileAccess::get_file_as_bytes(resolved_vvd_path, &vvd_error);
	ERR_FAIL_COND_V_MSG(mdl_error != OK, mdl_error, "Failed to load the MDL file.");
	ERR_FAIL_COND_V_MSG(vtx_error != OK, vtx_error, "Failed to load the VTX companion file.");
	ERR_FAIL_COND_V_MSG(vvd_error != OK, vvd_error, "Failed to load the VVD companion file.");

	const Error open_error = _open_bytes(mdl_data, vtx_data, vvd_data);
	if (open_error != OK) {
		close();
		return open_error;
	}

	const String anim_block_path = _resolve_anim_block_path(p_mdl_path, model->mdl);
	if (!anim_block_path.is_empty()) {
		Error anim_block_error = OK;
		const Vector<uint8_t> anim_block_data = FileAccess::get_file_as_bytes(anim_block_path, &anim_block_error);
		if (anim_block_error != OK) {
			close();
			return anim_block_error;
		}
		model->setAnimBlockData(_to_byte_vector(anim_block_data));
		sampled_animation_cache.clear();
	}

	mdl_path = p_mdl_path;
	vtx_path = resolved_vtx_path;
	vvd_path = resolved_vvd_path;
	if (!model->mdl.sequenceDescs.empty()) {
		sequence_descriptor = 0;
	}
	_update_skeleton_cache();
	_apply_pose();
	return OK;
}

Error SourceAnimPlayer::open_from_buffer(const PackedByteArray &p_mdl_data, const PackedByteArray &p_vtx_data, const PackedByteArray &p_vvd_data, const PackedByteArray &p_anim_block_data) {
	close();
	ERR_FAIL_COND_V_MSG(p_mdl_data.is_empty(), ERR_INVALID_PARAMETER, "MDL data must not be empty.");
	ERR_FAIL_COND_V_MSG(p_vtx_data.is_empty(), ERR_INVALID_PARAMETER, "VTX data must not be empty.");
	ERR_FAIL_COND_V_MSG(p_vvd_data.is_empty(), ERR_INVALID_PARAMETER, "VVD data must not be empty.");

	const Error open_error = _open_bytes(p_mdl_data, p_vtx_data, p_vvd_data, p_anim_block_data);
	if (open_error != OK) {
		close();
		return open_error;
	}

	if (!model->mdl.sequenceDescs.empty()) {
		sequence_descriptor = 0;
	}
	_update_skeleton_cache();
	_apply_pose();
	return OK;
}

void SourceAnimPlayer::close() {
	stop(false);
	_clear_ik_runtime();
	model.reset();
	sampled_animation_cache.clear();
	mdl_path = String();
	vtx_path = String();
	vvd_path = String();
	sequence_descriptor = -1;
	playback_time = 0.0;
	model_to_skeleton_bones.clear();
	update_configuration_warnings();
}

bool SourceAnimPlayer::is_open() const {
	return model != nullptr;
}

String SourceAnimPlayer::get_mdl_path() const {
	return mdl_path;
}

String SourceAnimPlayer::get_vtx_path() const {
	return vtx_path;
}

String SourceAnimPlayer::get_vvd_path() const {
	return vvd_path;
}

int SourceAnimPlayer::get_sequence_count() const {
	return model == nullptr ? 0 : static_cast<int>(model->mdl.sequenceDescs.size());
}

PackedStringArray SourceAnimPlayer::get_sequence_names() const {
	PackedStringArray names;
	if (model == nullptr) {
		return names;
	}
	for (size_t i = 0; i < model->mdl.sequenceDescs.size(); i++) {
		const String label = _from_utf8(model->mdl.sequenceDescs[i].label);
		names.push_back(label.is_empty() ? vformat("sequence_%d", static_cast<int>(i)) : label);
	}
	return names;
}

bool SourceAnimPlayer::has_sequence(const StringName &p_name) const {
	return find_sequence(p_name) != -1;
}

int SourceAnimPlayer::find_sequence(const StringName &p_name) const {
	if (model == nullptr) {
		return -1;
	}
	for (int i = 0; i < static_cast<int>(model->mdl.sequenceDescs.size()); i++) {
		if (StringName(_from_utf8(model->mdl.sequenceDescs[static_cast<size_t>(i)].label)) == p_name) {
			return i;
		}
	}
	return -1;
}

Error SourceAnimPlayer::play(double p_from_time) {
	ERR_FAIL_COND_V_MSG(model == nullptr, ERR_INVALID_PARAMETER, "SourceAnimPlayer must be opened before playback.");
	if (sequence_descriptor < 0 && !model->mdl.sequenceDescs.empty()) {
		sequence_descriptor = 0;
	}
	ERR_FAIL_COND_V_MSG(sequence_descriptor < 0 || sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size()), ERR_INVALID_PARAMETER, "SourceAnimPlayer does not have a valid sequence selected.");
	playing = true;
	_set_processing_enabled(true);
	seek(p_from_time, true);
	return OK;
}

Error SourceAnimPlayer::play_sequence(int p_sequence_descriptor, double p_from_time) {
	set_sequence_descriptor(p_sequence_descriptor);
	return play(p_from_time);
}

Error SourceAnimPlayer::play_sequence_by_name(const StringName &p_name, double p_from_time) {
	const int index = find_sequence(p_name);
	ERR_FAIL_COND_V_MSG(index == -1, ERR_DOES_NOT_EXIST, "Requested sequence name was not found.");
	return play_sequence(index, p_from_time);
}

void SourceAnimPlayer::stop(bool p_reset) {
	playing = false;
	_set_processing_enabled(false);
	playback_time = 0.0;
	if (p_reset) {
		_reset_mapped_bone_poses();
	} else if (model != nullptr && sequence_descriptor >= 0) {
		_apply_pose();
	}
}

void SourceAnimPlayer::seek(double p_time, bool p_update) {
	playback_time = MAX(p_time, 0.0);
	if (p_update) {
		_apply_pose();
	}
}

void SourceAnimPlayer::advance(double p_delta) {
	if (model == nullptr || sequence_descriptor < 0 || sequence_descriptor >= static_cast<int>(model->mdl.sequenceDescs.size()) || p_delta <= 0.0 || speed_scale <= 0.0f) {
		return;
	}

	const double length = _get_sequence_length(sequence_descriptor);
	if (length <= 0.0) {
		_apply_pose();
		return;
	}

	const mdlpp::MDL::SequenceDesc &sequence_desc_ref = model->mdl.sequenceDescs[static_cast<size_t>(sequence_descriptor)];
	const double previous_time = playback_time;
	const double delta = p_delta * static_cast<double>(speed_scale);
	bool looped = false;

	if (_sequence_loops(sequence_desc_ref)) {
		playback_time = Math::fposmod(playback_time + delta, length);
		looped = previous_time + delta >= length;
	} else {
		playback_time = MIN(playback_time + delta, length);
	}

	_emit_sequence_events(previous_time, playback_time, looped);
	_apply_pose();

	if (!_sequence_loops(sequence_desc_ref) && Math::is_equal_approx(playback_time, length)) {
		playing = false;
		_set_processing_enabled(false);
		emit_signal(SNAME("sequence_finished"), sequence_descriptor);
	}
}

bool SourceAnimPlayer::is_playing() const {
	return playing;
}

void SourceAnimPlayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_update_skeleton_cache();
			_set_processing_enabled(playing);
			if (model != nullptr && sequence_descriptor >= 0) {
				_apply_pose();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_clear_ik_runtime();
			skeleton_cache = ObjectID();
			model_to_skeleton_bones.clear();
		} break;
		case NOTIFICATION_PROCESS: {
			advance(get_process_delta_time());
		} break;
	}
}

void SourceAnimPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "mdl_path", "vtx_path", "vvd_path"), &SourceAnimPlayer::open, DEFVAL(String()), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("open_from_buffer", "mdl_data", "vtx_data", "vvd_data", "anim_block_data"), &SourceAnimPlayer::open_from_buffer, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("close"), &SourceAnimPlayer::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourceAnimPlayer::is_open);
	ClassDB::bind_method(D_METHOD("get_mdl_path"), &SourceAnimPlayer::get_mdl_path);
	ClassDB::bind_method(D_METHOD("get_vtx_path"), &SourceAnimPlayer::get_vtx_path);
	ClassDB::bind_method(D_METHOD("get_vvd_path"), &SourceAnimPlayer::get_vvd_path);
	ClassDB::bind_method(D_METHOD("set_skeleton_path", "skeleton_path"), &SourceAnimPlayer::set_skeleton_path);
	ClassDB::bind_method(D_METHOD("get_skeleton_path"), &SourceAnimPlayer::get_skeleton_path);
	ClassDB::bind_method(D_METHOD("set_sequence_descriptor", "sequence_descriptor"), &SourceAnimPlayer::set_sequence_descriptor);
	ClassDB::bind_method(D_METHOD("get_sequence_descriptor"), &SourceAnimPlayer::get_sequence_descriptor);
	ClassDB::bind_method(D_METHOD("set_blend_values", "blend_values"), &SourceAnimPlayer::set_blend_values);
	ClassDB::bind_method(D_METHOD("get_blend_values"), &SourceAnimPlayer::get_blend_values);
	ClassDB::bind_method(D_METHOD("set_ik_enabled", "enabled"), &SourceAnimPlayer::set_ik_enabled);
	ClassDB::bind_method(D_METHOD("is_ik_enabled"), &SourceAnimPlayer::is_ik_enabled);
	ClassDB::bind_method(D_METHOD("set_speed_scale", "speed_scale"), &SourceAnimPlayer::set_speed_scale);
	ClassDB::bind_method(D_METHOD("get_speed_scale"), &SourceAnimPlayer::get_speed_scale);
	ClassDB::bind_method(D_METHOD("set_current_time", "current_time"), &SourceAnimPlayer::set_current_time);
	ClassDB::bind_method(D_METHOD("get_current_time"), &SourceAnimPlayer::get_current_time);
	ClassDB::bind_method(D_METHOD("get_sequence_count"), &SourceAnimPlayer::get_sequence_count);
	ClassDB::bind_method(D_METHOD("get_sequence_names"), &SourceAnimPlayer::get_sequence_names);
	ClassDB::bind_method(D_METHOD("has_sequence", "name"), &SourceAnimPlayer::has_sequence);
	ClassDB::bind_method(D_METHOD("find_sequence", "name"), &SourceAnimPlayer::find_sequence);
	ClassDB::bind_method(D_METHOD("play", "from_time"), &SourceAnimPlayer::play, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("play_sequence", "sequence_descriptor", "from_time"), &SourceAnimPlayer::play_sequence, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("play_sequence_by_name", "name", "from_time"), &SourceAnimPlayer::play_sequence_by_name, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop", "reset"), &SourceAnimPlayer::stop, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("seek", "time", "update"), &SourceAnimPlayer::seek, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("advance", "delta"), &SourceAnimPlayer::advance);
	ClassDB::bind_method(D_METHOD("is_playing"), &SourceAnimPlayer::is_playing);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "skeleton_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Skeleton3D"), "set_skeleton_path", "get_skeleton_path");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sequence_descriptor"), "set_sequence_descriptor", "get_sequence_descriptor");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "blend_values"), "set_blend_values", "get_blend_values");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ik_enabled"), "set_ik_enabled", "is_ik_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed_scale", PROPERTY_HINT_RANGE, "0,32,0.01,or_greater"), "set_speed_scale", "get_speed_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "current_time", PROPERTY_HINT_RANGE, "0,3600,0.001,or_greater,suffix:s"), "set_current_time", "get_current_time");

	ADD_SIGNAL(MethodInfo("sequence_event", PropertyInfo(Variant::INT, "sequence_descriptor"), PropertyInfo(Variant::INT, "event_index"), PropertyInfo(Variant::STRING, "event_name"), PropertyInfo(Variant::INT, "event_id"), PropertyInfo(Variant::STRING, "options")));
	ADD_SIGNAL(MethodInfo("sequence_finished", PropertyInfo(Variant::INT, "sequence_descriptor")));
}