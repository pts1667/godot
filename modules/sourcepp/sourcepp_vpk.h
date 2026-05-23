/**************************************************************************/
/*  sourcepp_vpk.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/object/ref_counted.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vpkpp {
class PackFile;
class VPK;
}

class SourcePPVPK : public RefCounted {
	GDCLASS(SourcePPVPK, RefCounted);

	std::unique_ptr<vpkpp::VPK> archive;

	static void _bind_methods();

	static std::unique_ptr<vpkpp::VPK> _cast_vpk(std::unique_ptr<class vpkpp::PackFile> p_pack_file);
	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(const std::string &p_string);
	static PackedByteArray _to_packed_byte_array(std::span<const std::byte> p_bytes);
	static std::vector<std::byte> _to_byte_vector(const PackedByteArray &p_data);

	Error _set_archive(std::unique_ptr<vpkpp::PackFile> p_pack_file);
	vpkpp::VPK *get_archive();
	const vpkpp::VPK *get_archive() const;

public:
	SourcePPVPK();
	~SourcePPVPK() override;

	Error open(const String &p_path);
	Error create(const String &p_path, int p_version = 2);
	void close();
	bool is_open() const;

	String get_path() const;
	int get_file_count() const;
	PackedStringArray get_files() const;
	bool has_file(const String &p_path) const;
	PackedByteArray read_file(const String &p_path) const;
	String read_file_text(const String &p_path) const;

	bool add_file(const String &p_entry_path, const String &p_source_path, int p_preload_bytes = 0, bool p_save_to_directory = false);
	bool add_file_from_buffer(const String &p_entry_path, const PackedByteArray &p_data, int p_preload_bytes = 0, bool p_save_to_directory = false);
	int add_directory(const String &p_entry_base_dir, const String &p_source_dir, int p_preload_bytes = 0, bool p_save_to_directory = false);
	bool remove_file(const String &p_path);
	int remove_directory(const String &p_directory);

	Error bake(const String &p_output_dir = String(), bool p_generate_md5_entries = false);
	PackedStringArray verify_entry_checksums() const;
	bool verify_pack_checksum() const;
	bool verify_signature() const;
	bool has_pack_checksum() const;
	bool has_signature() const;

	int get_version() const;
	void set_version(int p_version);
	int get_chunk_size() const;
	void set_chunk_size(int p_chunk_size);
};