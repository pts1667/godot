/**************************************************************************/
/*  sourcepp_mdl.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_mdl.h"

#include "core/error/error_macros.h"
#include "core/math/transform_3d.h"
#include "core/object/class_db.h"

#include <mdlpp/mdlpp.h>

#include <cstring>
#include <utility>

namespace {

Vector3 _to_vector3(const sourcepp::math::Vec3f &p_vector) {
	return Vector3(p_vector[0], p_vector[1], p_vector[2]);
}

Quaternion _to_quaternion(const sourcepp::math::Quat &p_quaternion) {
	return Quaternion(p_quaternion[0], p_quaternion[1], p_quaternion[2], p_quaternion[3]);
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

Transform3D _to_transform_3d(const sourcepp::math::Mat3x4f &p_matrix) {
	return Transform3D(
			p_matrix[0][0], p_matrix[0][1], p_matrix[0][2],
			p_matrix[1][0], p_matrix[1][1], p_matrix[1][2],
			p_matrix[2][0], p_matrix[2][1], p_matrix[2][2],
			p_matrix[0][3], p_matrix[1][3], p_matrix[2][3]);
}

String _get_bone_track_name(const mdlpp::StudioModel *p_model, int p_bone) {
	if (p_bone >= 0 && p_bone < static_cast<int>(p_model->mdl.bones.size()) && !p_model->mdl.bones[static_cast<size_t>(p_bone)].name.empty()) {
		return String::utf8(p_model->mdl.bones[static_cast<size_t>(p_bone)].name.c_str());
	}
	return vformat("bone_%d", p_bone);
}

NodePath _make_bone_track_path(const NodePath &p_skeleton_path, const String &p_bone_name) {
	const String skeleton_path = p_skeleton_path.is_empty() ? String(".") : String(p_skeleton_path);
	return NodePath(skeleton_path + ":" + p_bone_name);
}

}

SourcePPMDL::SourcePPMDL() = default;

SourcePPMDL::~SourcePPMDL() = default;

std::string SourcePPMDL::_to_utf8(const String &p_string) {
	CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data());
}

String SourcePPMDL::_from_utf8(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

std::vector<std::byte> SourcePPMDL::_to_byte_vector(const Vector<uint8_t> &p_data) {
	std::vector<std::byte> out;
	out.resize(static_cast<size_t>(p_data.size()));
	if (!out.empty()) {
		std::memcpy(out.data(), p_data.ptr(), out.size());
	}
	return out;
}

String SourcePPMDL::_derive_companion_path(const String &p_model_path, const PackedStringArray &p_candidates) {
	const String base = p_model_path.get_basename();
	for (const String &suffix : p_candidates) {
		const String candidate = base + suffix;
		if (FileAccess::exists(candidate)) {
			return candidate;
		}
	}
	return String();
}

Error SourcePPMDL::_open_bytes(const Vector<uint8_t> &p_mdl_data, const Vector<uint8_t> &p_vtx_data, const Vector<uint8_t> &p_vvd_data, const Vector<uint8_t> &p_anim_block_data) {
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
	return OK;
}

Error SourcePPMDL::_get_baked_model(int p_lod, mdlpp::BakedModel &r_baked_model) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, ERR_INVALID_PARAMETER, "SourcePPMDL must be opened before use.");
	ERR_FAIL_COND_V_MSG(p_lod < 0 || p_lod >= get_model()->vtx.numLODs, ERR_INVALID_PARAMETER, "Requested LOD is out of range.");

	r_baked_model = get_model()->processModelData(p_lod);
	return OK;
}

mdlpp::StudioModel *SourcePPMDL::get_model() {
	return model.get();
}

const mdlpp::StudioModel *SourcePPMDL::get_model() const {
	return model.get();
}

Error SourcePPMDL::open(const String &p_mdl_path, const String &p_vtx_path, const String &p_vvd_path) {
	close();
	ERR_FAIL_COND_V_MSG(p_mdl_path.is_empty(), ERR_INVALID_PARAMETER, "MDL path must not be empty.");

	const String resolved_vtx_path = p_vtx_path.is_empty() ? _derive_companion_path(p_mdl_path, PackedStringArray{".dx90.vtx", ".vtx", ".dx80.vtx", ".sw.vtx", ".dx11.vtx"}) : p_vtx_path;
	const String resolved_vvd_path = p_vvd_path.is_empty() ? _derive_companion_path(p_mdl_path, PackedStringArray{".vvd"}) : p_vvd_path;
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
	}

	mdl_path = p_mdl_path;
	vtx_path = resolved_vtx_path;
	vvd_path = resolved_vvd_path;
	return OK;
}

Error SourcePPMDL::open_from_buffer(const PackedByteArray &p_mdl_data, const PackedByteArray &p_vtx_data, const PackedByteArray &p_vvd_data, const PackedByteArray &p_anim_block_data) {
	close();
	ERR_FAIL_COND_V_MSG(p_mdl_data.is_empty(), ERR_INVALID_PARAMETER, "MDL data must not be empty.");
	ERR_FAIL_COND_V_MSG(p_vtx_data.is_empty(), ERR_INVALID_PARAMETER, "VTX data must not be empty.");
	ERR_FAIL_COND_V_MSG(p_vvd_data.is_empty(), ERR_INVALID_PARAMETER, "VVD data must not be empty.");

	const Error open_error = _open_bytes(p_mdl_data, p_vtx_data, p_vvd_data, p_anim_block_data);
	if (open_error != OK) {
		close();
		return open_error;
	}

	return OK;
}

void SourcePPMDL::close() {
	model.reset();
	mdl_path = String();
	vtx_path = String();
	vvd_path = String();
}

bool SourcePPMDL::is_open() const {
	return model != nullptr;
}

String SourcePPMDL::get_name() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, String(), "SourcePPMDL must be opened before use.");
	return _from_utf8(get_model()->mdl.name);
}

int SourcePPMDL::get_version() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return get_model()->mdl.version;
}

int SourcePPMDL::get_checksum() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return get_model()->mdl.checksum;
}

int SourcePPMDL::get_lod_count() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return get_model()->vtx.numLODs;
}

String SourcePPMDL::get_mdl_path() const {
	return mdl_path;
}

String SourcePPMDL::get_vtx_path() const {
	return vtx_path;
}

String SourcePPMDL::get_vvd_path() const {
	return vvd_path;
}

PackedStringArray SourcePPMDL::get_materials() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, PackedStringArray(), "SourcePPMDL must be opened before use.");

	PackedStringArray out;
	for (const mdlpp::MDL::Material &material : get_model()->mdl.materials) {
		out.push_back(_from_utf8(material.name));
	}
	return out;
}

PackedStringArray SourcePPMDL::get_material_directories() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, PackedStringArray(), "SourcePPMDL must be opened before use.");

	PackedStringArray out;
	for (const std::string &directory : get_model()->mdl.materialDirectories) {
		out.push_back(_from_utf8(directory));
	}
	return out;
}

Array SourcePPMDL::get_skin_families() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Array(), "SourcePPMDL must be opened before use.");

	Array out;
	for (const std::vector<int16_t> &skin_family : get_model()->mdl.skins) {
		PackedInt32Array indices;
		indices.resize(static_cast<int>(skin_family.size()));
		for (int i = 0; i < indices.size(); i++) {
			indices.set(i, skin_family[static_cast<size_t>(i)]);
		}
		out.push_back(indices);
	}
	return out;
}

PackedStringArray SourcePPMDL::get_bone_names() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, PackedStringArray(), "SourcePPMDL must be opened before use.");

	PackedStringArray out;
	for (const mdlpp::MDL::Bone &bone : get_model()->mdl.bones) {
		out.push_back(_from_utf8(bone.name));
	}
	return out;
}

PackedStringArray SourcePPMDL::get_body_parts() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, PackedStringArray(), "SourcePPMDL must be opened before use.");

	PackedStringArray out;
	for (const mdlpp::MDL::BodyPart &body_part : get_model()->mdl.bodyParts) {
		out.push_back(_from_utf8(body_part.name));
	}
	return out;
}

int SourcePPMDL::get_attachment_count() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return static_cast<int>(get_model()->mdl.attachments.size());
}

Array SourcePPMDL::get_attachments() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Array(), "SourcePPMDL must be opened before use.");

	Array out;
	for (const mdlpp::MDL::Attachment &attachment : get_model()->mdl.attachments) {
		Dictionary attachment_info;
		attachment_info["name"] = _from_utf8(attachment.name);
		attachment_info["flags"] = static_cast<int>(attachment.flags);
		attachment_info["bone"] = attachment.bone;
		if (attachment.bone >= 0 && attachment.bone < static_cast<int>(get_model()->mdl.bones.size())) {
			attachment_info["bone_name"] = _from_utf8(get_model()->mdl.bones[static_cast<size_t>(attachment.bone)].name);
		} else {
			attachment_info["bone_name"] = String();
		}
		attachment_info["transform"] = _to_transform_3d(attachment.local);
		out.push_back(attachment_info);
	}
	return out;
}

int SourcePPMDL::get_animation_descriptor_count() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return static_cast<int>(get_model()->mdl.animDescs.size());
}

Array SourcePPMDL::get_animation_descriptors() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Array(), "SourcePPMDL must be opened before use.");

	Array out;
	for (const mdlpp::MDL::AnimDesc &anim_desc : get_model()->mdl.animDescs) {
		Dictionary anim_desc_info;
		anim_desc_info["name"] = _from_utf8(anim_desc.name);
		anim_desc_info["fps"] = anim_desc.fps;
		anim_desc_info["flags"] = static_cast<int>(anim_desc.flags);
		anim_desc_info["frame_count"] = anim_desc.frameCount;
		anim_desc_info["anim_block"] = anim_desc.animBlock;
		anim_desc_info["anim_index"] = anim_desc.animIndex;
		anim_desc_info["ik_rule_count"] = anim_desc.ikRuleCount;
		anim_desc_info["local_hierarchy_count"] = anim_desc.localHierarchyCount;
		anim_desc_info["section_index"] = anim_desc.sectionIndex;
		anim_desc_info["section_frames"] = anim_desc.sectionFrames;
		anim_desc_info["zero_frame_span"] = anim_desc.zeroFrameSpan;
		anim_desc_info["zero_frame_count"] = anim_desc.zeroFrameCount;
		anim_desc_info["zero_frame_index"] = anim_desc.zeroFrameIndex;
		anim_desc_info["zero_frame_stall_time"] = anim_desc.zeroFrameStallTime;
		anim_desc_info["has_inline_animation_data"] = anim_desc.animBlock == 0;

		Array movements;
		for (const mdlpp::Movement &movement : anim_desc.movements) {
			Dictionary movement_info;
			movement_info["end_frame"] = movement.endFrame;
			movement_info["flags"] = static_cast<int>(movement.flags);
			movement_info["velocity_start"] = movement.velocityStart;
			movement_info["velocity_end"] = movement.velocityEnd;
			movement_info["yaw_end"] = movement.yawEnd;
			movement_info["movement"] = _to_vector3(movement.movement);
			movement_info["relative_position"] = _to_vector3(movement.relativePosition);
			movements.push_back(movement_info);
		}
		anim_desc_info["movements"] = movements;
		out.push_back(anim_desc_info);
	}
	return out;
}

Dictionary SourcePPMDL::get_animation_data(int p_animation_descriptor) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Dictionary(), "SourcePPMDL must be opened before use.");

	mdlpp::SampledAnimation sampled_animation;
	ERR_FAIL_COND_V_MSG(!get_model()->sampleAnimation(p_animation_descriptor, sampled_animation), Dictionary(), "Requested animation descriptor could not be sampled.");

	Dictionary out;
	out["animation_descriptor"] = p_animation_descriptor;
	out["fps"] = sampled_animation.fps;
	out["frame_count"] = sampled_animation.frameCount;
	out["flags"] = static_cast<int>(sampled_animation.flags);
	out["length"] = sampled_animation.fps > 0.0f ? static_cast<double>(sampled_animation.frameCount) / static_cast<double>(sampled_animation.fps) : 0.0;

	Array tracks;
	for (const mdlpp::SampledAnimationTrack &track : sampled_animation.tracks) {
		Dictionary track_info;
		track_info["bone"] = track.bone;
		track_info["bone_name"] = _get_bone_track_name(get_model(), track.bone);

		PackedVector3Array positions;
		positions.resize(static_cast<int>(track.positions.size()));
		for (int i = 0; i < positions.size(); i++) {
			positions.set(i, _to_vector3(track.positions[static_cast<size_t>(i)]));
		}
		track_info["positions"] = positions;

		Array rotations;
		rotations.resize(static_cast<int>(track.rotations.size()));
		for (int i = 0; i < rotations.size(); i++) {
			rotations[i] = _to_quaternion(track.rotations[static_cast<size_t>(i)]);
		}
		track_info["rotations"] = rotations;
		tracks.push_back(track_info);
	}
	out["tracks"] = tracks;

	return out;
}

int SourcePPMDL::get_hitbox_set_count() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return static_cast<int>(get_model()->mdl.hitboxSets.size());
}

PackedStringArray SourcePPMDL::get_hitbox_set_names() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, PackedStringArray(), "SourcePPMDL must be opened before use.");

	PackedStringArray out;
	for (const mdlpp::MDL::HitboxSet &hitbox_set : get_model()->mdl.hitboxSets) {
		out.push_back(_from_utf8(hitbox_set.name));
	}
	return out;
}

int SourcePPMDL::get_sequence_descriptor_count() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return static_cast<int>(get_model()->mdl.sequenceDescs.size());
}

Array SourcePPMDL::get_sequence_descriptors() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Array(), "SourcePPMDL must be opened before use.");

	Array out;
	for (const mdlpp::MDL::SequenceDesc &sequence_desc : get_model()->mdl.sequenceDescs) {
		Dictionary sequence_desc_info;
		sequence_desc_info["label"] = _from_utf8(sequence_desc.label);
		sequence_desc_info["activity_name"] = _from_utf8(sequence_desc.activityName);
		sequence_desc_info["flags"] = static_cast<int>(sequence_desc.flags);
		sequence_desc_info["activity"] = sequence_desc.activity;
		sequence_desc_info["activity_weight"] = sequence_desc.activityWeight;
		sequence_desc_info["event_count"] = sequence_desc.eventCount;
		sequence_desc_info["bounding_box_min"] = _to_vector3(sequence_desc.boundingBoxMin);
		sequence_desc_info["bounding_box_max"] = _to_vector3(sequence_desc.boundingBoxMax);
		sequence_desc_info["blend_count"] = sequence_desc.blendCount;
		sequence_desc_info["group_size"] = Vector2i(sequence_desc.groupSize[0], sequence_desc.groupSize[1]);

		PackedInt32Array param_index;
		param_index.resize(2);
		param_index.set(0, sequence_desc.paramIndex[0]);
		param_index.set(1, sequence_desc.paramIndex[1]);
		sequence_desc_info["param_index"] = param_index;

		PackedFloat32Array param_start;
		param_start.resize(2);
		param_start.set(0, sequence_desc.paramStart[0]);
		param_start.set(1, sequence_desc.paramStart[1]);
		sequence_desc_info["param_start"] = param_start;

		PackedFloat32Array param_end;
		param_end.resize(2);
		param_end.set(0, sequence_desc.paramEnd[0]);
		param_end.set(1, sequence_desc.paramEnd[1]);
		sequence_desc_info["param_end"] = param_end;

		sequence_desc_info["param_parent"] = sequence_desc.paramParent;
		sequence_desc_info["fade_in_time"] = sequence_desc.fadeInTime;
		sequence_desc_info["fade_out_time"] = sequence_desc.fadeOutTime;
		sequence_desc_info["local_entry_node"] = sequence_desc.localEntryNode;
		sequence_desc_info["local_exit_node"] = sequence_desc.localExitNode;
		sequence_desc_info["node_flags"] = sequence_desc.nodeFlags;
		sequence_desc_info["entry_phase"] = sequence_desc.entryPhase;
		sequence_desc_info["exit_phase"] = sequence_desc.exitPhase;
		sequence_desc_info["last_frame"] = sequence_desc.lastFrame;
		sequence_desc_info["next_sequence"] = sequence_desc.nextSequence;
		sequence_desc_info["pose"] = sequence_desc.pose;
		sequence_desc_info["ik_rule_count"] = sequence_desc.ikRuleCount;
		sequence_desc_info["auto_layer_count"] = sequence_desc.autoLayerCount;
		sequence_desc_info["ik_lock_count"] = sequence_desc.ikLockCount;
		sequence_desc_info["cycle_pose_index"] = sequence_desc.cyclePoseIndex;

		PackedInt32Array animation_indices;
		animation_indices.resize(static_cast<int>(sequence_desc.animationIndices.size()));
		for (int i = 0; i < animation_indices.size(); i++) {
			animation_indices.set(i, sequence_desc.animationIndices[static_cast<size_t>(i)]);
		}
		sequence_desc_info["animation_indices"] = animation_indices;

		Array events;
		for (const mdlpp::MDL::Event &event : sequence_desc.events) {
			Dictionary event_info;
			event_info["cycle"] = event.cycle;
			event_info["event"] = event.event;
			event_info["type"] = event.type;
			event_info["options"] = _from_utf8(event.options);
			event_info["name"] = _from_utf8(event.name);
			events.push_back(event_info);
		}
		sequence_desc_info["events"] = events;

		Array auto_layers;
		for (const mdlpp::MDL::AutoLayer &auto_layer : sequence_desc.autoLayers) {
			Dictionary auto_layer_info;
			auto_layer_info["sequence"] = auto_layer.sequence;
			auto_layer_info["pose"] = auto_layer.pose;
			auto_layer_info["flags"] = auto_layer.flags;
			auto_layer_info["start"] = auto_layer.start;
			auto_layer_info["peak"] = auto_layer.peak;
			auto_layer_info["tail"] = auto_layer.tail;
			auto_layer_info["end"] = auto_layer.end;
			auto_layers.push_back(auto_layer_info);
		}
		sequence_desc_info["auto_layers"] = auto_layers;

		PackedFloat32Array bone_weights;
		bone_weights.resize(static_cast<int>(sequence_desc.boneWeights.size()));
		for (int i = 0; i < bone_weights.size(); i++) {
			bone_weights.set(i, sequence_desc.boneWeights[static_cast<size_t>(i)]);
		}
		sequence_desc_info["bone_weights"] = bone_weights;

		Array pose_keys;
		pose_keys.resize(2);
		for (int param = 0; param < 2; param++) {
			PackedFloat32Array param_keys;
			param_keys.resize(static_cast<int>(sequence_desc.poseKeys[param].size()));
			for (int key = 0; key < param_keys.size(); key++) {
				param_keys.set(key, sequence_desc.poseKeys[param][static_cast<size_t>(key)]);
			}
			pose_keys[param] = param_keys;
		}
		sequence_desc_info["pose_keys"] = pose_keys;

		PackedStringArray activity_modifiers;
		for (const mdlpp::MDL::ActivityModifier &activity_modifier : sequence_desc.activityModifiers) {
			activity_modifiers.push_back(_from_utf8(activity_modifier.name));
		}
		sequence_desc_info["activity_modifiers"] = activity_modifiers;
		sequence_desc_info["key_value_text"] = _from_utf8(sequence_desc.keyValueText);
		out.push_back(sequence_desc_info);
	}
	return out;
}

Ref<Animation> SourcePPMDL::create_sequence_animation(int p_sequence_descriptor, const NodePath &p_skeleton_path, int p_blend_x, int p_blend_y) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Ref<Animation>(), "SourcePPMDL must be opened before use.");
	ERR_FAIL_COND_V_MSG(p_sequence_descriptor < 0 || p_sequence_descriptor >= static_cast<int>(get_model()->mdl.sequenceDescs.size()), Ref<Animation>(), "Requested sequence descriptor is out of range.");

	const mdlpp::MDL::SequenceDesc &sequence_desc = get_model()->mdl.sequenceDescs[static_cast<size_t>(p_sequence_descriptor)];
	ERR_FAIL_COND_V_MSG(sequence_desc.groupSize[0] <= 0 || sequence_desc.groupSize[1] <= 0, Ref<Animation>(), "Requested sequence descriptor does not contain any animation blends.");

	const int blend_x = CLAMP(p_blend_x, 0, sequence_desc.groupSize[0] - 1);
	const int blend_y = CLAMP(p_blend_y, 0, sequence_desc.groupSize[1] - 1);
	const int blend_index = blend_y * sequence_desc.groupSize[0] + blend_x;
	ERR_FAIL_COND_V_MSG(blend_index < 0 || blend_index >= static_cast<int>(sequence_desc.animationIndices.size()), Ref<Animation>(), "Requested sequence blend is out of range.");

	mdlpp::SampledAnimation sampled_animation;
	ERR_FAIL_COND_V_MSG(!get_model()->sampleAnimation(sequence_desc.animationIndices[static_cast<size_t>(blend_index)], sampled_animation), Ref<Animation>(), "Selected sequence blend could not be sampled. External animblocks are not supported yet.");

	Ref<Animation> animation;
	animation.instantiate();
	if (sampled_animation.fps > 0.0f) {
		animation->set_step(1.0 / sampled_animation.fps);
		animation->set_length(static_cast<double>(sampled_animation.frameCount) / static_cast<double>(sampled_animation.fps));
	}

	for (const mdlpp::SampledAnimationTrack &track : sampled_animation.tracks) {
		const String bone_name = _get_bone_track_name(get_model(), track.bone);
		const NodePath track_path = _make_bone_track_path(p_skeleton_path, bone_name);

		const int position_track = animation->add_track(Animation::TYPE_POSITION_3D);
		animation->track_set_path(position_track, track_path);
		const int rotation_track = animation->add_track(Animation::TYPE_ROTATION_3D);
		animation->track_set_path(rotation_track, track_path);

		for (int frame = 0; frame < sampled_animation.frameCount; frame++) {
			const double time = sampled_animation.fps > 0.0f ? static_cast<double>(frame) / static_cast<double>(sampled_animation.fps) : 0.0;
			animation->position_track_insert_key(position_track, time, _to_vector3(track.positions[static_cast<size_t>(frame)]));
			animation->rotation_track_insert_key(rotation_track, time, _to_quaternion(track.rotations[static_cast<size_t>(frame)]));
		}
	}

	for (int event_index = 0; event_index < static_cast<int>(sequence_desc.events.size()); event_index++) {
		const mdlpp::MDL::Event &event = sequence_desc.events[static_cast<size_t>(event_index)];
		const String base_name = event.name.empty() ? vformat("event_%d", event.event) : _from_utf8(event.name);
		const double marker_time = animation->get_length() * static_cast<double>(event.cycle);
		animation->add_marker(StringName(vformat("%s_%d", base_name, event_index)), CLAMP(marker_time, 0.0, animation->get_length()));
	}

	return animation;
}

Array SourcePPMDL::get_hitboxes(int p_hitbox_set) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Array(), "SourcePPMDL must be opened before use.");
	ERR_FAIL_COND_V_MSG(p_hitbox_set < 0 || p_hitbox_set >= static_cast<int>(get_model()->mdl.hitboxSets.size()), Array(), "Requested hitbox set is out of range.");

	Array out;
	const mdlpp::MDL::HitboxSet &hitbox_set = get_model()->mdl.hitboxSets[static_cast<size_t>(p_hitbox_set)];
	for (const mdlpp::BBox &hitbox : hitbox_set.hitboxes) {
		Dictionary hitbox_info;
		hitbox_info["name"] = _from_utf8(hitbox.name);
		hitbox_info["bone"] = hitbox.bone;
		if (hitbox.bone >= 0 && hitbox.bone < static_cast<int>(get_model()->mdl.bones.size())) {
			hitbox_info["bone_name"] = _from_utf8(get_model()->mdl.bones[static_cast<size_t>(hitbox.bone)].name);
		} else {
			hitbox_info["bone_name"] = String();
		}
		hitbox_info["group"] = hitbox.group;
		hitbox_info["min"] = Vector3(hitbox.bboxMin[0], hitbox.bboxMin[1], hitbox.bboxMin[2]);
		hitbox_info["max"] = Vector3(hitbox.bboxMax[0], hitbox.bboxMax[1], hitbox.bboxMax[2]);
		out.push_back(hitbox_info);
	}
	return out;
}

int SourcePPMDL::get_vertex_count(int p_lod) const {
	mdlpp::BakedModel baked_model;
	ERR_FAIL_COND_V(_get_baked_model(p_lod, baked_model) != OK, 0);
	return static_cast<int>(baked_model.vertices.size());
}

int SourcePPMDL::get_surface_count(int p_lod) const {
	mdlpp::BakedModel baked_model;
	ERR_FAIL_COND_V(_get_baked_model(p_lod, baked_model) != OK, 0);
	return static_cast<int>(baked_model.meshes.size());
}

PackedInt32Array SourcePPMDL::get_surface_material_indices(int p_lod) const {
	mdlpp::BakedModel baked_model;
	ERR_FAIL_COND_V(_get_baked_model(p_lod, baked_model) != OK, PackedInt32Array());

	PackedInt32Array out;
	out.resize(static_cast<int>(baked_model.meshes.size()));
	for (int i = 0; i < out.size(); i++) {
		out.set(i, baked_model.meshes[static_cast<size_t>(i)].materialIndex);
	}
	return out;
}

PackedStringArray SourcePPMDL::get_surface_materials(int p_lod) const {
	mdlpp::BakedModel baked_model;
	ERR_FAIL_COND_V(_get_baked_model(p_lod, baked_model) != OK, PackedStringArray());

	PackedStringArray out;
	for (const mdlpp::BakedModel::Mesh &mesh : baked_model.meshes) {
		if (mesh.materialIndex >= 0 && mesh.materialIndex < static_cast<int>(get_model()->mdl.materials.size())) {
			out.push_back(_from_utf8(get_model()->mdl.materials[static_cast<size_t>(mesh.materialIndex)].name));
		} else {
			out.push_back(String());
		}
	}
	return out;
}

Ref<ArrayMesh> SourcePPMDL::create_mesh(int p_lod) const {
	mdlpp::BakedModel baked_model;
	ERR_FAIL_COND_V(_get_baked_model(p_lod, baked_model) != OK, Ref<ArrayMesh>());

	Ref<ArrayMesh> mesh;
	mesh.instantiate();

	Vector<Vector3> vertices;
	Vector<Vector3> normals;
	Vector<Vector2> uvs;
	vertices.resize(static_cast<int>(baked_model.vertices.size()));
	normals.resize(static_cast<int>(baked_model.vertices.size()));
	uvs.resize(static_cast<int>(baked_model.vertices.size()));

	for (int i = 0; i < vertices.size(); i++) {
		const mdlpp::BakedModel::Vertex &vertex = baked_model.vertices[static_cast<size_t>(i)];
		vertices.write[i] = Vector3(vertex.position[0], vertex.position[1], vertex.position[2]);
		normals.write[i] = Vector3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
		uvs.write[i] = Vector2(vertex.uv[0], vertex.uv[1]);
	}

	for (int mesh_index = 0; mesh_index < static_cast<int>(baked_model.meshes.size()); mesh_index++) {
		const mdlpp::BakedModel::Mesh &baked_mesh = baked_model.meshes[static_cast<size_t>(mesh_index)];
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;

		Vector<int> indices;
		indices.resize(static_cast<int>(baked_mesh.indices.size()));
		for (int i = 0; i < indices.size(); i++) {
			indices.write[i] = baked_mesh.indices[static_cast<size_t>(i)];
		}
		arrays[Mesh::ARRAY_INDEX] = indices;

		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		if (baked_mesh.materialIndex >= 0 && baked_mesh.materialIndex < static_cast<int>(get_model()->mdl.materials.size())) {
			mesh->surface_set_name(mesh_index, _from_utf8(get_model()->mdl.materials[static_cast<size_t>(baked_mesh.materialIndex)].name));
		}
	}

	return mesh;
}

void SourcePPMDL::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "mdl_path", "vtx_path", "vvd_path"), &SourcePPMDL::open, DEFVAL(String()), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("open_from_buffer", "mdl_data", "vtx_data", "vvd_data", "anim_block_data"), &SourcePPMDL::open_from_buffer, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("close"), &SourcePPMDL::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPMDL::is_open);

	ClassDB::bind_method(D_METHOD("get_name"), &SourcePPMDL::get_name);
	ClassDB::bind_method(D_METHOD("get_version"), &SourcePPMDL::get_version);
	ClassDB::bind_method(D_METHOD("get_checksum"), &SourcePPMDL::get_checksum);
	ClassDB::bind_method(D_METHOD("get_lod_count"), &SourcePPMDL::get_lod_count);
	ClassDB::bind_method(D_METHOD("get_mdl_path"), &SourcePPMDL::get_mdl_path);
	ClassDB::bind_method(D_METHOD("get_vtx_path"), &SourcePPMDL::get_vtx_path);
	ClassDB::bind_method(D_METHOD("get_vvd_path"), &SourcePPMDL::get_vvd_path);

	ClassDB::bind_method(D_METHOD("get_materials"), &SourcePPMDL::get_materials);
	ClassDB::bind_method(D_METHOD("get_material_directories"), &SourcePPMDL::get_material_directories);
	ClassDB::bind_method(D_METHOD("get_skin_families"), &SourcePPMDL::get_skin_families);
	ClassDB::bind_method(D_METHOD("get_bone_names"), &SourcePPMDL::get_bone_names);
	ClassDB::bind_method(D_METHOD("get_body_parts"), &SourcePPMDL::get_body_parts);
	ClassDB::bind_method(D_METHOD("get_attachment_count"), &SourcePPMDL::get_attachment_count);
	ClassDB::bind_method(D_METHOD("get_attachments"), &SourcePPMDL::get_attachments);
	ClassDB::bind_method(D_METHOD("get_animation_descriptor_count"), &SourcePPMDL::get_animation_descriptor_count);
	ClassDB::bind_method(D_METHOD("get_animation_descriptors"), &SourcePPMDL::get_animation_descriptors);
	ClassDB::bind_method(D_METHOD("get_animation_data", "animation_descriptor"), &SourcePPMDL::get_animation_data);
	ClassDB::bind_method(D_METHOD("get_hitbox_set_count"), &SourcePPMDL::get_hitbox_set_count);
	ClassDB::bind_method(D_METHOD("get_hitbox_set_names"), &SourcePPMDL::get_hitbox_set_names);
	ClassDB::bind_method(D_METHOD("get_hitboxes", "hitbox_set"), &SourcePPMDL::get_hitboxes, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_sequence_descriptor_count"), &SourcePPMDL::get_sequence_descriptor_count);
	ClassDB::bind_method(D_METHOD("get_sequence_descriptors"), &SourcePPMDL::get_sequence_descriptors);
	ClassDB::bind_method(D_METHOD("create_sequence_animation", "sequence_descriptor", "skeleton_path", "blend_x", "blend_y"), &SourcePPMDL::create_sequence_animation, DEFVAL(NodePath(".")), DEFVAL(0), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("get_vertex_count", "lod"), &SourcePPMDL::get_vertex_count, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_surface_count", "lod"), &SourcePPMDL::get_surface_count, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_surface_material_indices", "lod"), &SourcePPMDL::get_surface_material_indices, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_surface_materials", "lod"), &SourcePPMDL::get_surface_materials, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("create_mesh", "lod"), &SourcePPMDL::create_mesh, DEFVAL(0));

	ADD_PROPERTY(PropertyInfo(Variant::INT, "attachment_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_attachment_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "animation_descriptor_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_animation_descriptor_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "checksum", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_checksum");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hitbox_set_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_hitbox_set_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_lod_count");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mdl_path", PROPERTY_HINT_FILE, "*.mdl", PROPERTY_USAGE_READ_ONLY), "", "get_mdl_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sequence_descriptor_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_sequence_descriptor_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_version");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "vtx_path", PROPERTY_HINT_FILE, "*.vtx,*.dx90.vtx,*.dx80.vtx,*.sw.vtx,*.dx11.vtx", PROPERTY_USAGE_READ_ONLY), "", "get_vtx_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "vvd_path", PROPERTY_HINT_FILE, "*.vvd", PROPERTY_USAGE_READ_ONLY), "", "get_vvd_path");
}