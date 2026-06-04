/**************************************************************************/
/*  source_mdl_animation_data.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "source_mdl_animation_data.h"

#include "core/object/class_db.h"

#include <mdlpp/mdlpp.h>

namespace {

Vector3 to_vector3(const sourcepp::math::Vec3f &p_vector) {
	return Vector3(p_vector[0], p_vector[1], p_vector[2]);
}

Quaternion to_quaternion(const sourcepp::math::Quat &p_quaternion) {
	Quaternion q(p_quaternion[0], p_quaternion[1], p_quaternion[2], p_quaternion[3]);
	if (!q.is_finite() || Math::is_zero_approx(q.length_squared())) {
		return Quaternion();
	}
	return q.is_normalized() ? q : q.normalized();
}

String from_utf8(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

Dictionary encode_ik_lock(const SourceMDLAnimationData::IKLock &p_lock) {
	Dictionary out;
	out["chain"] = p_lock.chain;
	out["position_weight"] = p_lock.positionWeight;
	out["local_quaternion_weight"] = p_lock.localQuaternionWeight;
	out["flags"] = p_lock.flags;
	return out;
}

Dictionary encode_ik_rule(const mdlpp::MDL::IKRule &p_rule, int p_animation_index = -1, int p_blend_cell = -1) {
	Dictionary out;
	out["index"] = p_rule.index;
	out["type"] = p_rule.type;
	out["chain"] = p_rule.chain;
	out["bone"] = p_rule.bone;
	out["slot"] = p_rule.slot;
	out["height"] = p_rule.height;
	out["radius"] = p_rule.radius;
	out["floor"] = p_rule.floor;
	out["position"] = to_vector3(p_rule.position);
	out["rotation"] = to_quaternion(p_rule.rotation);
	out["compressed_ik_error_index"] = p_rule.compressedIKErrorIndex;
	out["start_frame"] = p_rule.startFrame;
	out["ik_error_index"] = p_rule.ikErrorIndex;
	out["start"] = p_rule.start;
	out["peak"] = p_rule.peak;
	out["tail"] = p_rule.tail;
	out["end"] = p_rule.end;
	out["contact"] = p_rule.contact;
	out["drop"] = p_rule.drop;
	out["top"] = p_rule.top;
	out["attachment"] = from_utf8(p_rule.attachment);
	if (p_animation_index >= 0) {
		out["animation"] = p_animation_index;
	}
	if (p_blend_cell >= 0) {
		out["blend_cell"] = p_blend_cell;
	}
	return out;
}

SourceMDLAnimationData::IKLock decode_ik_lock(const Dictionary &p_data) {
	SourceMDLAnimationData::IKLock out;
	out.chain = p_data.get("chain", -1);
	out.positionWeight = p_data.get("position_weight", 0.0f);
	out.localQuaternionWeight = p_data.get("local_quaternion_weight", 0.0f);
	out.flags = p_data.get("flags", 0);
	return out;
}

} // namespace

void SourceMDLAnimationData::_mark_dirty() {
	parsed_dirty = true;
	emit_changed();
}

void SourceMDLAnimationData::set_mdl_path(const String &p_path) {
	mdl_path = p_path;
	emit_changed();
}

void SourceMDLAnimationData::set_anim_block_path(const String &p_path) {
	anim_block_path = p_path;
	emit_changed();
}

void SourceMDLAnimationData::set_bone_names(const PackedStringArray &p_bone_names) {
	bone_names = p_bone_names;
	_mark_dirty();
}

void SourceMDLAnimationData::set_bone_controllers_data(const Array &p_data) {
	bone_controllers_data = p_data;
	_mark_dirty();
}

void SourceMDLAnimationData::set_sequences_data(const Array &p_data) {
	sequences_data = p_data;
	_mark_dirty();
}

void SourceMDLAnimationData::set_ik_chains_data(const Array &p_data) {
	ik_chains_data = p_data;
	_mark_dirty();
}

void SourceMDLAnimationData::set_ik_autoplay_locks_data(const Array &p_data) {
	ik_autoplay_locks_data = p_data;
	_mark_dirty();
}

void SourceMDLAnimationData::set_animations_data(const Array &p_data) {
	animations_data = p_data;
	_mark_dirty();
}

Error SourceMDLAnimationData::bake_from_studio_model(const mdlpp::StudioModel &p_model) {
	return bake_from_studio_models(p_model, std::vector<const mdlpp::StudioModel *>());
}

Error SourceMDLAnimationData::bake_from_studio_models(const mdlpp::StudioModel &p_model, const std::vector<const mdlpp::StudioModel *> &p_included_models) {
	PackedStringArray baked_bone_names;
	for (const mdlpp::MDL::Bone &bone : p_model.mdl.bones) {
		baked_bone_names.push_back(from_utf8(bone.name));
	}
	const int base_bone_count = baked_bone_names.size();

	Array baked_bone_controllers;
	for (const mdlpp::MDL::BoneController &controller : p_model.mdl.boneControllers) {
		Dictionary data;
		data["bone"] = controller.bone;
		data["type"] = controller.type;
		data["start"] = controller.start;
		data["end"] = controller.end;
		data["rest"] = controller.rest;
		data["input_field"] = controller.inputField;
		baked_bone_controllers.push_back(data);
	}

	Array baked_ik_chains;
	for (const mdlpp::MDL::IKChain &chain : p_model.mdl.ikChains) {
		Dictionary data;
		data["name"] = from_utf8(chain.name);
		data["link_type"] = chain.linkType;
		Array links;
		for (const mdlpp::MDL::IKLink &link : chain.links) {
			Dictionary link_data;
			link_data["bone"] = link.bone;
			link_data["knee_dir"] = to_vector3(link.kneeDir);
			links.push_back(link_data);
		}
		data["links"] = links;
		baked_ik_chains.push_back(data);
	}

	Array baked_ik_autoplay_locks;
	for (const mdlpp::MDL::IKLock &lock : p_model.mdl.ikAutoplayLocks) {
		SourceMDLAnimationData::IKLock baked_lock;
		baked_lock.chain = lock.chain;
		baked_lock.positionWeight = lock.positionWeight;
		baked_lock.localQuaternionWeight = lock.localQuaternionWeight;
		baked_lock.flags = lock.flags;
		baked_ik_autoplay_locks.push_back(encode_ik_lock(baked_lock));
	}

	Array baked_sequences;
	Array baked_animations;
	int sequence_offset = 0;
	int animation_offset = 0;
	auto append_model_animation_data = [&](const mdlpp::StudioModel &p_source_model) {
		const int local_sequence_offset = sequence_offset;
		const int local_animation_offset = animation_offset;

		for (const mdlpp::MDL::SequenceDesc &sequence : p_source_model.mdl.sequenceDescs) {
			Dictionary data;
			data["label"] = from_utf8(sequence.label);
			data["flags"] = static_cast<int>(sequence.flags);
			data["group_size"] = Vector2i(sequence.groupSize[0], sequence.groupSize[1]);
			data["param_index"] = Vector2i(sequence.paramIndex[0], sequence.paramIndex[1]);
			data["param_start"] = Vector2(sequence.paramStart[0], sequence.paramStart[1]);
			data["param_end"] = Vector2(sequence.paramEnd[0], sequence.paramEnd[1]);

			PackedInt32Array animation_indices;
			Array ik_rules;
			for (int blend_cell = 0; blend_cell < static_cast<int>(sequence.animationIndices.size()); blend_cell++) {
				const int16_t animation_index = sequence.animationIndices[static_cast<size_t>(blend_cell)];
				animation_indices.push_back(animation_index >= 0 ? local_animation_offset + animation_index : -1);
				if (animation_index < 0) {
					continue;
				}
				const std::vector<mdlpp::MDL::IKRule> animation_ik_rules = p_source_model.getAnimationIKRules(animation_index);
				for (const mdlpp::MDL::IKRule &rule : animation_ik_rules) {
					ik_rules.push_back(encode_ik_rule(rule, local_animation_offset + animation_index, blend_cell));
				}
			}
			data["animation_indices"] = animation_indices;
			data["ik_rules"] = ik_rules;

			Array events;
			for (const mdlpp::MDL::Event &event : sequence.events) {
				Dictionary event_data;
				event_data["cycle"] = event.cycle;
				event_data["event"] = event.event;
				event_data["type"] = event.type;
				event_data["options"] = from_utf8(event.options);
				event_data["name"] = from_utf8(event.name);
				events.push_back(event_data);
			}
			data["events"] = events;

			Array auto_layers;
			for (const mdlpp::MDL::AutoLayer &layer : sequence.autoLayers) {
				Dictionary layer_data;
				layer_data["sequence"] = layer.sequence >= 0 ? local_sequence_offset + layer.sequence : -1;
				layer_data["pose"] = layer.pose;
				layer_data["flags"] = layer.flags;
				layer_data["start"] = layer.start;
				layer_data["peak"] = layer.peak;
				layer_data["tail"] = layer.tail;
				layer_data["end"] = layer.end;
				auto_layers.push_back(layer_data);
			}
			data["auto_layers"] = auto_layers;

			PackedFloat32Array bone_weights;
			for (int bone_index = 0; bone_index < base_bone_count; bone_index++) {
				const float weight = bone_index < static_cast<int>(sequence.boneWeights.size()) ? sequence.boneWeights[static_cast<size_t>(bone_index)] : 1.0f;
				bone_weights.push_back(weight);
			}
			data["bone_weights"] = bone_weights;

			Array pose_keys;
			for (int axis = 0; axis < 2; axis++) {
				PackedFloat32Array axis_keys;
				for (const float key : sequence.poseKeys[axis]) {
					axis_keys.push_back(key);
				}
				pose_keys.push_back(axis_keys);
			}
			data["pose_keys"] = pose_keys;

			Array ik_locks;
			for (const mdlpp::MDL::IKLock &lock : sequence.ikLocks) {
				SourceMDLAnimationData::IKLock baked_lock;
				baked_lock.chain = lock.chain;
				baked_lock.positionWeight = lock.positionWeight;
				baked_lock.localQuaternionWeight = lock.localQuaternionWeight;
				baked_lock.flags = lock.flags;
				ik_locks.push_back(encode_ik_lock(baked_lock));
			}
			data["ik_locks"] = ik_locks;
			baked_sequences.push_back(data);
		}

		for (int animation_index = 0; animation_index < static_cast<int>(p_source_model.mdl.animDescs.size()); animation_index++) {
			mdlpp::SampledAnimation sampled;
			if (!p_source_model.sampleAnimation(animation_index, sampled)) {
				continue;
			}

			Dictionary data;
			data["animation_index"] = local_animation_offset + sampled.animationIndex;
			data["fps"] = sampled.fps;
			data["frame_count"] = sampled.frameCount;
			data["flags"] = static_cast<int>(sampled.flags);
			Array ik_rules;
			const std::vector<mdlpp::MDL::IKRule> animation_ik_rules = p_source_model.getAnimationIKRules(animation_index);
			for (const mdlpp::MDL::IKRule &rule : animation_ik_rules) {
				ik_rules.push_back(encode_ik_rule(rule, local_animation_offset + sampled.animationIndex));
			}
			data["ik_rules"] = ik_rules;
			Array tracks;
			for (int bone_index = 0; bone_index < base_bone_count; bone_index++) {
				Dictionary track_data;
				PackedVector3Array positions;
				Array rotations;
				if (bone_index < static_cast<int>(sampled.tracks.size())) {
					const mdlpp::SampledAnimationTrack &track = sampled.tracks[static_cast<size_t>(bone_index)];
					for (const sourcepp::math::Vec3f &position : track.positions) {
						positions.push_back(to_vector3(position));
					}
					for (const sourcepp::math::Quat &rotation : track.rotations) {
						rotations.push_back(to_quaternion(rotation));
					}
				}
				track_data["positions"] = positions;
				track_data["rotations"] = rotations;
				tracks.push_back(track_data);
			}
			data["tracks"] = tracks;
			baked_animations.push_back(data);
		}

		sequence_offset += static_cast<int>(p_source_model.mdl.sequenceDescs.size());
		animation_offset += static_cast<int>(p_source_model.mdl.animDescs.size());
	};

	append_model_animation_data(p_model);
	for (const mdlpp::StudioModel *included_model : p_included_models) {
		if (included_model == nullptr) {
			continue;
		}
		append_model_animation_data(*included_model);
	}

	bone_names = baked_bone_names;
	bone_controllers_data = baked_bone_controllers;
	sequences_data = baked_sequences;
	ik_chains_data = baked_ik_chains;
	ik_autoplay_locks_data = baked_ik_autoplay_locks;
	animations_data = baked_animations;
	_mark_dirty();
	return OK;
}

void SourceMDLAnimationData::_ensure_parsed() const {
	if (!parsed_dirty) {
		return;
	}

	parsed_bone_controllers.clear();
	for (const Variant &variant : bone_controllers_data) {
		Dictionary data = variant;
		BoneController controller;
		controller.bone = data.get("bone", -1);
		controller.type = data.get("type", 0);
		controller.start = data.get("start", 0.0f);
		controller.end = data.get("end", 0.0f);
		controller.rest = data.get("rest", 0);
		controller.inputField = data.get("input_field", -1);
		parsed_bone_controllers.push_back(controller);
	}

	parsed_ik_chains.clear();
	for (const Variant &variant : ik_chains_data) {
		Dictionary data = variant;
		IKChain chain;
		chain.name = data.get("name", String());
		chain.linkType = data.get("link_type", 0);
		Array links = data.get("links", Array());
		for (const Variant &link_variant : links) {
			Dictionary link_data = link_variant;
			IKLink link;
			link.bone = link_data.get("bone", -1);
			link.kneeDir = link_data.get("knee_dir", Vector3());
			chain.links.push_back(link);
		}
		parsed_ik_chains.push_back(chain);
	}

	parsed_ik_autoplay_locks.clear();
	for (const Variant &variant : ik_autoplay_locks_data) {
		parsed_ik_autoplay_locks.push_back(decode_ik_lock(variant));
	}

	parsed_sequences.clear();
	for (const Variant &variant : sequences_data) {
		Dictionary data = variant;
		SequenceDesc sequence;
		sequence.label = data.get("label", String());
		sequence.flags = data.get("flags", 0);
		const Vector2i group_size = data.get("group_size", Vector2i(1, 1));
		const Vector2i param_index = data.get("param_index", Vector2i(-1, -1));
		const Vector2 param_start = data.get("param_start", Vector2());
		const Vector2 param_end = data.get("param_end", Vector2());
		sequence.groupSize = { group_size.x, group_size.y };
		sequence.paramIndex = { param_index.x, param_index.y };
		sequence.paramStart = { param_start.x, param_start.y };
		sequence.paramEnd = { param_end.x, param_end.y };

		PackedInt32Array animation_indices = data.get("animation_indices", PackedInt32Array());
		for (const int animation_index : animation_indices) {
			sequence.animationIndices.push_back(animation_index);
		}

		Array events = data.get("events", Array());
		for (const Variant &event_variant : events) {
			Dictionary event_data = event_variant;
			Event event;
			event.cycle = event_data.get("cycle", 0.0f);
			event.event = event_data.get("event", 0);
			event.type = event_data.get("type", 0);
			event.options = event_data.get("options", String());
			event.name = event_data.get("name", String());
			sequence.events.push_back(event);
		}

		Array auto_layers = data.get("auto_layers", Array());
		for (const Variant &layer_variant : auto_layers) {
			Dictionary layer_data = layer_variant;
			AutoLayer layer;
			layer.sequence = layer_data.get("sequence", -1);
			layer.pose = layer_data.get("pose", 0);
			layer.flags = layer_data.get("flags", 0);
			layer.start = layer_data.get("start", 0.0f);
			layer.peak = layer_data.get("peak", 0.0f);
			layer.tail = layer_data.get("tail", 0.0f);
			layer.end = layer_data.get("end", 0.0f);
			sequence.autoLayers.push_back(layer);
		}

		PackedFloat32Array bone_weights = data.get("bone_weights", PackedFloat32Array());
		for (const float weight : bone_weights) {
			sequence.boneWeights.push_back(weight);
		}

		Array pose_keys = data.get("pose_keys", Array());
		for (int axis = 0; axis < MIN(2, pose_keys.size()); axis++) {
			PackedFloat32Array axis_keys = pose_keys[axis];
			for (const float key : axis_keys) {
				sequence.poseKeys[axis].push_back(key);
			}
		}

		Array ik_locks = data.get("ik_locks", Array());
		for (const Variant &lock_variant : ik_locks) {
			sequence.ikLocks.push_back(decode_ik_lock(lock_variant));
		}
		parsed_sequences.push_back(sequence);
	}

	parsed_animations.clear();
	for (const Variant &variant : animations_data) {
		Dictionary data = variant;
		SampledAnimation animation;
		animation.animationIndex = data.get("animation_index", -1);
		animation.fps = data.get("fps", 0.0f);
		animation.frameCount = data.get("frame_count", 0);
		animation.flags = data.get("flags", 0);
		Array tracks = data.get("tracks", Array());
		for (const Variant &track_variant : tracks) {
			Dictionary track_data = track_variant;
			SampledAnimationTrack track;
			PackedVector3Array positions = track_data.get("positions", PackedVector3Array());
			Array rotations = track_data.get("rotations", Array());
			for (const Vector3 &position : positions) {
				track.positions.push_back(position);
			}
			for (const Variant &rotation_variant : rotations) {
				track.rotations.push_back(rotation_variant);
			}
			animation.tracks.push_back(track);
		}
		parsed_animations.push_back(animation);
	}

	parsed_dirty = false;
}

bool SourceMDLAnimationData::has_required_data() const {
	return !bone_names.is_empty() && !sequences_data.is_empty() && !animations_data.is_empty();
}

int SourceMDLAnimationData::get_bone_controller_count() const {
	return bone_controllers_data.size();
}

Dictionary SourceMDLAnimationData::get_bone_controller(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, bone_controllers_data.size(), Dictionary());
	return bone_controllers_data[p_index];
}

int SourceMDLAnimationData::get_sequence_count() const {
	return sequences_data.size();
}

Dictionary SourceMDLAnimationData::get_sequence(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, sequences_data.size(), Dictionary());
	return sequences_data[p_index];
}

PackedStringArray SourceMDLAnimationData::get_sequence_names() const {
	PackedStringArray names;
	names.resize(sequences_data.size());
	for (int i = 0; i < sequences_data.size(); i++) {
		const Dictionary sequence = sequences_data[i];
		String label = sequence.get("label", String());
		if (label.is_empty()) {
			label = vformat("sequence_%d", i);
		}
		names.set(i, label);
	}
	return names;
}

int SourceMDLAnimationData::find_sequence(const StringName &p_name) const {
	for (int i = 0; i < sequences_data.size(); i++) {
		const Dictionary sequence = sequences_data[i];
		if (StringName(sequence.get("label", String())) == p_name) {
			return i;
		}
	}
	return -1;
}

int SourceMDLAnimationData::get_ik_chain_count() const {
	return ik_chains_data.size();
}

Dictionary SourceMDLAnimationData::get_ik_chain(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, ik_chains_data.size(), Dictionary());
	return ik_chains_data[p_index];
}

Array SourceMDLAnimationData::get_ik_autoplay_locks() const {
	return ik_autoplay_locks_data;
}

int SourceMDLAnimationData::get_animation_count() const {
	return animations_data.size();
}

Dictionary SourceMDLAnimationData::get_animation(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, animations_data.size(), Dictionary());
	return animations_data[p_index];
}

int SourceMDLAnimationData::find_animation_data_index(int p_animation_index) const {
	for (int i = 0; i < animations_data.size(); i++) {
		const Dictionary animation = animations_data[i];
		if (static_cast<int>(animation.get("animation_index", -1)) == p_animation_index) {
			return i;
		}
	}
	return -1;
}

Dictionary SourceMDLAnimationData::get_animation_by_index(int p_animation_index) const {
	const int data_index = find_animation_data_index(p_animation_index);
	if (data_index < 0) {
		return Dictionary();
	}
	return get_animation(data_index);
}

Dictionary SourceMDLAnimationData::get_track(int p_animation_index, int p_bone) const {
	const Dictionary animation = get_animation_by_index(p_animation_index);
	if (animation.is_empty()) {
		return Dictionary();
	}
	const Array tracks = animation.get("tracks", Array());
	ERR_FAIL_INDEX_V(p_bone, tracks.size(), Dictionary());
	return tracks[p_bone];
}

Dictionary SourceMDLAnimationData::get_summary() const {
	Dictionary summary;
	summary["mdl_path"] = mdl_path;
	summary["anim_block_path"] = anim_block_path;
	summary["bone_count"] = bone_names.size();
	summary["bone_controller_count"] = bone_controllers_data.size();
	summary["sequence_count"] = sequences_data.size();
	summary["ik_chain_count"] = ik_chains_data.size();
	summary["ik_autoplay_lock_count"] = ik_autoplay_locks_data.size();
	summary["animation_count"] = animations_data.size();
	summary["has_required_data"] = has_required_data();
	return summary;
}

const std::vector<SourceMDLAnimationData::BoneController> &SourceMDLAnimationData::get_bone_controllers() const {
	_ensure_parsed();
	return parsed_bone_controllers;
}

const std::vector<SourceMDLAnimationData::SequenceDesc> &SourceMDLAnimationData::get_sequences() const {
	_ensure_parsed();
	return parsed_sequences;
}

const std::vector<SourceMDLAnimationData::IKChain> &SourceMDLAnimationData::get_ik_chains() const {
	_ensure_parsed();
	return parsed_ik_chains;
}

const std::vector<SourceMDLAnimationData::IKLock> &SourceMDLAnimationData::get_parsed_ik_autoplay_locks() const {
	_ensure_parsed();
	return parsed_ik_autoplay_locks;
}

const std::vector<SourceMDLAnimationData::SampledAnimation> &SourceMDLAnimationData::get_animations() const {
	_ensure_parsed();
	return parsed_animations;
}

const SourceMDLAnimationData::SampledAnimation *SourceMDLAnimationData::find_animation(int p_animation_index) const {
	_ensure_parsed();
	for (const SampledAnimation &animation : parsed_animations) {
		if (animation.animationIndex == p_animation_index) {
			return &animation;
		}
	}
	return nullptr;
}

void SourceMDLAnimationData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mdl_path", "path"), &SourceMDLAnimationData::set_mdl_path);
	ClassDB::bind_method(D_METHOD("get_mdl_path"), &SourceMDLAnimationData::get_mdl_path);
	ClassDB::bind_method(D_METHOD("set_anim_block_path", "path"), &SourceMDLAnimationData::set_anim_block_path);
	ClassDB::bind_method(D_METHOD("get_anim_block_path"), &SourceMDLAnimationData::get_anim_block_path);
	ClassDB::bind_method(D_METHOD("set_bone_names", "bone_names"), &SourceMDLAnimationData::set_bone_names);
	ClassDB::bind_method(D_METHOD("get_bone_names"), &SourceMDLAnimationData::get_bone_names);
	ClassDB::bind_method(D_METHOD("set_bone_controllers_data", "data"), &SourceMDLAnimationData::set_bone_controllers_data);
	ClassDB::bind_method(D_METHOD("get_bone_controllers_data"), &SourceMDLAnimationData::get_bone_controllers_data);
	ClassDB::bind_method(D_METHOD("set_sequences_data", "data"), &SourceMDLAnimationData::set_sequences_data);
	ClassDB::bind_method(D_METHOD("get_sequences_data"), &SourceMDLAnimationData::get_sequences_data);
	ClassDB::bind_method(D_METHOD("set_ik_chains_data", "data"), &SourceMDLAnimationData::set_ik_chains_data);
	ClassDB::bind_method(D_METHOD("get_ik_chains_data"), &SourceMDLAnimationData::get_ik_chains_data);
	ClassDB::bind_method(D_METHOD("set_ik_autoplay_locks_data", "data"), &SourceMDLAnimationData::set_ik_autoplay_locks_data);
	ClassDB::bind_method(D_METHOD("get_ik_autoplay_locks_data"), &SourceMDLAnimationData::get_ik_autoplay_locks_data);
	ClassDB::bind_method(D_METHOD("set_animations_data", "data"), &SourceMDLAnimationData::set_animations_data);
	ClassDB::bind_method(D_METHOD("get_animations_data"), &SourceMDLAnimationData::get_animations_data);
	ClassDB::bind_method(D_METHOD("get_bone_count"), &SourceMDLAnimationData::get_bone_count);
	ClassDB::bind_method(D_METHOD("get_bone_controller_count"), &SourceMDLAnimationData::get_bone_controller_count);
	ClassDB::bind_method(D_METHOD("get_bone_controller", "index"), &SourceMDLAnimationData::get_bone_controller);
	ClassDB::bind_method(D_METHOD("get_sequence_count"), &SourceMDLAnimationData::get_sequence_count);
	ClassDB::bind_method(D_METHOD("get_sequence", "index"), &SourceMDLAnimationData::get_sequence);
	ClassDB::bind_method(D_METHOD("get_sequence_names"), &SourceMDLAnimationData::get_sequence_names);
	ClassDB::bind_method(D_METHOD("find_sequence", "name"), &SourceMDLAnimationData::find_sequence);
	ClassDB::bind_method(D_METHOD("get_ik_chain_count"), &SourceMDLAnimationData::get_ik_chain_count);
	ClassDB::bind_method(D_METHOD("get_ik_chain", "index"), &SourceMDLAnimationData::get_ik_chain);
	ClassDB::bind_method(D_METHOD("get_ik_autoplay_locks"), &SourceMDLAnimationData::get_ik_autoplay_locks);
	ClassDB::bind_method(D_METHOD("get_animation_count"), &SourceMDLAnimationData::get_animation_count);
	ClassDB::bind_method(D_METHOD("get_animation", "index"), &SourceMDLAnimationData::get_animation);
	ClassDB::bind_method(D_METHOD("find_animation_data_index", "animation_index"), &SourceMDLAnimationData::find_animation_data_index);
	ClassDB::bind_method(D_METHOD("get_animation_by_index", "animation_index"), &SourceMDLAnimationData::get_animation_by_index);
	ClassDB::bind_method(D_METHOD("get_track", "animation_index", "bone"), &SourceMDLAnimationData::get_track);
	ClassDB::bind_method(D_METHOD("get_summary"), &SourceMDLAnimationData::get_summary);
	ClassDB::bind_method(D_METHOD("has_required_data"), &SourceMDLAnimationData::has_required_data);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mdl_path", PROPERTY_HINT_FILE, "*.mdl"), "set_mdl_path", "get_mdl_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "anim_block_path", PROPERTY_HINT_FILE, "*.ani"), "set_anim_block_path", "get_anim_block_path");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "bone_names", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "set_bone_names", "get_bone_names");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "bone_controllers_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "set_bone_controllers_data", "get_bone_controllers_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "sequences_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "set_sequences_data", "get_sequences_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "ik_chains_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "set_ik_chains_data", "get_ik_chains_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "ik_autoplay_locks_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "set_ik_autoplay_locks_data", "get_ik_autoplay_locks_data");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "animations_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_INTERNAL), "set_animations_data", "get_animations_data");
}
