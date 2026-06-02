/**************************************************************************/
/*  sourcepp_import_cache.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/io/image.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "scene/resources/texture.h"

class SourcePPResolver;
class SourcePPVMT;

class SourcePPImportCache {
	HashMap<String, Ref<SourcePPVMT>> vmt_cache;
	HashMap<String, Error> vmt_error_cache;
	HashMap<String, Ref<Image>> image_cache;
	HashMap<String, Error> image_error_cache;
	HashMap<String, Ref<Texture2D>> texture_cache;
	HashMap<String, Error> texture_error_cache;

	static String _cache_key(const String &p_path, const String &p_game_id);

public:
	Ref<SourcePPVMT> get_vmt(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id, Error *r_error = nullptr);
	Ref<Image> get_vtf_image(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id, Error *r_error = nullptr);
	Ref<Texture2D> get_vtf_texture(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id, Error *r_error = nullptr);
};
