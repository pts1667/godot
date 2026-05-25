/**************************************************************************/
/*  sourcepp_resolver.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fspp {
class FileSystem;
}

class SourcePPResolver : public RefCounted {
	GDCLASS(SourcePPResolver, RefCounted);

	struct RegisteredGame {
		std::unique_ptr<fspp::FileSystem> file_system;
		std::unordered_map<std::string, std::unordered_map<std::string, std::string>> entry_map_by_search_path;
	};

	std::unordered_map<std::string, RegisteredGame> registered_games;
	std::vector<std::string> registration_order;

	static void _bind_methods();

	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(const std::string &p_string);
	static std::string _normalize_entry_path(const String &p_path);
	static std::string _normalize_search_path(const String &p_search_path);
	static PackedByteArray _to_packed_byte_array(const std::vector<std::byte> &p_bytes);

	Error _register_game(const std::string &p_game_id, std::unique_ptr<fspp::FileSystem> p_file_system);
	const RegisteredGame *_find_registered_game(std::string_view p_game_id) const;
	RegisteredGame *_find_registered_game(std::string_view p_game_id);

public:
	SourcePPResolver();
	~SourcePPResolver() override;

	Error register_local_game(const String &p_game_path);
	Error register_steam_game(int p_app_id, const String &p_game_id);
	bool unregister_game(const String &p_game_id);
	void clear();

	PackedStringArray get_registered_games() const;
	Dictionary get_entry_map(const String &p_search_path = "GAME") const;
	String get_file_vpk_path(const String &p_file_path, const String &p_game_id = String(), const String &p_search_path = "GAME") const;
	bool has_file(const String &p_file_path, const String &p_game_id = String(), const String &p_search_path = "GAME") const;
	PackedByteArray read_file(const String &p_file_path, const String &p_game_id = String(), const String &p_search_path = "GAME") const;
};