/**************************************************************************/
/*  sourcepp_import_cache.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_import_cache.h"

#include "sourcepp_resolver.h"
#include "sourcepp_vmt.h"
#include "sourcepp_vtf.h"

#include "scene/resources/image_texture.h"

String SourcePPImportCache::_cache_key(const String &p_path, const String &p_game_id) {
	return p_game_id.strip_edges().to_lower() + "|" + p_path.replace("\\", "/").strip_edges().to_lower();
}

Ref<SourcePPVMT> SourcePPImportCache::get_vmt(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id, Error *r_error) {
	const String key = _cache_key(p_path, p_game_id);
	if (vmt_cache.has(key)) {
		if (r_error != nullptr) {
			*r_error = OK;
		}
		return vmt_cache[key];
	}
	if (vmt_error_cache.has(key)) {
		if (r_error != nullptr) {
			*r_error = vmt_error_cache[key];
		}
		return Ref<SourcePPVMT>();
	}

	Ref<SourcePPVMT> vmt;
	vmt.instantiate();
	vmt->set_resolver(p_resolver);
	vmt->set_resolver_game_id(p_game_id);
	vmt->set_import_cache(this);
	const Error open_error = vmt->open(p_path);
	if (open_error != OK) {
		vmt_error_cache.insert(key, open_error);
		if (r_error != nullptr) {
			*r_error = open_error;
		}
		return Ref<SourcePPVMT>();
	}

	vmt_cache.insert(key, vmt);
	if (r_error != nullptr) {
		*r_error = OK;
	}
	return vmt;
}

Ref<Image> SourcePPImportCache::get_vtf_image(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id, Error *r_error) {
	const String key = _cache_key(p_path, p_game_id);
	if (image_cache.has(key)) {
		if (r_error != nullptr) {
			*r_error = OK;
		}
		return image_cache[key];
	}
	if (image_error_cache.has(key)) {
		if (r_error != nullptr) {
			*r_error = image_error_cache[key];
		}
		return Ref<Image>();
	}

	Ref<SourcePPVTF> vtf;
	vtf.instantiate();
	vtf->set_resolver(p_resolver);
	vtf->set_resolver_game_id(p_game_id);
	const Error open_error = vtf->open(p_path);
	if (open_error != OK) {
		image_error_cache.insert(key, open_error);
		if (r_error != nullptr) {
			*r_error = open_error;
		}
		return Ref<Image>();
	}

	Ref<Image> image = vtf->get_image();
	if (image.is_null() || image->is_empty()) {
		image_error_cache.insert(key, ERR_FILE_CORRUPT);
		if (r_error != nullptr) {
			*r_error = ERR_FILE_CORRUPT;
		}
		return Ref<Image>();
	}

	image_cache.insert(key, image);
	if (r_error != nullptr) {
		*r_error = OK;
	}
	return image;
}

Ref<Texture2D> SourcePPImportCache::get_vtf_texture(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id, Error *r_error) {
	const String key = _cache_key(p_path, p_game_id);
	if (texture_cache.has(key)) {
		if (r_error != nullptr) {
			*r_error = OK;
		}
		return texture_cache[key];
	}
	if (texture_error_cache.has(key)) {
		if (r_error != nullptr) {
			*r_error = texture_error_cache[key];
		}
		return Ref<Texture2D>();
	}

	Error image_error = OK;
	Ref<Image> image = get_vtf_image(p_path, p_resolver, p_game_id, &image_error);
	if (image_error != OK || image.is_null() || image->is_empty()) {
		texture_error_cache.insert(key, image_error == OK ? ERR_FILE_CORRUPT : image_error);
		if (r_error != nullptr) {
			*r_error = image_error == OK ? ERR_FILE_CORRUPT : image_error;
		}
		return Ref<Texture2D>();
	}

	Ref<Texture2D> texture = ImageTexture::create_from_image(image);
	if (texture.is_null()) {
		texture_error_cache.insert(key, ERR_CANT_CREATE);
		if (r_error != nullptr) {
			*r_error = ERR_CANT_CREATE;
		}
		return Ref<Texture2D>();
	}

	texture_cache.insert(key, texture);
	if (r_error != nullptr) {
		*r_error = OK;
	}
	return texture;
}
