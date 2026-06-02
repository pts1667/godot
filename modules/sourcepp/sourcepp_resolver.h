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

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fspp {
class FileSystem;
}

class SourcePPResolver : public RefCounted {
	GDCLASS(SourcePPResolver, RefCounted);

	struct RegisteredEntry {
		std::string source_path;
		bool is_vpk = false;
		int pack_file_index = -1;
	};

	struct MountedPakFile {
		String source_path;
		String temporary_zip_path;
	};

	struct RegisteredGame {
		std::unique_ptr<fspp::FileSystem> file_system;
		std::vector<MountedPakFile> pack_files;
		std::unordered_map<std::string, std::unordered_map<std::string, RegisteredEntry>> entry_map_by_search_path;
	};

	std::unordered_map<std::string, RegisteredGame> registered_games;
	std::vector<std::string> registration_order;

	static void _bind_methods();

	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(const std::string &p_string);
	static std::string _normalize_entry_path(const String &p_path);
	static std::string _normalize_search_path(const String &p_search_path);
	static String _path_to_string(const std::filesystem::path &p_path);
	static PackedByteArray _to_packed_byte_array(const std::vector<std::byte> &p_bytes);
	static PackedByteArray _read_zip_entry(const String &p_zip_path, const std::string &p_entry_path);
	static void _index_search_path_directory(const std::filesystem::path &p_root_path, const std::string &p_base_path, std::unordered_map<std::string, RegisteredEntry> &r_entry_map);

	Error _register_game(const std::string &p_game_id, std::unique_ptr<fspp::FileSystem> p_file_system);
	const RegisteredGame *_find_registered_game(std::string_view p_game_id) const;
	RegisteredGame *_find_registered_game(std::string_view p_game_id);

public:
	SourcePPResolver();
	~SourcePPResolver() override;

	Error register_local_game(const String &p_game_path);
	Error register_steam_game(int p_app_id, const String &p_game_id);
	Error register_bsp_pakfile(const String &p_bsp_path, const String &p_game_id = String(), const String &p_search_path = "GAME");
	bool unregister_bsp_pakfile(const String &p_bsp_path, const String &p_game_id = String(), const String &p_search_path = "GAME");
	bool unregister_game(const String &p_game_id);
	void clear();

	PackedStringArray get_registered_games() const;
	Dictionary get_entry_map(const String &p_search_path = "GAME") const;
	String get_file_vpk_path(const String &p_file_path, const String &p_game_id = String(), const String &p_search_path = "GAME") const;
	bool has_file(const String &p_file_path, const String &p_game_id = String(), const String &p_search_path = "GAME") const;
	PackedByteArray read_file(const String &p_file_path, const String &p_game_id = String(), const String &p_search_path = "GAME") const;
};
