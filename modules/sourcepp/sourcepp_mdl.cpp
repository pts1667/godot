/**************************************************************************/
/*  sourcepp_mdl.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_mdl.h"

#include "source_mdl_animation_data.h"
#include "sourcepp_import_cache.h"
#include "sourcepp_resolver.h"
#include "sourcepp_vmt.h"
#include "utils/sourcepp_utils.h"

#include "core/error/error_macros.h"
#include "core/io/resource_loader.h"
#include "core/math/transform_3d.h"
#include "core/object/class_db.h"
#include "scene/3d/bone_attachment_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/animatable_body_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/material.h"

#include <mdlpp/mdlpp.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace {

constexpr float SOURCE_IMPORT_ROTATION_X = -1.5707963267948966f;
const String SOURCE_SCRIPT_ANIM_PLAYER_PATH = "C:/Users/Thoma/Documents/sources/godot/modules/sourcepp/scripts/source_script_anim_player.gd";

struct SourceAnimSectionInfo {
	int32_t anim_block = -1;
	int32_t anim_index = 0;
};

Transform3D _to_bone_rest(const mdlpp::MDL::Bone &p_bone) {
	return Transform3D(Basis(SourcePPUtils::source_quaternion_to_quaternion(p_bone.rotationQuat)), SourcePPUtils::source_vector_to_vector3(p_bone.position));
}

Vector<uint8_t> _packed_to_vector(const PackedByteArray &p_bytes) {
	Vector<uint8_t> out;
	out.resize(p_bytes.size());
	if (!p_bytes.is_empty()) {
		std::memcpy(out.ptrw(), p_bytes.ptr(), static_cast<size_t>(p_bytes.size()));
	}
	return out;
}

String _resolve_anim_block_path(const String &p_model_path, const mdlpp::MDL::MDL &p_mdl, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id) {
	if (p_mdl.animBlocks.size() <= 1 || p_mdl.animBlockName.empty()) {
		return String();
	}

	const String anim_block_name = String::utf8(p_mdl.animBlockName.c_str()).replace("\\", "/");
	if (anim_block_name.is_empty()) {
		return String();
	}
	if (SourcePPUtils::path_exists_with_resolver(anim_block_name, p_resolver, p_game_id)) {
		return anim_block_name;
	}

	const String model_dir = p_model_path.get_base_dir();
	const String local_candidate = model_dir.path_join(anim_block_name.get_file());
	if (SourcePPUtils::path_exists_with_resolver(local_candidate, p_resolver, p_game_id)) {
		return local_candidate;
	}

	const String relative_candidate = model_dir.path_join(anim_block_name);
	if (SourcePPUtils::path_exists_with_resolver(relative_candidate, p_resolver, p_game_id)) {
		return relative_candidate;
	}

	const String fallback_candidate = p_model_path.get_basename() + ".ani";
	if (SourcePPUtils::path_exists_with_resolver(fallback_candidate, p_resolver, p_game_id)) {
		return fallback_candidate;
	}

	return String();
}

String _get_bone_track_name(const mdlpp::StudioModel *p_model, int p_bone) {
	if (p_bone >= 0 && p_bone < static_cast<int>(p_model->mdl.bones.size()) && !p_model->mdl.bones[static_cast<size_t>(p_bone)].name.empty()) {
		return String::utf8(p_model->mdl.bones[static_cast<size_t>(p_bone)].name.c_str());
	}
	return vformat("bone_%d", p_bone);
}

Vector<Transform3D> _build_global_bone_rests(const mdlpp::StudioModel *p_model) {
	Vector<Transform3D> global_rests;
	if (p_model == nullptr) {
		return global_rests;
	}

	const int bone_count = static_cast<int>(p_model->mdl.bones.size());
	global_rests.resize(bone_count);
	Vector<uint8_t> computed;
	computed.resize(bone_count);
	for (int i = 0; i < bone_count; i++) {
		computed.write[i] = 0;
	}

	auto resolve_rest = [&](auto &&p_self, int p_bone) -> Transform3D {
		ERR_FAIL_INDEX_V(p_bone, bone_count, Transform3D());
		if (computed[p_bone] != 0) {
			return global_rests[p_bone];
		}

		const mdlpp::MDL::Bone &bone = p_model->mdl.bones[static_cast<size_t>(p_bone)];
		Transform3D global_rest = _to_bone_rest(bone);
		if (bone.parent >= 0 && bone.parent < bone_count) {
			global_rest = p_self(p_self, bone.parent) * global_rest;
		}

		global_rests.write[p_bone] = global_rest;
		computed.write[p_bone] = 1;
		return global_rest;
	};

	for (int bone_index = 0; bone_index < bone_count; bone_index++) {
		(void)resolve_rest(resolve_rest, bone_index);
	}

	return global_rests;
}

Dictionary _make_bone_controller_info(const mdlpp::StudioModel *p_model, const mdlpp::MDL::BoneController &p_controller) {
	Dictionary out;
	out["bone"] = p_controller.bone;
	out["bone_name"] = _get_bone_track_name(p_model, p_controller.bone);
	out["type"] = p_controller.type;
	out["start"] = p_controller.start;
	out["end"] = p_controller.end;
	out["rest"] = p_controller.rest;
	out["rest_normalized"] = CLAMP(static_cast<float>(p_controller.rest) / 255.0f, 0.0f, 1.0f);
	out["input_field"] = p_controller.inputField;
	return out;
}

NodePath _make_bone_track_path(const NodePath &p_skeleton_path, const String &p_bone_name) {
	const String skeleton_path = p_skeleton_path.is_empty() ? String(".") : String(p_skeleton_path);
	return NodePath(skeleton_path + ":" + p_bone_name);
}

String _strip_prefix(const String &p_value, const String &p_prefix) {
	return p_value.begins_with(p_prefix) ? p_value.substr(p_prefix.length()) : p_value;
}

String _strip_material_extension(const String &p_value) {
	return p_value.to_lower().ends_with(".vmt") ? p_value.substr(0, p_value.length() - 4) : p_value;
}

void _append_unique_candidate(Vector<String> &r_candidates, const String &p_candidate) {
	const String normalized = SourcePPUtils::normalize_source_path(p_candidate);
	if (normalized.is_empty()) {
		return;
	}
	for (int i = 0; i < r_candidates.size(); i++) {
		if (r_candidates[i] == normalized) {
			return;
		}
	}
	r_candidates.push_back(normalized);
}

String _build_scene_name(const String &p_name, const String &p_path) {
	String scene_name = p_name;
	if (scene_name.is_empty() && !p_path.is_empty()) {
		scene_name = p_path.get_file().get_basename();
	}
	if (scene_name.is_empty()) {
		scene_name = "SourcePPMDL";
	}
	scene_name = scene_name.validate_node_name();
	return scene_name.is_empty() ? String("SourcePPMDL") : scene_name;
}

struct CollisionShapeSpec {
	Transform3D transform;
	Vector3 size;
};

struct CollisionBodySpec {
	int bone = -1;
	String bone_name;
	String name;
	String source;
	Transform3D transform;
	Vector<CollisionShapeSpec> shapes;
};

enum class ModelBindingMode {
	MODEL_SPACE,
	RIGID_BONE,
	SKINNED,
};

struct ModelBindingInfo {
	ModelBindingMode mode = ModelBindingMode::MODEL_SPACE;
	int bone = -1;
};

struct CollisionBoundsAccumulator {
	bool valid = false;
	Vector3 min;
	Vector3 max;

	void expand_to(const Vector3 &p_point) {
		if (!valid) {
			min = p_point;
			max = p_point;
			valid = true;
			return;
		}
		min.x = MIN(min.x, p_point.x);
		min.y = MIN(min.y, p_point.y);
		min.z = MIN(min.z, p_point.z);
		max.x = MAX(max.x, p_point.x);
		max.y = MAX(max.y, p_point.y);
		max.z = MAX(max.z, p_point.z);
	}
};

int _get_dominant_bone(const mdlpp::BakedModel::Vertex &p_vertex, int p_bone_count) {
	int dominant_bone = -1;
	float dominant_weight = -1.0f;
	for (int influence_index = 0; influence_index < static_cast<int>(p_vertex.bones.size()); influence_index++) {
		const int bone = p_vertex.bones[static_cast<size_t>(influence_index)];
		const float weight = p_vertex.weights[static_cast<size_t>(influence_index)];
		if (bone < 0 || bone >= p_bone_count) {
			continue;
		}
		if (weight > dominant_weight) {
			dominant_bone = bone;
			dominant_weight = weight;
		}
	}
	return dominant_bone;
}

bool _has_nonzero_bone_weights(const mdlpp::BakedModel::Vertex &p_vertex) {
	for (int influence_index = 0; influence_index < static_cast<int>(p_vertex.weights.size()); influence_index++) {
		if (p_vertex.weights[static_cast<size_t>(influence_index)] > 0.0001f) {
			return true;
		}
	}
	return false;
}

ModelBindingInfo _get_model_binding_info(const mdlpp::StudioModel *p_model, const mdlpp::BakedModel &p_baked_model) {
	ModelBindingInfo info;
	if (p_model == nullptr || p_model->mdl.bones.empty() || p_baked_model.vertices.empty()) {
		return info;
	}

	const int bone_count = static_cast<int>(p_model->mdl.bones.size());
	int rigid_bone = -1;
	bool saw_weighted_vertex = false;
	for (const mdlpp::BakedModel::Vertex &vertex : p_baked_model.vertices) {
		int vertex_bone = -1;
		int influence_count = 0;
		for (int influence_index = 0; influence_index < static_cast<int>(vertex.bones.size()); influence_index++) {
			const int bone = vertex.bones[static_cast<size_t>(influence_index)];
			const float weight = vertex.weights[static_cast<size_t>(influence_index)];
			if (weight <= 0.0001f || bone < 0 || bone >= bone_count) {
				continue;
			}
			saw_weighted_vertex = true;
			influence_count++;
			if (influence_count == 1) {
				vertex_bone = bone;
			} else {
				info.mode = ModelBindingMode::SKINNED;
				info.bone = -1;
				return info;
			}
		}

		if (influence_count == 0) {
			continue;
		}
		if (rigid_bone == -1) {
			rigid_bone = vertex_bone;
		} else if (rigid_bone != vertex_bone) {
			info.mode = ModelBindingMode::SKINNED;
			info.bone = -1;
			return info;
		}
	}

	if (rigid_bone >= 0) {
		info.mode = ModelBindingMode::RIGID_BONE;
		info.bone = rigid_bone;
		return info;
	}

	if (!saw_weighted_vertex && bone_count == 1) {
		info.mode = ModelBindingMode::RIGID_BONE;
		info.bone = 0;
	}

	return info;
}

Vector<CollisionBodySpec> _build_generated_collision_boxes(const mdlpp::StudioModel *p_model, const mdlpp::BakedModel &p_baked_model) {
	Vector<CollisionBodySpec> specs;
	if (p_model == nullptr || p_model->mdl.bones.empty() || p_baked_model.vertices.empty()) {
		return specs;
	}

	const int bone_count = static_cast<int>(p_model->mdl.bones.size());
	Vector<uint8_t> collision_bone_mask;
	collision_bone_mask.resize(bone_count);
	bool has_explicit_physics_bones = false;
	for (int bone_index = 0; bone_index < bone_count; bone_index++) {
		const bool uses_physics_bone = p_model->mdl.bones[static_cast<size_t>(bone_index)].physicsBone >= 0;
		collision_bone_mask.write[bone_index] = uses_physics_bone ? 1 : 0;
		has_explicit_physics_bones = has_explicit_physics_bones || uses_physics_bone;
	}
	if (!has_explicit_physics_bones) {
		for (int bone_index = 0; bone_index < bone_count; bone_index++) {
			collision_bone_mask.write[bone_index] = 1;
		}
	}

	const Vector<Transform3D> global_rests = _build_global_bone_rests(p_model);
	Vector<Transform3D> inverse_global_rests;
	inverse_global_rests.resize(global_rests.size());
	for (int bone_index = 0; bone_index < inverse_global_rests.size(); bone_index++) {
		inverse_global_rests.write[bone_index] = global_rests[bone_index].affine_inverse();
	}

	Vector<CollisionBoundsAccumulator> bounds;
	bounds.resize(bone_count);
	CollisionBoundsAccumulator unweighted_bounds;
	for (const mdlpp::BakedModel::Vertex &vertex : p_baked_model.vertices) {
		const int dominant_bone = _get_dominant_bone(vertex, bone_count);
		if (dominant_bone < 0 || dominant_bone >= bone_count) {
			if (!_has_nonzero_bone_weights(vertex)) {
				unweighted_bounds.expand_to(SourcePPUtils::source_vector_to_vector3(vertex.position));
			}
			continue;
		}
		if (collision_bone_mask[dominant_bone] == 0) {
			continue;
		}
		const Vector3 model_position = SourcePPUtils::source_vector_to_vector3(vertex.position);
		const Vector3 local_position = inverse_global_rests[dominant_bone].xform(model_position);
		bounds.write[dominant_bone].expand_to(local_position);
	}

	for (int bone_index = 0; bone_index < bone_count; bone_index++) {
		const CollisionBoundsAccumulator &bone_bounds = bounds[bone_index];
		if (!bone_bounds.valid) {
			continue;
		}

		const Vector3 raw_size = bone_bounds.max - bone_bounds.min;
		const Vector3 box_size(
				MAX(raw_size.x, 0.05f),
				MAX(raw_size.y, 0.05f),
				MAX(raw_size.z, 0.05f));
		CollisionBodySpec body_spec;
		body_spec.bone = bone_index;
		body_spec.bone_name = _get_bone_track_name(p_model, bone_index);
		body_spec.name = vformat("Collision_%s", body_spec.bone_name.is_empty() ? vformat("Bone_%d", bone_index) : body_spec.bone_name);
		body_spec.source = "generated_bounds";
		body_spec.transform.origin = (bone_bounds.min + bone_bounds.max) * 0.5f;

		CollisionShapeSpec shape_spec;
		shape_spec.size = box_size;
		body_spec.shapes.push_back(shape_spec);
		specs.push_back(body_spec);
	}

	if (unweighted_bounds.valid) {
		const Vector3 raw_size = unweighted_bounds.max - unweighted_bounds.min;
		const Vector3 box_size(
				MAX(raw_size.x, 0.05f),
				MAX(raw_size.y, 0.05f),
				MAX(raw_size.z, 0.05f));
		CollisionBodySpec body_spec;
		body_spec.bone = -1;
		body_spec.name = "Collision_Unweighted";
		body_spec.source = "generated_bounds";
		body_spec.transform.origin = (unweighted_bounds.min + unweighted_bounds.max) * 0.5f;

		CollisionShapeSpec shape_spec;
		shape_spec.size = box_size;
		body_spec.shapes.push_back(shape_spec);
		specs.push_back(body_spec);
	}

	return specs;
}

Vector<CollisionBodySpec> _build_hitbox_collision_boxes(const mdlpp::StudioModel *p_model, int p_hitbox_set = 0) {
	Vector<CollisionBodySpec> specs;
	if (p_model == nullptr || p_hitbox_set < 0 || p_hitbox_set >= static_cast<int>(p_model->mdl.hitboxSets.size())) {
		return specs;
	}

	const mdlpp::MDL::HitboxSet &hitbox_set = p_model->mdl.hitboxSets[static_cast<size_t>(p_hitbox_set)];
	for (int hitbox_index = 0; hitbox_index < static_cast<int>(hitbox_set.hitboxes.size()); hitbox_index++) {
		const mdlpp::BBox &hitbox = hitbox_set.hitboxes[static_cast<size_t>(hitbox_index)];
		if (hitbox.bone < 0 || hitbox.bone >= static_cast<int>(p_model->mdl.bones.size())) {
			continue;
		}

		const Vector3 min = SourcePPUtils::source_vector_to_vector3(hitbox.bboxMin);
		const Vector3 max = SourcePPUtils::source_vector_to_vector3(hitbox.bboxMax);
		const Vector3 raw_size = max - min;
		const Vector3 box_size(
				MAX(Math::abs(raw_size.x), 0.05f),
				MAX(Math::abs(raw_size.y), 0.05f),
				MAX(Math::abs(raw_size.z), 0.05f));

		CollisionBodySpec body_spec;
		body_spec.bone = hitbox.bone;
		body_spec.bone_name = _get_bone_track_name(p_model, hitbox.bone);
		body_spec.name = hitbox.name.empty() ? vformat("Hitbox_%s_%d", body_spec.bone_name, hitbox_index) : String::utf8(hitbox.name.c_str());
		body_spec.source = "hitbox";
		body_spec.transform.origin = (min + max) * 0.5f;

		CollisionShapeSpec shape_spec;
		shape_spec.size = box_size;
		body_spec.shapes.push_back(shape_spec);
		specs.push_back(body_spec);
	}

	return specs;
}

int _append_collision_bodies(Node3D *p_root, Skeleton3D *p_skeleton, const Vector<CollisionBodySpec> &p_specs) {
	if (p_root == nullptr || p_skeleton == nullptr) {
		return 0;
	}

	int created_shape_count = 0;
	Vector<AnimatableBody3D *> created_bodies;
	for (int spec_index = 0; spec_index < p_specs.size(); spec_index++) {
		const CollisionBodySpec &spec = p_specs[spec_index];
		AnimatableBody3D *collision_body = memnew(AnimatableBody3D);
		collision_body->set_name(spec.bone >= 0 ? String("AnimatableBody3D") : String("CollisionBody3D"));
		collision_body->set_transform(spec.transform);
		collision_body->set_meta("sourcepp_collision_source", spec.source);
		collision_body->set_meta("sourcepp_collision_bone", spec.bone);
		collision_body->set_meta("sourcepp_collision_bone_name", spec.bone_name);

		for (int shape_index = 0; shape_index < spec.shapes.size(); shape_index++) {
			const CollisionShapeSpec &shape_spec = spec.shapes[shape_index];
			CollisionShape3D *collision_shape = memnew(CollisionShape3D);
			collision_shape->set_name(vformat("CollisionShape3D_%d", shape_index));
			collision_shape->set_transform(shape_spec.transform);
			Ref<BoxShape3D> box_shape;
			box_shape.instantiate();
			box_shape->set_size(shape_spec.size);
			collision_shape->set_shape(box_shape);
			collision_shape->set_meta("sourcepp_collision_shape_type", "box");
			collision_shape->set_meta("sourcepp_collision_shape_size", shape_spec.size);
			collision_body->add_child(collision_shape);
			created_shape_count++;
		}

		if (spec.bone >= 0) {
			BoneAttachment3D *collision_attachment = memnew(BoneAttachment3D);
			String attachment_name = spec.name.validate_node_name();
			if (attachment_name.is_empty()) {
				attachment_name = vformat("Collision_%d", spec_index);
			}
			collision_attachment->set_name(attachment_name);
			collision_attachment->set_bone_idx(spec.bone);
			collision_attachment->set_bone_name(spec.bone_name);
			collision_attachment->add_child(collision_body);
			p_skeleton->add_child(collision_attachment);
		} else {
			String body_name = spec.name.validate_node_name();
			if (!body_name.is_empty()) {
				collision_body->set_name(body_name);
			}
			p_root->add_child(collision_body);
		}

		for (int body_index = 0; body_index < created_bodies.size(); body_index++) {
			AnimatableBody3D *other_body = created_bodies[body_index];
			collision_body->add_collision_exception_with(other_body);
			other_body->add_collision_exception_with(collision_body);
		}
		created_bodies.push_back(collision_body);
	}

	return created_shape_count;
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

PackedByteArray SourcePPMDL::_to_packed_byte_array(const Vector<uint8_t> &p_data) {
	PackedByteArray out;
	out.resize(p_data.size());
	if (!p_data.is_empty()) {
		std::memcpy(out.ptrw(), p_data.ptr(), static_cast<size_t>(p_data.size()));
	}
	return out;
}

std::vector<std::byte> SourcePPMDL::_to_byte_vector(const Vector<uint8_t> &p_data) {
	std::vector<std::byte> out;
	out.resize(static_cast<size_t>(p_data.size()));
	if (!out.empty()) {
		std::memcpy(out.data(), p_data.ptr(), out.size());
	}
	return out;
}

String SourcePPMDL::_resolve_companion_path(const String &p_model_path, const PackedStringArray &p_candidates) const {
	const String base = p_model_path.get_basename();
	for (const String &suffix : p_candidates) {
		const String candidate = base + suffix;
		if (SourcePPUtils::path_exists_with_resolver(candidate, resolver, resolver_game_id)) {
			return candidate;
		}
	}
	return String();
}

String SourcePPMDL::_resolve_include_model_path(const String &p_owner_model_path, const std::string &p_include_name) const {
	String include_path = SourcePPUtils::normalize_source_path(String::utf8(p_include_name.c_str()));
	if (include_path.is_empty()) {
		return String();
	}
	if (!include_path.to_lower().ends_with(".mdl")) {
		include_path += ".mdl";
	}

	Vector<String> candidates;
	_append_unique_candidate(candidates, include_path);

	const String owner_dir = p_owner_model_path.get_base_dir();
	if (!owner_dir.is_empty()) {
		_append_unique_candidate(candidates, owner_dir.path_join(include_path.get_file()));
		_append_unique_candidate(candidates, owner_dir.path_join(include_path));
	}

	if (!include_path.begins_with("models/")) {
		_append_unique_candidate(candidates, String("models").path_join(include_path));
		_append_unique_candidate(candidates, String("models").path_join(include_path.get_file()));
	}

	for (int i = 0; i < candidates.size(); i++) {
		if (SourcePPUtils::path_exists_with_resolver(candidates[i], resolver, resolver_game_id)) {
			return candidates[i];
		}
	}

	return String();
}

String SourcePPMDL::_resolve_material_path(const String &p_material_name) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, String(), "SourcePPMDL must be opened before use.");

	const String normalized_material = SourcePPUtils::normalize_source_path(p_material_name);
	if (normalized_material.is_empty()) {
		return String();
	}

	const String material_without_prefix = _strip_prefix(normalized_material, "materials/");
	const String material_base = _strip_material_extension(material_without_prefix);
	Vector<String> candidates;
	_append_unique_candidate(candidates, SourcePPUtils::ensure_extension(material_without_prefix, "vmt"));
	_append_unique_candidate(candidates, "materials/" + SourcePPUtils::ensure_extension(material_without_prefix, "vmt"));

	for (const std::string &directory_raw : get_model()->mdl.materialDirectories) {
		String directory = SourcePPUtils::normalize_source_path(_from_utf8(directory_raw));
		if (directory.ends_with("/")) {
			directory = directory.substr(0, directory.length() - 1);
		}
		directory = _strip_prefix(directory, "materials/");
		if (directory.is_empty()) {
			continue;
		}
		_append_unique_candidate(candidates, directory.path_join(SourcePPUtils::ensure_extension(material_base, "vmt")));
		_append_unique_candidate(candidates, String("materials/") + directory.path_join(SourcePPUtils::ensure_extension(material_base, "vmt")));
	}

	const String normalized_mdl_path = SourcePPUtils::normalize_source_path(mdl_path);
	const int models_index = normalized_mdl_path.find("/models/") >= 0 ? normalized_mdl_path.find("/models/") : (normalized_mdl_path.begins_with("models/") ? 0 : -1);
	const String game_root = models_index > 0 ? normalized_mdl_path.substr(0, models_index) : String();

	for (int candidate_index = 0; candidate_index < candidates.size(); candidate_index++) {
		const String &candidate = candidates[candidate_index];
		if (SourcePPUtils::path_exists_with_resolver(candidate, resolver, resolver_game_id)) {
			return candidate;
		}
		if (!game_root.is_empty()) {
			const String absolute_candidate = game_root.path_join(candidate);
			if (SourcePPUtils::path_exists_with_resolver(absolute_candidate, resolver, resolver_game_id)) {
				return absolute_candidate;
			}
		}
	}

	return String();
}

Ref<Material> SourcePPMDL::_create_import_material(int p_material_index, int p_skin_family, SourcePPImportCache *p_import_cache, HashMap<String, Ref<Material>> *r_material_cache) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Ref<Material>(), "SourcePPMDL must be opened before use.");

	int resolved_material_index = p_material_index;
	if (!get_model()->mdl.skins.empty() && p_skin_family >= 0 && p_skin_family < static_cast<int>(get_model()->mdl.skins.size())) {
		const std::vector<int16_t> &skin_family = get_model()->mdl.skins[static_cast<size_t>(p_skin_family)];
		if (resolved_material_index >= 0 && resolved_material_index < static_cast<int>(skin_family.size())) {
			resolved_material_index = skin_family[static_cast<size_t>(resolved_material_index)];
		}
	}

	const String material_cache_key = vformat("%d|%d", p_skin_family, resolved_material_index);
	if (r_material_cache != nullptr && r_material_cache->has(material_cache_key)) {
		return (*r_material_cache)[material_cache_key];
	}

	String material_name;
	if (resolved_material_index >= 0 && resolved_material_index < static_cast<int>(get_model()->mdl.materials.size())) {
		material_name = _from_utf8(get_model()->mdl.materials[static_cast<size_t>(resolved_material_index)].name);
	}

	const String material_path = _resolve_material_path(material_name);
	Ref<Material> material;
	if (!material_path.is_empty()) {
		Error vmt_error = OK;
		Ref<SourcePPVMT> vmt;
		if (p_import_cache != nullptr) {
			vmt = p_import_cache->get_vmt(material_path, resolver, resolver_game_id, &vmt_error);
		} else {
			vmt.instantiate();
			vmt->set_resolver(resolver);
			vmt->set_resolver_game_id(resolver_game_id);
			vmt_error = vmt->open(material_path);
		}
		if (vmt_error == OK && vmt.is_valid()) {
			material = vmt->create_material();
		}
	}

	if (material.is_null()) {
		Ref<StandardMaterial3D> placeholder;
		placeholder.instantiate();
		material = placeholder;
	}

	material->set_meta("sourcepp_mdl_material_index", resolved_material_index);
	material->set_meta("sourcepp_mdl_skin_family", p_skin_family);
	material->set_meta("sourcepp_mdl_material_name", material_name);
	material->set_meta("sourcepp_mdl_material_path", material_path);
	if (r_material_cache != nullptr) {
		r_material_cache->insert(material_cache_key, material);
	}
	return material;
}

Vector<uint8_t> SourcePPMDL::_read_file_bytes(const String &p_path, Error *r_error) const {
	Error error = OK;
	Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &error);
	if (error == OK) {
		if (r_error != nullptr) {
			*r_error = OK;
		}
		return bytes;
	}

	if (resolver.is_valid()) {
		const PackedByteArray resolved = resolver_game_id.is_empty() ? resolver->read_file(p_path) : resolver->read_file(p_path, resolver_game_id);
		const bool has_resolved_file = resolver_game_id.is_empty() ? resolver->has_file(p_path) : resolver->has_file(p_path, resolver_game_id);
		if (!resolved.is_empty() || has_resolved_file) {
			if (r_error != nullptr) {
				*r_error = OK;
			}
			return _packed_to_vector(resolved);
		}
	}

	if (r_error != nullptr) {
		*r_error = error == OK ? ERR_FILE_NOT_FOUND : error;
	}
	return Vector<uint8_t>();
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

Error SourcePPMDL::_load_included_models_recursive(const String &p_model_path, const mdlpp::StudioModel &p_source_model, std::unordered_set<std::string> &r_seen_paths, int p_depth) {
	static constexpr int MAX_INCLUDE_DEPTH = 32;
	if (p_depth >= MAX_INCLUDE_DEPTH) {
		return ERR_CYCLIC_LINK;
	}

	for (const mdlpp::MDL::IncludeModel &include_model : p_source_model.mdl.includeModels) {
		const String include_path = _resolve_include_model_path(p_model_path, include_model.name);
		if (include_path.is_empty()) {
			WARN_PRINT(vformat("Could not resolve included MDL '%s' referenced by '%s'.", String::utf8(include_model.name.c_str()), p_model_path));
			continue;
		}

		const String normalized_path = SourcePPUtils::normalize_source_path(include_path);
		const std::string key = _to_utf8(normalized_path.to_lower());
		if (r_seen_paths.contains(key)) {
			continue;
		}
		r_seen_paths.insert(key);

		Error mdl_error = OK;
		const Vector<uint8_t> mdl_data = _read_file_bytes(normalized_path, &mdl_error);
		if (mdl_error != OK) {
			WARN_PRINT(vformat("Could not read included MDL '%s'.", normalized_path));
			continue;
		}

		auto included_model = std::make_unique<mdlpp::StudioModel>();
		if (!included_model->openMDLOnly(_to_byte_vector(mdl_data))) {
			WARN_PRINT(vformat("Could not parse included MDL '%s'.", normalized_path));
			continue;
		}

		const String anim_block_path = _resolve_anim_block_path(normalized_path, included_model->mdl, resolver, resolver_game_id);
		if (!anim_block_path.is_empty()) {
			Error anim_block_error = OK;
			const Vector<uint8_t> anim_block_data = _read_file_bytes(anim_block_path, &anim_block_error);
			if (anim_block_error == OK) {
				included_model->setAnimBlockData(_to_byte_vector(anim_block_data));
			} else {
				WARN_PRINT(vformat("Could not read included MDL animation block '%s'.", anim_block_path));
			}
		}

		mdlpp::StudioModel *included_model_ptr = included_model.get();
		included_models.push_back(std::move(included_model));
		included_model_paths.push_back(normalized_path);

		const Error recurse_error = _load_included_models_recursive(normalized_path, *included_model_ptr, r_seen_paths, p_depth + 1);
		if (recurse_error != OK) {
			return recurse_error;
		}
	}

	return OK;
}

Error SourcePPMDL::_get_baked_model(int p_lod, mdlpp::BakedModel &r_baked_model) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, ERR_INVALID_PARAMETER, "SourcePPMDL must be opened before use.");
	ERR_FAIL_COND_V_MSG(p_lod < 0 || p_lod >= get_model()->vtx.numLODs, ERR_INVALID_PARAMETER, "Requested LOD is out of range.");

	r_baked_model = get_model()->processModelData(p_lod);
	return OK;
}

Error SourcePPMDL::open(const String &p_mdl_path, const String &p_vtx_path, const String &p_vvd_path) {
	close();
	ERR_FAIL_COND_V_MSG(p_mdl_path.is_empty(), ERR_INVALID_PARAMETER, "MDL path must not be empty.");

	const String resolved_vtx_path = p_vtx_path.is_empty() ? _resolve_companion_path(p_mdl_path, PackedStringArray{".dx90.vtx", ".vtx", ".dx80.vtx", ".sw.vtx", ".dx11.vtx"}) : p_vtx_path;
	const String resolved_vvd_path = p_vvd_path.is_empty() ? _resolve_companion_path(p_mdl_path, PackedStringArray{".vvd"}) : p_vvd_path;
	ERR_FAIL_COND_V_MSG(resolved_vtx_path.is_empty(), ERR_FILE_NOT_FOUND, "Could not resolve a companion VTX file for the requested MDL.");
	ERR_FAIL_COND_V_MSG(resolved_vvd_path.is_empty(), ERR_FILE_NOT_FOUND, "Could not resolve a companion VVD file for the requested MDL.");

	Error mdl_error = OK;
	Error vtx_error = OK;
	Error vvd_error = OK;
	const Vector<uint8_t> mdl_data = _read_file_bytes(p_mdl_path, &mdl_error);
	const Vector<uint8_t> vtx_data = _read_file_bytes(resolved_vtx_path, &vtx_error);
	const Vector<uint8_t> vvd_data = _read_file_bytes(resolved_vvd_path, &vvd_error);
	ERR_FAIL_COND_V_MSG(mdl_error != OK, mdl_error, "Failed to load the MDL file.");
	ERR_FAIL_COND_V_MSG(vtx_error != OK, vtx_error, "Failed to load the VTX companion file.");
	ERR_FAIL_COND_V_MSG(vvd_error != OK, vvd_error, "Failed to load the VVD companion file.");

	const Error open_error = _open_bytes(mdl_data, vtx_data, vvd_data);
	if (open_error != OK) {
		close();
		return open_error;
	}

	const String anim_block_path = _resolve_anim_block_path(p_mdl_path, model->mdl, resolver, resolver_game_id);
	if (!anim_block_path.is_empty()) {
		Error anim_block_error = OK;
		const Vector<uint8_t> anim_block_data = _read_file_bytes(anim_block_path, &anim_block_error);
		if (anim_block_error != OK) {
			close();
			return anim_block_error;
		}
		model->setAnimBlockData(_to_byte_vector(anim_block_data));
		anim_block_data_cache = anim_block_data;
	} else {
		anim_block_data_cache.clear();
	}

	std::unordered_set<std::string> seen_include_paths;
	seen_include_paths.insert(_to_utf8(SourcePPUtils::normalize_source_path(p_mdl_path).to_lower()));
	const Error include_error = _load_included_models_recursive(p_mdl_path, *model, seen_include_paths, 0);
	if (include_error != OK) {
		close();
		return include_error;
	}

	mdl_data_cache = mdl_data;
	vtx_data_cache = vtx_data;
	vvd_data_cache = vvd_data;
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

	mdl_data_cache = p_mdl_data;
	vtx_data_cache = p_vtx_data;
	vvd_data_cache = p_vvd_data;
	anim_block_data_cache = p_anim_block_data;
	return OK;
}

void SourcePPMDL::close() {
	model.reset();
	included_models.clear();
	included_model_paths.clear();
	mdl_path = String();
	vtx_path = String();
	vvd_path = String();
	mdl_data_cache.clear();
	vtx_data_cache.clear();
	vvd_data_cache.clear();
	anim_block_data_cache.clear();
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

Array SourcePPMDL::get_skeleton_bones() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Array(), "SourcePPMDL must be opened before use.");

	Array out;
	for (int bone_index = 0; bone_index < static_cast<int>(get_model()->mdl.bones.size()); bone_index++) {
		const mdlpp::MDL::Bone &bone = get_model()->mdl.bones[static_cast<size_t>(bone_index)];
		Dictionary bone_info;
		bone_info["index"] = bone_index;
		bone_info["name"] = _get_bone_track_name(get_model(), bone_index);
		bone_info["parent"] = bone.parent;
		bone_info["rest"] = _to_bone_rest(bone);
		bone_info["enabled"] = true;
		out.push_back(bone_info);
	}
	return out;
}

int SourcePPMDL::get_bone_controller_count() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, 0, "SourcePPMDL must be opened before use.");
	return static_cast<int>(get_model()->mdl.boneControllers.size());
}

Array SourcePPMDL::get_bone_controllers() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Array(), "SourcePPMDL must be opened before use.");

	Array out;
	for (const mdlpp::MDL::BoneController &controller : get_model()->mdl.boneControllers) {
		out.push_back(_make_bone_controller_info(get_model(), controller));
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
		attachment_info["transform"] = SourcePPUtils::source_matrix_to_transform_3d(attachment.local);
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
		Array sections;
		if (anim_desc.sectionIndex > 0 && anim_desc.sectionFrames > 0) {
			const auto &mdl_data = get_model()->getMDLData();
			const int section_count = (anim_desc.frameCount / anim_desc.sectionFrames) + 2;
			for (int section_index = 0; section_index < section_count; section_index++) {
				const uint64_t section_offset = anim_desc.fileOffset + static_cast<uint64_t>(anim_desc.sectionIndex) + sizeof(SourceAnimSectionInfo) * static_cast<uint64_t>(section_index);
				if (section_offset + sizeof(SourceAnimSectionInfo) > mdl_data.size()) {
					break;
				}
				SourceAnimSectionInfo section_info;
				std::memcpy(&section_info, mdl_data.data() + section_offset, sizeof(SourceAnimSectionInfo));
				Dictionary section_data;
				section_data["section"] = section_index;
				section_data["anim_block"] = section_info.anim_block;
				section_data["anim_index"] = section_info.anim_index;
				sections.push_back(section_data);
			}
		}
		anim_desc_info["sections"] = sections;

		Array movements;
		for (const mdlpp::Movement &movement : anim_desc.movements) {
			Dictionary movement_info;
			movement_info["end_frame"] = movement.endFrame;
			movement_info["flags"] = static_cast<int>(movement.flags);
			movement_info["velocity_start"] = movement.velocityStart;
			movement_info["velocity_end"] = movement.velocityEnd;
			movement_info["yaw_end"] = movement.yawEnd;
			movement_info["movement"] = SourcePPUtils::source_vector_to_vector3(movement.movement);
			movement_info["relative_position"] = SourcePPUtils::source_vector_to_vector3(movement.relativePosition);
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
			positions.set(i, SourcePPUtils::source_vector_to_vector3(track.positions[static_cast<size_t>(i)]));
		}
		track_info["positions"] = positions;

		Array rotations;
		rotations.resize(static_cast<int>(track.rotations.size()));
		for (int i = 0; i < rotations.size(); i++) {
			rotations[i] = SourcePPUtils::source_quaternion_to_quaternion(track.rotations[static_cast<size_t>(i)]);
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
		sequence_desc_info["bounding_box_min"] = SourcePPUtils::source_vector_to_vector3(sequence_desc.boundingBoxMin);
		sequence_desc_info["bounding_box_max"] = SourcePPUtils::source_vector_to_vector3(sequence_desc.boundingBoxMax);
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
			animation->position_track_insert_key(position_track, time, SourcePPUtils::source_vector_to_vector3(track.positions[static_cast<size_t>(frame)]));
			animation->rotation_track_insert_key(rotation_track, time, SourcePPUtils::source_quaternion_to_quaternion(track.rotations[static_cast<size_t>(frame)]));
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

	int surface_count = 0;
	for (const mdlpp::BakedModel::Mesh &mesh : baked_model.meshes) {
		if (!mesh.indices.empty()) {
			surface_count++;
		}
	}
	return surface_count;
}

PackedInt32Array SourcePPMDL::get_surface_material_indices(int p_lod) const {
	mdlpp::BakedModel baked_model;
	ERR_FAIL_COND_V(_get_baked_model(p_lod, baked_model) != OK, PackedInt32Array());

	PackedInt32Array out;
	for (const mdlpp::BakedModel::Mesh &mesh : baked_model.meshes) {
		if (mesh.indices.empty()) {
			continue;
		}
		out.push_back(mesh.materialIndex);
	}
	return out;
}

PackedStringArray SourcePPMDL::get_surface_materials(int p_lod) const {
	mdlpp::BakedModel baked_model;
	ERR_FAIL_COND_V(_get_baked_model(p_lod, baked_model) != OK, PackedStringArray());

	PackedStringArray out;
	for (const mdlpp::BakedModel::Mesh &mesh : baked_model.meshes) {
		if (mesh.indices.empty()) {
			continue;
		}
		if (!mesh.materialName.empty()) {
			out.push_back(_from_utf8(mesh.materialName));
		} else if (mesh.materialIndex >= 0 && mesh.materialIndex < static_cast<int>(get_model()->mdl.materials.size())) {
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

	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedFloat32Array tangents;
	PackedInt32Array bones;
	PackedFloat32Array weights;
	vertices.resize(static_cast<int>(baked_model.vertices.size()));
	normals.resize(static_cast<int>(baked_model.vertices.size()));
	uvs.resize(static_cast<int>(baked_model.vertices.size()));
	tangents.resize(static_cast<int>(baked_model.vertices.size()) * 4);
	bones.resize(static_cast<int>(baked_model.vertices.size()) * 4);
	weights.resize(static_cast<int>(baked_model.vertices.size()) * 4);

	for (int i = 0; i < vertices.size(); i++) {
		const mdlpp::BakedModel::Vertex &vertex = baked_model.vertices[static_cast<size_t>(i)];
		vertices.write[i] = Vector3(vertex.position[0], vertex.position[1], vertex.position[2]);
		normals.write[i] = Vector3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
		uvs.write[i] = Vector2(vertex.uv[0], vertex.uv[1]);
		tangents.write[i * 4 + 0] = vertex.tangent[0];
		tangents.write[i * 4 + 1] = vertex.tangent[1];
		tangents.write[i * 4 + 2] = vertex.tangent[2];
		tangents.write[i * 4 + 3] = vertex.tangent[3];
		for (int weight_index = 0; weight_index < 4; weight_index++) {
			bones.write[i * 4 + weight_index] = vertex.bones[static_cast<size_t>(weight_index)];
			weights.write[i * 4 + weight_index] = vertex.weights[static_cast<size_t>(weight_index)];
		}
	}

	for (int mesh_index = 0; mesh_index < static_cast<int>(baked_model.meshes.size()); mesh_index++) {
		const mdlpp::BakedModel::Mesh &baked_mesh = baked_model.meshes[static_cast<size_t>(mesh_index)];
		if (baked_mesh.indices.empty()) {
			continue;
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_TANGENT] = tangents;
		arrays[Mesh::ARRAY_TEX_UV] = uvs;
		arrays[Mesh::ARRAY_BONES] = bones;
		arrays[Mesh::ARRAY_WEIGHTS] = weights;

		PackedInt32Array indices;
		indices.resize(static_cast<int>(baked_mesh.indices.size()));
		for (int i = 0; i < indices.size(); i++) {
			indices.set(i, static_cast<int>(baked_mesh.indices[static_cast<size_t>(i)]));
		}
		arrays[Mesh::ARRAY_INDEX] = indices;

		const int surface_index = mesh->get_surface_count();
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		if (!baked_mesh.materialName.empty()) {
			mesh->surface_set_name(surface_index, _from_utf8(baked_mesh.materialName));
		} else if (baked_mesh.materialIndex >= 0 && baked_mesh.materialIndex < static_cast<int>(get_model()->mdl.materials.size())) {
			mesh->surface_set_name(surface_index, _from_utf8(get_model()->mdl.materials[static_cast<size_t>(baked_mesh.materialIndex)].name));
		}
	}

	return mesh;
}

Ref<Skin> SourcePPMDL::create_skin() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, Ref<Skin>(), "SourcePPMDL must be opened before use.");

	Ref<Skin> skin;
	skin.instantiate();
	const int bone_count = static_cast<int>(get_model()->mdl.bones.size());
	skin->set_bind_count(bone_count);

	const Vector<Transform3D> global_rests = _build_global_bone_rests(get_model());
	for (int bone_index = 0; bone_index < bone_count; bone_index++) {
		skin->set_bind_bone(bone_index, bone_index);
		skin->set_bind_name(bone_index, StringName(_get_bone_track_name(get_model(), bone_index)));
		skin->set_bind_pose(bone_index, global_rests[bone_index].affine_inverse());
	}

	return skin;
}

Skeleton3D *SourcePPMDL::create_skeleton() const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, nullptr, "SourcePPMDL must be opened before use.");

	Skeleton3D *skeleton = memnew(Skeleton3D);
	for (int bone_index = 0; bone_index < static_cast<int>(get_model()->mdl.bones.size()); bone_index++) {
		skeleton->add_bone(_get_bone_track_name(get_model(), bone_index));
	}

	for (int bone_index = 0; bone_index < static_cast<int>(get_model()->mdl.bones.size()); bone_index++) {
		const mdlpp::MDL::Bone &bone = get_model()->mdl.bones[static_cast<size_t>(bone_index)];
		if (bone.parent >= 0 && bone.parent < static_cast<int>(get_model()->mdl.bones.size())) {
			skeleton->set_bone_parent(bone_index, bone.parent);
		}
		skeleton->set_bone_rest(bone_index, _to_bone_rest(bone));
	}
	skeleton->reset_bone_poses();
	return skeleton;
}

Node3D *SourcePPMDL::create_model_node(int p_skin_family, bool p_include_attachments, bool p_include_collision) const {
	ERR_FAIL_COND_V_MSG(get_model() == nullptr, nullptr, "SourcePPMDL must be opened before use.");
	ERR_FAIL_COND_V_MSG(p_skin_family < 0, nullptr, "Skin family must be non-negative.");
	ERR_FAIL_COND_V_MSG(!get_model()->mdl.skins.empty() && p_skin_family >= static_cast<int>(get_model()->mdl.skins.size()), nullptr, "Requested skin family is out of range.");

	Ref<Skin> skin = create_skin();
	ERR_FAIL_COND_V_MSG(skin.is_null(), nullptr, "Failed to create the imported skin.");

	Skeleton3D *skeleton = create_skeleton();
	ERR_FAIL_NULL_V_MSG(skeleton, nullptr, "Failed to create the imported skeleton.");
	skeleton->set_name("Skeleton3D");

	Ref<SourceMDLAnimationData> animation_data;
	animation_data.instantiate();
	animation_data->set_mdl_path(mdl_path);
	std::vector<const mdlpp::StudioModel *> included_model_ptrs;
	included_model_ptrs.reserve(included_models.size());
	for (const std::unique_ptr<mdlpp::StudioModel> &included_model : included_models) {
		included_model_ptrs.push_back(included_model.get());
	}
	const Error animation_bake_error = animation_data->bake_from_studio_models(*get_model(), included_model_ptrs);
	if (animation_bake_error != OK) {
		memdelete(skeleton);
		ERR_FAIL_V_MSG(nullptr, "Failed to bake Source animation data for the imported model.");
	}
	skeleton->reset_bone_poses();

	Node3D *root = memnew(Node3D);
	root->set_name(_build_scene_name(get_name(), mdl_path));
	root->set_rotation(Vector3(SOURCE_IMPORT_ROTATION_X, 0.0f, 0.0f));
	root->set_scale(Vector3(SourcePPUtils::SOURCE_UNIT_TO_METERS, SourcePPUtils::SOURCE_UNIT_TO_METERS, SourcePPUtils::SOURCE_UNIT_TO_METERS));

	const int lod_count = MAX(get_lod_count(), 1);
	float visibility_step = 20.0f;
	float visibility_margin = 2.0f;
	int collision_shape_count = 0;
	int collision_body_count = 0;
	String collision_source = "none";
	String phy_collision_path;
	std::unordered_map<int, BoneAttachment3D *> rigid_mesh_attachments;
	SourcePPImportCache import_cache;
	HashMap<String, Ref<Material>> material_cache;

	auto get_or_create_rigid_mesh_attachment = [&](int p_bone) -> BoneAttachment3D * {
		auto existing = rigid_mesh_attachments.find(p_bone);
		if (existing != rigid_mesh_attachments.end()) {
			return existing->second;
		}

		BoneAttachment3D *attachment = memnew(BoneAttachment3D);
		String attachment_name = vformat("RigidMesh_%s", _get_bone_track_name(get_model(), p_bone));
		attachment_name = attachment_name.validate_node_name();
		if (attachment_name.is_empty()) {
			attachment_name = vformat("RigidMesh_%d", p_bone);
		}
		attachment->set_name(attachment_name);
		attachment->set_bone_idx(p_bone);
		attachment->set_bone_name(_get_bone_track_name(get_model(), p_bone));
		skeleton->add_child(attachment);
		rigid_mesh_attachments.emplace(p_bone, attachment);
		return attachment;
	};

	for (int lod_index = 0; lod_index < lod_count; lod_index++) {
		mdlpp::BakedModel baked_model;
		ERR_FAIL_COND_V_MSG(_get_baked_model(lod_index, baked_model) != OK, nullptr, "Failed to bake one of the imported LOD meshes.");
		const ModelBindingInfo binding_info = _get_model_binding_info(get_model(), baked_model);

		Ref<ArrayMesh> mesh = create_mesh(lod_index);
		ERR_FAIL_COND_V_MSG(mesh.is_null(), nullptr, "Failed to create one of the imported LOD meshes.");

		if (lod_index == 0) {
			const AABB mesh_aabb = mesh->get_aabb();
			const float size_metric = MAX(MAX(mesh_aabb.size.x, mesh_aabb.size.y), mesh_aabb.size.z);
			visibility_step = MAX(size_metric * 8.0f, 20.0f);
			visibility_margin = MAX(visibility_step * 0.15f, 2.0f);
		}

		MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_name(vformat("MeshInstance3D_LOD%d", lod_index));
		mesh_instance->set_mesh(mesh);
		mesh_instance->set_meta("sourcepp_mdl_lod", lod_index);
		mesh_instance->set_meta("sourcepp_mesh_binding_bone", binding_info.bone);
		switch (binding_info.mode) {
			case ModelBindingMode::SKINNED: {
				mesh_instance->set_skin(skin);
				mesh_instance->set_skeleton_path(NodePath("../Skeleton3D"));
				mesh_instance->set_meta("sourcepp_mesh_binding_mode", "skinned");
			} break;
			case ModelBindingMode::RIGID_BONE: {
				mesh_instance->set_meta("sourcepp_mesh_binding_mode", "rigid_bone");
			} break;
			case ModelBindingMode::MODEL_SPACE:
			default: {
				mesh_instance->set_meta("sourcepp_mesh_binding_mode", "model_space");
			} break;
		}

		const PackedInt32Array surface_material_indices = get_surface_material_indices(lod_index);
		for (int surface_index = 0; surface_index < mesh->get_surface_count() && surface_index < surface_material_indices.size(); surface_index++) {
			mesh_instance->set_surface_override_material(surface_index, _create_import_material(surface_material_indices[surface_index], p_skin_family, &import_cache, &material_cache));
		}

		if (lod_count > 1) {
			const float range_begin = lod_index == 0 ? 0.0f : visibility_step * static_cast<float>(lod_index);
			const float range_end = lod_index + 1 < lod_count ? visibility_step * static_cast<float>(lod_index + 1) : 0.0f;
			mesh_instance->set_visibility_range_begin(range_begin);
			mesh_instance->set_visibility_range_end(range_end);
			mesh_instance->set_visibility_range_begin_margin(lod_index == 0 ? 0.0f : visibility_margin);
			mesh_instance->set_visibility_range_end_margin(lod_index + 1 < lod_count ? visibility_margin : 0.0f);
			mesh_instance->set_visibility_range_fade_mode(GeometryInstance3D::VISIBILITY_RANGE_FADE_DISABLED);
		}

		if (binding_info.mode == ModelBindingMode::RIGID_BONE && binding_info.bone >= 0) {
			get_or_create_rigid_mesh_attachment(binding_info.bone)->add_child(mesh_instance);
		} else {
			root->add_child(mesh_instance);
		}
	}

	if (p_include_attachments) {
		for (int attachment_index = 0; attachment_index < static_cast<int>(get_model()->mdl.attachments.size()); attachment_index++) {
			const mdlpp::MDL::Attachment &attachment = get_model()->mdl.attachments[static_cast<size_t>(attachment_index)];
			BoneAttachment3D *attachment_node = memnew(BoneAttachment3D);
			String attachment_name = _from_utf8(attachment.name);
			if (attachment_name.is_empty()) {
				attachment_name = vformat("Attachment_%d", attachment_index);
			}
			attachment_node->set_name(attachment_name.validate_node_name());
			attachment_node->set_bone_idx(attachment.bone);
			attachment_node->set_bone_name(_get_bone_track_name(get_model(), attachment.bone));
			attachment_node->set_transform(SourcePPUtils::source_matrix_to_transform_3d(attachment.local));
			skeleton->add_child(attachment_node);
		}
	}

	if (p_include_collision) {
		Vector<CollisionBodySpec> collision_specs;
		collision_specs = _build_hitbox_collision_boxes(get_model());
		if (!collision_specs.is_empty()) {
			collision_source = "hitbox";
		} else {
			mdlpp::BakedModel baked_model;
			if (_get_baked_model(0, baked_model) == OK) {
				collision_specs = _build_generated_collision_boxes(get_model(), baked_model);
				if (!collision_specs.is_empty()) {
					collision_source = "generated_bounds";
				}
			}
		}

		if (!collision_specs.is_empty()) {
			collision_body_count = collision_specs.size();
			collision_shape_count = _append_collision_bodies(root, skeleton, collision_specs);
		}
	}

	root->add_child(skeleton);
	root->set_meta("sourcepp_mdl_skin_family", p_skin_family);
	root->set_meta("sourcepp_mdl_lod_count", lod_count);
	root->set_meta("sourcepp_mdl_visibility_range_step", visibility_step);
	root->set_meta("sourcepp_mdl_visibility_range_margin", visibility_margin);
	root->set_meta("sourcepp_mdl_path", mdl_path);
	root->set_meta("sourcepp_vtx_path", vtx_path);
	root->set_meta("sourcepp_vvd_path", vvd_path);
	root->set_meta("sourcepp_included_mdl_paths", included_model_paths);
	root->set_meta("sourcepp_phy_path", phy_collision_path);
	root->set_meta("sourcepp_materials", get_materials());
	root->set_meta("sourcepp_animation_data", animation_data);
	root->set_meta("sourcepp_sequences", animation_data->get_sequence_names());
	root->set_meta("sourcepp_collision_source", collision_source);
	root->set_meta("sourcepp_collision_body_count", collision_body_count);
	root->set_meta("sourcepp_collision_shape_count", collision_shape_count);

	Ref<Resource> script_player_script = ResourceLoader::load(SOURCE_SCRIPT_ANIM_PLAYER_PATH, "Script");
	if (script_player_script.is_null()) {
		memdelete(root);
		ERR_FAIL_V_MSG(nullptr, vformat("Failed to load SourceScriptAnimPlayer script at %s.", SOURCE_SCRIPT_ANIM_PLAYER_PATH));
	}

	Node *anim_player = memnew(Node);
	anim_player->set_name("SourceScriptAnimPlayer");
	anim_player->set_script(script_player_script);
	root->add_child(anim_player);
	const Variant setup_result = anim_player->call("setup_from_imported_root", root);
	if (setup_result.get_type() != Variant::BOOL || !bool(setup_result)) {
		memdelete(root);
		ERR_FAIL_V_MSG(nullptr, "Failed to initialize SourceScriptAnimPlayer for the imported model.");
	}
	anim_player->set("sequence_descriptor", -1);
	return root;
}

void SourcePPMDL::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resolver", "resolver"), &SourcePPMDL::set_resolver);
	ClassDB::bind_method(D_METHOD("get_resolver"), &SourcePPMDL::get_resolver);
	ClassDB::bind_method(D_METHOD("set_resolver_game_id", "game_id"), &SourcePPMDL::set_resolver_game_id);
	ClassDB::bind_method(D_METHOD("get_resolver_game_id"), &SourcePPMDL::get_resolver_game_id);
	ClassDB::bind_method(D_METHOD("open", "mdl_path", "vtx_path", "vvd_path"), &SourcePPMDL::open, DEFVAL(String()), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("open_from_buffer", "mdl_data", "vtx_data", "vvd_data", "anim_block_data"), &SourcePPMDL::open_from_buffer, DEFVAL(PackedByteArray()));
	ClassDB::bind_method(D_METHOD("close"), &SourcePPMDL::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPMDL::is_open);

	ClassDB::bind_method(D_METHOD("get_name"), &SourcePPMDL::get_name);
	ClassDB::bind_method(D_METHOD("get_version"), &SourcePPMDL::get_version);
	ClassDB::bind_method(D_METHOD("get_checksum"), &SourcePPMDL::get_checksum);
	ClassDB::bind_method(D_METHOD("get_included_model_paths"), &SourcePPMDL::get_included_model_paths);
	ClassDB::bind_method(D_METHOD("get_lod_count"), &SourcePPMDL::get_lod_count);
	ClassDB::bind_method(D_METHOD("get_mdl_path"), &SourcePPMDL::get_mdl_path);
	ClassDB::bind_method(D_METHOD("get_vtx_path"), &SourcePPMDL::get_vtx_path);
	ClassDB::bind_method(D_METHOD("get_vvd_path"), &SourcePPMDL::get_vvd_path);

	ClassDB::bind_method(D_METHOD("get_materials"), &SourcePPMDL::get_materials);
	ClassDB::bind_method(D_METHOD("get_material_directories"), &SourcePPMDL::get_material_directories);
	ClassDB::bind_method(D_METHOD("get_skin_families"), &SourcePPMDL::get_skin_families);
	ClassDB::bind_method(D_METHOD("get_bone_names"), &SourcePPMDL::get_bone_names);
	ClassDB::bind_method(D_METHOD("get_skeleton_bones"), &SourcePPMDL::get_skeleton_bones);
	ClassDB::bind_method(D_METHOD("get_bone_controller_count"), &SourcePPMDL::get_bone_controller_count);
	ClassDB::bind_method(D_METHOD("get_bone_controllers"), &SourcePPMDL::get_bone_controllers);
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
	ClassDB::bind_method(D_METHOD("create_skin"), &SourcePPMDL::create_skin);
	ClassDB::bind_method(D_METHOD("create_skeleton"), &SourcePPMDL::create_skeleton);
	ClassDB::bind_method(D_METHOD("create_model_node", "skin_family", "include_attachments", "include_collision"), &SourcePPMDL::create_model_node, DEFVAL(0), DEFVAL(true), DEFVAL(true));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "resolver", PROPERTY_HINT_RESOURCE_TYPE, "SourcePPResolver"), "set_resolver", "get_resolver");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "resolver_game_id"), "set_resolver_game_id", "get_resolver_game_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "attachment_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_attachment_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "animation_descriptor_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_animation_descriptor_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bone_controller_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_bone_controller_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "checksum", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_checksum");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "hitbox_set_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_hitbox_set_count");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "included_model_paths", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_included_model_paths");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_lod_count");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "mdl_path", PROPERTY_HINT_FILE, "*.mdl", PROPERTY_USAGE_READ_ONLY), "", "get_mdl_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sequence_descriptor_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_sequence_descriptor_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_READ_ONLY), "", "get_version");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "vtx_path", PROPERTY_HINT_FILE, "*.vtx,*.dx90.vtx,*.dx80.vtx,*.sw.vtx,*.dx11.vtx", PROPERTY_USAGE_READ_ONLY), "", "get_vtx_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "vvd_path", PROPERTY_HINT_FILE, "*.vvd", PROPERTY_USAGE_READ_ONLY), "", "get_vvd_path");
}
