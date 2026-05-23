/**************************************************************************/
/*  sourcepp_vpk.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_vpk.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"

#include <vpkpp/Options.h>
#include <vpkpp/PackFile.h>
#include <vpkpp/format/VPK.h>

#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

SourcePPVPK::SourcePPVPK() = default;

SourcePPVPK::~SourcePPVPK() = default;

std::unique_ptr<vpkpp::VPK> SourcePPVPK::_cast_vpk(std::unique_ptr<vpkpp::PackFile> p_pack_file) {
	vpkpp::VPK *vpk = dynamic_cast<vpkpp::VPK *>(p_pack_file.get());
	if (vpk == nullptr) {
		return {};
	}
	p_pack_file.release();
	return std::unique_ptr<vpkpp::VPK>(vpk);
}

std::string SourcePPVPK::_to_utf8(const String &p_string) {
	CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data());
}

String SourcePPVPK::_from_utf8(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

PackedByteArray SourcePPVPK::_to_packed_byte_array(std::span<const std::byte> p_bytes) {
	PackedByteArray out;
	out.resize(static_cast<int>(p_bytes.size()));
	if (!p_bytes.empty()) {
		std::memcpy(out.ptrw(), p_bytes.data(), p_bytes.size());
	}
	return out;
}

std::vector<std::byte> SourcePPVPK::_to_byte_vector(const PackedByteArray &p_data) {
	std::vector<std::byte> out;
	out.resize(static_cast<size_t>(p_data.size()));
	if (!out.empty()) {
		std::memcpy(out.data(), p_data.ptr(), out.size());
	}
	return out;
}

Error SourcePPVPK::_set_archive(std::unique_ptr<vpkpp::PackFile> p_pack_file) {
	auto vpk = _cast_vpk(std::move(p_pack_file));
	if (vpk == nullptr) {
		archive.reset();
		return ERR_FILE_CANT_OPEN;
	}
	archive = std::move(vpk);
	return OK;
}

vpkpp::VPK *SourcePPVPK::get_archive() {
	return archive.get();
}

const vpkpp::VPK *SourcePPVPK::get_archive() const {
	return archive.get();
}

Error SourcePPVPK::open(const String &p_path) {
	close();
	return _set_archive(vpkpp::VPK::open(_to_utf8(p_path)));
}

Error SourcePPVPK::create(const String &p_path, int p_version) {
	close();
	return _set_archive(vpkpp::VPK::create(_to_utf8(p_path), static_cast<uint32_t>(p_version)));
}

void SourcePPVPK::close() {
	archive.reset();
}

bool SourcePPVPK::is_open() const {
	return archive != nullptr;
}

String SourcePPVPK::get_path() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, String(), "SourcePPVPK must be opened before use.");
	return _from_utf8(std::string(get_archive()->getFilepath()));
}

int SourcePPVPK::get_file_count() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, 0, "SourcePPVPK must be opened before use.");
	return static_cast<int>(get_archive()->getEntryCount());
}

PackedStringArray SourcePPVPK::get_files() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, PackedStringArray(), "SourcePPVPK must be opened before use.");

	PackedStringArray out;
	get_archive()->runForAllEntries([&out](const std::string &p_path, const vpkpp::Entry &) {
		out.push_back(_from_utf8(p_path));
	});
	return out;
}

bool SourcePPVPK::has_file(const String &p_path) const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");
	return get_archive()->hasEntry(_to_utf8(p_path));
}

PackedByteArray SourcePPVPK::read_file(const String &p_path) const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, PackedByteArray(), "SourcePPVPK must be opened before use.");

	const auto bytes = get_archive()->readEntry(_to_utf8(p_path));
	ERR_FAIL_COND_V_MSG(!bytes, PackedByteArray(), "File does not exist in the loaded VPK archive: " + p_path);
	return _to_packed_byte_array(*bytes);
}

String SourcePPVPK::read_file_text(const String &p_path) const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, String(), "SourcePPVPK must be opened before use.");

	const auto text = get_archive()->readEntryText(_to_utf8(p_path));
	ERR_FAIL_COND_V_MSG(!text, String(), "File does not exist in the loaded VPK archive: " + p_path);
	return _from_utf8(*text);
}

bool SourcePPVPK::add_file(const String &p_entry_path, const String &p_source_path, int p_preload_bytes, bool p_save_to_directory) {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");

	vpkpp::EntryOptions options{};
	options.vpk_preloadBytes = CLAMP(p_preload_bytes, 0, 65535);
	options.vpk_saveToDirectory = p_save_to_directory;
	return get_archive()->addEntry(_to_utf8(p_entry_path), _to_utf8(p_source_path), options);
}

bool SourcePPVPK::add_file_from_buffer(const String &p_entry_path, const PackedByteArray &p_data, int p_preload_bytes, bool p_save_to_directory) {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");

	vpkpp::EntryOptions options{};
	options.vpk_preloadBytes = CLAMP(p_preload_bytes, 0, 65535);
	options.vpk_saveToDirectory = p_save_to_directory;
	std::vector<std::byte> bytes = _to_byte_vector(p_data);
	return get_archive()->addEntry(_to_utf8(p_entry_path), std::span<const std::byte>(bytes.data(), bytes.size()), options);
}

int SourcePPVPK::add_directory(const String &p_entry_base_dir, const String &p_source_dir, int p_preload_bytes, bool p_save_to_directory) {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, -1, "SourcePPVPK must be opened before use.");

	vpkpp::EntryOptions options{};
	options.vpk_preloadBytes = CLAMP(p_preload_bytes, 0, 65535);
	options.vpk_saveToDirectory = p_save_to_directory;
	return static_cast<int>(get_archive()->addDirectory(_to_utf8(p_entry_base_dir), _to_utf8(p_source_dir), options));
}

bool SourcePPVPK::remove_file(const String &p_path) {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");
	return get_archive()->removeEntry(_to_utf8(p_path));
}

int SourcePPVPK::remove_directory(const String &p_directory) {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, 0, "SourcePPVPK must be opened before use.");
	return static_cast<int>(get_archive()->removeDirectory(_to_utf8(p_directory)));
}

Error SourcePPVPK::bake(const String &p_output_dir, bool p_generate_md5_entries) {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, ERR_INVALID_PARAMETER, "SourcePPVPK must be opened before use.");

	vpkpp::BakeOptions options{};
	options.vpk_generateMD5Entries = p_generate_md5_entries;
	return static_cast<vpkpp::PackFile *>(get_archive())->bake(_to_utf8(p_output_dir), options) ? OK : FAILED;
}

PackedStringArray SourcePPVPK::verify_entry_checksums() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, PackedStringArray(), "SourcePPVPK must be opened before use.");

	PackedStringArray out;
	for (const std::string &path : get_archive()->verifyEntryChecksums()) {
		out.push_back(_from_utf8(path));
	}
	return out;
}

bool SourcePPVPK::verify_pack_checksum() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");
	return get_archive()->verifyPackFileChecksum();
}

bool SourcePPVPK::verify_signature() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");
	return get_archive()->verifyPackFileSignature();
}

bool SourcePPVPK::has_pack_checksum() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");
	return get_archive()->hasPackFileChecksum();
}

bool SourcePPVPK::has_signature() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, false, "SourcePPVPK must be opened before use.");
	return get_archive()->hasPackFileSignature();
}

int SourcePPVPK::get_version() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, 0, "SourcePPVPK must be opened before use.");
	return static_cast<int>(get_archive()->getVersion());
}

void SourcePPVPK::set_version(int p_version) {
	ERR_FAIL_COND_MSG(get_archive() == nullptr, "SourcePPVPK must be opened before use.");
	get_archive()->setVersion(static_cast<uint32_t>(p_version));
}

int SourcePPVPK::get_chunk_size() const {
	ERR_FAIL_COND_V_MSG(get_archive() == nullptr, 0, "SourcePPVPK must be opened before use.");
	return static_cast<int>(get_archive()->getChunkSize());
}

void SourcePPVPK::set_chunk_size(int p_chunk_size) {
	ERR_FAIL_COND_MSG(get_archive() == nullptr, "SourcePPVPK must be opened before use.");
	ERR_FAIL_COND_MSG(p_chunk_size < 0, "Chunk size must be non-negative.");
	get_archive()->setChunkSize(static_cast<uint32_t>(p_chunk_size));
}

void SourcePPVPK::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "path"), &SourcePPVPK::open);
	ClassDB::bind_method(D_METHOD("create", "path", "version"), &SourcePPVPK::create, DEFVAL(2));
	ClassDB::bind_method(D_METHOD("close"), &SourcePPVPK::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPVPK::is_open);

	ClassDB::bind_method(D_METHOD("get_path"), &SourcePPVPK::get_path);
	ClassDB::bind_method(D_METHOD("get_file_count"), &SourcePPVPK::get_file_count);
	ClassDB::bind_method(D_METHOD("get_files"), &SourcePPVPK::get_files);
	ClassDB::bind_method(D_METHOD("has_file", "path"), &SourcePPVPK::has_file);
	ClassDB::bind_method(D_METHOD("read_file", "path"), &SourcePPVPK::read_file);
	ClassDB::bind_method(D_METHOD("read_file_text", "path"), &SourcePPVPK::read_file_text);

	ClassDB::bind_method(D_METHOD("add_file", "entry_path", "source_path", "preload_bytes", "save_to_directory"), &SourcePPVPK::add_file, DEFVAL(0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_file_from_buffer", "entry_path", "data", "preload_bytes", "save_to_directory"), &SourcePPVPK::add_file_from_buffer, DEFVAL(0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_directory", "entry_base_dir", "source_dir", "preload_bytes", "save_to_directory"), &SourcePPVPK::add_directory, DEFVAL(0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("remove_file", "path"), &SourcePPVPK::remove_file);
	ClassDB::bind_method(D_METHOD("remove_directory", "directory"), &SourcePPVPK::remove_directory);

	ClassDB::bind_method(D_METHOD("bake", "output_dir", "generate_md5_entries"), &SourcePPVPK::bake, DEFVAL(String()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("verify_entry_checksums"), &SourcePPVPK::verify_entry_checksums);
	ClassDB::bind_method(D_METHOD("verify_pack_checksum"), &SourcePPVPK::verify_pack_checksum);
	ClassDB::bind_method(D_METHOD("verify_signature"), &SourcePPVPK::verify_signature);
	ClassDB::bind_method(D_METHOD("has_pack_checksum"), &SourcePPVPK::has_pack_checksum);
	ClassDB::bind_method(D_METHOD("has_signature"), &SourcePPVPK::has_signature);

	ClassDB::bind_method(D_METHOD("get_version"), &SourcePPVPK::get_version);
	ClassDB::bind_method(D_METHOD("set_version", "version"), &SourcePPVPK::set_version);
	ClassDB::bind_method(D_METHOD("get_chunk_size"), &SourcePPVPK::get_chunk_size);
	ClassDB::bind_method(D_METHOD("set_chunk_size", "chunk_size"), &SourcePPVPK::set_chunk_size);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "version"), "set_version", "get_version");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_size"), "set_chunk_size", "get_chunk_size");
}