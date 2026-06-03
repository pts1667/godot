/**************************************************************************/
/*  sourcepp_bsp.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/io/image.h"
#include "core/math/transform_3d.h"
#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include <bsppp/BSP.h>

#include <memory>
#include <string_view>
#include <vector>

class HalfEdgeMesh;
class ArrayMesh;
class Material;
class Node3D;
class SourcePPImportCache;
class SourcePPResolver;

class SourcePPBSP : public RefCounted {
	GDCLASS(SourcePPBSP, RefCounted);

	std::unique_ptr<bsppp::BSP> bsp;
	Ref<SourcePPResolver> resolver;
	String resolver_game_id;
	String source_path;
	String temporary_backing_path;
	String mounted_bsp_pakfile_path;
	String mounted_bsp_pakfile_game_id;
	int model_index = 0;
	Ref<HalfEdgeMesh> halfedge_mesh;
	PackedInt32Array face_material_ids;
	Array face_uvs;
	std::vector<bsppp::BSPVertex> bsp_vertices;
	std::vector<bsppp::BSPFace> bsp_faces;
	std::vector<bsppp::BSPEdge> bsp_edges;
	std::vector<bsppp::BSPSurfEdge> bsp_surf_edges;
	std::vector<bsppp::BSPBrushModel> bsp_models;
	std::vector<bsppp::BSPEntityKeyValues> bsp_entities;
	std::vector<bsppp::BSPTextureInfo> bsp_texture_info;
	std::vector<bsppp::BSPTextureData> bsp_texture_data;
	PackedStringArray material_paths;
	std::vector<int> texdata_to_material_id;
	struct DisplacementInfo {
		Vector3 start_position;
		int32_t disp_vert_start = 0;
		int32_t disp_tri_start = 0;
		int32_t power = 0;
		int32_t contents = 0;
		uint16_t map_face = 0;
	};
	struct DisplacementVertex {
		Vector3 vector;
		float distance = 0.0f;
	};
	struct DisplacementTriangle {
		uint16_t tags = 0;
	};
	std::vector<DisplacementInfo> bsp_displacement_infos;
	std::vector<DisplacementVertex> bsp_displacement_vertices;
	std::vector<DisplacementTriangle> bsp_displacement_triangles;
	struct StaticProp {
		String model_path;
		Vector3 origin;
		Vector3 angles;
		uint16_t prop_type = 0;
		uint16_t first_leaf = 0;
		uint16_t leaf_count = 0;
		uint8_t solid = 0;
		uint32_t flags = 0;
		int32_t skin = 0;
		float fade_min_dist = 0.0f;
		float fade_max_dist = 0.0f;
		Vector3 lighting_origin;
		float forced_fade_scale = 1.0f;
		uint16_t min_dx_level = 0;
		uint16_t max_dx_level = 0;
		uint16_t lightmap_resolution_x = 0;
		uint16_t lightmap_resolution_y = 0;
		PackedInt32Array leaves;
	};
	std::vector<StaticProp> static_props;

	static void _bind_methods();

	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(std::string_view p_string);
	static Vector3 _source_to_godot_position(const sourcepp::math::Vec3f &p_position);
	static int32_t _read_lump_i32(const std::vector<std::byte> &p_bytes, size_t p_offset, bool p_big_endian);
	static String _read_lump_string(const std::vector<std::byte> &p_bytes, int32_t p_offset);
	void _clear_temporary_backing_file();
	void _mount_current_bsp_pakfile();
	void _unmount_current_bsp_pakfile();

	Error _cache_lumps();
	Error _cache_material_paths();
	Error _cache_displacements();
	Error _cache_static_props();
	Error _rebuild_current_halfedge_mesh();
	int _get_face_material_id(const bsppp::BSPFace &p_face) const;
	bool _is_skybox_material(int p_material_id) const;
	Vector2 _get_face_uv(const bsppp::BSPFace &p_face, const sourcepp::math::Vec3f &p_position) const;
	Vector2 _get_face_uv(const bsppp::BSPFace &p_face, const Vector3 &p_position) const;
	String _resolve_material_path(const String &p_material_name) const;
	Dictionary _entity_to_dictionary(const bsppp::BSPEntityKeyValues &p_entity) const;
	String _get_entity_value(const bsppp::BSPEntityKeyValues &p_entity, const String &p_key, const String &p_default = String()) const;
	int _get_entity_bmodel_index(const bsppp::BSPEntityKeyValues &p_entity) const;
	Transform3D _get_entity_transform(const bsppp::BSPEntityKeyValues &p_entity) const;
	Ref<Image> _create_fallback_texture_array_image() const;
	Dictionary _get_asset_source_info(const String &p_asset_path, bool p_missing) const;
	void _record_asset_metadata(Dictionary *r_asset_metadata, const String &p_asset_path, const String &p_asset_type, const String &p_material_type, bool p_missing, const Dictionary &p_metadata) const;
	Ref<Image> _load_material_texture_array_image(int p_material_id, SourcePPImportCache *p_import_cache, const Ref<Image> &p_fallback_image, Image::AlphaMode *r_alpha_mode = nullptr, Dictionary *r_asset_metadata = nullptr, bool p_warn_missing = false) const;
	Vector<Ref<Image>> _load_texture_array_images(SourcePPImportCache *p_import_cache, const Ref<Image> &p_fallback_image, Dictionary *r_asset_metadata, std::vector<Image::AlphaMode> &r_alpha_modes, bool p_warn_missing) const;
	bool _is_material_transparent(int p_material_id, Image::AlphaMode p_alpha_mode, SourcePPImportCache *p_import_cache) const;
	bool _is_material_water(int p_material_id, SourcePPImportCache *p_import_cache) const;
	Ref<Material> _create_texture_array_material(int p_surface_type, const Vector<Ref<Image>> &p_layer_images) const;
	Error _build_atlased_surface_arrays(const Ref<HalfEdgeMesh> &p_mesh, const PackedInt32Array &p_face_material_ids, const Array &p_face_uvs, int p_surface_type, const std::vector<bool> &p_transparent_materials, const std::vector<bool> &p_water_materials, Array &r_arrays) const;
	Ref<ArrayMesh> _create_array_mesh_from_halfedge(const Ref<HalfEdgeMesh> &p_mesh, const PackedInt32Array &p_face_material_ids, const Array &p_face_uvs, const std::vector<bool> &p_transparent_materials, const std::vector<bool> &p_water_materials, const Vector<Ref<Material>> &p_surface_materials) const;
	Ref<ArrayMesh> _create_model_array_mesh(int p_model_index, const std::vector<bool> &p_transparent_materials, const std::vector<bool> &p_water_materials, const Vector<Ref<Material>> &p_surface_materials) const;
	bool _extract_face_source_polygon(const bsppp::BSPFace &p_face, PackedVector3Array &r_source_vertices) const;
	bool _append_displacement_mesh_data(const bsppp::BSPFace &p_face, const PackedVector3Array &p_source_polygon, int p_material_id, PackedVector3Array &r_vertices, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs) const;
	Error _build_model_mesh_data(int p_model_index, PackedVector3Array &r_vertices, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs) const;

public:
	SourcePPBSP();
	~SourcePPBSP() override;

	void set_resolver(const Ref<SourcePPResolver> &p_resolver);
	Ref<SourcePPResolver> get_resolver() const;
	void set_resolver_game_id(const String &p_game_id);
	String get_resolver_game_id() const;
	void set_model_index(int p_model_index);
	int get_model_index() const;
	void set_halfedge_mesh(const Ref<HalfEdgeMesh> &p_halfedge_mesh);
	Ref<HalfEdgeMesh> get_halfedge_mesh() const;
	void set_face_material_ids(const PackedInt32Array &p_face_material_ids);
	PackedInt32Array get_face_material_ids() const;

	Error open(const String &p_path);
	Error open_from_buffer(const PackedByteArray &p_data, const String &p_path = String());
	void close();
	bool is_open() const;

	String get_path() const;
	int get_version() const;
	int get_map_revision() const;
	int get_model_count() const;
	int get_static_prop_count() const;
	int get_displacement_count() const;
	PackedStringArray get_material_paths() const;
	Node3D *create_node() const;
};
