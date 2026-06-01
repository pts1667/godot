/**************************************************************************/
/*  sourcepp_bsp.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "core/variant/array.h"

#include <bsppp/BSP.h>

#include <memory>
#include <string_view>
#include <vector>

class HalfEdgeMesh;

class SourcePPBSP : public RefCounted {
	GDCLASS(SourcePPBSP, RefCounted);

	std::unique_ptr<bsppp::BSP> bsp;
	String source_path;
	std::vector<bsppp::BSPVertex> bsp_vertices;
	std::vector<bsppp::BSPFace> bsp_faces;
	std::vector<bsppp::BSPEdge> bsp_edges;
	std::vector<bsppp::BSPSurfEdge> bsp_surf_edges;
	std::vector<bsppp::BSPBrushModel> bsp_models;
	std::vector<bsppp::BSPTextureInfo> bsp_texture_info;
	std::vector<bsppp::BSPTextureData> bsp_texture_data;
	PackedStringArray material_paths;
	std::vector<int> texdata_to_material_id;

	static void _bind_methods();

	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(std::string_view p_string);
	static Vector3 _source_to_godot_position(const sourcepp::math::Vec3f &p_position);
	static int32_t _read_lump_i32(const std::vector<std::byte> &p_bytes, size_t p_offset, bool p_big_endian);
	static String _read_lump_string(const std::vector<std::byte> &p_bytes, int32_t p_offset);

	Error _cache_lumps();
	Error _cache_material_paths();
	int _get_face_material_id(const bsppp::BSPFace &p_face) const;
	Error _build_model_mesh_data(int p_model_index, PackedVector3Array &r_vertices, Array &r_faces, PackedInt32Array &r_face_material_ids) const;

public:
	SourcePPBSP();
	~SourcePPBSP() override;

	Error open(const String &p_path);
	void close();
	bool is_open() const;

	String get_path() const;
	int get_version() const;
	int get_map_revision() const;
	int get_model_count() const;
	PackedStringArray get_material_paths() const;
	PackedInt32Array get_face_material_ids(int p_model_index = 0) const;
	Ref<HalfEdgeMesh> create_halfedge_mesh(int p_model_index = 0) const;
};