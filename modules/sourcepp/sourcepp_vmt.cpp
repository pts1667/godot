/**************************************************************************/
/*  sourcepp_vmt.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_vmt.h"

#include "sourcepp_import_cache.h"
#include "sourcepp_resolver.h"
#include "sourcepp_vtf.h"

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/variant/array.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

#include <kvpp/KV1.h>

#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using VMTDocument = kvpp::KV1<std::string>;
using VMTElement = kvpp::KV1ElementReadable<std::string>;

String _string_from_utf8(std::string_view p_string) {
	return String::utf8(p_string.data(), static_cast<int>(p_string.size()));
}

const VMTElement *_get_root_element(const VMTDocument *p_document) {
	if (p_document == nullptr || p_document->getChildCount() == 0) {
		return nullptr;
	}
	return &(*p_document)[0];
}

void _append_dictionary_value(Dictionary &r_dictionary, const String &p_key, const Variant &p_value) {
	if (!r_dictionary.has(p_key)) {
		r_dictionary[p_key] = p_value;
		return;
	}

	Variant existing = r_dictionary[p_key];
	Array values;
	if (existing.get_type() == Variant::ARRAY) {
		values = existing;
	} else {
		values.push_back(existing);
	}
	values.push_back(p_value);
	r_dictionary[p_key] = values;
}

Variant _element_to_variant(const VMTElement &p_element) {
	if (p_element.getChildCount() > 0) {
		Dictionary dictionary;
		if (!p_element.getValue().empty()) {
			dictionary["_value"] = _string_from_utf8(p_element.getValue());
		}
		if (!p_element.getConditional().empty()) {
			dictionary["_conditional"] = _string_from_utf8(p_element.getConditional());
		}
		for (const VMTElement &child : p_element.getChildren()) {
			_append_dictionary_value(dictionary, _string_from_utf8(child.getKey()), _element_to_variant(child));
		}
		return dictionary;
	}

	if (!p_element.getConditional().empty()) {
		Dictionary dictionary;
		dictionary["value"] = _string_from_utf8(p_element.getValue());
		dictionary["conditional"] = _string_from_utf8(p_element.getConditional());
		return dictionary;
	}

	return _string_from_utf8(p_element.getValue());
}

String _normalize_texture_path(const String &p_path) {
	return p_path.replace("\\", "/").strip_edges();
}

String _ensure_vtf_extension(const String &p_path) {
	return p_path.get_extension().to_lower() == "vtf" ? p_path : p_path + ".vtf";
}

String _ensure_vmt_extension(const String &p_path) {
	return p_path.get_extension().to_lower() == "vmt" ? p_path : p_path + ".vmt";
}

bool _is_patch_material(const VMTElement *p_root) {
	return p_root != nullptr && _string_from_utf8(p_root->getKey()).strip_edges().to_lower() == "patch";
}

bool _get_child_value_for_key(const VMTElement *p_parent, const std::string &p_key, String *r_value) {
	if (p_parent == nullptr || !(*p_parent)) {
		return false;
	}

	const VMTElement &direct = (*p_parent)[p_key];
	if (direct) {
		if (r_value != nullptr) {
			*r_value = _string_from_utf8(direct.getValue());
		}
		return true;
	}

	for (const VMTElement &child : p_parent->getChildren()) {
		if (child.getChildCount() == 0) {
			continue;
		}

		const VMTElement &nested = child[p_key];
		if (nested) {
			if (r_value != nullptr) {
				*r_value = _string_from_utf8(nested.getValue());
			}
			return true;
		}
	}

	return false;
}

bool _parse_bool_value(const VMTElement *p_element, bool p_default) {
	if (p_element == nullptr || !(*p_element) || p_element->getValue().empty()) {
		return p_default;
	}
	return p_element->getValue<bool>();
}

float _parse_float_value(const VMTElement *p_element, float p_default) {
	if (p_element == nullptr || !(*p_element) || p_element->getValue().empty()) {
		return p_default;
	}
	return p_element->getValue<float>();
}

BaseMaterial3D::BlendMode _detail_blend_mode_from_value(const String &p_raw_mode) {
	const String mode = p_raw_mode.strip_edges().to_lower();
	if (mode == "add" || mode == "additive") {
		return BaseMaterial3D::BLEND_MODE_ADD;
	}
	if (mode == "subtract" || mode == "sub") {
		return BaseMaterial3D::BLEND_MODE_SUB;
	}
	if (mode == "multiply" || mode == "mul" || mode == "mod2x") {
		return BaseMaterial3D::BLEND_MODE_MUL;
	}
	return BaseMaterial3D::BLEND_MODE_MIX;
}

PackedStringArray _get_supported_texture_keys() {
	return PackedStringArray{
		"$basetexture",
		"$detail",
		"$bumpmap",
		"$normalmap",
		"$selfillummask",
		"$envmapmask",
		"$reflecttexture",
		"$refracttexture",
	};
}

} // namespace

SourcePPVMT::SourcePPVMT() = default;

SourcePPVMT::~SourcePPVMT() = default;

void SourcePPVMT::set_resolver(const Ref<SourcePPResolver> &p_resolver) {
	resolver = p_resolver;
}

Ref<SourcePPResolver> SourcePPVMT::get_resolver() const {
	return resolver;
}

void SourcePPVMT::set_resolver_game_id(const String &p_game_id) {
	resolver_game_id = p_game_id.strip_edges().to_lower();
}

String SourcePPVMT::get_resolver_game_id() const {
	return resolver_game_id;
}

void SourcePPVMT::set_import_cache(SourcePPImportCache *p_import_cache) {
	import_cache = p_import_cache;
	if (patch_include_material.is_valid()) {
		patch_include_material->set_import_cache(p_import_cache);
	}
}

std::string SourcePPVMT::_to_utf8(const String &p_string) {
	const CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data());
}

String SourcePPVMT::_from_utf8(std::string_view p_string) {
	return String::utf8(p_string.data(), static_cast<int>(p_string.size()));
}

std::vector<std::byte> SourcePPVMT::_to_byte_vector(const PackedByteArray &p_data) {
	std::vector<std::byte> out;
	out.resize(static_cast<size_t>(p_data.size()));
	if (!out.empty()) {
		std::memcpy(out.data(), p_data.ptr(), out.size());
	}
	return out;
}

String SourcePPVMT::_get_texture_path_for_key(const String &p_key) const {
	return _normalize_texture_path(get_value(p_key));
}

bool SourcePPVMT::_has_file(const String &p_path) const {
	if (FileAccess::exists(p_path)) {
		return true;
	}
	if (!resolver.is_valid()) {
		return false;
	}
	return resolver_game_id.is_empty() ? resolver->has_file(p_path) : resolver->has_file(p_path, resolver_game_id);
}

PackedByteArray SourcePPVMT::_read_file_bytes(const String &p_path, Error *r_error) const {
	Error error = OK;
	PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path, &error);
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
			return resolved;
		}
	}

	if (r_error != nullptr) {
		*r_error = error == OK ? ERR_FILE_NOT_FOUND : error;
	}
	return PackedByteArray();
}

String SourcePPVMT::_resolve_material_asset_path(const String &p_material_path) const {
	const String normalized_path = _normalize_texture_path(p_material_path);
	if (normalized_path.is_empty()) {
		return String();
	}

	PackedStringArray candidates;
	candidates.push_back(normalized_path);
	candidates.push_back(_ensure_vmt_extension(normalized_path));

	if (!normalized_path.begins_with("materials/")) {
		const String materials_path = "materials/" + normalized_path;
		candidates.push_back(materials_path);
		candidates.push_back(_ensure_vmt_extension(materials_path));
	}

	if (!source_path.is_empty()) {
		const String normalized_source_path = source_path.replace("\\", "/");
		const int materials_index = normalized_source_path.find("materials/");
		if (materials_index >= 0) {
			const String source_root = normalized_source_path.substr(0, materials_index);
			candidates.push_back(source_root + _ensure_vmt_extension(normalized_path.begins_with("materials/") ? normalized_path : String("materials/") + normalized_path));
		}
	}

	for (const String &candidate : candidates) {
		if (_has_file(candidate)) {
			return candidate;
		}
	}

	return String();
}

Error SourcePPVMT::_load_patch_include() {
	patch_include_material.unref();

	const VMTElement *root = _get_root_element(get_material());
	if (!_is_patch_material(root)) {
		return OK;
	}

	const VMTElement &include_element = (*root)["include"];
	if (!include_element) {
		return OK;
	}

	const String include_path = _normalize_texture_path(_from_utf8(include_element.getValue()));
	const String resolved_include_path = _resolve_material_asset_path(include_path);
	if (resolved_include_path.is_empty() || (!source_path.is_empty() && resolved_include_path == source_path)) {
		return OK;
	}

	Ref<SourcePPVMT> included_material;
	if (import_cache != nullptr) {
		Error open_error = OK;
		included_material = import_cache->get_vmt(resolved_include_path, resolver, resolver_game_id, &open_error);
		if (open_error != OK) {
			return open_error;
		}
	} else {
		included_material.instantiate();
		included_material->set_resolver(resolver);
		included_material->set_resolver_game_id(resolver_game_id);
		const Error open_error = included_material->open(resolved_include_path);
		if (open_error != OK) {
			return open_error;
		}
	}

	patch_include_material = included_material;
	return OK;
}

bool SourcePPVMT::_get_patch_value_for_key(const String &p_key, String *r_value) const {
	const VMTElement *root = _get_root_element(get_material());
	if (!_is_patch_material(root)) {
		return false;
	}

	const std::string key = _to_utf8(p_key);
	const VMTElement &insert_block = (*root)["insert"];
	if (_get_child_value_for_key(insert_block ? &insert_block : nullptr, key, r_value)) {
		return true;
	}

	const VMTElement &replace_block = (*root)["replace"];
	if (_get_child_value_for_key(replace_block ? &replace_block : nullptr, key, r_value)) {
		return true;
	}

	return false;
}

String SourcePPVMT::_resolve_texture_asset_path(const String &p_texture_path) const {
	const String normalized_path = _normalize_texture_path(p_texture_path);
	if (normalized_path.is_empty()) {
		return String();
	}

	PackedStringArray candidates;
	candidates.push_back(normalized_path);
	candidates.push_back(_ensure_vtf_extension(normalized_path));

	if (!normalized_path.begins_with("materials/")) {
		const String materials_path = "materials/" + normalized_path;
		candidates.push_back(materials_path);
		candidates.push_back(_ensure_vtf_extension(materials_path));
	}

	if (!source_path.is_empty()) {
		const String normalized_source_path = source_path.replace("\\", "/");
		const int materials_index = normalized_source_path.find("materials/");
		if (materials_index >= 0) {
			const String source_root = normalized_source_path.substr(0, materials_index);
			const String materials_base = source_root + _ensure_vtf_extension(normalized_path.begins_with("materials/") ? normalized_path : String("materials/") + normalized_path);
			candidates.push_back(materials_base);
		}

		const String sibling_candidate = source_path.get_base_dir().path_join(_ensure_vtf_extension(normalized_path.get_file()));
		candidates.push_back(sibling_candidate);
	}

	for (const String &candidate : candidates) {
		if (_has_file(candidate)) {
			return candidate;
		}
	}

	return String();
}

Ref<Texture2D> SourcePPVMT::_load_texture_reference(const String &p_texture_path) const {
	const String resolved_path = _resolve_texture_asset_path(p_texture_path);
	if (resolved_path.is_empty()) {
		return Ref<Texture2D>();
	}

	Ref<SourcePPVTF> vtf;
	if (import_cache != nullptr) {
		return import_cache->get_vtf_texture(resolved_path, resolver, resolver_game_id);
	}

	vtf.instantiate();
	vtf->set_resolver(resolver);
	vtf->set_resolver_game_id(resolver_game_id);
	if (vtf->open(resolved_path) != OK) {
		return Ref<Texture2D>();
	}

	Ref<Image> image = vtf->get_image();
	if (image.is_null() || image->is_empty()) {
		return Ref<Texture2D>();
	}

	return ImageTexture::create_from_image(image);
}

VMTDocument *SourcePPVMT::get_material() {
	return material.get();
}

const VMTDocument *SourcePPVMT::get_material() const {
	return material.get();
}

Error SourcePPVMT::open(const String &p_path) {
	SourcePPImportCache *current_import_cache = import_cache;
	close();
	import_cache = current_import_cache;

	Error read_error = OK;
	const PackedByteArray data = _read_file_bytes(p_path, &read_error);
	ERR_FAIL_COND_V_MSG(read_error != OK, read_error, "Failed to load the VMT file.");

	const Error open_error = open_from_buffer(data);
	if (open_error != OK) {
		return open_error;
	}

	source_path = p_path;
	_load_patch_include();
	return OK;
}

Error SourcePPVMT::open_from_buffer(const PackedByteArray &p_data) {
	SourcePPImportCache *current_import_cache = import_cache;
	close();
	import_cache = current_import_cache;
	ERR_FAIL_COND_V_MSG(p_data.is_empty(), ERR_INVALID_PARAMETER, "VMT data must not be empty.");

	const std::vector<std::byte> bytes = _to_byte_vector(p_data);
	const std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	auto parsed = std::make_unique<VMTDocument>(std::string_view{text.data(), text.size()});
	const VMTElement *root = _get_root_element(parsed.get());
	ERR_FAIL_COND_V_MSG(root == nullptr || root->getKey().empty(), ERR_PARSE_ERROR, "Failed to parse VMT KeyValues data.");

	material = std::move(parsed);
	source_path = String();
	_load_patch_include();
	return OK;
}

void SourcePPVMT::close() {
	patch_include_material.unref();
	material.reset();
	import_cache = nullptr;
	source_path = String();
}

bool SourcePPVMT::is_open() const {
	return material != nullptr;
}

String SourcePPVMT::get_path() const {
	return source_path;
}

String SourcePPVMT::get_shader() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, String(), "SourcePPVMT must be opened before use.");
	if (patch_include_material.is_valid()) {
		return patch_include_material->get_shader();
	}
	const VMTElement *root = _get_root_element(get_material());
	ERR_FAIL_NULL_V(root, String());
	return _from_utf8(root->getKey());
}

Dictionary SourcePPVMT::get_properties() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, Dictionary(), "SourcePPVMT must be opened before use.");

	Dictionary out;
	const VMTElement *root = _get_root_element(get_material());
	ERR_FAIL_NULL_V(root, out);
	for (const VMTElement &child : root->getChildren()) {
		_append_dictionary_value(out, _from_utf8(child.getKey()), _element_to_variant(child));
	}
	return out;
}

bool SourcePPVMT::has_value(const String &p_key) const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, false, "SourcePPVMT must be opened before use.");
	if (_get_patch_value_for_key(p_key, nullptr)) {
		return true;
	}
	if (patch_include_material.is_valid() && patch_include_material->has_value(p_key)) {
		return true;
	}
	const VMTElement *root = _get_root_element(get_material());
	ERR_FAIL_NULL_V(root, false);
	return static_cast<bool>((*root)[_to_utf8(p_key)]);
}

String SourcePPVMT::get_value(const String &p_key, const String &p_default) const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, p_default, "SourcePPVMT must be opened before use.");
	String patch_value;
	if (_get_patch_value_for_key(p_key, &patch_value)) {
		return patch_value;
	}
	if (patch_include_material.is_valid() && patch_include_material->has_value(p_key)) {
		return patch_include_material->get_value(p_key, p_default);
	}
	const VMTElement *root = _get_root_element(get_material());
	ERR_FAIL_NULL_V(root, p_default);
	const VMTElement &element = (*root)[_to_utf8(p_key)];
	if (!element) {
		return p_default;
	}
	return _from_utf8(element.getValue());
}

String SourcePPVMT::get_base_texture_path() const {
	return _get_texture_path_for_key("$basetexture");
}

String SourcePPVMT::get_resolved_base_texture_path() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, String(), "SourcePPVMT must be opened before use.");
	return _resolve_texture_asset_path(get_base_texture_path());
}

String SourcePPVMT::get_detail_texture_path() const {
	return _get_texture_path_for_key("$detail");
}

String SourcePPVMT::get_resolved_detail_texture_path() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, String(), "SourcePPVMT must be opened before use.");
	return _resolve_texture_asset_path(get_detail_texture_path());
}

String SourcePPVMT::get_bump_map_path() const {
	return _get_texture_path_for_key("$bumpmap");
}

String SourcePPVMT::get_resolved_bump_map_path() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, String(), "SourcePPVMT must be opened before use.");
	return _resolve_texture_asset_path(get_bump_map_path());
}

String SourcePPVMT::get_normal_map_path() const {
	const String normal_map = _get_texture_path_for_key("$normalmap");
	if (!normal_map.is_empty()) {
		return normal_map;
	}
	const String shader_name = get_shader().strip_edges().to_lower();
	return shader_name == "water" || shader_name.begins_with("water_") ? String("dev/water_normal") : String();
}

String SourcePPVMT::get_resolved_normal_map_path() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, String(), "SourcePPVMT must be opened before use.");
	return _resolve_texture_asset_path(get_normal_map_path());
}

String SourcePPVMT::get_self_illum_mask_path() const {
	return _get_texture_path_for_key("$selfillummask");
}

String SourcePPVMT::get_resolved_self_illum_mask_path() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, String(), "SourcePPVMT must be opened before use.");
	return _resolve_texture_asset_path(get_self_illum_mask_path());
}

Dictionary SourcePPVMT::get_texture_dependencies() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, Dictionary(), "SourcePPVMT must be opened before use.");

	Dictionary out;
	for (const String &key : _get_supported_texture_keys()) {
		const String texture_path = _get_texture_path_for_key(key);
		if (!texture_path.is_empty()) {
			out[key] = texture_path;
		}
	}
	return out;
}

Dictionary SourcePPVMT::get_resolved_texture_dependencies() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, Dictionary(), "SourcePPVMT must be opened before use.");

	Dictionary out;
	for (const String &key : _get_supported_texture_keys()) {
		const String texture_path = _get_texture_path_for_key(key);
		if (texture_path.is_empty()) {
			continue;
		}
		const String resolved_path = _resolve_texture_asset_path(texture_path);
		if (!resolved_path.is_empty()) {
			out[key] = resolved_path;
		}
	}
	return out;
}

float SourcePPVMT::get_detail_blend_factor() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, 1.0f, "SourcePPVMT must be opened before use.");
	const VMTElement *root = _get_root_element(get_material());
	ERR_FAIL_NULL_V(root, 1.0f);
	const VMTElement &element = (*root)["$detailblendfactor"];
	return _parse_float_value(element ? &element : nullptr, 1.0f);
}

float SourcePPVMT::get_detail_scale() const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, 1.0f, "SourcePPVMT must be opened before use.");
	const VMTElement *root = _get_root_element(get_material());
	ERR_FAIL_NULL_V(root, 1.0f);
	const VMTElement &element = (*root)["$detailscale"];
	return _parse_float_value(element ? &element : nullptr, 1.0f);
}

Ref<StandardMaterial3D> SourcePPVMT::create_material(const Ref<Texture2D> &p_base_texture, const Ref<Texture2D> &p_detail_texture, const Ref<Texture2D> &p_detail_mask) const {
	ERR_FAIL_COND_V_MSG(get_material() == nullptr, Ref<StandardMaterial3D>(), "SourcePPVMT must be opened before use.");

	const Ref<Texture2D> base_texture = p_base_texture.is_valid() ? p_base_texture : _load_texture_reference(get_base_texture_path());
	const Ref<Texture2D> detail_texture = p_detail_texture.is_valid() ? p_detail_texture : _load_texture_reference(get_detail_texture_path());
	const Ref<Texture2D> bump_texture = _load_texture_reference(get_bump_map_path());
	const Ref<Texture2D> self_illum_mask = _load_texture_reference(get_self_illum_mask_path());
	const VMTElement *root = _get_root_element(get_material());
	const bool self_illum_enabled = root != nullptr ? _parse_bool_value(static_cast<bool>((*root)["$selfillum"]) ? &(*root)["$selfillum"] : nullptr, false) : false;

	Ref<StandardMaterial3D> generated_material;
	generated_material.instantiate();
	generated_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, base_texture);

	if (detail_texture.is_valid()) {
		generated_material->set_feature(BaseMaterial3D::FEATURE_DETAIL, true);
		generated_material->set_texture(BaseMaterial3D::TEXTURE_DETAIL_ALBEDO, detail_texture);
		generated_material->set_detail_blend_mode(_detail_blend_mode_from_value(get_value("$detailblendmode")));
		if (p_detail_mask.is_valid()) {
			generated_material->set_texture(BaseMaterial3D::TEXTURE_DETAIL_MASK, p_detail_mask);
		}
	}

	if (bump_texture.is_valid()) {
		generated_material->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
		generated_material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, bump_texture);
	}

	if (self_illum_mask.is_valid() || self_illum_enabled) {
		generated_material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
		generated_material->set_emission(Color(1.0, 1.0, 1.0));
		if (self_illum_mask.is_valid()) {
			generated_material->set_texture(BaseMaterial3D::TEXTURE_EMISSION, self_illum_mask);
		}
	}

	if (root != nullptr) {
		const VMTElement &nocull = (*root)["$nocull"];
		if (_parse_bool_value(nocull ? &nocull : nullptr, false)) {
			generated_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		}
	}

	generated_material->set_meta("sourcepp_vmt_path", get_path());
	generated_material->set_meta("sourcepp_vmt_shader", get_shader());
	generated_material->set_meta("sourcepp_vmt_basetexture", get_base_texture_path());
	generated_material->set_meta("sourcepp_vmt_resolved_basetexture", get_resolved_base_texture_path());
	generated_material->set_meta("sourcepp_vmt_detail", get_detail_texture_path());
	generated_material->set_meta("sourcepp_vmt_resolved_detail", get_resolved_detail_texture_path());
	generated_material->set_meta("sourcepp_vmt_bumpmap", get_bump_map_path());
	generated_material->set_meta("sourcepp_vmt_resolved_bumpmap", get_resolved_bump_map_path());
	generated_material->set_meta("sourcepp_vmt_selfillummask", get_self_illum_mask_path());
	generated_material->set_meta("sourcepp_vmt_resolved_selfillummask", get_resolved_self_illum_mask_path());
	generated_material->set_meta("sourcepp_vmt_texture_dependencies", get_texture_dependencies());
	generated_material->set_meta("sourcepp_vmt_resolved_texture_dependencies", get_resolved_texture_dependencies());
	generated_material->set_meta("sourcepp_vmt_detail_scale", get_detail_scale());
	generated_material->set_meta("sourcepp_vmt_detail_blend_factor", get_detail_blend_factor());
	generated_material->set_meta("sourcepp_vmt_detail_blend_mode", get_value("$detailblendmode"));

	return generated_material;
}

void SourcePPVMT::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resolver", "resolver"), &SourcePPVMT::set_resolver);
	ClassDB::bind_method(D_METHOD("get_resolver"), &SourcePPVMT::get_resolver);
	ClassDB::bind_method(D_METHOD("set_resolver_game_id", "game_id"), &SourcePPVMT::set_resolver_game_id);
	ClassDB::bind_method(D_METHOD("get_resolver_game_id"), &SourcePPVMT::get_resolver_game_id);
	ClassDB::bind_method(D_METHOD("open", "path"), &SourcePPVMT::open);
	ClassDB::bind_method(D_METHOD("open_from_buffer", "data"), &SourcePPVMT::open_from_buffer);
	ClassDB::bind_method(D_METHOD("close"), &SourcePPVMT::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPVMT::is_open);

	ClassDB::bind_method(D_METHOD("get_path"), &SourcePPVMT::get_path);
	ClassDB::bind_method(D_METHOD("get_shader"), &SourcePPVMT::get_shader);
	ClassDB::bind_method(D_METHOD("get_properties"), &SourcePPVMT::get_properties);
	ClassDB::bind_method(D_METHOD("has_value", "key"), &SourcePPVMT::has_value);
	ClassDB::bind_method(D_METHOD("get_value", "key", "default_value"), &SourcePPVMT::get_value, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_base_texture_path"), &SourcePPVMT::get_base_texture_path);
	ClassDB::bind_method(D_METHOD("get_resolved_base_texture_path"), &SourcePPVMT::get_resolved_base_texture_path);
	ClassDB::bind_method(D_METHOD("get_detail_texture_path"), &SourcePPVMT::get_detail_texture_path);
	ClassDB::bind_method(D_METHOD("get_resolved_detail_texture_path"), &SourcePPVMT::get_resolved_detail_texture_path);
	ClassDB::bind_method(D_METHOD("get_bump_map_path"), &SourcePPVMT::get_bump_map_path);
	ClassDB::bind_method(D_METHOD("get_resolved_bump_map_path"), &SourcePPVMT::get_resolved_bump_map_path);
	ClassDB::bind_method(D_METHOD("get_normal_map_path"), &SourcePPVMT::get_normal_map_path);
	ClassDB::bind_method(D_METHOD("get_resolved_normal_map_path"), &SourcePPVMT::get_resolved_normal_map_path);
	ClassDB::bind_method(D_METHOD("get_self_illum_mask_path"), &SourcePPVMT::get_self_illum_mask_path);
	ClassDB::bind_method(D_METHOD("get_resolved_self_illum_mask_path"), &SourcePPVMT::get_resolved_self_illum_mask_path);
	ClassDB::bind_method(D_METHOD("get_texture_dependencies"), &SourcePPVMT::get_texture_dependencies);
	ClassDB::bind_method(D_METHOD("get_resolved_texture_dependencies"), &SourcePPVMT::get_resolved_texture_dependencies);
	ClassDB::bind_method(D_METHOD("get_detail_blend_factor"), &SourcePPVMT::get_detail_blend_factor);
	ClassDB::bind_method(D_METHOD("get_detail_scale"), &SourcePPVMT::get_detail_scale);
	ClassDB::bind_method(D_METHOD("create_material", "base_texture", "detail_texture", "detail_mask"), &SourcePPVMT::create_material, DEFVAL(Ref<Texture2D>()), DEFVAL(Ref<Texture2D>()), DEFVAL(Ref<Texture2D>()));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "resolver", PROPERTY_HINT_RESOURCE_TYPE, "SourcePPResolver"), "set_resolver", "get_resolver");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "resolver_game_id"), "set_resolver_game_id", "get_resolver_game_id");
}
