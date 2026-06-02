/**************************************************************************/
/*  sourcepp_vmt.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

#include <kvpp/KV1.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class StandardMaterial3D;
class SourcePPImportCache;
class SourcePPResolver;
class Texture2D;

class SourcePPVMT : public RefCounted {
	GDCLASS(SourcePPVMT, RefCounted);

	std::unique_ptr<kvpp::KV1<std::string>> material;
	Ref<SourcePPVMT> patch_include_material;
	Ref<SourcePPResolver> resolver;
	SourcePPImportCache *import_cache = nullptr;
	String resolver_game_id;
	String source_path;

	static void _bind_methods();

	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(std::string_view p_string);
	static std::vector<std::byte> _to_byte_vector(const PackedByteArray &p_data);
	String _get_texture_path_for_key(const String &p_key) const;
	bool _has_file(const String &p_path) const;
	PackedByteArray _read_file_bytes(const String &p_path, Error *r_error) const;
	String _resolve_material_asset_path(const String &p_material_path) const;
	Error _load_patch_include();
	bool _get_patch_value_for_key(const String &p_key, String *r_value) const;
	String _resolve_texture_asset_path(const String &p_texture_path) const;
	Ref<Texture2D> _load_texture_reference(const String &p_texture_path) const;

	kvpp::KV1<std::string> *get_material();
	const kvpp::KV1<std::string> *get_material() const;

public:
	SourcePPVMT();
	~SourcePPVMT() override;

	void set_resolver(const Ref<SourcePPResolver> &p_resolver);
	Ref<SourcePPResolver> get_resolver() const;
	void set_resolver_game_id(const String &p_game_id);
	String get_resolver_game_id() const;
	void set_import_cache(SourcePPImportCache *p_import_cache);

	Error open(const String &p_path);
	Error open_from_buffer(const PackedByteArray &p_data);
	void close();
	bool is_open() const;

	String get_path() const;
	String get_shader() const;
	Dictionary get_properties() const;
	bool has_value(const String &p_key) const;
	String get_value(const String &p_key, const String &p_default = String()) const;
	String get_base_texture_path() const;
	String get_resolved_base_texture_path() const;
	String get_detail_texture_path() const;
	String get_resolved_detail_texture_path() const;
	String get_bump_map_path() const;
	String get_resolved_bump_map_path() const;
	String get_self_illum_mask_path() const;
	String get_resolved_self_illum_mask_path() const;
	Dictionary get_texture_dependencies() const;
	Dictionary get_resolved_texture_dependencies() const;
	float get_detail_blend_factor() const;
	float get_detail_scale() const;
	Ref<StandardMaterial3D> create_material(const Ref<Texture2D> &p_base_texture = Ref<Texture2D>(), const Ref<Texture2D> &p_detail_texture = Ref<Texture2D>(), const Ref<Texture2D> &p_detail_mask = Ref<Texture2D>()) const;
};
