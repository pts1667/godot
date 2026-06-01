/**************************************************************************/
/*  sourcepp_bsp.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp.h"

#include "sourcepp_resolver.h"
#include "sourcepp_vmt.h"

#include "modules/halfedge/halfedge_mesh.h"

#include "core/io/dir_access.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/math/geometry_2d.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"

#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

constexpr float SOURCE_UNIT_TO_METERS = 0.0254f;
constexpr float BSP_HALFEDGE_COPLANAR_EPSILON = 0.001f;
constexpr float BSP_HALFEDGE_COLLINEAR_EPSILON = 0.0001f;

bool _path_exists_with_resolver(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id) {
	if (FileAccess::exists(p_path)) {
		return true;
	}
	if (!p_resolver.is_valid()) {
		return false;
	}
	return p_game_id.is_empty() ? p_resolver->has_file(p_path) : p_resolver->has_file(p_path, p_game_id);
}

String _normalize_source_path(const String &p_path) {
	return p_path.replace("\\", "/").strip_edges();
}

String _strip_material_prefix(const String &p_path) {
	return p_path.begins_with("materials/") ? p_path.substr(10) : p_path;
}

String _with_vmt_extension(const String &p_path) {
	return p_path.get_extension().to_lower() == "vmt" ? p_path : p_path + ".vmt";
}

bool _compute_polygon_plane(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, Plane &r_plane) {
	if (p_polygon.size() < 3) {
		return false;
	}

	const int origin_index = p_polygon[0];
	for (int i = 1; i < p_polygon.size() - 1; i++) {
		const Vector3 &a = p_vertices[origin_index];
		const Vector3 &b = p_vertices[p_polygon[i]];
		const Vector3 &c = p_vertices[p_polygon[i + 1]];
		if ((b - a).cross(c - a).length() <= BSP_HALFEDGE_COLLINEAR_EPSILON) {
			continue;
		}
		r_plane = Plane(a, b, c);
		return true;
	}

	return false;
}

bool _simplify_bsp_polygon(const PackedVector3Array &p_vertices, PackedInt32Array &r_polygon) {
	if (r_polygon.size() < 3) {
		return false;
	}

	bool changed = true;
	while (changed && r_polygon.size() >= 3) {
		changed = false;
		for (int i = 0; i < r_polygon.size(); i++) {
			const int prev_index = (i - 1 + r_polygon.size()) % r_polygon.size();
			const int next_index = (i + 1) % r_polygon.size();
			const int prev_vertex = r_polygon[prev_index];
			const int vertex = r_polygon[i];
			const int next_vertex = r_polygon[next_index];

			if (prev_vertex == vertex || vertex == next_vertex || prev_vertex == next_vertex) {
				r_polygon.remove_at(i);
				changed = true;
				break;
			}

			const Vector3 &prev = p_vertices[prev_vertex];
			const Vector3 &current = p_vertices[vertex];
			const Vector3 &next = p_vertices[next_vertex];
			if ((current - prev).cross(next - current).length() <= BSP_HALFEDGE_COLLINEAR_EPSILON) {
				r_polygon.remove_at(i);
				changed = true;
				break;
			}
		}
	}

	if (r_polygon.size() < 3) {
		return false;
	}

	for (int i = 0; i < r_polygon.size(); i++) {
		for (int j = i + 1; j < r_polygon.size(); j++) {
			if (r_polygon[i] == r_polygon[j]) {
				return false;
			}
		}
	}

	return true;
}

bool _project_bsp_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, PackedVector2Array &r_projected_vertices) {
	Plane plane;
	if (!_compute_polygon_plane(p_vertices, p_polygon, plane)) {
		return false;
	}

	r_projected_vertices.resize(p_polygon.size());
	const Vector3 normal = plane.normal.normalized();
	const Vector3 reference = Math::abs(normal.dot(Vector3(0, 1, 0))) < 0.999f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	const Vector3 tangent = normal.cross(reference).normalized();
	const Vector3 bitangent = normal.cross(tangent).normalized();

	for (int i = 0; i < p_polygon.size(); i++) {
		const Vector3 &vertex = p_vertices[p_polygon[i]];
		if (Math::abs(plane.distance_to(vertex)) > BSP_HALFEDGE_COPLANAR_EPSILON) {
			return false;
		}
		r_projected_vertices.set(i, Vector2(tangent.dot(vertex), bitangent.dot(vertex)));
	}

	return true;
}

bool _is_halfedge_compatible_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon) {
	PackedVector2Array projected_vertices;
	if (!_project_bsp_polygon(p_vertices, p_polygon, projected_vertices)) {
		return false;
	}

	return !Geometry2D::triangulate_polygon(projected_vertices).is_empty();
}

void _append_triangulated_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, int p_material_id, Array &r_faces, PackedInt32Array &r_face_material_ids) {
	PackedVector2Array projected_vertices;
	if (!_project_bsp_polygon(p_vertices, p_polygon, projected_vertices)) {
		return;
	}

	const PackedInt32Array triangulated_indices = Geometry2D::triangulate_polygon(projected_vertices);
	for (int i = 0; i < triangulated_indices.size(); i += 3) {
		if (i + 2 >= triangulated_indices.size()) {
			break;
		}

		PackedInt32Array triangle;
		triangle.push_back(p_polygon[triangulated_indices[i]]);
		triangle.push_back(p_polygon[triangulated_indices[i + 1]]);
		triangle.push_back(p_polygon[triangulated_indices[i + 2]]);

		Plane triangle_plane;
		if (!_compute_polygon_plane(p_vertices, triangle, triangle_plane)) {
			continue;
		}

		r_faces.push_back(triangle);
		r_face_material_ids.push_back(p_material_id);
	}
}

} // namespace

void SourcePPBSP::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resolver", "resolver"), &SourcePPBSP::set_resolver);
	ClassDB::bind_method(D_METHOD("get_resolver"), &SourcePPBSP::get_resolver);
	ClassDB::bind_method(D_METHOD("set_resolver_game_id", "game_id"), &SourcePPBSP::set_resolver_game_id);
	ClassDB::bind_method(D_METHOD("get_resolver_game_id"), &SourcePPBSP::get_resolver_game_id);
	ClassDB::bind_method(D_METHOD("set_model_index", "model_index"), &SourcePPBSP::set_model_index);
	ClassDB::bind_method(D_METHOD("get_model_index"), &SourcePPBSP::get_model_index);
	ClassDB::bind_method(D_METHOD("set_halfedge_mesh", "halfedge_mesh"), &SourcePPBSP::set_halfedge_mesh);
	ClassDB::bind_method(D_METHOD("get_halfedge_mesh"), &SourcePPBSP::get_halfedge_mesh);
	ClassDB::bind_method(D_METHOD("set_face_material_ids", "face_material_ids"), &SourcePPBSP::set_face_material_ids);
	ClassDB::bind_method(D_METHOD("get_face_material_ids"), &SourcePPBSP::get_face_material_ids);
	ClassDB::bind_method(D_METHOD("open", "path"), &SourcePPBSP::open);
	ClassDB::bind_method(D_METHOD("open_from_buffer", "data", "path"), &SourcePPBSP::open_from_buffer, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("close"), &SourcePPBSP::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPBSP::is_open);
	ClassDB::bind_method(D_METHOD("get_path"), &SourcePPBSP::get_path);
	ClassDB::bind_method(D_METHOD("get_version"), &SourcePPBSP::get_version);
	ClassDB::bind_method(D_METHOD("get_map_revision"), &SourcePPBSP::get_map_revision);
	ClassDB::bind_method(D_METHOD("get_model_count"), &SourcePPBSP::get_model_count);
	ClassDB::bind_method(D_METHOD("get_material_paths"), &SourcePPBSP::get_material_paths);
	ClassDB::bind_method(D_METHOD("create_node"), &SourcePPBSP::create_node);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "resolver", PROPERTY_HINT_RESOURCE_TYPE, "SourcePPResolver"), "set_resolver", "get_resolver");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "resolver_game_id"), "set_resolver_game_id", "get_resolver_game_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "model_index"), "set_model_index", "get_model_index");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "halfedge_mesh"), "set_halfedge_mesh", "get_halfedge_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "face_material_ids"), "set_face_material_ids", "get_face_material_ids");
}

SourcePPBSP::SourcePPBSP() = default;

SourcePPBSP::~SourcePPBSP() {
	close();
}

void SourcePPBSP::set_resolver(const Ref<SourcePPResolver> &p_resolver) {
	resolver = p_resolver;
}

Ref<SourcePPResolver> SourcePPBSP::get_resolver() const {
	return resolver;
}

void SourcePPBSP::set_resolver_game_id(const String &p_game_id) {
	resolver_game_id = p_game_id.strip_edges();
}

String SourcePPBSP::get_resolver_game_id() const {
	return resolver_game_id;
}

void SourcePPBSP::set_model_index(int p_model_index) {
	ERR_FAIL_COND_MSG(p_model_index < 0, "Model index must be non-negative.");
	if (model_index == p_model_index) {
		return;
	}

	const int previous_model_index = model_index;
	const Ref<HalfEdgeMesh> previous_halfedge_mesh = halfedge_mesh;
	const PackedInt32Array previous_face_material_ids = face_material_ids;

	model_index = p_model_index;
	if (bsp != nullptr) {
		const Error rebuild_error = _rebuild_current_halfedge_mesh();
		if (rebuild_error != OK) {
			model_index = previous_model_index;
			halfedge_mesh = previous_halfedge_mesh;
			face_material_ids = previous_face_material_ids;
			ERR_FAIL_MSG("Failed to rebuild the BSP half-edge mesh for the requested model.");
		}
	}
}

int SourcePPBSP::get_model_index() const {
	return model_index;
}

void SourcePPBSP::set_halfedge_mesh(const Ref<HalfEdgeMesh> &p_halfedge_mesh) {
	halfedge_mesh = p_halfedge_mesh;
}

Ref<HalfEdgeMesh> SourcePPBSP::get_halfedge_mesh() const {
	return halfedge_mesh;
}

void SourcePPBSP::set_face_material_ids(const PackedInt32Array &p_face_material_ids) {
	face_material_ids = p_face_material_ids;
}

PackedInt32Array SourcePPBSP::get_face_material_ids() const {
	return face_material_ids;
}

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

void SourcePPBSP::_clear_temporary_backing_file() {
	if (temporary_backing_path.is_empty()) {
		return;
	}

	const String path_to_remove = temporary_backing_path;
	temporary_backing_path = String();
	DirAccess::remove_absolute(path_to_remove);
}

Error SourcePPBSP::_rebuild_current_halfedge_mesh() {
	if (bsp == nullptr || bsp_models.empty()) {
		halfedge_mesh.unref();
		face_material_ids = PackedInt32Array();
		return OK;
	}

	PackedVector3Array mesh_vertices;
	Array mesh_faces;
	PackedInt32Array rebuilt_face_material_ids;
	const Error build_error = _build_model_mesh_data(model_index, mesh_vertices, mesh_faces, rebuilt_face_material_ids);
	ERR_FAIL_COND_V(build_error != OK, build_error);

	Ref<HalfEdgeMesh> mesh;
	mesh.instantiate();
	const Error set_faces_error = mesh->set_faces(mesh_vertices, mesh_faces);
	ERR_FAIL_COND_V_MSG(set_faces_error != OK, set_faces_error, "Failed to create a half-edge mesh from BSP faces.");

	halfedge_mesh = mesh;
	face_material_ids = rebuilt_face_material_ids;
	return OK;
}

String SourcePPBSP::_resolve_material_path(const String &p_material_name) const {
	const String normalized_material_path = _normalize_source_path(p_material_name);
	if (normalized_material_path.is_empty()) {
		return String();
	}

	const String relative_material_path = _with_vmt_extension(_strip_material_prefix(normalized_material_path));
	const String materials_relative_path = String("materials/") + relative_material_path;

	String game_root;
	const String normalized_bsp_path = _normalize_source_path(source_path);
	const int maps_index = normalized_bsp_path.find("/maps/") >= 0 ? normalized_bsp_path.find("/maps/") : (normalized_bsp_path.begins_with("maps/") ? 0 : -1);
	if (maps_index > 0) {
		game_root = normalized_bsp_path.substr(0, maps_index);
	}

	const String candidates[4] = {
		relative_material_path,
		materials_relative_path,
		game_root.is_empty() ? String() : game_root.path_join(relative_material_path),
		game_root.is_empty() ? String() : game_root.path_join(materials_relative_path),
	};

	for (const String &candidate : candidates) {
		if (!candidate.is_empty() && _path_exists_with_resolver(candidate, resolver, resolver_game_id)) {
			return candidate;
		}
	}

	return String();
}

Ref<Material> SourcePPBSP::_create_import_material(int p_material_id) const {
	Ref<Material> material;
	String material_name;
	String resolved_material_path;

	if (p_material_id >= 0 && p_material_id < material_paths.size()) {
		material_name = material_paths[p_material_id];
		resolved_material_path = _resolve_material_path(material_name);
		if (!resolved_material_path.is_empty()) {
			Ref<SourcePPVMT> vmt;
			vmt.instantiate();
			vmt->set_resolver(resolver);
			vmt->set_resolver_game_id(resolver_game_id);
			if (vmt->open(resolved_material_path) == OK) {
				material = vmt->create_material();
			}
		}
	}

	if (material.is_null()) {
		Ref<StandardMaterial3D> fallback_material;
		fallback_material.instantiate();
		material = fallback_material;
	}

	material->set_meta("sourcepp_bsp_material_id", p_material_id);
	material->set_meta("sourcepp_bsp_material_name", material_name);
	material->set_meta("sourcepp_bsp_material_path", resolved_material_path);
	return material;
}

Error SourcePPBSP::_build_surface_arrays_for_material(int p_material_id, Array &r_arrays) const {
	ERR_FAIL_COND_V_MSG(halfedge_mesh.is_null(), ERR_UNCONFIGURED, "Half-edge mesh must be available before creating a render mesh.");

	r_arrays.resize(Mesh::ARRAY_MAX);

	const PackedVector3Array all_vertices = halfedge_mesh->get_vertices();
	PackedVector3Array surface_vertices;
	PackedVector3Array surface_normals;
	PackedInt32Array surface_indices;

	const int face_count = halfedge_mesh->get_face_count();
	for (int face_index = 0; face_index < face_count; face_index++) {
		const int face_material_id = face_index < face_material_ids.size() ? face_material_ids[face_index] : -1;
		if (face_material_id != p_material_id) {
			continue;
		}

		const PackedInt32Array face_vertex_indices = halfedge_mesh->get_face_vertex_indices(face_index);
		if (face_vertex_indices.size() < 3) {
			continue;
		}

		const Dictionary face_projection = halfedge_mesh->get_face_projection(face_index);
		ERR_FAIL_COND_V_MSG(!face_projection.has("vertices"), ERR_INVALID_DATA, "Half-edge face projection data is missing for BSP triangulation.");
		const PackedVector2Array projected_vertices = face_projection["vertices"];
		const PackedInt32Array triangulated_indices = Geometry2D::triangulate_polygon(projected_vertices);
		if (triangulated_indices.is_empty()) {
			continue;
		}

		Vector3 face_normal = Vector3(0, 1, 0);
		const Dictionary face_data = halfedge_mesh->get_face_data(face_index);
		if (face_data.has("normal")) {
			face_normal = face_data["normal"];
		}

		const int vertex_offset = surface_vertices.size();
		for (int face_vertex_index = 0; face_vertex_index < face_vertex_indices.size(); face_vertex_index++) {
			const int mesh_vertex_index = face_vertex_indices[face_vertex_index];
			ERR_FAIL_INDEX_V(mesh_vertex_index, all_vertices.size(), ERR_INVALID_DATA);
			surface_vertices.push_back(all_vertices[mesh_vertex_index]);
			surface_normals.push_back(face_normal);
		}

		for (int triangle_index = 0; triangle_index < triangulated_indices.size(); triangle_index++) {
			surface_indices.push_back(vertex_offset + triangulated_indices[triangle_index]);
		}
	}

	r_arrays[Mesh::ARRAY_VERTEX] = surface_vertices;
	r_arrays[Mesh::ARRAY_NORMAL] = surface_normals;
	r_arrays[Mesh::ARRAY_INDEX] = surface_indices;
	return OK;
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
		HashMap<int, int> face_vertex_remap;
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
			if (face_vertex_remap.has(bsp_vertex_index)) {
				mesh_vertex_index = face_vertex_remap[bsp_vertex_index];
			} else {
				mesh_vertex_index = r_vertices.size();
				face_vertex_remap.insert(bsp_vertex_index, mesh_vertex_index);
				r_vertices.push_back(_source_to_godot_position(bsp_vertices[static_cast<size_t>(bsp_vertex_index)].position));
			}
			polygon.push_back(mesh_vertex_index);
		}

		if (polygon.size() >= 2 && polygon[0] == polygon[polygon.size() - 1]) {
			polygon.remove_at(polygon.size() - 1);
		}
		if (!_simplify_bsp_polygon(r_vertices, polygon)) {
			continue;
		}

		const int material_id = _get_face_material_id(face);
		if (_is_halfedge_compatible_polygon(r_vertices, polygon)) {
			r_faces.push_back(polygon);
			r_face_material_ids.push_back(material_id);
		} else {
			_append_triangulated_polygon(r_vertices, polygon, material_id, r_faces, r_face_material_ids);
		}
	}

	return OK;
}

Error SourcePPBSP::open(const String &p_path) {
	close();
	ERR_FAIL_COND_V_MSG(p_path.is_empty(), ERR_INVALID_PARAMETER, "BSP path must not be empty.");

	auto opened_bsp = std::make_unique<bsppp::BSP>(_to_utf8(p_path), false);
	ERR_FAIL_COND_V_MSG(!(*opened_bsp), ERR_FILE_CANT_OPEN, "Failed to open BSP file.");

	bsp = std::move(opened_bsp);
	source_path = p_path;

	const Error cache_error = _cache_lumps();
	if (cache_error != OK) {
		close();
		return cache_error;
	}

	const Error rebuild_error = _rebuild_current_halfedge_mesh();
	if (rebuild_error != OK) {
		close();
		return rebuild_error;
	}

	return OK;
}

Error SourcePPBSP::open_from_buffer(const PackedByteArray &p_data, const String &p_path) {
	close();
	ERR_FAIL_COND_V_MSG(p_data.is_empty(), ERR_INVALID_PARAMETER, "BSP data must not be empty.");

	Error temp_error = OK;
	Ref<FileAccess> temp_file = FileAccess::create_temp(FileAccess::WRITE_READ, "sourcepp_bsp_", ".bsp", true, &temp_error);
	ERR_FAIL_COND_V_MSG(temp_error != OK || temp_file.is_null(), temp_error == OK ? ERR_CANT_CREATE : temp_error, "Failed to create a temporary BSP backing file.");

	temporary_backing_path = temp_file->get_path_absolute();
	temp_file->store_buffer(p_data);
	temp_file->flush();
	temp_file->close();

	auto opened_bsp = std::make_unique<bsppp::BSP>(_to_utf8(temporary_backing_path), false);
	if (!(*opened_bsp)) {
		_clear_temporary_backing_file();
		return ERR_FILE_CANT_OPEN;
	}

	bsp = std::move(opened_bsp);
	source_path = p_path.is_empty() ? temporary_backing_path : p_path;

	const Error cache_error = _cache_lumps();
	if (cache_error != OK) {
		close();
		return cache_error;
	}

	const Error rebuild_error = _rebuild_current_halfedge_mesh();
	if (rebuild_error != OK) {
		close();
		return rebuild_error;
	}

	return OK;
}

void SourcePPBSP::close() {
	bsp.reset();
	source_path = String();
	halfedge_mesh.unref();
	face_material_ids = PackedInt32Array();
	bsp_vertices.clear();
	bsp_faces.clear();
	bsp_edges.clear();
	bsp_surf_edges.clear();
	bsp_models.clear();
	bsp_texture_info.clear();
	bsp_texture_data.clear();
	texdata_to_material_id.clear();
	material_paths.clear();
	_clear_temporary_backing_file();
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

Node3D *SourcePPBSP::create_node() const {
	ERR_FAIL_COND_V_MSG(halfedge_mesh.is_null(), nullptr, "Half-edge mesh must be available before creating a BSP node.");

	Ref<ArrayMesh> render_mesh;
	render_mesh.instantiate();

	Vector<int> used_material_ids;
	const int face_count = halfedge_mesh->get_face_count();
	for (int face_index = 0; face_index < face_count; face_index++) {
		const int material_id = face_index < face_material_ids.size() ? face_material_ids[face_index] : -1;
		bool already_added = false;
		for (int material_index = 0; material_index < used_material_ids.size(); material_index++) {
			if (used_material_ids[material_index] == material_id) {
				already_added = true;
				break;
			}
		}
		if (!already_added) {
			used_material_ids.push_back(material_id);
		}
	}

	for (int material_index = 0; material_index < used_material_ids.size(); material_index++) {
		Array arrays;
		const int material_id = used_material_ids[material_index];
		const Error build_surface_error = _build_surface_arrays_for_material(material_id, arrays);
		ERR_FAIL_COND_V(build_surface_error != OK, nullptr);

		const PackedVector3Array surface_vertices = arrays[Mesh::ARRAY_VERTEX];
		if (surface_vertices.is_empty()) {
			continue;
		}

		render_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		render_mesh->surface_set_material(render_mesh->get_surface_count() - 1, _create_import_material(material_id));
	}

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name(source_path.is_empty() ? String("SourcePPBSP") : source_path.get_file().get_basename());
	mesh_instance->set_mesh(render_mesh);
	mesh_instance->set_meta("sourcepp_bsp_path", source_path);
	mesh_instance->set_meta("sourcepp_bsp_model_index", model_index);
	return mesh_instance;
}
