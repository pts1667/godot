/**************************************************************************/
/*  sourcepp_resolver.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_resolver.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"

#include <fspp/fspp.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string_view>

SourcePPResolver::SourcePPResolver() = default;

SourcePPResolver::~SourcePPResolver() = default;

std::string SourcePPResolver::_to_utf8(const String &p_string) {
	const CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data());
}

String SourcePPResolver::_from_utf8(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

std::string SourcePPResolver::_normalize_entry_path(const String &p_path) {
	return _to_utf8(p_path.replace("\\", "/").strip_edges().to_lower());
}

std::string SourcePPResolver::_normalize_search_path(const String &p_search_path) {
	return _to_utf8(p_search_path.strip_edges().to_lower());
}

String SourcePPResolver::_path_to_string(const std::filesystem::path &p_path) {
	std::filesystem::path preferred_path = p_path;
	preferred_path.make_preferred();
	return _from_utf8(preferred_path.string());
}

PackedByteArray SourcePPResolver::_to_packed_byte_array(const std::vector<std::byte> &p_bytes) {
	PackedByteArray out;
	out.resize(static_cast<int>(p_bytes.size()));
	if (!p_bytes.empty()) {
		std::memcpy(out.ptrw(), p_bytes.data(), p_bytes.size());
	}
	return out;
}

void SourcePPResolver::_index_search_path_directory(const std::filesystem::path &p_root_path, const std::string &p_base_path, std::unordered_map<std::string, RegisteredEntry> &r_entry_map) {
	const std::filesystem::path base_directory = p_root_path / p_base_path;
	std::error_code error;
	if (!std::filesystem::exists(base_directory, error) || !std::filesystem::is_directory(base_directory, error)) {
		return;
	}

	for (std::filesystem::recursive_directory_iterator iterator(base_directory, std::filesystem::directory_options::skip_permission_denied, error), end; iterator != end; iterator.increment(error)) {
		if (error) {
			error.clear();
			continue;
		}

		const std::filesystem::directory_entry &entry = *iterator;
		if (!entry.is_regular_file(error)) {
			error.clear();
			continue;
		}

		const std::filesystem::path relative_path = std::filesystem::relative(entry.path(), base_directory, error);
		if (error) {
			error.clear();
			continue;
		}

		const std::string entry_path = _normalize_entry_path(_from_utf8(relative_path.generic_string()));
		if (entry_path.empty()) {
			continue;
		}

		const auto existing = r_entry_map.find(entry_path);
		if (existing != r_entry_map.end() && !existing->second.is_vpk) {
			continue;
		}

		r_entry_map[entry_path] = RegisteredEntry{
			.source_path = _to_utf8(_path_to_string(entry.path())),
			.is_vpk = false,
		};
	}
}

Error SourcePPResolver::_register_game(const std::string &p_game_id, std::unique_ptr<fspp::FileSystem> p_file_system) {
	ERR_FAIL_NULL_V_MSG(p_file_system, ERR_INVALID_PARAMETER, "File system must not be null.");

	RegisteredGame registered_game;
	registered_game.file_system = std::move(p_file_system);
	const std::filesystem::path root_path = registered_game.file_system->getRootPath();
	for (const auto &[search_path, pack_files] : registered_game.file_system->getSearchPathVPKs()) {
		auto &entry_map = registered_game.entry_map_by_search_path[search_path];
		for (const auto &pack_file : pack_files) {
			const std::string pack_file_path(pack_file->getFilepath());
			pack_file->runForAllEntries([&entry_map, &pack_file_path](const std::string &p_path, const vpkpp::Entry &) {
				entry_map.try_emplace(SourcePPResolver::_normalize_entry_path(SourcePPResolver::_from_utf8(p_path)), RegisteredEntry{ .source_path = pack_file_path, .is_vpk = true });
			});
		}
	}
	for (const auto &[search_path, directories] : registered_game.file_system->getSearchPathDirs()) {
		auto &entry_map = registered_game.entry_map_by_search_path[search_path];
		for (const std::string &directory : directories) {
			_index_search_path_directory(root_path, directory, entry_map);
		}
	}

	if (!registered_games.contains(p_game_id)) {
		registration_order.push_back(p_game_id);
	}
	registered_games[p_game_id] = std::move(registered_game);
	return OK;
}

const SourcePPResolver::RegisteredGame *SourcePPResolver::_find_registered_game(std::string_view p_game_id) const {
	if (const auto iterator = registered_games.find(std::string(p_game_id)); iterator != registered_games.end()) {
		return &iterator->second;
	}
	return nullptr;
}

SourcePPResolver::RegisteredGame *SourcePPResolver::_find_registered_game(std::string_view p_game_id) {
	if (const auto iterator = registered_games.find(std::string(p_game_id)); iterator != registered_games.end()) {
		return &iterator->second;
	}
	return nullptr;
}

Error SourcePPResolver::register_local_game(const String &p_game_path) {
	ERR_FAIL_COND_V_MSG(p_game_path.is_empty(), ERR_INVALID_PARAMETER, "Game path must not be empty.");

	auto file_system = fspp::FileSystem::load(_to_utf8(p_game_path));
	ERR_FAIL_COND_V_MSG(!file_system.has_value(), ERR_FILE_CANT_OPEN, "Could not load Source file system from the requested local path.");

	const String game_id = p_game_path.get_file();
	return _register_game(_to_utf8(game_id.to_lower()), std::make_unique<fspp::FileSystem>(std::move(file_system.value())));
}

Error SourcePPResolver::register_steam_game(int p_app_id, const String &p_game_id) {
	ERR_FAIL_COND_V_MSG(p_app_id < 0, ERR_INVALID_PARAMETER, "App ID must be non-negative.");
	ERR_FAIL_COND_V_MSG(p_game_id.is_empty(), ERR_INVALID_PARAMETER, "Game ID must not be empty.");

	auto file_system = fspp::FileSystem::load(static_cast<steampp::AppID>(p_app_id), _to_utf8(p_game_id));
	ERR_FAIL_COND_V_MSG(!file_system.has_value(), ERR_FILE_CANT_OPEN, "Could not load Source file system from the requested Steam game.");

	return _register_game(_to_utf8(p_game_id.to_lower()), std::make_unique<fspp::FileSystem>(std::move(file_system.value())));
}

bool SourcePPResolver::unregister_game(const String &p_game_id) {
	const std::string normalized_game_id = _to_utf8(p_game_id.strip_edges().to_lower());
	if (!registered_games.erase(normalized_game_id)) {
		return false;
	}
	std::erase(registration_order, normalized_game_id);
	return true;
}

void SourcePPResolver::clear() {
	registered_games.clear();
	registration_order.clear();
}

PackedStringArray SourcePPResolver::get_registered_games() const {
	PackedStringArray out;
	for (const std::string &game_id : registration_order) {
		out.push_back(_from_utf8(game_id));
	}
	return out;
}

Dictionary SourcePPResolver::get_entry_map(const String &p_search_path) const {
	const std::string search_path = _normalize_search_path(p_search_path);
	Dictionary out;
	for (const std::string &game_id : registration_order) {
		const RegisteredGame *registered_game = _find_registered_game(game_id);
		if (registered_game == nullptr) {
			continue;
		}
		const auto iterator = registered_game->entry_map_by_search_path.find(search_path);
		if (iterator == registered_game->entry_map_by_search_path.end()) {
			continue;
		}

		Dictionary game_entries;
		for (const auto &[entry_path, entry_info] : iterator->second) {
			game_entries[_from_utf8(entry_path)] = _from_utf8(entry_info.source_path);
		}
		out[_from_utf8(game_id)] = game_entries;
	}
	return out;
}

String SourcePPResolver::get_file_vpk_path(const String &p_file_path, const String &p_game_id, const String &p_search_path) const {
	const std::string entry_path = _normalize_entry_path(p_file_path);
	const std::string search_path = _normalize_search_path(p_search_path);

	auto resolve_from_game = [&](const RegisteredGame *p_registered_game) -> String {
		if (p_registered_game == nullptr) {
			return String();
		}
		const auto search_iterator = p_registered_game->entry_map_by_search_path.find(search_path);
		if (search_iterator == p_registered_game->entry_map_by_search_path.end()) {
			return String();
		}
		const auto entry_iterator = search_iterator->second.find(entry_path);
		if (entry_iterator == search_iterator->second.end()) {
			return String();
		}
		return _from_utf8(entry_iterator->second.source_path);
	};

	if (!p_game_id.is_empty()) {
		return resolve_from_game(_find_registered_game(_to_utf8(p_game_id.strip_edges().to_lower())));
	}

	for (const std::string &game_id : registration_order) {
		if (String vpk_path = resolve_from_game(_find_registered_game(game_id)); !vpk_path.is_empty()) {
			return vpk_path;
		}
	}
	return String();
}

bool SourcePPResolver::has_file(const String &p_file_path, const String &p_game_id, const String &p_search_path) const {
	const std::string file_path = _normalize_entry_path(p_file_path);
	const std::string search_path = _normalize_search_path(p_search_path);

	auto game_has_file = [&](const RegisteredGame *p_registered_game) {
		return p_registered_game != nullptr && p_registered_game->file_system != nullptr && p_registered_game->file_system->read(file_path, search_path, false).has_value();
	};

	if (!p_game_id.is_empty()) {
		return game_has_file(_find_registered_game(_to_utf8(p_game_id.strip_edges().to_lower())));
	}

	for (const std::string &game_id : registration_order) {
		if (game_has_file(_find_registered_game(game_id))) {
			return true;
		}
	}
	return false;
}

PackedByteArray SourcePPResolver::read_file(const String &p_file_path, const String &p_game_id, const String &p_search_path) const {
	const std::string file_path = _normalize_entry_path(p_file_path);
	const std::string search_path = _normalize_search_path(p_search_path);

	auto read_from_game = [&](const RegisteredGame *p_registered_game) -> PackedByteArray {
		if (p_registered_game == nullptr || p_registered_game->file_system == nullptr) {
			return PackedByteArray();
		}
		const auto bytes = p_registered_game->file_system->read(file_path, search_path, false);
		if (!bytes.has_value()) {
			return PackedByteArray();
		}
		return _to_packed_byte_array(*bytes);
	};

	if (!p_game_id.is_empty()) {
		return read_from_game(_find_registered_game(_to_utf8(p_game_id.strip_edges().to_lower())));
	}

	for (const std::string &game_id : registration_order) {
		PackedByteArray bytes = read_from_game(_find_registered_game(game_id));
		if (!bytes.is_empty()) {
			return bytes;
		}
	}
	return PackedByteArray();
}

void SourcePPResolver::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_local_game", "game_path"), &SourcePPResolver::register_local_game);
	ClassDB::bind_method(D_METHOD("register_steam_game", "app_id", "game_id"), &SourcePPResolver::register_steam_game);
	ClassDB::bind_method(D_METHOD("unregister_game", "game_id"), &SourcePPResolver::unregister_game);
	ClassDB::bind_method(D_METHOD("clear"), &SourcePPResolver::clear);
	ClassDB::bind_method(D_METHOD("get_registered_games"), &SourcePPResolver::get_registered_games);
	ClassDB::bind_method(D_METHOD("get_entry_map", "search_path"), &SourcePPResolver::get_entry_map, DEFVAL(String("GAME")));
	ClassDB::bind_method(D_METHOD("get_file_vpk_path", "file_path", "game_id", "search_path"), &SourcePPResolver::get_file_vpk_path, DEFVAL(String()), DEFVAL(String("GAME")));
	ClassDB::bind_method(D_METHOD("has_file", "file_path", "game_id", "search_path"), &SourcePPResolver::has_file, DEFVAL(String()), DEFVAL(String("GAME")));
	ClassDB::bind_method(D_METHOD("read_file", "file_path", "game_id", "search_path"), &SourcePPResolver::read_file, DEFVAL(String()), DEFVAL(String("GAME")));
}