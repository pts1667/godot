/**************************************************************************/
/*  source_mdl_animation_data.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/io/resource.h"

#include <array>
#include <vector>

namespace mdlpp {
struct SampledAnimation;
struct StudioModel;
} // namespace mdlpp

class SourceMDLAnimationData : public Resource {
	GDCLASS(SourceMDLAnimationData, Resource);

public:
	struct BoneController {
		int bone = -1;
		int type = 0;
		float start = 0.0f;
		float end = 0.0f;
		int rest = 0;
		int inputField = -1;
	};

	struct Event {
		float cycle = 0.0f;
		int event = 0;
		int type = 0;
		String options;
		String name;
	};

	struct AutoLayer {
		int sequence = -1;
		int pose = 0;
		int flags = 0;
		float start = 0.0f;
		float peak = 0.0f;
		float tail = 0.0f;
		float end = 0.0f;
	};

	struct IKLink {
		int bone = -1;
		Vector3 kneeDir;
	};

	struct IKChain {
		String name;
		int linkType = 0;
		std::vector<IKLink> links;
	};

	struct IKLock {
		int chain = -1;
		float positionWeight = 0.0f;
		float localQuaternionWeight = 0.0f;
		int flags = 0;
	};

	struct SequenceDesc {
		String label;
		int flags = 0;
		std::array<int, 2> groupSize = { 1, 1 };
		std::array<int, 2> paramIndex = { -1, -1 };
		std::array<float, 2> paramStart = { 0.0f, 0.0f };
		std::array<float, 2> paramEnd = { 0.0f, 0.0f };
		std::vector<int> animationIndices;
		std::vector<Event> events;
		std::vector<AutoLayer> autoLayers;
		std::vector<float> boneWeights;
		std::array<std::vector<float>, 2> poseKeys;
		std::vector<IKLock> ikLocks;
	};

	struct SampledAnimationTrack {
		std::vector<Vector3> positions;
		std::vector<Quaternion> rotations;
	};

	struct SampledAnimation {
		int animationIndex = -1;
		float fps = 0.0f;
		int frameCount = 0;
		int flags = 0;
		std::vector<SampledAnimationTrack> tracks;
	};

private:
	String mdl_path;
	String anim_block_path;
	PackedStringArray bone_names;
	Array bone_controllers_data;
	Array sequences_data;
	Array ik_chains_data;
	Array ik_autoplay_locks_data;
	Array animations_data;

	mutable bool parsed_dirty = true;
	mutable std::vector<BoneController> parsed_bone_controllers;
	mutable std::vector<SequenceDesc> parsed_sequences;
	mutable std::vector<IKChain> parsed_ik_chains;
	mutable std::vector<IKLock> parsed_ik_autoplay_locks;
	mutable std::vector<SampledAnimation> parsed_animations;

	static void _bind_methods();
	void _mark_dirty();
	void _ensure_parsed() const;

public:
	void set_mdl_path(const String &p_path);
	String get_mdl_path() const { return mdl_path; }

	void set_anim_block_path(const String &p_path);
	String get_anim_block_path() const { return anim_block_path; }

	void set_bone_names(const PackedStringArray &p_bone_names);
	PackedStringArray get_bone_names() const { return bone_names; }

	void set_bone_controllers_data(const Array &p_data);
	Array get_bone_controllers_data() const { return bone_controllers_data; }

	void set_sequences_data(const Array &p_data);
	Array get_sequences_data() const { return sequences_data; }

	void set_ik_chains_data(const Array &p_data);
	Array get_ik_chains_data() const { return ik_chains_data; }

	void set_ik_autoplay_locks_data(const Array &p_data);
	Array get_ik_autoplay_locks_data() const { return ik_autoplay_locks_data; }

	void set_animations_data(const Array &p_data);
	Array get_animations_data() const { return animations_data; }

	int get_bone_controller_count() const;
	Dictionary get_bone_controller(int p_index) const;
	int get_sequence_count() const;
	Dictionary get_sequence(int p_index) const;
	PackedStringArray get_sequence_names() const;
	int find_sequence(const StringName &p_name) const;
	int get_ik_chain_count() const;
	Dictionary get_ik_chain(int p_index) const;
	Array get_ik_autoplay_locks() const;
	int get_animation_count() const;
	Dictionary get_animation(int p_index) const;
	int find_animation_data_index(int p_animation_index) const;
	Dictionary get_animation_by_index(int p_animation_index) const;
	Dictionary get_track(int p_animation_index, int p_bone) const;
	Dictionary get_summary() const;

	Error bake_from_studio_model(const mdlpp::StudioModel &p_model);
	Error bake_from_studio_models(const mdlpp::StudioModel &p_model, const std::vector<const mdlpp::StudioModel *> &p_included_models);
	bool has_required_data() const;
	int get_bone_count() const { return bone_names.size(); }

	const std::vector<BoneController> &get_bone_controllers() const;
	const std::vector<SequenceDesc> &get_sequences() const;
	const std::vector<IKChain> &get_ik_chains() const;
	const std::vector<IKLock> &get_parsed_ik_autoplay_locks() const;
	const std::vector<SampledAnimation> &get_animations() const;
	const SampledAnimation *find_animation(int p_animation_index) const;
};
