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
#include "sourcepp_vtf.h"

#include "modules/halfedge/halfedge_mesh.h"

#include "core/io/dir_access.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/math/geometry_2d.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/shader.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

constexpr float SOURCE_UNIT_TO_METERS = 0.0254f;
constexpr float BSP_HALFEDGE_COPLANAR_EPSILON = 0.001f;
constexpr float BSP_HALFEDGE_COLLINEAR_EPSILON = 0.0001f;
constexpr int BSP_TEXTURE_ARRAY_SIZE = 512;

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

bool _simplify_bsp_polygon(const PackedVector3Array &p_vertices, PackedInt32Array &r_polygon, PackedVector2Array *r_uvs = nullptr) {
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
				if (r_uvs != nullptr && i < r_uvs->size()) {
					r_uvs->remove_at(i);
				}
				changed = true;
				break;
			}

			const Vector3 &prev = p_vertices[prev_vertex];
			const Vector3 &current = p_vertices[vertex];
			const Vector3 &next = p_vertices[next_vertex];
			if ((current - prev).cross(next - current).length() <= BSP_HALFEDGE_COLLINEAR_EPSILON) {
				r_polygon.remove_at(i);
				if (r_uvs != nullptr && i < r_uvs->size()) {
					r_uvs->remove_at(i);
				}
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

void _reverse_bsp_polygon(PackedInt32Array &r_polygon) {
	for (int i = 1; i < (r_polygon.size() + 1) / 2; i++) {
		const int opposite_index = r_polygon.size() - i;
		const int value = r_polygon[i];
		r_polygon.set(i, r_polygon[opposite_index]);
		r_polygon.set(opposite_index, value);
	}
}

void _reverse_bsp_polygon_uvs(PackedVector2Array &r_uvs) {
	for (int i = 1; i < (r_uvs.size() + 1) / 2; i++) {
		const int opposite_index = r_uvs.size() - i;
		const Vector2 value = r_uvs[i];
		r_uvs.set(i, r_uvs[opposite_index]);
		r_uvs.set(opposite_index, value);
	}
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

PackedInt32Array _triangulate_projected_polygon_preserve_winding(const PackedVector2Array &p_projected_vertices) {
	PackedInt32Array triangulated_indices = Geometry2D::triangulate_polygon(p_projected_vertices);
	if (!triangulated_indices.is_empty()) {
		return triangulated_indices;
	}

	PackedVector2Array reversed_projected_vertices;
	reversed_projected_vertices.resize(p_projected_vertices.size());
	for (int i = 0; i < p_projected_vertices.size(); i++) {
		reversed_projected_vertices.set(i, p_projected_vertices[p_projected_vertices.size() - 1 - i]);
	}

	triangulated_indices = Geometry2D::triangulate_polygon(reversed_projected_vertices);
	if (triangulated_indices.is_empty()) {
		return triangulated_indices;
	}

	PackedInt32Array remapped_indices;
	remapped_indices.resize(triangulated_indices.size());
	for (int i = 0; i < triangulated_indices.size(); i += 3) {
		if (i + 2 >= triangulated_indices.size()) {
			break;
		}

		remapped_indices.set(i, p_projected_vertices.size() - 1 - triangulated_indices[i]);
		remapped_indices.set(i + 1, p_projected_vertices.size() - 1 - triangulated_indices[i + 2]);
		remapped_indices.set(i + 2, p_projected_vertices.size() - 1 - triangulated_indices[i + 1]);
	}

	return remapped_indices;
}

bool _is_halfedge_compatible_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon) {
	PackedVector2Array projected_vertices;
	if (!_project_bsp_polygon(p_vertices, p_polygon, projected_vertices)) {
		return false;
	}

	return !_triangulate_projected_polygon_preserve_winding(projected_vertices).is_empty();
}

void _append_triangulated_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, const PackedVector2Array &p_uvs, int p_material_id, bool p_reverse_winding, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs) {
	PackedVector2Array projected_vertices;
	if (!_project_bsp_polygon(p_vertices, p_polygon, projected_vertices)) {
		return;
	}

	const PackedInt32Array triangulated_indices = _triangulate_projected_polygon_preserve_winding(projected_vertices);
	for (int i = 0; i < triangulated_indices.size(); i += 3) {
		if (i + 2 >= triangulated_indices.size()) {
			break;
		}

		PackedInt32Array triangle;
		PackedVector2Array triangle_uvs;
		triangle.push_back(p_polygon[triangulated_indices[i]]);
		triangle.push_back(p_polygon[triangulated_indices[i + 1]]);
		triangle.push_back(p_polygon[triangulated_indices[i + 2]]);
		triangle_uvs.push_back(p_uvs[triangulated_indices[i]]);
		triangle_uvs.push_back(p_uvs[triangulated_indices[i + 1]]);
		triangle_uvs.push_back(p_uvs[triangulated_indices[i + 2]]);
		if (p_reverse_winding) {
			const int value = triangle[1];
			triangle.set(1, triangle[2]);
			triangle.set(2, value);
			const Vector2 uv = triangle_uvs[1];
			triangle_uvs.set(1, triangle_uvs[2]);
			triangle_uvs.set(2, uv);
		}

		Plane triangle_plane;
		if (!_compute_polygon_plane(p_vertices, triangle, triangle_plane)) {
			continue;
		}

		r_faces.push_back(triangle);
		r_face_material_ids.push_back(p_material_id);
		r_face_uvs.push_back(triangle_uvs);
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
	const Array previous_face_uvs = face_uvs;

	model_index = p_model_index;
	if (bsp != nullptr) {
		const Error rebuild_error = _rebuild_current_halfedge_mesh();
		if (rebuild_error != OK) {
			model_index = previous_model_index;
			halfedge_mesh = previous_halfedge_mesh;
			face_material_ids = previous_face_material_ids;
			face_uvs = previous_face_uvs;
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
		face_uvs.clear();
		return OK;
	}

	PackedVector3Array mesh_vertices;
	Array mesh_faces;
	PackedInt32Array rebuilt_face_material_ids;
	Array rebuilt_face_uvs;
	const Error build_error = _build_model_mesh_data(model_index, mesh_vertices, mesh_faces, rebuilt_face_material_ids, rebuilt_face_uvs);
	ERR_FAIL_COND_V(build_error != OK, build_error);

	Ref<HalfEdgeMesh> mesh;
	mesh.instantiate();
	const Error set_faces_error = mesh->set_faces(mesh_vertices, mesh_faces);
	ERR_FAIL_COND_V_MSG(set_faces_error != OK, set_faces_error, "Failed to create a half-edge mesh from BSP faces.");

	halfedge_mesh = mesh;
	face_material_ids = rebuilt_face_material_ids;
	face_uvs = rebuilt_face_uvs;
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

Vector2 SourcePPBSP::_get_face_uv(const bsppp::BSPFace &p_face, const sourcepp::math::Vec3f &p_position) const {
	if (p_face.texInfo < 0 || static_cast<size_t>(p_face.texInfo) >= bsp_texture_info.size()) {
		return Vector2();
	}

	const bsppp::BSPTextureInfo &texture_info = bsp_texture_info[static_cast<size_t>(p_face.texInfo)];
	const int texture_data_index = texture_info.textureData;
	float texture_width = 1.0f;
	float texture_height = 1.0f;
	if (texture_data_index >= 0 && static_cast<size_t>(texture_data_index) < bsp_texture_data.size()) {
		const bsppp::BSPTextureData &texture_data = bsp_texture_data[static_cast<size_t>(texture_data_index)];
		texture_width = MAX(1.0f, static_cast<float>(texture_data.width));
		texture_height = MAX(1.0f, static_cast<float>(texture_data.height));
	}

	const float u = p_position[0] * texture_info.textureVector1[0] + p_position[1] * texture_info.textureVector1[1] + p_position[2] * texture_info.textureVector1[2] + texture_info.textureVector1[3];
	const float v = p_position[0] * texture_info.textureVector2[0] + p_position[1] * texture_info.textureVector2[1] + p_position[2] * texture_info.textureVector2[2] + texture_info.textureVector2[3];
	return Vector2(u / texture_width, v / texture_height);
}

Ref<Image> SourcePPBSP::_create_fallback_texture_array_image() const {
	Ref<Image> image = Image::create_empty(BSP_TEXTURE_ARRAY_SIZE, BSP_TEXTURE_ARRAY_SIZE, false, Image::FORMAT_RGBA8);
	ERR_FAIL_COND_V(image.is_null(), image);
	image->fill(Color(0.85f, 0.1f, 0.85f, 1.0f));
	const int square_size = BSP_TEXTURE_ARRAY_SIZE / 8;
	for (int y = 0; y < BSP_TEXTURE_ARRAY_SIZE; y++) {
		for (int x = 0; x < BSP_TEXTURE_ARRAY_SIZE; x++) {
			if (((x / square_size) + (y / square_size)) % 2 == 0) {
				image->set_pixel(x, y, Color(0.15f, 0.15f, 0.15f, 1.0f));
			}
		}
	}
	image->generate_mipmaps();
	return image;
}

Ref<Image> SourcePPBSP::_load_material_texture_array_image(int p_material_id) const {
	Ref<Image> fallback_image = _create_fallback_texture_array_image();
	if (p_material_id < 0 || p_material_id >= material_paths.size()) {
		return fallback_image;
	}

	const String material_path = _resolve_material_path(material_paths[p_material_id]);
	if (material_path.is_empty()) {
		return fallback_image;
	}

	Ref<SourcePPVMT> vmt;
	vmt.instantiate();
	vmt->set_resolver(resolver);
	vmt->set_resolver_game_id(resolver_game_id);
	if (vmt->open(material_path) != OK) {
		return fallback_image;
	}

	const String base_texture_path = vmt->get_resolved_base_texture_path();
	if (base_texture_path.is_empty()) {
		return fallback_image;
	}

	Ref<SourcePPVTF> vtf;
	vtf.instantiate();
	vtf->set_resolver(resolver);
	vtf->set_resolver_game_id(resolver_game_id);
	if (vtf->open(base_texture_path) != OK) {
		return fallback_image;
	}

	Ref<Image> image = vtf->get_image();
	if (image.is_null() || image->is_empty()) {
		return fallback_image;
	}

	if (image->is_compressed() && image->decompress() != OK) {
		return fallback_image;
	}
	image->clear_mipmaps();
	if (image->get_format() != Image::FORMAT_RGBA8) {
		image->convert(Image::FORMAT_RGBA8);
	}
	if (image->get_width() != BSP_TEXTURE_ARRAY_SIZE || image->get_height() != BSP_TEXTURE_ARRAY_SIZE) {
		image->resize(BSP_TEXTURE_ARRAY_SIZE, BSP_TEXTURE_ARRAY_SIZE, Image::INTERPOLATE_LANCZOS);
	}
	image->generate_mipmaps();
	return image;
}

Ref<Material> SourcePPBSP::_create_texture_array_material() const {
	Vector<Ref<Image>> layer_images;
	layer_images.resize(material_paths.size() + 1);
	for (int material_id = 0; material_id < material_paths.size(); material_id++) {
		layer_images.write[material_id] = _load_material_texture_array_image(material_id);
	}
	layer_images.write[material_paths.size()] = _create_fallback_texture_array_image();

	Ref<Texture2DArray> texture_array;
	texture_array.instantiate();
	const Error texture_array_error = texture_array->create_from_images(layer_images);
	ERR_FAIL_COND_V_MSG(texture_array_error != OK, Ref<Material>(), "Failed to create a BSP texture array.");

	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(
			"shader_type spatial;\n"
			"render_mode cull_back;\n"
			"uniform sampler2DArray sourcepp_base_textures : source_color, filter_linear_mipmap, repeat_enable;\n"
			"varying float sourcepp_texture_layer;\n"
			"void vertex() {\n"
			"	sourcepp_texture_layer = CUSTOM0.x;\n"
			"}\n"
			"void fragment() {\n"
			"	vec4 base_color = texture(sourcepp_base_textures, vec3(UV, sourcepp_texture_layer));\n"
			"	ALBEDO = base_color.rgb;\n"
			"	ALPHA = base_color.a;\n"
			"}\n");

	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	material->set_shader_parameter("sourcepp_base_textures", texture_array);
	material->set_meta("sourcepp_bsp_material_mode", "texture_array");
	material->set_meta("sourcepp_bsp_texture_layer_count", texture_array->get_layers());
	return material;
}

Error SourcePPBSP::_build_atlased_surface_arrays(Array &r_arrays) const {
	ERR_FAIL_COND_V_MSG(halfedge_mesh.is_null(), ERR_UNCONFIGURED, "Half-edge mesh must be available before creating a render mesh.");

	r_arrays.resize(Mesh::ARRAY_MAX);

	const PackedVector3Array all_vertices = halfedge_mesh->get_vertices();
	PackedVector3Array surface_vertices;
	PackedVector3Array surface_normals;
	PackedVector2Array surface_uvs;
	PackedFloat32Array surface_texture_layers;
	PackedInt32Array surface_indices;

	const int fallback_texture_layer = material_paths.size();
	const int face_count = halfedge_mesh->get_face_count();
	for (int face_index = 0; face_index < face_count; face_index++) {
		const PackedInt32Array face_vertex_indices = halfedge_mesh->get_face_vertex_indices(face_index);
		if (face_vertex_indices.size() < 3) {
			continue;
		}

		const Dictionary face_projection = halfedge_mesh->get_face_projection(face_index);
		ERR_FAIL_COND_V_MSG(!face_projection.has("vertices"), ERR_INVALID_DATA, "Half-edge face projection data is missing for BSP triangulation.");
		const PackedVector2Array projected_vertices = face_projection["vertices"];
		const PackedInt32Array triangulated_indices = _triangulate_projected_polygon_preserve_winding(projected_vertices);
		if (triangulated_indices.is_empty()) {
			continue;
		}

		Vector3 face_normal = Vector3(0, 1, 0);
		const Dictionary face_data = halfedge_mesh->get_face_data(face_index);
		if (face_data.has("normal")) {
			face_normal = face_data["normal"];
		}

		PackedVector2Array face_texture_uvs;
		if (face_index < face_uvs.size()) {
			face_texture_uvs = face_uvs[face_index];
		}

		const int face_material_id = face_index < face_material_ids.size() ? face_material_ids[face_index] : -1;
		const int texture_layer = (face_material_id >= 0 && face_material_id < material_paths.size()) ? face_material_id : fallback_texture_layer;
		const int vertex_offset = surface_vertices.size();
		for (int face_vertex_index = 0; face_vertex_index < face_vertex_indices.size(); face_vertex_index++) {
			const int mesh_vertex_index = face_vertex_indices[face_vertex_index];
			ERR_FAIL_INDEX_V(mesh_vertex_index, all_vertices.size(), ERR_INVALID_DATA);
			surface_vertices.push_back(all_vertices[mesh_vertex_index]);
			surface_normals.push_back(face_normal);
			surface_uvs.push_back(face_vertex_index < face_texture_uvs.size() ? face_texture_uvs[face_vertex_index] : Vector2());
			surface_texture_layers.push_back(static_cast<float>(texture_layer));
		}

		for (int triangle_index = 0; triangle_index < triangulated_indices.size(); triangle_index++) {
			surface_indices.push_back(vertex_offset + triangulated_indices[triangle_index]);
		}
	}

	r_arrays[Mesh::ARRAY_VERTEX] = surface_vertices;
	r_arrays[Mesh::ARRAY_NORMAL] = surface_normals;
	r_arrays[Mesh::ARRAY_TEX_UV] = surface_uvs;
	r_arrays[Mesh::ARRAY_CUSTOM0] = surface_texture_layers;
	r_arrays[Mesh::ARRAY_INDEX] = surface_indices;
	return OK;
}

Error SourcePPBSP::_build_model_mesh_data(int p_model_index, PackedVector3Array &r_vertices, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs) const {
	r_vertices = PackedVector3Array();
	r_faces.clear();
	r_face_material_ids = PackedInt32Array();
	r_face_uvs.clear();

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
		PackedVector2Array polygon_uvs;
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
			polygon_uvs.push_back(_get_face_uv(face, bsp_vertices[static_cast<size_t>(bsp_vertex_index)].position));
		}

		if (polygon.size() >= 2 && polygon[0] == polygon[polygon.size() - 1]) {
			polygon.remove_at(polygon.size() - 1);
			polygon_uvs.remove_at(polygon_uvs.size() - 1);
		}
		if (!_simplify_bsp_polygon(r_vertices, polygon, &polygon_uvs)) {
			continue;
		}
		const int material_id = _get_face_material_id(face);
		if (_is_halfedge_compatible_polygon(r_vertices, polygon)) {
			PackedInt32Array reversed_polygon = polygon;
			PackedVector2Array reversed_uvs = polygon_uvs;
			_reverse_bsp_polygon(reversed_polygon);
			_reverse_bsp_polygon_uvs(reversed_uvs);
			if (_is_halfedge_compatible_polygon(r_vertices, reversed_polygon)) {
				r_faces.push_back(reversed_polygon);
				r_face_material_ids.push_back(material_id);
				r_face_uvs.push_back(reversed_uvs);
			} else {
				_append_triangulated_polygon(r_vertices, polygon, polygon_uvs, material_id, true, r_faces, r_face_material_ids, r_face_uvs);
			}
		} else {
			_append_triangulated_polygon(r_vertices, polygon, polygon_uvs, material_id, true, r_faces, r_face_material_ids, r_face_uvs);
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
	face_uvs.clear();
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

	Array arrays;
	const Error build_surface_error = _build_atlased_surface_arrays(arrays);
	ERR_FAIL_COND_V(build_surface_error != OK, nullptr);

	const PackedVector3Array surface_vertices = arrays[Mesh::ARRAY_VERTEX];
	if (!surface_vertices.is_empty()) {
		const BitField<Mesh::ArrayFormat> surface_flags = static_cast<uint64_t>(Mesh::ARRAY_CUSTOM_R_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
		render_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), surface_flags);
		render_mesh->surface_set_material(0, _create_texture_array_material());
	}

	Node3D *root_node = memnew(Node3D);
	root_node->set_name(source_path.is_empty() ? String("SourcePPBSP") : source_path.get_file().get_basename());
	root_node->set_meta("sourcepp_bsp_path", source_path);
	root_node->set_meta("sourcepp_bsp_model_index", model_index);

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("WorldGeometry");
	mesh_instance->set_mesh(render_mesh);
	mesh_instance->set_meta("sourcepp_bsp_path", source_path);
	mesh_instance->set_meta("sourcepp_bsp_model_index", model_index);
	root_node->add_child(mesh_instance);
	return root_node;
}
