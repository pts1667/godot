/**************************************************************************/
/*  sourcepp_bsp.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp.h"

#include "entities/sourcepp_bsp_entity.h"
#include "entities/sourcepp_bsp_entity_utils.h"
#include "bsp/sourcepp_bsp_geometry.h"
#include "bsp/sourcepp_bsp_lump_utils.h"
#include "sourcepp_bsp_shape.h"
#include "sourcepp_import_cache.h"
#include "sourcepp_mdl.h"
#include "sourcepp_resolver.h"
#include "sourcepp_vmt.h"
#include "sourcepp_vtf.h"
#include "utils/sourcepp_utils.h"

#include "modules/halfedge/halfedge_mesh.h"

#include "core/io/dir_access.h"
#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/shader.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

constexpr float BSP_HALFEDGE_COLLINEAR_EPSILON = 0.0001f;
constexpr int BSP_TEXTURE_ARRAY_SIZE = 512;
constexpr size_t STATIC_PROP_NAME_LENGTH = 128;
constexpr size_t BSP_DISPINFO_SIZE = 176;
constexpr size_t BSP_DISPVERT_SIZE = 20;
constexpr size_t BSP_DISPTRI_SIZE = 2;
constexpr int BSP_MIN_DISPLACEMENT_POWER = 2;
constexpr int BSP_MAX_DISPLACEMENT_POWER = 4;

enum BSPRenderSurfaceType {
	BSP_RENDER_SURFACE_OPAQUE = 0,
	BSP_RENDER_SURFACE_TRANSPARENT = 1,
	BSP_RENDER_SURFACE_WATER = 2,
	BSP_RENDER_SURFACE_COUNT = 3,
};

String _alpha_mode_to_string(Image::AlphaMode p_alpha_mode) {
	switch (p_alpha_mode) {
		case Image::ALPHA_BIT:
			return "bit";
		case Image::ALPHA_BLEND:
			return "blend";
		case Image::ALPHA_NONE:
		default:
			return "none";
	}
}

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

bool _source_material_bool_value(const Ref<SourcePPVMT> &p_vmt, const String &p_key) {
	if (p_vmt.is_null() || !p_vmt->has_value(p_key)) {
		return false;
	}

	const String value = p_vmt->get_value(p_key).strip_edges().to_lower();
	return value == "1" || value == "true";
}

float _source_material_float_value(const Ref<SourcePPVMT> &p_vmt, const String &p_key, float p_default) {
	if (p_vmt.is_null() || !p_vmt->has_value(p_key)) {
		return p_default;
	}

	return p_vmt->get_value(p_key).to_float();
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
	ClassDB::bind_method(D_METHOD("get_static_prop_count"), &SourcePPBSP::get_static_prop_count);
	ClassDB::bind_method(D_METHOD("get_displacement_count"), &SourcePPBSP::get_displacement_count);
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
	_unmount_current_bsp_pakfile();
	resolver = p_resolver;
	_mount_current_bsp_pakfile();
}

Ref<SourcePPResolver> SourcePPBSP::get_resolver() const {
	return resolver;
}

void SourcePPBSP::set_resolver_game_id(const String &p_game_id) {
	_unmount_current_bsp_pakfile();
	resolver_game_id = p_game_id.strip_edges();
	_mount_current_bsp_pakfile();
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

void SourcePPBSP::_mount_current_bsp_pakfile() {
	if (!resolver.is_valid() || bsp == nullptr) {
		return;
	}

	const String backing_path = temporary_backing_path.is_empty() ? source_path : temporary_backing_path;
	if (backing_path.is_empty()) {
		return;
	}
	if (mounted_bsp_pakfile_path == backing_path && mounted_bsp_pakfile_game_id == resolver_game_id) {
		return;
	}

	const Error mount_error = resolver->register_bsp_pakfile(backing_path, resolver_game_id);
	if (mount_error != OK) {
		WARN_PRINT(vformat("Failed to mount BSP Pakfile lump for '%s'.", source_path));
		return;
	}

	mounted_bsp_pakfile_path = backing_path;
	mounted_bsp_pakfile_game_id = resolver_game_id;
}

void SourcePPBSP::_unmount_current_bsp_pakfile() {
	if (!resolver.is_valid() || mounted_bsp_pakfile_path.is_empty()) {
		mounted_bsp_pakfile_path = String();
		mounted_bsp_pakfile_game_id = String();
		return;
	}

	resolver->unregister_bsp_pakfile(mounted_bsp_pakfile_path, mounted_bsp_pakfile_game_id);
	mounted_bsp_pakfile_path = String();
	mounted_bsp_pakfile_game_id = String();
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
	const String normalized_material_path = SourcePPUtils::normalize_source_path(p_material_name);
	if (normalized_material_path.is_empty()) {
		return String();
	}

	const String relative_material_path = SourcePPUtils::ensure_extension(SourcePPUtils::strip_material_prefix(normalized_material_path), "vmt");
	const String materials_relative_path = String("materials/") + relative_material_path;

	String game_root;
	const String normalized_bsp_path = SourcePPUtils::normalize_source_path(source_path);
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
		if (!candidate.is_empty() && SourcePPUtils::path_exists_with_resolver(candidate, resolver, resolver_game_id)) {
			return candidate;
		}
	}

	return String();
}

Dictionary SourcePPBSP::_entity_to_dictionary(const bsppp::BSPEntityKeyValues &p_entity) const {
	Dictionary out;
	for (const bsppp::BSPEntityKeyValues::Element &element : p_entity.getKeyValues()) {
		const String key = _from_utf8(element.getKey());
		const String value = _from_utf8(element.getValue());
		if (!out.has(key)) {
			out[key] = value;
			continue;
		}

		Variant existing = out[key];
		Array values;
		if (existing.get_type() == Variant::ARRAY) {
			values = existing;
		} else {
			values.push_back(existing);
		}
		values.push_back(value);
		out[key] = values;
	}
	return out;
}

String SourcePPBSP::_get_entity_value(const bsppp::BSPEntityKeyValues &p_entity, const String &p_key, const String &p_default) const {
	const String requested_key = p_key.strip_edges().to_lower();
	for (const bsppp::BSPEntityKeyValues::Element &element : p_entity.getKeyValues()) {
		if (_from_utf8(element.getKey()).strip_edges().to_lower() == requested_key) {
			return _from_utf8(element.getValue());
		}
	}
	return p_default;
}

int SourcePPBSP::_get_entity_bmodel_index(const bsppp::BSPEntityKeyValues &p_entity) const {
	const String model_value = _get_entity_value(p_entity, "model").strip_edges();
	if (!model_value.begins_with("*")) {
		return -1;
	}

	const String model_index_string = model_value.substr(1).strip_edges();
	if (!model_index_string.is_valid_int()) {
		return -1;
	}
	return model_index_string.to_int();
}

Transform3D SourcePPBSP::_get_entity_transform(const bsppp::BSPEntityKeyValues &p_entity) const {
	const Vector3 source_origin = SourcePPUtils::parse_source_vector_string(_get_entity_value(p_entity, "origin"));
	const Vector3 source_angles = SourcePPUtils::parse_source_vector_string(_get_entity_value(p_entity, "angles"));
	return Transform3D(SourcePPUtils::source_angles_to_godot_basis(source_angles), SourcePPUtils::source_vector_to_godot_direction(source_origin) * SourcePPUtils::SOURCE_UNIT_TO_METERS);
}

Error SourcePPBSP::_cache_lumps() {
	ERR_FAIL_COND_V_MSG(bsp == nullptr, ERR_UNCONFIGURED, "No BSP is currently open.");

	bsp_entities = bsp->getLumpData<bsppp::BSPLump::ENTITIES>();
	bsp_vertices = bsp->getLumpData<bsppp::BSPLump::VERTEXES>();
	bsp_faces = bsp->getLumpData<bsppp::BSPLump::FACES>();
	bsp_edges = bsp->getLumpData<bsppp::BSPLump::EDGES>();
	bsp_surf_edges = bsp->getLumpData<bsppp::BSPLump::SURFEDGES>();
	bsp_models = bsp->getLumpData<bsppp::BSPLump::MODELS>();
	bsp_texture_info = bsp->getLumpData<bsppp::BSPLump::TEXINFO>();
	bsp_texture_data = bsp->getLumpData<bsppp::BSPLump::TEXDATA>();

	const Error material_error = _cache_material_paths();
	ERR_FAIL_COND_V(material_error != OK, material_error);

	const Error displacement_error = _cache_displacements();
	ERR_FAIL_COND_V(displacement_error != OK, displacement_error);

	return _cache_static_props();
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

bool SourcePPBSP::_is_skybox_material(int p_material_id) const {
	if (p_material_id < 0 || p_material_id >= material_paths.size()) {
		return false;
	}

	const String normalized_material_path = SourcePPUtils::strip_material_prefix(material_paths[p_material_id]).replace("\\", "/").to_lower();
	return normalized_material_path == "tools/toolsskybox" ||
			normalized_material_path == "tools/toolsskybox.vmt" ||
			normalized_material_path == "tools/toolsskybox2d" ||
			normalized_material_path == "tools/toolsskybox2d.vmt";
}

Vector2 SourcePPBSP::_get_face_uv(const bsppp::BSPFace &p_face, const sourcepp::math::Vec3f &p_position) const {
	return _get_face_uv(p_face, Vector3(p_position[0], p_position[1], p_position[2]));
}

Vector2 SourcePPBSP::_get_face_uv(const bsppp::BSPFace &p_face, const Vector3 &p_position) const {
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

	const float u = p_position.x * texture_info.textureVector1[0] + p_position.y * texture_info.textureVector1[1] + p_position.z * texture_info.textureVector1[2] + texture_info.textureVector1[3];
	const float v = p_position.x * texture_info.textureVector2[0] + p_position.y * texture_info.textureVector2[1] + p_position.z * texture_info.textureVector2[2] + texture_info.textureVector2[3];
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

Dictionary SourcePPBSP::_get_asset_source_info(const String &p_asset_path, bool p_missing) const {
	Dictionary info;
	info["asset_source"] = "unknown";
	info["source_path"] = String();
	info["game_id"] = String();
	info["search_path"] = "GAME";
	info["is_missing"] = p_missing;
	info["is_pak"] = false;

	if (p_asset_path.is_empty()) {
		return info;
	}

	if (FileAccess::exists(p_asset_path)) {
		info["asset_source"] = "non-PAK";
		info["source_path"] = p_asset_path;
		info["is_missing"] = p_missing;
		return info;
	}

	if (resolver.is_valid()) {
		Dictionary resolver_info = resolver_game_id.is_empty() ? resolver->get_file_source_info(p_asset_path) : resolver->get_file_source_info(p_asset_path, resolver_game_id);
		if (!resolver_info.is_empty()) {
			resolver_info["is_missing"] = p_missing || static_cast<bool>(resolver_info["is_missing"]);
			return resolver_info;
		}
	}

	return info;
}

Error SourcePPBSP::_cache_displacements() {
	bsp_displacement_infos.clear();
	bsp_displacement_vertices.clear();
	bsp_displacement_triangles.clear();

	ERR_FAIL_COND_V_MSG(bsp == nullptr, ERR_UNCONFIGURED, "No BSP is currently open.");

	const auto dispinfo_lump = bsp->getLumpData(bsppp::BSPLump::DISPINFO);
	const auto dispverts_lump = bsp->getLumpData(bsppp::BSPLump::DISP_VERTS);
	const auto disptris_lump = bsp->getLumpData(bsppp::BSPLump::DISP_TRIS);
	if (!dispinfo_lump || dispinfo_lump->empty()) {
		return OK;
	}

	if (dispinfo_lump->size() % BSP_DISPINFO_SIZE != 0) {
		WARN_PRINT(vformat("BSP displacement info lump has %d trailing bytes; ignoring incomplete records.", static_cast<int>(dispinfo_lump->size() % BSP_DISPINFO_SIZE)));
	}
	if (dispverts_lump && dispverts_lump->size() % BSP_DISPVERT_SIZE != 0) {
		WARN_PRINT(vformat("BSP displacement vertex lump has %d trailing bytes; ignoring incomplete records.", static_cast<int>(dispverts_lump->size() % BSP_DISPVERT_SIZE)));
	}
	if (disptris_lump && disptris_lump->size() % BSP_DISPTRI_SIZE != 0) {
		WARN_PRINT(vformat("BSP displacement triangle lump has %d trailing bytes; ignoring incomplete records.", static_cast<int>(disptris_lump->size() % BSP_DISPTRI_SIZE)));
	}

	const size_t dispinfo_count = dispinfo_lump->size() / BSP_DISPINFO_SIZE;
	bsp_displacement_infos.reserve(dispinfo_count);
	for (size_t dispinfo_index = 0; dispinfo_index < dispinfo_count; dispinfo_index++) {
		const size_t offset = dispinfo_index * BSP_DISPINFO_SIZE;
		DisplacementInfo info;
		info.start_position = SourcePPBSPLumpUtils::read_source_vector3_le(*dispinfo_lump, offset);
		info.disp_vert_start = SourcePPBSPLumpUtils::read_i32_le(*dispinfo_lump, offset + 12);
		info.disp_tri_start = SourcePPBSPLumpUtils::read_i32_le(*dispinfo_lump, offset + 16);
		info.power = SourcePPBSPLumpUtils::read_i32_le(*dispinfo_lump, offset + 20);
		info.contents = SourcePPBSPLumpUtils::read_i32_le(*dispinfo_lump, offset + 32);
		info.map_face = SourcePPBSPLumpUtils::read_u16_le(*dispinfo_lump, offset + 36);
		bsp_displacement_infos.push_back(info);
	}

	if (dispverts_lump) {
		const size_t dispvert_count = dispverts_lump->size() / BSP_DISPVERT_SIZE;
		bsp_displacement_vertices.reserve(dispvert_count);
		for (size_t dispvert_index = 0; dispvert_index < dispvert_count; dispvert_index++) {
			const size_t offset = dispvert_index * BSP_DISPVERT_SIZE;
			DisplacementVertex vertex;
			vertex.vector = SourcePPBSPLumpUtils::read_source_vector3_le(*dispverts_lump, offset);
			vertex.distance = SourcePPBSPLumpUtils::read_f32_le(*dispverts_lump, offset + 12);
			bsp_displacement_vertices.push_back(vertex);
		}
	}

	if (disptris_lump) {
		const size_t disptri_count = disptris_lump->size() / BSP_DISPTRI_SIZE;
		bsp_displacement_triangles.reserve(disptri_count);
		for (size_t disptri_index = 0; disptri_index < disptri_count; disptri_index++) {
			DisplacementTriangle triangle;
			triangle.tags = SourcePPBSPLumpUtils::read_u16_le(*disptris_lump, disptri_index * BSP_DISPTRI_SIZE);
			bsp_displacement_triangles.push_back(triangle);
		}
	}

	return OK;
}

void SourcePPBSP::_record_asset_metadata(Dictionary *r_asset_metadata, const String &p_asset_path, const String &p_asset_type, const String &p_material_type, bool p_missing, const Dictionary &p_metadata) const {
	if (r_asset_metadata == nullptr) {
		return;
	}

	const String asset_path = SourcePPUtils::normalize_source_path(p_asset_path);
	if (asset_path.is_empty()) {
		return;
	}

	Dictionary asset_info = _get_asset_source_info(asset_path, p_missing);
	asset_info["asset_type"] = p_asset_type;
	asset_info["material_type"] = p_material_type;
	asset_info["metadata"] = p_metadata;
	(*r_asset_metadata)[asset_path] = asset_info;
}

Ref<Image> SourcePPBSP::_load_material_texture_array_image(int p_material_id, SourcePPImportCache *p_import_cache, const Ref<Image> &p_fallback_image, Image::AlphaMode *r_alpha_mode, Dictionary *r_asset_metadata, bool p_warn_missing) const {
	if (r_alpha_mode != nullptr) {
		*r_alpha_mode = Image::ALPHA_NONE;
	}

	Ref<Image> fallback_image = p_fallback_image.is_valid() ? p_fallback_image : _create_fallback_texture_array_image();
	if (p_material_id < 0 || p_material_id >= material_paths.size()) {
		return fallback_image;
	}

	const String material_name = material_paths[p_material_id];
	const String requested_material_path = SourcePPUtils::ensure_extension(SourcePPUtils::strip_material_prefix(SourcePPUtils::normalize_source_path(material_name)), "vmt");
	const String material_path = _resolve_material_path(material_name);
	if (material_path.is_empty()) {
		Dictionary material_metadata;
		material_metadata["material_id"] = p_material_id;
		material_metadata["material_name"] = material_name;
		material_metadata["requested_material_path"] = requested_material_path;
		material_metadata["resolved_material_path"] = String();
		material_metadata["reason"] = "material_not_found";
		_record_asset_metadata(r_asset_metadata, requested_material_path, "material", "unknown", true, material_metadata);
		if (p_warn_missing) {
			WARN_PRINT(vformat("SourcePP BSP missing material '%s' while importing '%s'.", material_name, source_path));
		}
		return fallback_image;
	}

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
	if (vmt_error != OK || vmt.is_null()) {
		Dictionary material_metadata;
		material_metadata["material_id"] = p_material_id;
		material_metadata["material_name"] = material_name;
		material_metadata["requested_material_path"] = requested_material_path;
		material_metadata["resolved_material_path"] = material_path;
		material_metadata["reason"] = "material_open_failed";
		_record_asset_metadata(r_asset_metadata, material_path, "material", "unknown", true, material_metadata);
		if (p_warn_missing) {
			WARN_PRINT(vformat("SourcePP BSP material '%s' resolved to '%s' but could not be opened.", material_name, material_path));
		}
		return fallback_image;
	}

	const String material_type = vmt->get_shader();
	const bool is_water_material = material_type.strip_edges().to_lower() == "water" || material_type.strip_edges().to_lower().begins_with("water_");
	Dictionary material_metadata;
	material_metadata["material_id"] = p_material_id;
	material_metadata["material_name"] = material_name;
	material_metadata["requested_material_path"] = requested_material_path;
	material_metadata["resolved_material_path"] = material_path;
	material_metadata["shader"] = material_type;
	material_metadata["properties"] = vmt->get_properties();
	material_metadata["texture_dependencies"] = vmt->get_texture_dependencies();
	material_metadata["resolved_texture_dependencies"] = vmt->get_resolved_texture_dependencies();
	material_metadata["is_translucent"] = _source_material_bool_value(vmt, "$translucent");
	material_metadata["is_additive"] = _source_material_bool_value(vmt, "$additive");
	material_metadata["alpha"] = _source_material_float_value(vmt, "$alpha", 1.0f);
	_record_asset_metadata(r_asset_metadata, material_path, "material", material_type, false, material_metadata);

	String base_texture_request_path = vmt->get_base_texture_path();
	String base_texture_path = vmt->get_resolved_base_texture_path();
	String texture_role = "$basetexture";
	if (is_water_material && base_texture_path.is_empty()) {
		base_texture_request_path = vmt->get_normal_map_path();
		base_texture_path = vmt->get_resolved_normal_map_path();
		texture_role = "$normalmap";
		if (base_texture_request_path.is_empty()) {
			base_texture_request_path = "dev/water_normal";
		}
	}
	if (base_texture_path.is_empty()) {
		Dictionary texture_metadata;
		texture_metadata["material_id"] = p_material_id;
		texture_metadata["material_name"] = material_name;
		texture_metadata["material_path"] = material_path;
		texture_metadata["texture_role"] = texture_role;
		texture_metadata["requested_texture_path"] = base_texture_request_path;
		texture_metadata["resolved_texture_path"] = String();
		texture_metadata["reason"] = base_texture_request_path.is_empty() ? "texture_not_declared" : "texture_not_found";
		const String metadata_key = base_texture_request_path.is_empty() ? material_path + ":" + texture_role : SourcePPUtils::ensure_extension(base_texture_request_path, "vtf");
		_record_asset_metadata(r_asset_metadata, metadata_key, "texture", material_type, true, texture_metadata);
		if (p_warn_missing) {
			WARN_PRINT(vformat("SourcePP BSP material '%s' has no resolvable %s while importing '%s'.", material_path, texture_role, source_path));
		}
		return fallback_image;
	}

	Error texture_error = OK;
	Ref<Image> image;
	if (p_import_cache != nullptr) {
		image = p_import_cache->get_vtf_image(base_texture_path, resolver, resolver_game_id, &texture_error);
	} else {
		Ref<SourcePPVTF> vtf;
		vtf.instantiate();
		vtf->set_resolver(resolver);
		vtf->set_resolver_game_id(resolver_game_id);
		texture_error = vtf->open(base_texture_path);
		if (texture_error == OK) {
			image = vtf->get_image();
		}
	}
	if (texture_error != OK) {
		Dictionary texture_metadata;
		texture_metadata["material_id"] = p_material_id;
		texture_metadata["material_name"] = material_name;
		texture_metadata["material_path"] = material_path;
		texture_metadata["texture_role"] = texture_role;
		texture_metadata["requested_texture_path"] = base_texture_request_path;
		texture_metadata["resolved_texture_path"] = base_texture_path;
		texture_metadata["reason"] = "texture_open_failed";
		_record_asset_metadata(r_asset_metadata, base_texture_path, "texture", material_type, true, texture_metadata);
		if (p_warn_missing) {
			WARN_PRINT(vformat("SourcePP BSP texture '%s' referenced by material '%s' could not be opened.", base_texture_path, material_path));
		}
		return fallback_image;
	}

	if (image.is_null() || image->is_empty()) {
		Dictionary texture_metadata;
		texture_metadata["material_id"] = p_material_id;
		texture_metadata["material_name"] = material_name;
		texture_metadata["material_path"] = material_path;
		texture_metadata["texture_role"] = texture_role;
		texture_metadata["requested_texture_path"] = base_texture_request_path;
		texture_metadata["resolved_texture_path"] = base_texture_path;
		texture_metadata["reason"] = "texture_image_empty";
		_record_asset_metadata(r_asset_metadata, base_texture_path, "texture", material_type, true, texture_metadata);
		if (p_warn_missing) {
			WARN_PRINT(vformat("SourcePP BSP texture '%s' referenced by material '%s' did not produce an image.", base_texture_path, material_path));
		}
		return fallback_image;
	}
	image = image->duplicate();

	if (image->is_compressed() && image->decompress() != OK) {
		Dictionary texture_metadata;
		texture_metadata["material_id"] = p_material_id;
		texture_metadata["material_name"] = material_name;
		texture_metadata["material_path"] = material_path;
		texture_metadata["texture_role"] = texture_role;
		texture_metadata["requested_texture_path"] = base_texture_request_path;
		texture_metadata["resolved_texture_path"] = base_texture_path;
		texture_metadata["reason"] = "texture_decompress_failed";
		_record_asset_metadata(r_asset_metadata, base_texture_path, "texture", material_type, true, texture_metadata);
		if (p_warn_missing) {
			WARN_PRINT(vformat("SourcePP BSP texture '%s' referenced by material '%s' could not be decompressed.", base_texture_path, material_path));
		}
		return fallback_image;
	}
	image->clear_mipmaps();
	if (image->get_format() != Image::FORMAT_RGBA8) {
		image->convert(Image::FORMAT_RGBA8);
	}
	if (image->get_width() != BSP_TEXTURE_ARRAY_SIZE || image->get_height() != BSP_TEXTURE_ARRAY_SIZE) {
		image->resize(BSP_TEXTURE_ARRAY_SIZE, BSP_TEXTURE_ARRAY_SIZE, Image::INTERPOLATE_LANCZOS);
	}
	if (r_alpha_mode != nullptr) {
		*r_alpha_mode = image->detect_alpha();
	}
	const Image::AlphaMode alpha_mode = image->detect_alpha();
	Dictionary texture_metadata;
	texture_metadata["material_id"] = p_material_id;
	texture_metadata["material_name"] = material_name;
	texture_metadata["material_path"] = material_path;
	texture_metadata["texture_role"] = texture_role;
	texture_metadata["requested_texture_path"] = base_texture_request_path;
	texture_metadata["resolved_texture_path"] = base_texture_path;
	texture_metadata["alpha_mode"] = _alpha_mode_to_string(alpha_mode);
	texture_metadata["width"] = image->get_width();
	texture_metadata["height"] = image->get_height();
	_record_asset_metadata(r_asset_metadata, base_texture_path, "texture", material_type, false, texture_metadata);
	image->generate_mipmaps();
	return image;
}

Vector<Ref<Image>> SourcePPBSP::_load_texture_array_images(SourcePPImportCache *p_import_cache, const Ref<Image> &p_fallback_image, Dictionary *r_asset_metadata, std::vector<Image::AlphaMode> &r_alpha_modes, bool p_warn_missing) const {
	Vector<Ref<Image>> layer_images;
	layer_images.resize(material_paths.size() + 1);
	r_alpha_modes.clear();
	r_alpha_modes.resize(material_paths.size(), Image::ALPHA_NONE);
	for (int material_id = 0; material_id < material_paths.size(); material_id++) {
		layer_images.write[material_id] = _load_material_texture_array_image(material_id, p_import_cache, p_fallback_image, &r_alpha_modes[static_cast<size_t>(material_id)], r_asset_metadata, p_warn_missing);
	}
	layer_images.write[material_paths.size()] = p_fallback_image.is_valid() ? p_fallback_image : _create_fallback_texture_array_image();
	return layer_images;
}

bool SourcePPBSP::_is_material_transparent(int p_material_id, Image::AlphaMode p_alpha_mode, SourcePPImportCache *p_import_cache) const {
	if (p_material_id < 0 || p_material_id >= material_paths.size()) {
		return false;
	}
	if (_is_material_water(p_material_id, p_import_cache)) {
		return true;
	}

	const String material_path = _resolve_material_path(material_paths[p_material_id]);
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
			if (_source_material_bool_value(vmt, "$translucent") || _source_material_bool_value(vmt, "$additive")) {
				return true;
			}
			if (_source_material_float_value(vmt, "$alpha", 1.0f) < 0.999f) {
				return true;
			}
		}
	}

	return false;
}

bool SourcePPBSP::_is_material_water(int p_material_id, SourcePPImportCache *p_import_cache) const {
	if (p_material_id < 0 || p_material_id >= material_paths.size()) {
		return false;
	}

	const String material_path = _resolve_material_path(material_paths[p_material_id]);
	if (material_path.is_empty()) {
		return false;
	}

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
	if (vmt_error != OK || vmt.is_null()) {
		return false;
	}

	const String shader_name = vmt->get_shader().strip_edges().to_lower();
	return shader_name == "water" || shader_name.begins_with("water_");
}

Ref<Material> SourcePPBSP::_create_texture_array_material(int p_surface_type, const Vector<Ref<Image>> &p_layer_images) const {
	Ref<Texture2DArray> texture_array;
	texture_array.instantiate();
	const Error texture_array_error = texture_array->create_from_images(p_layer_images);
	ERR_FAIL_COND_V_MSG(texture_array_error != OK, Ref<Material>(), "Failed to create a BSP texture array.");

	Ref<Shader> shader;
	shader.instantiate();
	if (p_surface_type == BSP_RENDER_SURFACE_WATER) {
		shader->set_code(
				"shader_type spatial;\n"
				"render_mode cull_back, blend_mix, depth_prepass_alpha;\n"
				"uniform sampler2DArray sourcepp_base_textures : source_color, filter_linear_mipmap, repeat_enable;\n"
				"uniform vec4 sourcepp_water_tint : source_color = vec4(0.45, 0.72, 0.82, 0.68);\n"
				"uniform vec2 sourcepp_water_scroll_1 = vec2(0.018, 0.006);\n"
				"uniform vec2 sourcepp_water_scroll_2 = vec2(-0.011, 0.014);\n"
				"uniform float sourcepp_water_scale = 1.0;\n"
				"varying float sourcepp_texture_layer;\n"
				"void vertex() {\n"
				"	sourcepp_texture_layer = CUSTOM0.x;\n"
				"}\n"
				"void fragment() {\n"
				"	vec2 uv = UV * sourcepp_water_scale;\n"
				"	vec4 water_a = texture(sourcepp_base_textures, vec3(uv + sourcepp_water_scroll_1 * TIME, sourcepp_texture_layer));\n"
				"	vec4 water_b = texture(sourcepp_base_textures, vec3(uv + sourcepp_water_scroll_2 * TIME, sourcepp_texture_layer));\n"
				"	vec4 base_color = mix(water_a, water_b, 0.5) * sourcepp_water_tint;\n"
				"	ALBEDO = base_color.rgb;\n"
				"	ALPHA = clamp(base_color.a * sourcepp_water_tint.a, 0.25, 0.9);\n"
				"	ROUGHNESS = 0.08;\n"
				"	METALLIC = 0.0;\n"
				"}\n");
	} else if (p_surface_type == BSP_RENDER_SURFACE_TRANSPARENT) {
		shader->set_code(
				"shader_type spatial;\n"
				"render_mode cull_back, blend_mix;\n"
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
	} else {
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
				"	ALPHA_SCISSOR_THRESHOLD = 0.5;\n"
				"}\n");
	}

	Ref<ShaderMaterial> material;
	material.instantiate();
	material->set_shader(shader);
	material->set_shader_parameter("sourcepp_base_textures", texture_array);
	const String material_mode = p_surface_type == BSP_RENDER_SURFACE_WATER ? String("texture_array_water") : (p_surface_type == BSP_RENDER_SURFACE_TRANSPARENT ? String("texture_array_transparent") : String("texture_array_opaque"));
	material->set_meta("sourcepp_bsp_material_mode", material_mode);
	material->set_meta("sourcepp_bsp_texture_layer_count", texture_array->get_layers());
	if (p_surface_type == BSP_RENDER_SURFACE_TRANSPARENT || p_surface_type == BSP_RENDER_SURFACE_WATER) {
		material->set_render_priority(1);
	}
	return material;
}

Error SourcePPBSP::_build_atlased_surface_arrays(const Ref<HalfEdgeMesh> &p_mesh, const PackedInt32Array &p_face_material_ids, const Array &p_face_uvs, int p_surface_type, const std::vector<bool> &p_transparent_materials, const std::vector<bool> &p_water_materials, Array &r_arrays) const {
	ERR_FAIL_COND_V_MSG(p_mesh.is_null(), ERR_UNCONFIGURED, "Half-edge mesh must be available before creating a render mesh.");

	r_arrays.resize(Mesh::ARRAY_MAX);

	const PackedVector3Array all_vertices = p_mesh->get_vertices();
	PackedVector3Array surface_vertices;
	PackedVector3Array surface_normals;
	PackedVector2Array surface_uvs;
	PackedFloat32Array surface_texture_layers;
	PackedInt32Array surface_indices;

	const int fallback_texture_layer = material_paths.size();
	const int face_count = p_mesh->get_face_count();
	for (int face_index = 0; face_index < face_count; face_index++) {
		const PackedInt32Array face_vertex_indices = p_mesh->get_face_vertex_indices(face_index);
		if (face_vertex_indices.size() < 3) {
			continue;
		}

		const Dictionary face_projection = p_mesh->get_face_projection(face_index);
		ERR_FAIL_COND_V_MSG(!face_projection.has("vertices"), ERR_INVALID_DATA, "Half-edge face projection data is missing for BSP triangulation.");
		const PackedVector2Array projected_vertices = face_projection["vertices"];
		const PackedInt32Array triangulated_indices = SourcePPBSPGeometry::triangulate_projected_polygon_preserve_winding(projected_vertices);
		if (triangulated_indices.is_empty()) {
			continue;
		}

		Vector3 face_normal = Vector3(0, 1, 0);
		const Dictionary face_data = p_mesh->get_face_data(face_index);
		if (face_data.has("normal")) {
			face_normal = face_data["normal"];
		}

		PackedVector2Array face_texture_uvs;
		if (face_index < p_face_uvs.size()) {
			face_texture_uvs = p_face_uvs[face_index];
		}

		const int face_material_id = face_index < p_face_material_ids.size() ? p_face_material_ids[face_index] : -1;
		const bool face_is_water = face_material_id >= 0 && static_cast<size_t>(face_material_id) < p_water_materials.size() && p_water_materials[static_cast<size_t>(face_material_id)];
		const bool face_is_transparent = face_material_id >= 0 && static_cast<size_t>(face_material_id) < p_transparent_materials.size() && p_transparent_materials[static_cast<size_t>(face_material_id)];
		const int face_surface_type = face_is_water ? BSP_RENDER_SURFACE_WATER : (face_is_transparent ? BSP_RENDER_SURFACE_TRANSPARENT : BSP_RENDER_SURFACE_OPAQUE);
		if (face_surface_type != p_surface_type) {
			continue;
		}

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

Error SourcePPBSP::_cache_static_props() {
	static_props.clear();
	ERR_FAIL_COND_V_MSG(bsp == nullptr, ERR_UNCONFIGURED, "No BSP is currently open.");

	const std::optional<std::vector<std::byte>> prop_lump = bsp->getGameLumpData(bsppp::BSPGameLump::SIGNATURE_STATIC_PROPS);
	if (!prop_lump.has_value() || prop_lump->empty()) {
		return OK;
	}

	const int version = static_cast<int>(bsp->getGameLumpVersion(bsppp::BSPGameLump::SIGNATURE_STATIC_PROPS));
	const size_t record_size = SourcePPBSPLumpUtils::static_prop_record_size(version);
	if (record_size == 0) {
		WARN_PRINT(vformat("Unsupported BSP static prop lump version %d while importing '%s'.", version, source_path));
		return OK;
	}
	if (version > 10) {
		WARN_PRINT(vformat("BSP static prop lump version %d is newer than Source SDK 2013; parsing common v7+ fields for '%s'.", version, source_path));
	}

	const std::vector<std::byte> &bytes = prop_lump.value();
	size_t offset = 0;
	if (!SourcePPBSPLumpUtils::can_read_bytes(bytes, offset, sizeof(int32_t))) {
		return ERR_INVALID_DATA;
	}

	const int32_t dict_count = SourcePPBSPLumpUtils::read_i32_le(bytes, offset);
	offset += sizeof(int32_t);
	ERR_FAIL_COND_V_MSG(dict_count < 0, ERR_INVALID_DATA, "BSP static prop dictionary count is negative.");
	ERR_FAIL_COND_V_MSG(!SourcePPBSPLumpUtils::can_read_bytes(bytes, offset, static_cast<size_t>(dict_count) * STATIC_PROP_NAME_LENGTH), ERR_INVALID_DATA, "BSP static prop dictionary is truncated.");

	PackedStringArray model_paths;
	model_paths.resize(dict_count);
	for (int dict_index = 0; dict_index < dict_count; dict_index++) {
		String model_path = SourcePPUtils::normalize_source_path(SourcePPBSPLumpUtils::read_fixed_utf8_string(bytes, offset, STATIC_PROP_NAME_LENGTH));
		if (!model_path.is_empty()) {
			model_path = SourcePPUtils::ensure_extension(model_path, "mdl");
		}
		model_paths.set(dict_index, model_path);
		offset += STATIC_PROP_NAME_LENGTH;
	}

	ERR_FAIL_COND_V_MSG(!SourcePPBSPLumpUtils::can_read_bytes(bytes, offset, sizeof(int32_t)), ERR_INVALID_DATA, "BSP static prop leaf count is missing.");
	const int32_t leaf_count = SourcePPBSPLumpUtils::read_i32_le(bytes, offset);
	offset += sizeof(int32_t);
	ERR_FAIL_COND_V_MSG(leaf_count < 0, ERR_INVALID_DATA, "BSP static prop leaf count is negative.");
	ERR_FAIL_COND_V_MSG(!SourcePPBSPLumpUtils::can_read_bytes(bytes, offset, static_cast<size_t>(leaf_count) * sizeof(uint16_t)), ERR_INVALID_DATA, "BSP static prop leaf array is truncated.");

	std::vector<uint16_t> leaves;
	leaves.resize(static_cast<size_t>(leaf_count));
	for (int leaf_index = 0; leaf_index < leaf_count; leaf_index++) {
		leaves[static_cast<size_t>(leaf_index)] = SourcePPBSPLumpUtils::read_u16_le(bytes, offset);
		offset += sizeof(uint16_t);
	}

	ERR_FAIL_COND_V_MSG(!SourcePPBSPLumpUtils::can_read_bytes(bytes, offset, sizeof(int32_t)), ERR_INVALID_DATA, "BSP static prop count is missing.");
	const int32_t prop_count = SourcePPBSPLumpUtils::read_i32_le(bytes, offset);
	offset += sizeof(int32_t);
	ERR_FAIL_COND_V_MSG(prop_count < 0, ERR_INVALID_DATA, "BSP static prop count is negative.");
	ERR_FAIL_COND_V_MSG(!SourcePPBSPLumpUtils::can_read_bytes(bytes, offset, static_cast<size_t>(prop_count) * record_size), ERR_INVALID_DATA, "BSP static prop array is truncated.");

	static_props.reserve(static_cast<size_t>(prop_count));
	for (int prop_index = 0; prop_index < prop_count; prop_index++) {
		const size_t prop_offset = offset + static_cast<size_t>(prop_index) * record_size;
		StaticProp prop;
		prop.origin = SourcePPBSPLumpUtils::read_source_vector3_le(bytes, prop_offset + 0);
		prop.angles = SourcePPBSPLumpUtils::read_source_vector3_le(bytes, prop_offset + 12);
		prop.prop_type = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 24);
		prop.first_leaf = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 26);
		prop.leaf_count = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 28);
		prop.solid = SourcePPBSPLumpUtils::read_u8(bytes, prop_offset + 30);
		prop.skin = SourcePPBSPLumpUtils::read_i32_le(bytes, prop_offset + 32);
		prop.fade_min_dist = SourcePPBSPLumpUtils::read_f32_le(bytes, prop_offset + 36);
		prop.fade_max_dist = SourcePPBSPLumpUtils::read_f32_le(bytes, prop_offset + 40);
		prop.lighting_origin = SourcePPBSPLumpUtils::read_source_vector3_le(bytes, prop_offset + 44);
		if (version <= 6) {
			prop.flags = SourcePPBSPLumpUtils::read_u8(bytes, prop_offset + 31);
			if (version >= 5) {
				prop.forced_fade_scale = SourcePPBSPLumpUtils::read_f32_le(bytes, prop_offset + 56);
			}
			if (version >= 6) {
				prop.min_dx_level = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 60);
				prop.max_dx_level = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 62);
			}
		} else {
			prop.forced_fade_scale = SourcePPBSPLumpUtils::read_f32_le(bytes, prop_offset + 56);
			prop.min_dx_level = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 60);
			prop.max_dx_level = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 62);
			prop.flags = SourcePPBSPLumpUtils::read_u32_le(bytes, prop_offset + 64);
			prop.lightmap_resolution_x = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 68);
			prop.lightmap_resolution_y = SourcePPBSPLumpUtils::read_u16_le(bytes, prop_offset + 70);
		}

		if (prop.prop_type < static_cast<uint16_t>(model_paths.size())) {
			prop.model_path = model_paths[prop.prop_type];
		} else {
			WARN_PRINT(vformat("BSP static prop %d references invalid prop dictionary entry %d while importing '%s'.", prop_index, prop.prop_type, source_path));
		}

		const uint32_t first_leaf = prop.first_leaf;
		const uint32_t static_prop_leaf_count = prop.leaf_count;
		if (first_leaf + static_prop_leaf_count <= leaves.size()) {
			for (uint32_t leaf_index = 0; leaf_index < static_prop_leaf_count; leaf_index++) {
				prop.leaves.push_back(leaves[static_cast<size_t>(first_leaf + leaf_index)]);
			}
		} else if (static_prop_leaf_count > 0) {
			WARN_PRINT(vformat("BSP static prop %d has an out-of-range leaf span while importing '%s'.", prop_index, source_path));
		}

		static_props.push_back(prop);
	}

	return OK;
}

Ref<ArrayMesh> SourcePPBSP::_create_array_mesh_from_halfedge(const Ref<HalfEdgeMesh> &p_mesh, const PackedInt32Array &p_face_material_ids, const Array &p_face_uvs, const std::vector<bool> &p_transparent_materials, const std::vector<bool> &p_water_materials, const Vector<Ref<Material>> &p_surface_materials) const {
	Ref<ArrayMesh> render_mesh;
	render_mesh.instantiate();

	const BitField<Mesh::ArrayFormat> surface_flags = static_cast<uint64_t>(Mesh::ARRAY_CUSTOM_R_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
	for (int surface_type = 0; surface_type < BSP_RENDER_SURFACE_COUNT; surface_type++) {
		Array arrays;
		const Error build_surface_error = _build_atlased_surface_arrays(p_mesh, p_face_material_ids, p_face_uvs, surface_type, p_transparent_materials, p_water_materials, arrays);
		ERR_FAIL_COND_V(build_surface_error != OK, Ref<ArrayMesh>());

		const PackedVector3Array surface_vertices = arrays[Mesh::ARRAY_VERTEX];
		if (surface_vertices.is_empty()) {
			continue;
		}

		render_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), surface_flags);
		render_mesh->surface_set_material(render_mesh->get_surface_count() - 1, surface_type < p_surface_materials.size() ? p_surface_materials[surface_type] : Ref<Material>());
	}

	return render_mesh;
}

Ref<ArrayMesh> SourcePPBSP::_create_model_array_mesh(int p_model_index, const std::vector<bool> &p_transparent_materials, const std::vector<bool> &p_water_materials, const Vector<Ref<Material>> &p_surface_materials) const {
	PackedVector3Array model_vertices;
	Array model_faces;
	PackedInt32Array model_face_material_ids;
	Array model_face_uvs;
	const Error model_error = _build_model_mesh_data(p_model_index, model_vertices, model_faces, model_face_material_ids, model_face_uvs);
	ERR_FAIL_COND_V(model_error != OK, Ref<ArrayMesh>());

	Ref<HalfEdgeMesh> model_halfedge_mesh;
	model_halfedge_mesh.instantiate();
	const Error set_faces_error = model_halfedge_mesh->set_faces(model_vertices, model_faces);
	ERR_FAIL_COND_V_MSG(set_faces_error != OK, Ref<ArrayMesh>(), "Failed to create a half-edge mesh from BSP bmodel faces.");

	return _create_array_mesh_from_halfedge(model_halfedge_mesh, model_face_material_ids, model_face_uvs, p_transparent_materials, p_water_materials, p_surface_materials);
}

bool SourcePPBSP::_extract_face_source_polygon(const bsppp::BSPFace &p_face, PackedVector3Array &r_source_vertices) const {
	r_source_vertices = PackedVector3Array();
	if (p_face.numEdges < 3) {
		return false;
	}

	int previous_bsp_vertex_index = -1;
	for (int face_edge_offset = 0; face_edge_offset < p_face.numEdges; face_edge_offset++) {
		const int surf_edge_index = p_face.firstEdge + face_edge_offset;
		if (surf_edge_index < 0 || static_cast<size_t>(surf_edge_index) >= bsp_surf_edges.size()) {
			r_source_vertices = PackedVector3Array();
			return false;
		}

		const int edge_reference = bsp_surf_edges[static_cast<size_t>(surf_edge_index)].surfEdge;
		const int edge_index = edge_reference >= 0 ? edge_reference : -edge_reference;
		if (edge_index < 0 || static_cast<size_t>(edge_index) >= bsp_edges.size()) {
			r_source_vertices = PackedVector3Array();
			return false;
		}

		const bsppp::BSPEdge &edge = bsp_edges[static_cast<size_t>(edge_index)];
		const int bsp_vertex_index = edge_reference >= 0 ? static_cast<int>(edge.v0) : static_cast<int>(edge.v1);
		if (bsp_vertex_index < 0 || static_cast<size_t>(bsp_vertex_index) >= bsp_vertices.size()) {
			r_source_vertices = PackedVector3Array();
			return false;
		}

		if (previous_bsp_vertex_index == bsp_vertex_index) {
			continue;
		}
		previous_bsp_vertex_index = bsp_vertex_index;

		const sourcepp::math::Vec3f &position = bsp_vertices[static_cast<size_t>(bsp_vertex_index)].position;
		r_source_vertices.push_back(Vector3(position[0], position[1], position[2]));
	}

	if (r_source_vertices.size() >= 2 && r_source_vertices[0].is_equal_approx(r_source_vertices[r_source_vertices.size() - 1])) {
		r_source_vertices.remove_at(r_source_vertices.size() - 1);
	}

	bool changed = true;
	while (changed && r_source_vertices.size() >= 3) {
		changed = false;
		for (int i = 0; i < r_source_vertices.size(); i++) {
			const int previous_index = (i - 1 + r_source_vertices.size()) % r_source_vertices.size();
			const int next_index = (i + 1) % r_source_vertices.size();
			const Vector3 previous = r_source_vertices[previous_index];
			const Vector3 current = r_source_vertices[i];
			const Vector3 next = r_source_vertices[next_index];
			if (previous.is_equal_approx(current) || current.is_equal_approx(next) || (current - previous).cross(next - current).length() <= BSP_HALFEDGE_COLLINEAR_EPSILON) {
				r_source_vertices.remove_at(i);
				changed = true;
				break;
			}
		}
	}

	for (int i = 0; i < r_source_vertices.size(); i++) {
		for (int j = i + 1; j < r_source_vertices.size(); j++) {
			if (r_source_vertices[i].is_equal_approx(r_source_vertices[j])) {
				return false;
			}
		}
	}

	return r_source_vertices.size() >= 3;
}

bool SourcePPBSP::_append_displacement_mesh_data(const bsppp::BSPFace &p_face, const PackedVector3Array &p_source_polygon, int p_material_id, PackedVector3Array &r_vertices, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs) const {
	ERR_FAIL_COND_V_MSG(p_face.dispInfo < 0 || static_cast<size_t>(p_face.dispInfo) >= bsp_displacement_infos.size(), false, "BSP face references an out-of-range displacement info index.");
	const DisplacementInfo &disp_info = bsp_displacement_infos[static_cast<size_t>(p_face.dispInfo)];
	if (disp_info.power < BSP_MIN_DISPLACEMENT_POWER || disp_info.power > BSP_MAX_DISPLACEMENT_POWER) {
		WARN_PRINT(vformat("Skipping BSP displacement with unsupported power %d.", disp_info.power));
		return false;
	}
	if (p_source_polygon.size() != 4) {
		WARN_PRINT(vformat("Skipping BSP displacement on a non-quad face with %d vertices.", p_source_polygon.size()));
		return false;
	}

	const int side_quads = 1 << disp_info.power;
	const int side_vertices = side_quads + 1;
	const int disp_vertex_count = SourcePPBSPLumpUtils::displacement_power_vertex_count(disp_info.power);
	const int disp_triangle_count = SourcePPBSPLumpUtils::displacement_power_triangle_count(disp_info.power);
	if (disp_info.disp_vert_start < 0 || static_cast<int64_t>(disp_info.disp_vert_start) + disp_vertex_count > static_cast<int64_t>(bsp_displacement_vertices.size())) {
		WARN_PRINT("Skipping BSP displacement with out-of-range displacement vertex data.");
		return false;
	}
	if (disp_info.disp_tri_start < 0 || static_cast<int64_t>(disp_info.disp_tri_start) + disp_triangle_count > static_cast<int64_t>(bsp_displacement_triangles.size())) {
		WARN_PRINT("BSP displacement triangle tag data is missing or truncated; importing geometry anyway because tags are ignored.");
	}

	int start_corner = 0;
	float best_start_distance = p_source_polygon[0].distance_squared_to(disp_info.start_position);
	for (int corner = 1; corner < p_source_polygon.size(); corner++) {
		const float distance = p_source_polygon[corner].distance_squared_to(disp_info.start_position);
		if (distance < best_start_distance) {
			best_start_distance = distance;
			start_corner = corner;
		}
	}

	Vector3 corners[4];
	for (int i = 0; i < 4; i++) {
		corners[i] = p_source_polygon[(start_corner + i) % 4];
	}

	PackedVector3Array reference_vertices;
	reference_vertices.resize(4);
	for (int i = 0; i < 4; i++) {
		reference_vertices.set(i, SourcePPUtils::source_vector_to_godot_direction(p_source_polygon[i]) * SourcePPUtils::SOURCE_UNIT_TO_METERS);
	}
	PackedInt32Array reference_polygon;
	reference_polygon.push_back(0);
	reference_polygon.push_back(3);
	reference_polygon.push_back(2);
	reference_polygon.push_back(1);
	Plane reference_plane;
	if (!SourcePPBSPGeometry::compute_polygon_plane(reference_vertices, reference_polygon, reference_plane)) {
		WARN_PRINT("Skipping BSP displacement with a degenerate base face.");
		return false;
	}
	const Vector3 reference_normal = reference_plane.normal.normalized();

	std::vector<int> grid_indices;
	grid_indices.resize(static_cast<size_t>(side_vertices * side_vertices));
	std::vector<Vector2> grid_uvs;
	grid_uvs.resize(static_cast<size_t>(side_vertices * side_vertices));
	for (int y = 0; y < side_vertices; y++) {
		const float v = static_cast<float>(y) / static_cast<float>(side_quads);
		const Vector3 left = corners[0].lerp(corners[3], v);
		const Vector3 right = corners[1].lerp(corners[2], v);
		for (int x = 0; x < side_vertices; x++) {
			const float u = static_cast<float>(x) / static_cast<float>(side_quads);
			const int grid_index = y * side_vertices + x;
			const DisplacementVertex &disp_vertex = bsp_displacement_vertices[static_cast<size_t>(disp_info.disp_vert_start + grid_index)];
			const Vector3 source_position = left.lerp(right, u) + disp_vertex.vector * disp_vertex.distance;
			grid_indices[static_cast<size_t>(grid_index)] = r_vertices.size();
			grid_uvs[static_cast<size_t>(grid_index)] = _get_face_uv(p_face, source_position);
			r_vertices.push_back(SourcePPUtils::source_vector_to_godot_direction(source_position) * SourcePPUtils::SOURCE_UNIT_TO_METERS);
		}
	}

	for (int y = 0; y < side_quads; y++) {
		for (int x = 0; x < side_quads; x++) {
			const int v00 = grid_indices[static_cast<size_t>(y * side_vertices + x)];
			const int v10 = grid_indices[static_cast<size_t>(y * side_vertices + x + 1)];
			const int v01 = grid_indices[static_cast<size_t>((y + 1) * side_vertices + x)];
			const int v11 = grid_indices[static_cast<size_t>((y + 1) * side_vertices + x + 1)];
			const int indices[6] = { v00, v10, v11, v00, v11, v01 };
			const int uv_indices[6] = {
				y * side_vertices + x,
				y * side_vertices + x + 1,
				(y + 1) * side_vertices + x + 1,
				y * side_vertices + x,
				(y + 1) * side_vertices + x + 1,
				(y + 1) * side_vertices + x
			};

			for (int triangle_offset = 0; triangle_offset < 6; triangle_offset += 3) {
				PackedInt32Array triangle;
				PackedVector2Array triangle_uvs;
				triangle.push_back(indices[triangle_offset]);
				triangle.push_back(indices[triangle_offset + 1]);
				triangle.push_back(indices[triangle_offset + 2]);
				triangle_uvs.push_back(grid_uvs[static_cast<size_t>(uv_indices[triangle_offset])]);
				triangle_uvs.push_back(grid_uvs[static_cast<size_t>(uv_indices[triangle_offset + 1])]);
				triangle_uvs.push_back(grid_uvs[static_cast<size_t>(uv_indices[triangle_offset + 2])]);

				Plane triangle_plane;
				if (!SourcePPBSPGeometry::compute_polygon_plane(r_vertices, triangle, triangle_plane)) {
					continue;
				}
				if (triangle_plane.normal.dot(reference_normal) < 0.0f) {
					const int index = triangle[1];
					triangle.set(1, triangle[2]);
					triangle.set(2, index);
					const Vector2 uv = triangle_uvs[1];
					triangle_uvs.set(1, triangle_uvs[2]);
					triangle_uvs.set(2, uv);
				}

				r_faces.push_back(triangle);
				r_face_material_ids.push_back(p_material_id);
				r_face_uvs.push_back(triangle_uvs);
			}
		}
	}

	return true;
}

Error SourcePPBSP::_build_model_mesh_data(int p_model_index, PackedVector3Array &r_vertices, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs) const {
	r_vertices = PackedVector3Array();
	r_faces.clear();
	r_face_material_ids = PackedInt32Array();
	r_face_uvs.clear();

	ERR_FAIL_COND_V_MSG(bsp == nullptr, ERR_UNCONFIGURED, "No BSP is currently open.");
	ERR_FAIL_INDEX_V_MSG(p_model_index, static_cast<int>(bsp_models.size()), ERR_INVALID_PARAMETER, "Requested BSP brush model is out of range.");

	const bsppp::BSPBrushModel &model = bsp_models[static_cast<size_t>(p_model_index)];
	ERR_FAIL_COND_V_MSG(model.firstFace < 0 || model.numFaces < 0, ERR_INVALID_DATA, "BSP brush model has a negative face range.");
	ERR_FAIL_COND_V_MSG(static_cast<int64_t>(model.firstFace) + static_cast<int64_t>(model.numFaces) > static_cast<int64_t>(bsp_faces.size()), ERR_INVALID_DATA, "BSP brush model face range is out of bounds.");

	for (int model_face_offset = 0; model_face_offset < model.numFaces; model_face_offset++) {
		const int bsp_face_index = model.firstFace + model_face_offset;
		if (bsp_face_index < 0 || static_cast<size_t>(bsp_face_index) >= bsp_faces.size()) {
			continue;
		}

		const bsppp::BSPFace &face = bsp_faces[static_cast<size_t>(bsp_face_index)];
		if (face.numEdges < 3) {
			continue;
		}

		PackedVector3Array source_polygon;
		if (!_extract_face_source_polygon(face, source_polygon)) {
			continue;
		}

		const int material_id = _get_face_material_id(face);
		if (_is_skybox_material(material_id)) {
			continue;
		}
		if (face.dispInfo >= 0) {
			_append_displacement_mesh_data(face, source_polygon, material_id, r_vertices, r_faces, r_face_material_ids, r_face_uvs);
			continue;
		}

		PackedInt32Array polygon;
		PackedVector2Array polygon_uvs;
		for (int source_vertex_index = 0; source_vertex_index < source_polygon.size(); source_vertex_index++) {
			const int mesh_vertex_index = r_vertices.size();
			r_vertices.push_back(SourcePPUtils::source_vector_to_godot_direction(source_polygon[source_vertex_index]) * SourcePPUtils::SOURCE_UNIT_TO_METERS);
			polygon.push_back(mesh_vertex_index);
			polygon_uvs.push_back(_get_face_uv(face, source_polygon[source_vertex_index]));
		}

		if (polygon.size() >= 2 && polygon[0] == polygon[polygon.size() - 1]) {
			polygon.remove_at(polygon.size() - 1);
			polygon_uvs.remove_at(polygon_uvs.size() - 1);
		}
		if (!SourcePPBSPGeometry::simplify_polygon(r_vertices, polygon, &polygon_uvs)) {
			continue;
		}
		if (SourcePPBSPGeometry::is_halfedge_compatible_polygon(r_vertices, polygon)) {
			PackedInt32Array reversed_polygon = polygon;
			PackedVector2Array reversed_uvs = polygon_uvs;
			SourcePPBSPGeometry::reverse_polygon(reversed_polygon);
			SourcePPBSPGeometry::reverse_polygon_uvs(reversed_uvs);
			if (SourcePPBSPGeometry::is_halfedge_compatible_polygon(r_vertices, reversed_polygon)) {
				r_faces.push_back(reversed_polygon);
				r_face_material_ids.push_back(material_id);
				r_face_uvs.push_back(reversed_uvs);
			} else {
				SourcePPBSPGeometry::append_triangulated_polygon(r_vertices, polygon, polygon_uvs, material_id, true, r_faces, r_face_material_ids, r_face_uvs);
			}
		} else {
			SourcePPBSPGeometry::append_triangulated_polygon(r_vertices, polygon, polygon_uvs, material_id, true, r_faces, r_face_material_ids, r_face_uvs);
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
	_mount_current_bsp_pakfile();

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
	_mount_current_bsp_pakfile();

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
	_unmount_current_bsp_pakfile();
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
	bsp_entities.clear();
	bsp_texture_info.clear();
	bsp_texture_data.clear();
	bsp_displacement_infos.clear();
	bsp_displacement_vertices.clear();
	bsp_displacement_triangles.clear();
	texdata_to_material_id.clear();
	material_paths.clear();
	static_props.clear();
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

int SourcePPBSP::get_static_prop_count() const {
	return static_cast<int>(static_props.size());
}

int SourcePPBSP::get_displacement_count() const {
	return static_cast<int>(bsp_displacement_infos.size());
}

PackedStringArray SourcePPBSP::get_material_paths() const {
	return material_paths;
}

Node3D *SourcePPBSP::create_node() const {
	ERR_FAIL_COND_V_MSG(bsp == nullptr, nullptr, "No BSP is currently open.");
	ERR_FAIL_COND_V_MSG(halfedge_mesh.is_null(), nullptr, "Half-edge mesh must be available before creating a BSP node.");

	SourcePPImportCache import_cache;
	const Ref<Image> fallback_image = _create_fallback_texture_array_image();
	Dictionary asset_metadata;
	std::vector<Image::AlphaMode> material_alpha_modes;
	const Vector<Ref<Image>> layer_images = _load_texture_array_images(&import_cache, fallback_image, &asset_metadata, material_alpha_modes, true);

	std::vector<bool> transparent_materials;
	transparent_materials.resize(material_paths.size(), false);
	std::vector<bool> water_materials;
	water_materials.resize(material_paths.size(), false);
	for (int material_id = 0; material_id < material_paths.size(); material_id++) {
		water_materials[static_cast<size_t>(material_id)] = _is_material_water(material_id, &import_cache);
		const Image::AlphaMode alpha_mode = static_cast<size_t>(material_id) < material_alpha_modes.size() ? material_alpha_modes[static_cast<size_t>(material_id)] : Image::ALPHA_NONE;
		transparent_materials[static_cast<size_t>(material_id)] = _is_material_transparent(material_id, alpha_mode, &import_cache);
	}

	Vector<Ref<Material>> surface_materials;
	surface_materials.resize(BSP_RENDER_SURFACE_COUNT);
	for (int surface_type = 0; surface_type < BSP_RENDER_SURFACE_COUNT; surface_type++) {
		Ref<Material> surface_material = _create_texture_array_material(surface_type, layer_images);
		if (surface_material.is_valid()) {
			surface_material->set_meta("sourcepp_bsp_asset_metadata", asset_metadata);
		}
		surface_materials.write[surface_type] = surface_material;
	}

	const Ref<ArrayMesh> render_mesh = _create_array_mesh_from_halfedge(halfedge_mesh, face_material_ids, face_uvs, transparent_materials, water_materials, surface_materials);
	ERR_FAIL_COND_V(render_mesh.is_null(), nullptr);

	Node3D *root_node = memnew(Node3D);
	root_node->set_name(source_path.is_empty() ? String("SourcePPBSP") : source_path.get_file().get_basename());
	root_node->set_meta("sourcepp_bsp_path", source_path);
	root_node->set_meta("sourcepp_bsp_model_index", model_index);
	root_node->set_meta("sourcepp_bsp_displacement_count", static_cast<int>(bsp_displacement_infos.size()));
	root_node->set_meta("sourcepp_bsp_asset_metadata", asset_metadata);

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_name("WorldGeometry");
	mesh_instance->set_mesh(render_mesh);
	mesh_instance->set_meta("sourcepp_bsp_path", source_path);
	mesh_instance->set_meta("sourcepp_bsp_model_index", model_index);
	mesh_instance->set_meta("sourcepp_bsp_displacement_count", static_cast<int>(bsp_displacement_infos.size()));
	mesh_instance->set_meta("sourcepp_bsp_asset_metadata", asset_metadata);
	root_node->add_child(mesh_instance);

	const PackedVector3Array collision_faces = SourcePPBSPGeometry::build_collision_faces_from_mesh(render_mesh);
	if (!collision_faces.is_empty()) {
		StaticBody3D *collision_body = memnew(StaticBody3D);
		collision_body->set_name("WorldCollision");
		collision_body->set_meta("sourcepp_bsp_path", source_path);
		collision_body->set_meta("sourcepp_bsp_model_index", model_index);
		root_node->add_child(collision_body);

		Ref<BSPShape3D> bsp_shape;
		bsp_shape.instantiate();
		bsp_shape->set_faces(collision_faces);

		CollisionShape3D *collision_shape = memnew(CollisionShape3D);
		collision_shape->set_name("BSPShape3D");
		collision_shape->set_shape(bsp_shape);
		collision_shape->set_meta("sourcepp_bsp_path", source_path);
		collision_shape->set_meta("sourcepp_bsp_model_index", model_index);
		collision_shape->set_meta("sourcepp_bsp_collision_face_count", bsp_shape->get_face_count());
		collision_shape->set_meta("sourcepp_bsp_collision_bounds", bsp_shape->get_bounds());
		collision_body->add_child(collision_shape);
	}

	if (model_index != 0) {
		return root_node;
	}

	Node3D *static_props_node = memnew(Node3D);
	static_props_node->set_name("StaticProps");
	static_props_node->set_meta("sourcepp_bsp_path", source_path);
	static_props_node->set_meta("sourcepp_static_prop_count", static_cast<int>(static_props.size()));
	root_node->add_child(static_props_node);

	HashMap<String, Ref<SourcePPMDL>> static_prop_model_cache;
	HashMap<String, bool> static_prop_missing_model_cache;
	for (int prop_index = 0; prop_index < static_cast<int>(static_props.size()); prop_index++) {
		const StaticProp &prop = static_props[static_cast<size_t>(prop_index)];

		Node3D *prop_node = memnew(Node3D);
		String prop_name = prop.model_path.is_empty() ? vformat("StaticProp_%d", prop_index) : vformat("StaticProp_%d_%s", prop_index, prop.model_path.get_file().get_basename());
		prop_name = prop_name.validate_node_name();
		if (prop_name.is_empty()) {
			prop_name = vformat("StaticProp_%d", prop_index);
		}
		prop_node->set_name(prop_name);
		prop_node->set_transform(Transform3D(SourcePPUtils::source_angles_to_godot_basis(prop.angles), SourcePPUtils::source_vector_to_godot_direction(prop.origin) * SourcePPUtils::SOURCE_UNIT_TO_METERS));
		prop_node->set_meta("sourcepp_bsp_path", source_path);
		prop_node->set_meta("sourcepp_static_prop_index", prop_index);
		prop_node->set_meta("sourcepp_static_prop_model", prop.model_path);
		prop_node->set_meta("sourcepp_static_prop_prop_type", prop.prop_type);
		prop_node->set_meta("sourcepp_static_prop_origin_source", prop.origin);
		prop_node->set_meta("sourcepp_static_prop_angles_source", prop.angles);
		prop_node->set_meta("sourcepp_static_prop_first_leaf", prop.first_leaf);
		prop_node->set_meta("sourcepp_static_prop_leaf_count", prop.leaf_count);
		prop_node->set_meta("sourcepp_static_prop_leaves", prop.leaves);
		prop_node->set_meta("sourcepp_static_prop_solid", prop.solid);
		prop_node->set_meta("sourcepp_static_prop_flags", prop.flags);
		prop_node->set_meta("sourcepp_static_prop_skin", prop.skin);
		prop_node->set_meta("sourcepp_static_prop_fade_min_dist", prop.fade_min_dist);
		prop_node->set_meta("sourcepp_static_prop_fade_max_dist", prop.fade_max_dist);
		prop_node->set_meta("sourcepp_static_prop_lighting_origin_source", prop.lighting_origin);
		prop_node->set_meta("sourcepp_static_prop_forced_fade_scale", prop.forced_fade_scale);
		prop_node->set_meta("sourcepp_static_prop_min_dx_level", prop.min_dx_level);
		prop_node->set_meta("sourcepp_static_prop_max_dx_level", prop.max_dx_level);
		prop_node->set_meta("sourcepp_static_prop_lightmap_resolution_x", prop.lightmap_resolution_x);
		prop_node->set_meta("sourcepp_static_prop_lightmap_resolution_y", prop.lightmap_resolution_y);
		static_props_node->add_child(prop_node);

		if (prop.model_path.is_empty()) {
			prop_node->set_meta("sourcepp_static_prop_missing_model", true);
			continue;
		}

		Ref<SourcePPMDL> prop_mdl;
		if (Ref<SourcePPMDL> *cached_mdl = static_prop_model_cache.getptr(prop.model_path)) {
			prop_mdl = *cached_mdl;
		} else if (!static_prop_missing_model_cache.has(prop.model_path)) {
			prop_mdl.instantiate();
			prop_mdl->set_resolver(resolver);
			prop_mdl->set_resolver_game_id(resolver_game_id);
			const Error mdl_open_error = prop_mdl->open(prop.model_path);
			if (mdl_open_error == OK) {
				static_prop_model_cache.insert(prop.model_path, prop_mdl);
			} else {
				static_prop_missing_model_cache.insert(prop.model_path, true);
				prop_mdl.unref();
				WARN_PRINT(vformat("Failed to import BSP static prop model '%s' while importing '%s'.", prop.model_path, source_path));
			}
		}

		if (prop_mdl.is_null()) {
			prop_node->set_meta("sourcepp_static_prop_missing_model", true);
			continue;
		}

		const int skin_family = MAX(prop.skin, 0);
		Node3D *model_node = prop_mdl->create_model_node(skin_family, false, false);
		if (model_node == nullptr && skin_family != 0) {
			WARN_PRINT(vformat("BSP static prop %d requested unavailable skin %d for '%s'; retrying skin 0.", prop_index, prop.skin, prop.model_path));
			model_node = prop_mdl->create_model_node(0, false, false);
		}
		if (model_node == nullptr) {
			prop_node->set_meta("sourcepp_static_prop_model_import_failed", true);
			WARN_PRINT(vformat("Failed to create node for BSP static prop model '%s' while importing '%s'.", prop.model_path, source_path));
			continue;
		}
		model_node->set_name("Model");
		model_node->set_meta("sourcepp_static_prop_index", prop_index);
		prop_node->add_child(model_node);
	}

	Node3D *bmodels_node = memnew(Node3D);
	bmodels_node->set_name("BModels");
	root_node->add_child(bmodels_node);

	std::vector<bool> referenced_models;
	referenced_models.resize(bsp_models.size(), false);
	if (!referenced_models.empty()) {
		referenced_models[0] = true;
	}

	for (int entity_index = 0; entity_index < static_cast<int>(bsp_entities.size()); entity_index++) {
		const bsppp::BSPEntityKeyValues &entity = bsp_entities[static_cast<size_t>(entity_index)];
		const int bmodel_index = _get_entity_bmodel_index(entity);
		if (bmodel_index < 0) {
			continue;
		}
		if (bmodel_index == 0 || bmodel_index >= static_cast<int>(bsp_models.size())) {
			WARN_PRINT(vformat("SourcePP BSP entity %d references invalid bmodel '*%d' while importing '%s'.", entity_index, bmodel_index, source_path));
			continue;
		}
		referenced_models[static_cast<size_t>(bmodel_index)] = true;

		const String classname = _get_entity_value(entity, "classname", "bmodel");
		const String targetname = _get_entity_value(entity, "targetname");
		String node_name = vformat("BModel_%d_%s", bmodel_index, classname);
		if (!targetname.is_empty()) {
			node_name += "_" + targetname;
		}
		node_name = node_name.validate_node_name();
		if (node_name.is_empty()) {
			node_name = vformat("BModel_%d", bmodel_index);
		}

		const Dictionary entity_keyvalues = _entity_to_dictionary(entity);
		const Array entity_outputs = SourcePPBSPEntityUtils::parse_source_outputs(entity);

		Node3D *entity_node = SourcePPBSPEntityUtils::create_brush_entity_node(classname);
		entity_node->set_name(node_name);
		Transform3D entity_transform = _get_entity_transform(entity);
		entity_transform.basis = (entity_transform.basis * Basis(Vector3(1, 0, 0), Math::deg_to_rad(90.0))).orthonormalized();
		entity_node->set_transform(entity_transform);
		SourcePPBSPEntityUtils::setup_entity_node(entity_node, classname, targetname, entity_index, bmodel_index, entity_keyvalues, entity_outputs);
		SourcePPBSPEntityUtils::configure_specific_node(entity_node, entity_keyvalues);
		entity_node->set_meta("sourcepp_bsp_model_index", bmodel_index);
		entity_node->set_meta("sourcepp_bsp_entity_index", entity_index);
		entity_node->set_meta("sourcepp_bsp_entity_classname", classname);
		if (!targetname.is_empty()) {
			entity_node->set_meta("sourcepp_bsp_entity_targetname", targetname);
		}
		entity_node->set_meta("sourcepp_bsp_entity_keyvalues", entity_keyvalues);
		entity_node->set_meta("sourcepp_bsp_entity_outputs", entity_outputs);
		bmodels_node->add_child(entity_node);

		const Ref<ArrayMesh> bmodel_mesh = _create_model_array_mesh(bmodel_index, transparent_materials, water_materials, surface_materials);
		if (bmodel_mesh.is_null() || bmodel_mesh->get_surface_count() == 0) {
			WARN_PRINT(vformat("SourcePP BSP bmodel '*%d' referenced by entity %d did not produce visible geometry.", bmodel_index, entity_index));
			continue;
		}

		SourcePPBSPEntityUtils::add_geometry_child(entity_node, bmodel_mesh, source_path, bmodel_index, entity_index, asset_metadata);
		const String classname_lower = classname.to_lower();
		if (SourcePPBSPEntityUtils::is_trigger_class(classname_lower) || SourcePPBSPEntityUtils::is_body_class(classname_lower)) {
			const bool collision_disabled = classname_lower == "func_brush" && _dict_int(entity_keyvalues, "Solidity", 0) == 1;
			SourcePPBSPEntityUtils::add_collision_child(entity_node, bmodel_mesh, source_path, bmodel_index, entity_index, collision_disabled);
		}
	}

	Node3D *point_entities_node = nullptr;
	HashMap<String, Ref<SourcePPMDL>> entity_model_cache;
	HashMap<String, bool> entity_missing_model_cache;
	bool light_environment_imported = false;
	for (int entity_index = 0; entity_index < static_cast<int>(bsp_entities.size()); entity_index++) {
		const bsppp::BSPEntityKeyValues &entity = bsp_entities[static_cast<size_t>(entity_index)];
		if (_get_entity_bmodel_index(entity) >= 0) {
			continue;
		}

		const String classname = _get_entity_value(entity, "classname");
		const String classname_lower = classname.to_lower();
		if (classname_lower.is_empty() || classname_lower == "worldspawn") {
			continue;
		}
		if (classname_lower == "light_environment") {
			if (light_environment_imported) {
				continue;
			}
			light_environment_imported = true;
		}

		const String targetname = _get_entity_value(entity, "targetname");
		const String model_path = SourcePPBSPEntityUtils::normalize_source_model_path(_get_entity_value(entity, "model"));
		const Dictionary entity_keyvalues = _entity_to_dictionary(entity);
		const Array entity_outputs = SourcePPBSPEntityUtils::parse_source_outputs(entity);

		Node3D *entity_node = SourcePPBSPEntityUtils::create_point_entity_node(classname);
		String node_name = targetname.is_empty() ? classname : classname + "_" + targetname;
		if (targetname.is_empty() && !model_path.is_empty()) {
			node_name += "_" + model_path.get_file().get_basename();
		}
		node_name = node_name.validate_node_name();
		if (node_name.is_empty()) {
			node_name = vformat("Entity_%d_%s", entity_index, classname);
		}
		entity_node->set_name(node_name);
		entity_node->set_transform(_get_entity_transform(entity));
		SourcePPBSPEntityUtils::setup_entity_node(entity_node, classname, targetname, entity_index, -1, entity_keyvalues, entity_outputs);
		SourcePPBSPEntityUtils::configure_specific_node(entity_node, entity_keyvalues);
		entity_node->set_meta("sourcepp_bsp_entity_model", model_path);
		entity_node->set_meta("sourcepp_bsp_entity_is_dynamic_model", SourcePPBSPEntityUtils::is_dynamic_model_entity_class(classname_lower));
		entity_node->set_meta("sourcepp_bsp_entity_is_physics_model", SourcePPBSPEntityUtils::is_physics_entity_class(classname_lower));

		if (point_entities_node == nullptr) {
			point_entities_node = memnew(Node3D);
			point_entities_node->set_name("PointEntities");
			root_node->add_child(point_entities_node);
		}
		point_entities_node->add_child(entity_node);

		if (model_path.is_empty()) {
			continue;
		}

		Ref<SourcePPMDL> entity_mdl;
		if (Ref<SourcePPMDL> *cached_mdl = entity_model_cache.getptr(model_path)) {
			entity_mdl = *cached_mdl;
		} else if (!entity_missing_model_cache.has(model_path)) {
			entity_mdl.instantiate();
			entity_mdl->set_resolver(resolver);
			entity_mdl->set_resolver_game_id(resolver_game_id);
			const Error mdl_open_error = entity_mdl->open(model_path);
			if (mdl_open_error == OK) {
				entity_model_cache.insert(model_path, entity_mdl);
			} else {
				entity_missing_model_cache.insert(model_path, true);
				entity_mdl.unref();
				entity_node->set_meta("sourcepp_bsp_entity_missing_model", true);
				WARN_PRINT(vformat("Failed to import BSP entity model '%s' for entity %d (%s) while importing '%s'.", model_path, entity_index, classname, source_path));
			}
		}

		if (entity_mdl.is_null()) {
			entity_node->set_meta("sourcepp_bsp_entity_missing_model", true);
			continue;
		}

		const int skin_family = MAX(_dict_int(entity_keyvalues, "skin", 0), 0);
		Node3D *model_node = entity_mdl->create_model_node(skin_family, false, false);
		if (model_node == nullptr && skin_family != 0) {
			WARN_PRINT(vformat("BSP entity %d (%s) requested unavailable skin %d for '%s'; retrying skin 0.", entity_index, classname, skin_family, model_path));
			model_node = entity_mdl->create_model_node(0, false, false);
		}
		if (model_node == nullptr) {
			entity_node->set_meta("sourcepp_bsp_entity_model_import_failed", true);
			WARN_PRINT(vformat("Failed to create node for BSP entity model '%s' on entity %d (%s) while importing '%s'.", model_path, entity_index, classname, source_path));
			continue;
		}
		model_node->set_name("Model");
		model_node->set_meta("sourcepp_bsp_entity_index", entity_index);
		model_node->set_meta("sourcepp_bsp_entity_classname", classname);
		model_node->set_meta("sourcepp_bsp_entity_model", model_path);
		entity_node->add_child(model_node);

		if (SourcePPBSPEntityUtils::is_physics_entity_class(classname_lower)) {
			if (CollisionObject3D *collision_body = Object::cast_to<CollisionObject3D>(entity_node)) {
				SourcePPBSPEntityUtils::add_bounds_collision_child(collision_body, model_node, source_path, entity_index, model_path);
			}
		}
	}

	Node3D *unreferenced_node = nullptr;
	for (int bmodel_index = 1; bmodel_index < static_cast<int>(bsp_models.size()); bmodel_index++) {
		if (static_cast<size_t>(bmodel_index) < referenced_models.size() && referenced_models[static_cast<size_t>(bmodel_index)]) {
			continue;
		}

		const Ref<ArrayMesh> bmodel_mesh = _create_model_array_mesh(bmodel_index, transparent_materials, water_materials, surface_materials);
		if (bmodel_mesh.is_null() || bmodel_mesh->get_surface_count() == 0) {
			continue;
		}
		if (unreferenced_node == nullptr) {
			unreferenced_node = memnew(Node3D);
			unreferenced_node->set_name("Unreferenced");
			bmodels_node->add_child(unreferenced_node);
		}

		Node3D *bmodel_node = memnew(Node3D);
		bmodel_node->set_name(vformat("BModel_%d", bmodel_index));
		bmodel_node->set_rotation_degrees(Vector3(90, 0, 0));
		bmodel_node->set_meta("sourcepp_bsp_model_index", bmodel_index);
		bmodel_node->set_meta("sourcepp_bsp_unreferenced", true);
		unreferenced_node->add_child(bmodel_node);

		MeshInstance3D *bmodel_mesh_instance = memnew(MeshInstance3D);
		bmodel_mesh_instance->set_name("Geometry");
		bmodel_mesh_instance->set_mesh(bmodel_mesh);
		bmodel_mesh_instance->set_meta("sourcepp_bsp_path", source_path);
		bmodel_mesh_instance->set_meta("sourcepp_bsp_model_index", bmodel_index);
		bmodel_mesh_instance->set_meta("sourcepp_bsp_asset_metadata", asset_metadata);
		bmodel_node->add_child(bmodel_mesh_instance);
	}

	return root_node;
}
