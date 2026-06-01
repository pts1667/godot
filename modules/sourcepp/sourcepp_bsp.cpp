/**************************************************************************/
/*  sourcepp_bsp.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp.h"

#include "modules/halfedge/halfedge_mesh.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

constexpr float SOURCE_UNIT_TO_METERS = 0.0254f;

} // namespace

void SourcePPBSP::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "path"), &SourcePPBSP::open);
	ClassDB::bind_method(D_METHOD("close"), &SourcePPBSP::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPBSP::is_open);
	ClassDB::bind_method(D_METHOD("get_path"), &SourcePPBSP::get_path);
	ClassDB::bind_method(D_METHOD("get_version"), &SourcePPBSP::get_version);
	ClassDB::bind_method(D_METHOD("get_map_revision"), &SourcePPBSP::get_map_revision);
	ClassDB::bind_method(D_METHOD("get_model_count"), &SourcePPBSP::get_model_count);
	ClassDB::bind_method(D_METHOD("get_material_paths"), &SourcePPBSP::get_material_paths);
	ClassDB::bind_method(D_METHOD("get_face_material_ids", "model_index"), &SourcePPBSP::get_face_material_ids, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("create_halfedge_mesh", "model_index"), &SourcePPBSP::create_halfedge_mesh, DEFVAL(0));
}

SourcePPBSP::SourcePPBSP() = default;

SourcePPBSP::~SourcePPBSP() = default;

std::string SourcePPBSP::_to_utf8(const String &p_string) {
	const CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data());
}

String SourcePPBSP::_from_utf8(std::string_view p_string) {
	return String::utf8(p_string.data(), static_cast<int>(p_string.size()));
}

Vector3 SourcePPBSP::_source_to_godot_position(const sourcepp::math::Vec3f &p_position) {
	return Vector3(p_position[0], p_position[2], -p_position[1]) * SOURCE_UNIT_TO_METERS;
}

int32_t SourcePPBSP::_read_lump_i32(const std::vector<std::byte> &p_bytes, size_t p_offset, bool p_big_endian) {
	if (p_offset + sizeof(uint32_t) > p_bytes.size()) {
		return -1;
	}

	const uint32_t b0 = static_cast<uint32_t>(std::to_integer<uint8_t>(p_bytes[p_offset + 0]));
	const uint32_t b1 = static_cast<uint32_t>(std::to_integer<uint8_t>(p_bytes[p_offset + 1]));
	const uint32_t b2 = static_cast<uint32_t>(std::to_integer<uint8_t>(p_bytes[p_offset + 2]));
	const uint32_t b3 = static_cast<uint32_t>(std::to_integer<uint8_t>(p_bytes[p_offset + 3]));
	const uint32_t value = p_big_endian ? ((b0 << 24) | (b1 << 16) | (b2 << 8) | b3) : (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
	return static_cast<int32_t>(value);
}

String SourcePPBSP::_read_lump_string(const std::vector<std::byte> &p_bytes, int32_t p_offset) {
	if (p_offset < 0 || static_cast<size_t>(p_offset) >= p_bytes.size()) {
		return String();
	}

	size_t end_offset = static_cast<size_t>(p_offset);
	while (end_offset < p_bytes.size() && std::to_integer<uint8_t>(p_bytes[end_offset]) != 0) {
		end_offset++;
	}

	return String::utf8(reinterpret_cast<const char *>(p_bytes.data()) + p_offset, static_cast<int>(end_offset - static_cast<size_t>(p_offset)));
}

Error SourcePPBSP::_cache_lumps() {
	ERR_FAIL_COND_V_MSG(bsp == nullptr, ERR_UNCONFIGURED, "No BSP is currently open.");

	bsp_vertices = bsp->getLumpData<bsppp::BSPLump::VERTEXES>();
	bsp_faces = bsp->getLumpData<bsppp::BSPLump::FACES>();
	bsp_edges = bsp->getLumpData<bsppp::BSPLump::EDGES>();
	bsp_surf_edges = bsp->getLumpData<bsppp::BSPLump::SURFEDGES>();
	bsp_models = bsp->getLumpData<bsppp::BSPLump::MODELS>();
	bsp_texture_info = bsp->getLumpData<bsppp::BSPLump::TEXINFO>();
	bsp_texture_data = bsp->getLumpData<bsppp::BSPLump::TEXDATA>();

	return _cache_material_paths();
}

Error SourcePPBSP::_cache_material_paths() {
	material_paths.clear();
	texdata_to_material_id.clear();
	texdata_to_material_id.resize(bsp_texture_data.size(), -1);

	ERR_FAIL_COND_V_MSG(bsp == nullptr, ERR_UNCONFIGURED, "No BSP is currently open.");

	const auto string_data = bsp->getLumpData(bsppp::BSPLump::TEXDATA_STRING_DATA);
	const auto string_table = bsp->getLumpData(bsppp::BSPLump::TEXDATA_STRING_TABLE);
	if (!string_data || !string_table) {
		return OK;
	}

	HashMap<String, int> material_lookup;
	const bool big_endian = bsp->isConsole();
	const size_t string_table_entries = string_table->size() / sizeof(int32_t);

	for (int texdata_index = 0; texdata_index < static_cast<int>(bsp_texture_data.size()); texdata_index++) {
		const int32_t string_table_index = bsp_texture_data[static_cast<size_t>(texdata_index)].nameStringTableID;
		if (string_table_index < 0 || static_cast<size_t>(string_table_index) >= string_table_entries) {
			continue;
		}

		const int32_t string_offset = _read_lump_i32(*string_table, static_cast<size_t>(string_table_index) * sizeof(int32_t), big_endian);
		String material_path = _read_lump_string(*string_data, string_offset).replace("\\", "/");
		if (material_path.is_empty()) {
			continue;
		}

		if (!material_lookup.has(material_path)) {
			material_lookup.insert(material_path, material_paths.size());
			material_paths.push_back(material_path);
		}
		texdata_to_material_id[static_cast<size_t>(texdata_index)] = material_lookup[material_path];
	}

	return OK;
}

int SourcePPBSP::_get_face_material_id(const bsppp::BSPFace &p_face) const {
	if (p_face.texInfo < 0 || static_cast<size_t>(p_face.texInfo) >= bsp_texture_info.size()) {
		return -1;
	}

	const int texdata_index = bsp_texture_info[static_cast<size_t>(p_face.texInfo)].textureData;
	if (texdata_index < 0 || static_cast<size_t>(texdata_index) >= texdata_to_material_id.size()) {
		return -1;
	}

	return texdata_to_material_id[static_cast<size_t>(texdata_index)];
}

Error SourcePPBSP::_build_model_mesh_data(int p_model_index, PackedVector3Array &r_vertices, Array &r_faces, PackedInt32Array &r_face_material_ids) const {
	r_vertices = PackedVector3Array();
	r_faces.clear();
	r_face_material_ids = PackedInt32Array();

	ERR_FAIL_COND_V_MSG(bsp == nullptr, ERR_UNCONFIGURED, "No BSP is currently open.");
	ERR_FAIL_INDEX_V_MSG(p_model_index, static_cast<int>(bsp_models.size()), ERR_INVALID_PARAMETER, "Requested BSP brush model is out of range.");

	const bsppp::BSPBrushModel &model = bsp_models[static_cast<size_t>(p_model_index)];
	HashMap<int, int> vertex_remap;

	for (int model_face_offset = 0; model_face_offset < model.numFaces; model_face_offset++) {
		const int bsp_face_index = model.firstFace + model_face_offset;
		if (bsp_face_index < 0 || static_cast<size_t>(bsp_face_index) >= bsp_faces.size()) {
			continue;
		}

		const bsppp::BSPFace &face = bsp_faces[static_cast<size_t>(bsp_face_index)];
		if (face.dispInfo >= 0 || face.numEdges < 3) {
			continue;
		}

		PackedInt32Array polygon;
		int previous_bsp_vertex_index = -1;
		for (int face_edge_offset = 0; face_edge_offset < face.numEdges; face_edge_offset++) {
			const int surf_edge_index = face.firstEdge + face_edge_offset;
			if (surf_edge_index < 0 || static_cast<size_t>(surf_edge_index) >= bsp_surf_edges.size()) {
				polygon = PackedInt32Array();
				break;
			}

			const int edge_reference = bsp_surf_edges[static_cast<size_t>(surf_edge_index)].surfEdge;
			const int edge_index = edge_reference >= 0 ? edge_reference : -edge_reference;
			if (edge_index < 0 || static_cast<size_t>(edge_index) >= bsp_edges.size()) {
				polygon = PackedInt32Array();
				break;
			}

			const bsppp::BSPEdge &edge = bsp_edges[static_cast<size_t>(edge_index)];
			const int bsp_vertex_index = edge_reference >= 0 ? static_cast<int>(edge.v0) : static_cast<int>(edge.v1);
			if (bsp_vertex_index < 0 || static_cast<size_t>(bsp_vertex_index) >= bsp_vertices.size()) {
				polygon = PackedInt32Array();
				break;
			}

			if (previous_bsp_vertex_index == bsp_vertex_index) {
				continue;
			}
			previous_bsp_vertex_index = bsp_vertex_index;

			int mesh_vertex_index = -1;
			if (vertex_remap.has(bsp_vertex_index)) {
				mesh_vertex_index = vertex_remap[bsp_vertex_index];
			} else {
				mesh_vertex_index = r_vertices.size();
				vertex_remap.insert(bsp_vertex_index, mesh_vertex_index);
				r_vertices.push_back(_source_to_godot_position(bsp_vertices[static_cast<size_t>(bsp_vertex_index)].position));
			}

			polygon.push_back(mesh_vertex_index);
		}

		if (polygon.size() >= 2 && polygon[0] == polygon[polygon.size() - 1]) {
			polygon.remove_at(polygon.size() - 1);
		}
		if (polygon.size() < 3) {
			continue;
		}

		r_faces.push_back(polygon);
		r_face_material_ids.push_back(_get_face_material_id(face));
	}

	return OK;
}

Error SourcePPBSP::open(const String &p_path) {
	close();

	auto opened_bsp = std::make_unique<bsppp::BSP>(_to_utf8(p_path), false);
	ERR_FAIL_COND_V_MSG(!(*opened_bsp), ERR_FILE_CANT_OPEN, "Failed to open BSP file.");

	bsp = std::move(opened_bsp);
	source_path = p_path;

	const Error cache_error = _cache_lumps();
	if (cache_error != OK) {
		close();
		return cache_error;
	}

	return OK;
}

void SourcePPBSP::close() {
	bsp.reset();
	source_path = String();
	bsp_vertices.clear();
	bsp_faces.clear();
	bsp_edges.clear();
	bsp_surf_edges.clear();
	bsp_models.clear();
	bsp_texture_info.clear();
	bsp_texture_data.clear();
	texdata_to_material_id.clear();
	material_paths.clear();
}

bool SourcePPBSP::is_open() const {
	return bsp != nullptr;
}

String SourcePPBSP::get_path() const {
	return source_path;
}

int SourcePPBSP::get_version() const {
	ERR_FAIL_COND_V_MSG(bsp == nullptr, 0, "No BSP is currently open.");
	return static_cast<int>(bsp->getVersion());
}

int SourcePPBSP::get_map_revision() const {
	ERR_FAIL_COND_V_MSG(bsp == nullptr, 0, "No BSP is currently open.");
	return static_cast<int>(bsp->getMapRevision());
}

int SourcePPBSP::get_model_count() const {
	return static_cast<int>(bsp_models.size());
}

PackedStringArray SourcePPBSP::get_material_paths() const {
	return material_paths;
}

PackedInt32Array SourcePPBSP::get_face_material_ids(int p_model_index) const {
	PackedVector3Array mesh_vertices;
	Array mesh_faces;
	PackedInt32Array face_material_ids;
	const Error build_error = _build_model_mesh_data(p_model_index, mesh_vertices, mesh_faces, face_material_ids);
	ERR_FAIL_COND_V(build_error != OK, PackedInt32Array());
	return face_material_ids;
}

Ref<HalfEdgeMesh> SourcePPBSP::create_halfedge_mesh(int p_model_index) const {
	PackedVector3Array mesh_vertices;
	Array mesh_faces;
	PackedInt32Array face_material_ids;
	const Error build_error = _build_model_mesh_data(p_model_index, mesh_vertices, mesh_faces, face_material_ids);
	ERR_FAIL_COND_V(build_error != OK, Ref<HalfEdgeMesh>());

	Ref<HalfEdgeMesh> mesh;
	mesh.instantiate();
	const Error set_faces_error = mesh->set_faces(mesh_vertices, mesh_faces);
	ERR_FAIL_COND_V_MSG(set_faces_error != OK, Ref<HalfEdgeMesh>(), "Failed to create a half-edge mesh from BSP faces.");
	return mesh;
}