/**************************************************************************/
/*  sourcepp_bsp_entity_utils.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp_entity_utils.h"

#include "sourcepp_bsp_entity.h"

#include "modules/sourcepp/bsp/sourcepp_bsp_geometry.h"
#include "modules/sourcepp/sourcepp_bsp_shape.h"
#include "modules/sourcepp/utils/sourcepp_utils.h"

#include "core/error/error_macros.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/animatable_body_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/mesh.h"

#include <string_view>

namespace {

String _sourcepp_string_from_utf8(std::string_view p_string) {
	return String::utf8(p_string.data(), static_cast<int>(p_string.size()));
}

String _dict_string(const Dictionary &p_dict, const StringName &p_key, const String &p_default = String()) {
	const String key = String(p_key);
	return p_dict.has(key) ? String(p_dict[key]) : p_default;
}

double _dict_float(const Dictionary &p_dict, const StringName &p_key, double p_default = 0.0) {
	const String key = String(p_key);
	return p_dict.has(key) ? static_cast<double>(p_dict[key]) : p_default;
}

int _dict_int(const Dictionary &p_dict, const StringName &p_key, int p_default = 0) {
	const String key = String(p_key);
	return p_dict.has(key) ? static_cast<int>(p_dict[key]) : p_default;
}

bool _dict_bool(const Dictionary &p_dict, const StringName &p_key, bool p_default = false) {
	const String key = String(p_key);
	if (!p_dict.has(key)) {
		return p_default;
	}
	const Variant value = p_dict[key];
	if (value.get_type() == Variant::BOOL) {
		return value;
	}
	const String value_string = String(value).strip_edges().to_lower();
	return value_string == "1" || value_string == "true" || value_string == "yes";
}

Color _parse_source_color_string(const String &p_value, const Color &p_default = Color(1, 1, 1)) {
	const PackedStringArray components = p_value.strip_edges().replace(",", " ").split(" ", false);
	if (components.size() < 3) {
		return p_default;
	}

	return Color(
			CLAMP(components[0].to_float() / 255.0f, 0.0f, 1.0f),
			CLAMP(components[1].to_float() / 255.0f, 0.0f, 1.0f),
			CLAMP(components[2].to_float() / 255.0f, 0.0f, 1.0f),
			components.size() > 3 ? CLAMP(components[3].to_float() / 255.0f, 0.0f, 1.0f) : 1.0f);
}

void _parse_source_light_value(const Dictionary &p_keyvalues, Color &r_color, float &r_energy) {
	const String light_value = _dict_string(p_keyvalues, "_light").strip_edges().replace(",", " ");
	const PackedStringArray components = light_value.split(" ", false);
	if (components.size() >= 3) {
		r_color = Color(
				CLAMP(components[0].to_float() / 255.0f, 0.0f, 1.0f),
				CLAMP(components[1].to_float() / 255.0f, 0.0f, 1.0f),
				CLAMP(components[2].to_float() / 255.0f, 0.0f, 1.0f));
	}
	if (components.size() >= 4) {
		r_energy = MAX(components[3].to_float() / 255.0f, 0.0f);
	}
	if (components.size() < 3 && p_keyvalues.has("rendercolor")) {
		r_color = _parse_source_color_string(_dict_string(p_keyvalues, "rendercolor"), r_color);
	}
	if (p_keyvalues.has("renderamt")) {
		r_energy *= MAX(_dict_float(p_keyvalues, "renderamt", 255.0) / 255.0, 0.0);
	}
}

float _source_light_range_from_keyvalues(const Dictionary &p_keyvalues, float p_default_source_units) {
	if (p_keyvalues.has("_distance")) {
		return MAX(_dict_float(p_keyvalues, "_distance", p_default_source_units), 1.0) * SourcePPUtils::SOURCE_UNIT_TO_METERS;
	}
	if (p_keyvalues.has("distance")) {
		return MAX(_dict_float(p_keyvalues, "distance", p_default_source_units), 1.0) * SourcePPUtils::SOURCE_UNIT_TO_METERS;
	}
	return p_default_source_units * SourcePPUtils::SOURCE_UNIT_TO_METERS;
}

void _apply_source_light_direction(Node3D *p_node, const Dictionary &p_keyvalues) {
	ERR_FAIL_NULL(p_node);
	if (!p_keyvalues.has("pitch")) {
		return;
	}

	Vector3 source_angles = SourcePPUtils::parse_source_vector_string(_dict_string(p_keyvalues, "angles", "0 0 0"));
	source_angles.x = _dict_float(p_keyvalues, "pitch", source_angles.x);
	p_node->set_basis(SourcePPUtils::source_angles_to_godot_basis(source_angles));
	p_node->set_meta("sourcepp_light_direction_source_angles", source_angles);
}

bool _is_source_output_key(const String &p_key) {
	return p_key.begins_with("On") || p_key.begins_with("on");
}

void _set_sourcepp_entity_metadata(Node3D *p_node, const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs) {
	ERR_FAIL_NULL(p_node);
	p_node->set_meta("sourcepp_bsp_entity_classname", p_classname);
	p_node->set_meta("sourcepp_bsp_entity_targetname", p_targetname);
	p_node->set_meta("sourcepp_bsp_entity_index", p_entity_index);
	p_node->set_meta("sourcepp_bsp_model_index", p_model_index);
	p_node->set_meta("sourcepp_bsp_entity_keyvalues", p_keyvalues);
	p_node->set_meta("sourcepp_bsp_entity_outputs", p_outputs);
}

void _expand_aabb_with_transformed_aabb(const AABB &p_aabb, const Transform3D &p_transform, AABB &r_aabb, bool &r_has_aabb) {
	for (int corner_index = 0; corner_index < 8; corner_index++) {
		const Vector3 corner = p_aabb.position + Vector3(
				(corner_index & 1) ? p_aabb.size.x : 0.0f,
				(corner_index & 2) ? p_aabb.size.y : 0.0f,
				(corner_index & 4) ? p_aabb.size.z : 0.0f);
		const Vector3 transformed_corner = p_transform.xform(corner);
		if (!r_has_aabb) {
			r_aabb = AABB(transformed_corner, Vector3());
			r_has_aabb = true;
		} else {
			r_aabb.expand_to(transformed_corner);
		}
	}
}

void _accumulate_mesh_instance_aabb(Node *p_node, const Transform3D &p_parent_transform, AABB &r_aabb, bool &r_has_aabb) {
	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	const Transform3D node_transform = node_3d != nullptr ? p_parent_transform * node_3d->get_transform() : p_parent_transform;
	if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
		const Ref<Mesh> mesh = mesh_instance->get_mesh();
		if (mesh.is_valid()) {
			_expand_aabb_with_transformed_aabb(mesh->get_aabb(), node_transform, r_aabb, r_has_aabb);
		}
	}
	for (int child_index = 0; child_index < p_node->get_child_count(); child_index++) {
		_accumulate_mesh_instance_aabb(p_node->get_child(child_index), node_transform, r_aabb, r_has_aabb);
	}
}

} // namespace

namespace SourcePPBSPEntityUtils {

Array parse_source_outputs(const bsppp::BSPEntityKeyValues &p_entity) {
	Array outputs;
	for (const bsppp::BSPEntityKeyValues::Element &element : p_entity.getKeyValues()) {
		const String key = _sourcepp_string_from_utf8(element.getKey());
		if (!_is_source_output_key(key)) {
			continue;
		}

		const String value = _sourcepp_string_from_utf8(element.getValue());
		const PackedStringArray parts = value.split(",", true);
		Dictionary output;
		output["output"] = key;
		output["raw"] = value;
		output["target"] = parts.size() > 0 ? parts[0].strip_edges() : String();
		output["input"] = parts.size() > 1 ? parts[1].strip_edges() : String();
		output["parameter"] = parts.size() > 2 ? parts[2].strip_edges() : String();
		output["delay"] = parts.size() > 3 ? parts[3].strip_edges().to_float() : 0.0;
		output["max_times_to_fire"] = parts.size() > 4 ? parts[4].strip_edges().to_int() : -1;
		outputs.push_back(output);
	}
	return outputs;
}

bool is_trigger_class(const String &p_classname) {
	const String classname = p_classname.to_lower();
	return classname == "trigger_multiple" || classname == "trigger_once" || classname == "trigger_hurt";
}

bool is_body_class(const String &p_classname) {
	const String classname = p_classname.to_lower();
	return classname == "func_brush" || classname == "func_door" || classname == "func_physbox";
}

bool is_visual_only_class(const String &p_classname) {
	const String classname = p_classname.to_lower();
	return classname == "func_illusionary" || classname == "func_detail";
}

bool is_physics_entity_class(const String &p_classname) {
	const String classname = p_classname.to_lower();
	return classname == "prop_physics" || classname == "prop_physics_multiplayer" || classname == "prop_ragdoll";
}

bool is_static_entity_class(const String &p_classname) {
	const String classname = p_classname.to_lower();
	return classname == "prop_static";
}

bool is_dynamic_model_entity_class(const String &p_classname) {
	const String classname = p_classname.to_lower();
	return classname == "prop_dynamic" || classname == "prop_dynamic_override" || classname == "dynamic_prop" || classname == "npc_combine_camera" || classname == "npc_turret_floor";
}

bool is_source_model_path(const String &p_model) {
	const String model = SourcePPUtils::normalize_source_path(p_model).strip_edges();
	if (model.is_empty() || model.begins_with("*")) {
		return false;
	}
	if (model.get_extension().to_lower() == "mdl") {
		return true;
	}
	return model.to_lower().begins_with("models/");
}

String normalize_source_model_path(const String &p_model) {
	const String model = SourcePPUtils::normalize_source_path(p_model);
	return is_source_model_path(model) ? SourcePPUtils::ensure_extension(model, "mdl") : String();
}

Node3D *create_brush_entity_node(const String &p_classname) {
	const String classname = p_classname.to_lower();
	if (classname == "trigger_multiple") {
		return memnew(SourcePPTriggerMultiple3D);
	}
	if (classname == "trigger_once") {
		return memnew(SourcePPTriggerOnce3D);
	}
	if (classname == "trigger_hurt") {
		return memnew(SourcePPTriggerHurt3D);
	}
	if (classname == "func_brush") {
		return memnew(SourcePPFuncBrush3D);
	}
	if (classname == "func_door") {
		return memnew(SourcePPFuncDoor3D);
	}
	if (classname == "func_illusionary") {
		return memnew(SourcePPFuncIllusionary3D);
	}
	if (classname == "func_useableladder") {
		return memnew(SourcePPLadder3D);
	}
	if (classname == "info_ladder_dismount") {
		return memnew(SourcePPLadderDismount3D);
	}
	if (is_body_class(classname)) {
		return memnew(SourcePPBrushBody3D);
	}
	return memnew(SourcePPBrushEntity3D);
}

Node3D *create_point_entity_node(const String &p_classname) {
	const String classname = p_classname.to_lower();
	if (classname == "light") {
		return memnew(OmniLight3D);
	}
	if (classname == "light_spot") {
		return memnew(SpotLight3D);
	}
	if (classname == "light_environment" || classname == "env_sun") {
		return memnew(DirectionalLight3D);
	}
	if (is_physics_entity_class(p_classname)) {
		return memnew(RigidBody3D);
	}
	if (is_static_entity_class(p_classname)) {
		return memnew(StaticBody3D);
	}
	if (is_dynamic_model_entity_class(p_classname)) {
		return memnew(AnimatableBody3D);
	}
	return create_brush_entity_node(p_classname);
}

void setup_entity_node(Node3D *p_node, const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs) {
	_set_sourcepp_entity_metadata(p_node, p_classname, p_targetname, p_entity_index, p_model_index, p_keyvalues, p_outputs);
	if (SourcePPBrushEntity3D *entity = Object::cast_to<SourcePPBrushEntity3D>(p_node)) {
		entity->setup_sourcepp_entity(p_classname, p_targetname, p_entity_index, p_model_index, p_keyvalues, p_outputs);
		entity->set_entity_enabled(!_dict_bool(p_keyvalues, "StartDisabled", false));
		return;
	}
	if (SourcePPBrushArea3D *area = Object::cast_to<SourcePPBrushArea3D>(p_node)) {
		area->setup_sourcepp_entity(p_classname, p_targetname, p_entity_index, p_model_index, p_keyvalues, p_outputs);
		area->set_entity_enabled(!_dict_bool(p_keyvalues, "StartDisabled", false));
		return;
	}
	if (SourcePPBrushBody3D *body = Object::cast_to<SourcePPBrushBody3D>(p_node)) {
		body->setup_sourcepp_entity(p_classname, p_targetname, p_entity_index, p_model_index, p_keyvalues, p_outputs);
		body->set_entity_enabled(!_dict_bool(p_keyvalues, "StartDisabled", false));
	}
}

void configure_specific_node(Node3D *p_node, const Dictionary &p_keyvalues) {
	if (SourcePPTriggerMultiple3D *trigger_multiple = Object::cast_to<SourcePPTriggerMultiple3D>(p_node)) {
		trigger_multiple->set_wait(_dict_float(p_keyvalues, "wait", 0.2));
	}
	if (SourcePPTriggerHurt3D *trigger_hurt = Object::cast_to<SourcePPTriggerHurt3D>(p_node)) {
		trigger_hurt->set_damage(_dict_float(p_keyvalues, "damage", 10.0));
		trigger_hurt->set_damage_cap(_dict_float(p_keyvalues, "damagecap", 20.0));
		trigger_hurt->set_damage_type(_dict_int(p_keyvalues, "damagetype", 0));
		trigger_hurt->set_damage_model(_dict_int(p_keyvalues, "damagemodel", 0));
		trigger_hurt->set_no_damage_force(_dict_bool(p_keyvalues, "nodmgforce", false));
	}
	if (SourcePPFuncBrush3D *func_brush = Object::cast_to<SourcePPFuncBrush3D>(p_node)) {
		func_brush->set_solidity(_dict_int(p_keyvalues, "Solidity", 0));
		func_brush->set_solid_bsp(_dict_bool(p_keyvalues, "solidbsp", false));
	}
	if (SourcePPFuncDoor3D *func_door = Object::cast_to<SourcePPFuncDoor3D>(p_node)) {
		func_door->set_move_direction(SourcePPUtils::source_angles_to_godot_basis(SourcePPUtils::parse_source_vector_string(_dict_string(p_keyvalues, "movedir", "0 0 0"))).xform(Vector3(1, 0, 0)).normalized());
		func_door->set_speed(_dict_float(p_keyvalues, "speed", 100.0));
		func_door->set_wait(_dict_float(p_keyvalues, "wait", 4.0));
		func_door->set_lip(_dict_float(p_keyvalues, "lip", 0.0));
		func_door->set_spawn_position(_dict_int(p_keyvalues, "spawnpos", 0));
		func_door->set_locked((_dict_int(p_keyvalues, "spawnflags", 0) & 2048) != 0);
	}
	if (SourcePPLadder3D *ladder = Object::cast_to<SourcePPLadder3D>(p_node)) {
		ladder->set_point0(SourcePPUtils::source_vector_to_godot_direction(SourcePPUtils::parse_source_vector_string(_dict_string(p_keyvalues, "point0"))) * SourcePPUtils::SOURCE_UNIT_TO_METERS);
		ladder->set_point1(SourcePPUtils::source_vector_to_godot_direction(SourcePPUtils::parse_source_vector_string(_dict_string(p_keyvalues, "point1"))) * SourcePPUtils::SOURCE_UNIT_TO_METERS);
		ladder->set_ladder_surface_properties(_dict_string(p_keyvalues, "ladderSurfaceProperties"));
		ladder->set_fake_ladder((_dict_int(p_keyvalues, "spawnflags", 0) & 1) != 0);
	}
	if (SourcePPLadderDismount3D *dismount = Object::cast_to<SourcePPLadderDismount3D>(p_node)) {
		dismount->set_ladder_target(_dict_string(p_keyvalues, "target"));
	}
	if (Light3D *light = Object::cast_to<Light3D>(p_node)) {
		Color light_color = Color(1, 1, 1);
		float light_energy = 1.0f;
		_parse_source_light_value(p_keyvalues, light_color, light_energy);
		light->set_color(light_color);
		light->set_param(Light3D::PARAM_ENERGY, light_energy);
		light->set_param(Light3D::PARAM_INDIRECT_ENERGY, light_energy);
		light->set_bake_mode(Light3D::BAKE_DYNAMIC);
		light->set_shadow(_dict_bool(p_keyvalues, "spawnflags", false) || _dict_bool(p_keyvalues, "enableshadows", false));
		light->set_meta("sourcepp_light_color", light_color);
		light->set_meta("sourcepp_light_energy", light_energy);
	}
	if (OmniLight3D *omni_light = Object::cast_to<OmniLight3D>(p_node)) {
		const float range = _source_light_range_from_keyvalues(p_keyvalues, 512.0f);
		omni_light->set_param(Light3D::PARAM_RANGE, range);
		omni_light->set_param(Light3D::PARAM_ATTENUATION, 1.0f);
		omni_light->set_meta("sourcepp_light_range", range);
	}
	if (SpotLight3D *spot_light = Object::cast_to<SpotLight3D>(p_node)) {
		_apply_source_light_direction(spot_light, p_keyvalues);
		const float range = _source_light_range_from_keyvalues(p_keyvalues, 768.0f);
		const float outer_cone = CLAMP(_dict_float(p_keyvalues, "_cone", 45.0), 1.0, 179.0);
		const float inner_cone = CLAMP(_dict_float(p_keyvalues, "_inner_cone", outer_cone * 0.5), 0.0, outer_cone);
		spot_light->set_param(Light3D::PARAM_RANGE, range);
		spot_light->set_param(Light3D::PARAM_ATTENUATION, 1.0f);
		spot_light->set_param(Light3D::PARAM_SPOT_ANGLE, outer_cone);
		spot_light->set_param(Light3D::PARAM_SPOT_ATTENUATION, MAX(1.0f, 1.0f + static_cast<float>((outer_cone - inner_cone) / MAX(outer_cone, 1.0))));
		spot_light->set_meta("sourcepp_light_range", range);
		spot_light->set_meta("sourcepp_light_outer_cone", outer_cone);
		spot_light->set_meta("sourcepp_light_inner_cone", inner_cone);
	}
	if (DirectionalLight3D *directional_light = Object::cast_to<DirectionalLight3D>(p_node)) {
		_apply_source_light_direction(directional_light, p_keyvalues);
		directional_light->set_param(Light3D::PARAM_ENERGY, MAX(directional_light->get_param(Light3D::PARAM_ENERGY), 1.0f));
		directional_light->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_AND_SKY);
	}
}

void add_geometry_child(Node3D *p_node, const Ref<ArrayMesh> &p_mesh, const String &p_source_path, int p_model_index, int p_entity_index, const Dictionary &p_asset_metadata) {
	if (p_mesh.is_null() || p_mesh->get_surface_count() == 0) {
		return;
	}

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("Geometry");
	mesh_instance->set_mesh(p_mesh);
	mesh_instance->set_meta("sourcepp_bsp_path", p_source_path);
	mesh_instance->set_meta("sourcepp_bsp_model_index", p_model_index);
	mesh_instance->set_meta("sourcepp_bsp_entity_index", p_entity_index);
	mesh_instance->set_meta("sourcepp_bsp_asset_metadata", p_asset_metadata);
	p_node->add_child(mesh_instance);
}

void add_collision_child(Node3D *p_node, const Ref<ArrayMesh> &p_mesh, const String &p_source_path, int p_model_index, int p_entity_index, bool p_disabled) {
	const PackedVector3Array collision_faces = SourcePPBSPGeometry::build_collision_faces_from_mesh(p_mesh);
	if (collision_faces.is_empty()) {
		return;
	}

	Ref<BSPShape3D> bsp_shape;
	bsp_shape.instantiate();
	bsp_shape->set_faces(collision_faces);

	CollisionShape3D *collision_shape = memnew(CollisionShape3D);
	collision_shape->set_name("BSPShape3D");
	collision_shape->set_shape(bsp_shape);
	collision_shape->set_disabled(p_disabled);
	collision_shape->set_meta("sourcepp_bsp_path", p_source_path);
	collision_shape->set_meta("sourcepp_bsp_model_index", p_model_index);
	collision_shape->set_meta("sourcepp_bsp_entity_index", p_entity_index);
	collision_shape->set_meta("sourcepp_bsp_collision_face_count", bsp_shape->get_face_count());
	collision_shape->set_meta("sourcepp_bsp_collision_bounds", bsp_shape->get_bounds());
	p_node->add_child(collision_shape);
}

void add_bounds_collision_child(CollisionObject3D *p_body, Node3D *p_model_node, const String &p_source_path, int p_entity_index, const String &p_model_path) {
	ERR_FAIL_NULL(p_body);
	ERR_FAIL_NULL(p_model_node);

	AABB bounds;
	bool has_bounds = false;
	_accumulate_mesh_instance_aabb(p_model_node, Transform3D(), bounds, has_bounds);
	if (!has_bounds || bounds.size.is_zero_approx()) {
		return;
	}

	Ref<BoxShape3D> box_shape;
	box_shape.instantiate();
	box_shape->set_size(bounds.size);

	CollisionShape3D *collision_shape = memnew(CollisionShape3D);
	collision_shape->set_name("BoundsCollision");
	collision_shape->set_shape(box_shape);
	collision_shape->set_transform(Transform3D(Basis(), bounds.get_center()));
	collision_shape->set_meta("sourcepp_bsp_path", p_source_path);
	collision_shape->set_meta("sourcepp_bsp_entity_index", p_entity_index);
	collision_shape->set_meta("sourcepp_model_path", p_model_path);
	collision_shape->set_meta("sourcepp_collision_source", "model_bounds");
	collision_shape->set_meta("sourcepp_collision_bounds", bounds);
	p_body->add_child(collision_shape);
}

} // namespace SourcePPBSPEntityUtils
