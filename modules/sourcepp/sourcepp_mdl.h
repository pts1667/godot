/**************************************************************************/
/*  sourcepp_mdl.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/object/ref_counted.h"
#include "scene/resources/3d/skin.h"
#include "scene/resources/animation.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace mdlpp {
struct BakedModel;
struct StudioModel;
}

class Skeleton3D;
class Node3D;
class SourcePPResolver;

class SourcePPMDL : public RefCounted {
	GDCLASS(SourcePPMDL, RefCounted);

	std::unique_ptr<mdlpp::StudioModel> model;
	Ref<SourcePPResolver> resolver;
	String resolver_game_id;
	String mdl_path;
	String vtx_path;
	String vvd_path;
	Vector<uint8_t> mdl_data_cache;
	Vector<uint8_t> vtx_data_cache;
	Vector<uint8_t> vvd_data_cache;
	Vector<uint8_t> anim_block_data_cache;

	static void _bind_methods();

	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(const std::string &p_string);
	static PackedByteArray _to_packed_byte_array(const Vector<uint8_t> &p_data);
	static std::vector<std::byte> _to_byte_vector(const Vector<uint8_t> &p_data);
	String _resolve_companion_path(const String &p_model_path, const PackedStringArray &p_candidates) const;
	String _resolve_material_path(const String &p_material_name) const;
	Ref<Material> _create_import_material(int p_material_index, int p_skin_family) const;
	Vector<uint8_t> _read_file_bytes(const String &p_path, Error *r_error) const;

	Error _open_bytes(const Vector<uint8_t> &p_mdl_data, const Vector<uint8_t> &p_vtx_data, const Vector<uint8_t> &p_vvd_data, const Vector<uint8_t> &p_anim_block_data = Vector<uint8_t>());
	Error _get_baked_model(int p_lod, mdlpp::BakedModel &r_baked_model) const;

	mdlpp::StudioModel *get_model();
	const mdlpp::StudioModel *get_model() const;

public:
	SourcePPMDL();
	~SourcePPMDL() override;

	void set_resolver(const Ref<SourcePPResolver> &p_resolver);
	Ref<SourcePPResolver> get_resolver() const;
	void set_resolver_game_id(const String &p_game_id);
	String get_resolver_game_id() const;

	Error open(const String &p_mdl_path, const String &p_vtx_path = String(), const String &p_vvd_path = String());
	Error open_from_buffer(const PackedByteArray &p_mdl_data, const PackedByteArray &p_vtx_data, const PackedByteArray &p_vvd_data, const PackedByteArray &p_anim_block_data = PackedByteArray());
	void close();
	bool is_open() const;

	String get_name() const;
	int get_version() const;
	int get_checksum() const;
	int get_lod_count() const;
	String get_mdl_path() const;
	String get_vtx_path() const;
	String get_vvd_path() const;

	PackedStringArray get_materials() const;
	PackedStringArray get_material_directories() const;
	Array get_skin_families() const;
	PackedStringArray get_bone_names() const;
	Array get_skeleton_bones() const;
	int get_bone_controller_count() const;
	Array get_bone_controllers() const;
	PackedStringArray get_body_parts() const;
	int get_attachment_count() const;
	Array get_attachments() const;
	int get_animation_descriptor_count() const;
	Array get_animation_descriptors() const;
	Dictionary get_animation_data(int p_animation_descriptor) const;
	int get_hitbox_set_count() const;
	PackedStringArray get_hitbox_set_names() const;
	Array get_hitboxes(int p_hitbox_set = 0) const;
	int get_sequence_descriptor_count() const;
	Array get_sequence_descriptors() const;
 	Ref<Animation> create_sequence_animation(int p_sequence_descriptor, const NodePath &p_skeleton_path = NodePath("."), int p_blend_x = 0, int p_blend_y = 0) const;

	int get_vertex_count(int p_lod = 0) const;
	int get_surface_count(int p_lod = 0) const;
	PackedInt32Array get_surface_material_indices(int p_lod = 0) const;
	PackedStringArray get_surface_materials(int p_lod = 0) const;
	Ref<ArrayMesh> create_mesh(int p_lod = 0) const;
	Ref<Skin> create_skin() const;
	Skeleton3D *create_skeleton() const;
	Node3D *create_model_node(int p_skin_family = 0, bool p_include_attachments = true) const;
};