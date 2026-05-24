/**************************************************************************/
/*  sourcepp_vtf.h                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/io/image.h"
#include "core/object/ref_counted.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vtfpp {
enum class ImageFormat : int32_t;
class VTF;
}

class SourcePPVTF : public RefCounted {
	GDCLASS(SourcePPVTF, RefCounted);

	std::unique_ptr<vtfpp::VTF> texture;
	String source_path;

	static void _bind_methods();

	static std::string _to_utf8(const String &p_string);
	static String _from_utf8(const std::string &p_string);
	static PackedByteArray _to_packed_byte_array(std::span<const std::byte> p_bytes);
	static std::vector<std::byte> _to_byte_vector(const Vector<uint8_t> &p_data);
	static Ref<Image> _to_rgba8_image(std::span<const std::byte> p_data, int p_width, int p_height);
	static Error _normalize_image(const Ref<Image> &p_image, Ref<Image> &r_image, vtfpp::ImageFormat &r_format);

	vtfpp::VTF *get_texture();
	const vtfpp::VTF *get_texture() const;

public:
	enum ImageFormatConstant {
		FORMAT_RGBA8888 = 0,
		FORMAT_RGB888 = 2,
		FORMAT_RGB565 = 4,
		FORMAT_I8 = 5,
		FORMAT_IA88 = 6,
		FORMAT_BGRA8888 = 12,
		FORMAT_DXT1 = 13,
		FORMAT_DXT3 = 14,
		FORMAT_DXT5 = 15,
		FORMAT_RGBA16161616F = 24,
		FORMAT_RGBA16161616 = 25,
		FORMAT_R32F = 27,
		FORMAT_RGB323232F = 28,
		FORMAT_RGBA32323232F = 29,
		FORMAT_RG1616F = 30,
		FORMAT_RG3232F = 31,
		FORMAT_RGBX8888 = 32,
		FORMAT_EMPTY = 33,
		FORMAT_ATI2N = 34,
		FORMAT_ATI1N = 35,
		FORMAT_RGBA1010102 = 36,
		FORMAT_BGRA1010102 = 37,
		FORMAT_R16F = 38,
		FORMAT_TITANFALL_BC6H = 66,
		FORMAT_TITANFALL_BC7 = 67,
		FORMAT_STRATA_R8 = 69,
		FORMAT_STRATA_BC7 = 70,
		FORMAT_STRATA_BC6H = 71,
	};

	enum PlatformConstant {
		PLATFORM_UNKNOWN = 0x000,
		PLATFORM_PC = 0x007,
		PLATFORM_XBOX = 0x005,
		PLATFORM_X360 = 0x360,
		PLATFORM_PS3_ORANGEBOX = 0x333,
		PLATFORM_PS3_PORTAL2 = 0x334,
	};

	SourcePPVTF();
	~SourcePPVTF() override;

	Error open(const String &p_path, bool p_parse_header_only = false);
	Error open_from_buffer(const PackedByteArray &p_data, bool p_parse_header_only = false);
	Error create(int p_width, int p_height, int p_format = FORMAT_RGBA8888, int p_version = 4, bool p_srgb = false, bool p_generate_mips = true, bool p_generate_thumbnail = true);
	Error create_from_image(const Ref<Image> &p_image, int p_version = 4, bool p_srgb = false, bool p_generate_mips = true, bool p_generate_thumbnail = true);
	void close();
	bool is_open() const;

	String get_path() const;
	bool has_image() const;
	Ref<Image> get_image(int p_mip = 0, int p_frame = 0, int p_face = 0, int p_slice = 0) const;
	bool set_image(const Ref<Image> &p_image, int p_mip = 0, int p_frame = 0, int p_face = 0, int p_slice = 0);

	bool has_thumbnail_data() const;
	Ref<Image> get_thumbnail() const;
	bool set_thumbnail(const Ref<Image> &p_image);
	void compute_thumbnail();
	void remove_thumbnail();

	PackedByteArray bake() const;
	bool save(const String &p_path);

	int get_width(int p_mip = 0) const;
	int get_height(int p_mip = 0) const;
	int get_depth(int p_mip = 0) const;
	int get_mip_count() const;
	int get_frame_count() const;
	int get_face_count() const;

	int get_format() const;
	void set_format(int p_format, float p_quality = -1.0f);

	int get_version() const;
	void set_version(int p_version);

	int get_flags() const;
	void set_flags(int p_flags);

	bool is_srgb() const;
	void set_srgb(bool p_srgb);

	int get_platform() const;
	void set_platform(int p_platform);
};

VARIANT_ENUM_CAST(SourcePPVTF::ImageFormatConstant)
VARIANT_ENUM_CAST(SourcePPVTF::PlatformConstant)