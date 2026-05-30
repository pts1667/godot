/**************************************************************************/
/*  sourcepp_vtf.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_vtf.h"

#include "sourcepp_resolver.h"

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"

#include <vtfpp/VTF.h>

#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

SourcePPVTF::SourcePPVTF() = default;

SourcePPVTF::~SourcePPVTF() = default;

void SourcePPVTF::set_resolver(const Ref<SourcePPResolver> &p_resolver) {
	resolver = p_resolver;
}

Ref<SourcePPResolver> SourcePPVTF::get_resolver() const {
	return resolver;
}

void SourcePPVTF::set_resolver_game_id(const String &p_game_id) {
	resolver_game_id = p_game_id.strip_edges().to_lower();
}

String SourcePPVTF::get_resolver_game_id() const {
	return resolver_game_id;
}

std::string SourcePPVTF::_to_utf8(const String &p_string) {
	CharString utf8 = p_string.utf8();
	return std::string(utf8.get_data());
}

String SourcePPVTF::_from_utf8(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

PackedByteArray SourcePPVTF::_to_packed_byte_array(std::span<const std::byte> p_bytes) {
	PackedByteArray out;
	out.resize(static_cast<int>(p_bytes.size()));
	if (!p_bytes.empty()) {
		std::memcpy(out.ptrw(), p_bytes.data(), p_bytes.size());
	}
	return out;
}

std::vector<std::byte> SourcePPVTF::_to_byte_vector(const PackedByteArray &p_data) {
	std::vector<std::byte> out;
	out.resize(static_cast<size_t>(p_data.size()));
	if (!out.empty()) {
		std::memcpy(out.data(), p_data.ptr(), out.size());
	}
	return out;
}

Ref<Image> SourcePPVTF::_to_rgba8_image(std::span<const std::byte> p_data, int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return Ref<Image>();
	}

	Vector<uint8_t> image_data;
	image_data.resize(static_cast<int>(p_data.size()));
	if (!p_data.empty()) {
		std::memcpy(image_data.ptrw(), p_data.data(), p_data.size());
	}
	return Image::create_from_data(p_width, p_height, false, Image::FORMAT_RGBA8, image_data);
}

Error SourcePPVTF::_normalize_image(const Ref<Image> &p_image, Ref<Image> &r_image, vtfpp::ImageFormat &r_format) {
	ERR_FAIL_COND_V_MSG(p_image.is_null(), ERR_INVALID_PARAMETER, "Image must not be null.");
	ERR_FAIL_COND_V_MSG(p_image->is_empty(), ERR_INVALID_PARAMETER, "Image must not be empty.");

	r_image.instantiate();
	r_image->copy_internals_from(p_image);

	if (r_image->is_compressed()) {
		const Error err = r_image->decompress();
		ERR_FAIL_COND_V_MSG(err != OK, err, "Compressed images must be decompressed before encoding to VTF.");
	}
	if (r_image->has_mipmaps()) {
		r_image->clear_mipmaps();
	}

	switch (r_image->get_format()) {
		case Image::FORMAT_L8:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_I8);
			return OK;
		case Image::FORMAT_LA8:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_IA88);
			return OK;
		case Image::FORMAT_RGB8:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGB888);
			return OK;
		case Image::FORMAT_RGBA8:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGBA8888);
			return OK;
		case Image::FORMAT_RGB565:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGB565);
			return OK;
		case Image::FORMAT_RF:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_R32F);
			return OK;
		case Image::FORMAT_RGF:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RG3232F);
			return OK;
		case Image::FORMAT_RGBF:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGB323232F);
			return OK;
		case Image::FORMAT_RGBAF:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGBA32323232F);
			return OK;
		case Image::FORMAT_RH:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_R16F);
			return OK;
		case Image::FORMAT_RGH:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RG1616F);
			return OK;
		case Image::FORMAT_RGBH:
			r_image->convert(Image::FORMAT_RGBAH);
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGBA16161616F);
			return OK;
		case Image::FORMAT_RGBAH:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGBA16161616F);
			return OK;
		case Image::FORMAT_RGB16:
			r_image->convert(Image::FORMAT_RGBA16);
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGBA16161616);
			return OK;
		case Image::FORMAT_RGBA16:
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGBA16161616);
			return OK;
		default:
			r_image->convert(Image::FORMAT_RGBA8);
			r_format = static_cast<vtfpp::ImageFormat>(FORMAT_RGBA8888);
			return OK;
	}
}

PackedByteArray SourcePPVTF::_read_file_bytes(const String &p_path, Error *r_error) const {
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

vtfpp::VTF *SourcePPVTF::get_texture() {
	return texture.get();
}

const vtfpp::VTF *SourcePPVTF::get_texture() const {
	return texture.get();
}

Error SourcePPVTF::open(const String &p_path, bool p_parse_header_only) {
	close();

	if (FileAccess::exists(p_path)) {
		auto loaded = std::make_unique<vtfpp::VTF>(std::filesystem::path(_to_utf8(p_path)), p_parse_header_only);
		if (!static_cast<bool>(*loaded)) {
			return ERR_FILE_CANT_OPEN;
		}
		texture = std::move(loaded);
		source_path = p_path;
		return OK;
	}

	Error read_error = OK;
	const PackedByteArray data = _read_file_bytes(p_path, &read_error);
	ERR_FAIL_COND_V_MSG(read_error != OK, read_error, "Failed to load the VTF file.");

	const Error open_error = open_from_buffer(data, p_parse_header_only);
	if (open_error != OK) {
		return open_error;
	}

	source_path = p_path;
	return OK;
}

Error SourcePPVTF::open_from_buffer(const PackedByteArray &p_data, bool p_parse_header_only) {
	close();

	std::vector<std::byte> data = _to_byte_vector(p_data);
	auto loaded = std::make_unique<vtfpp::VTF>(std::span<const std::byte>(data.data(), data.size()), p_parse_header_only);
	if (!static_cast<bool>(*loaded)) {
		return ERR_FILE_CANT_OPEN;
	}
	texture = std::move(loaded);
	source_path = String();
	return OK;
}

Error SourcePPVTF::create(int p_width, int p_height, int p_format, int p_version, bool p_srgb, bool p_generate_mips, bool p_generate_thumbnail) {
	ERR_FAIL_COND_V_MSG(p_width <= 0 || p_width > UINT16_MAX, ERR_INVALID_PARAMETER, "Width must be between 1 and 65535.");
	ERR_FAIL_COND_V_MSG(p_height <= 0 || p_height > UINT16_MAX, ERR_INVALID_PARAMETER, "Height must be between 1 and 65535.");
	ERR_FAIL_COND_V_MSG(p_version < 0, ERR_INVALID_PARAMETER, "Version must be non-negative.");

	close();

	vtfpp::VTF::CreationOptions options{};
	options.version = static_cast<uint32_t>(p_version);
	options.computeMips = p_generate_mips;
	options.computeThumbnail = p_generate_thumbnail;

	auto created = vtfpp::VTF::create(static_cast<vtfpp::ImageFormat>(p_format), static_cast<uint16_t>(p_width), static_cast<uint16_t>(p_height), options);
	if (!static_cast<bool>(created)) {
		return ERR_CANT_CREATE;
	}

	texture = std::make_unique<vtfpp::VTF>(std::move(created));
	texture->setSRGB(p_srgb);
	source_path = String();
	return OK;
}

Error SourcePPVTF::create_from_image(const Ref<Image> &p_image, int p_version, bool p_srgb, bool p_generate_mips, bool p_generate_thumbnail) {
	ERR_FAIL_COND_V_MSG(p_version < 0, ERR_INVALID_PARAMETER, "Version must be non-negative.");

	Ref<Image> image;
	vtfpp::ImageFormat image_format{};
	const Error err = _normalize_image(p_image, image, image_format);
	ERR_FAIL_COND_V(err != OK, err);
	ERR_FAIL_COND_V_MSG(image->get_width() > UINT16_MAX || image->get_height() > UINT16_MAX, ERR_INVALID_PARAMETER, "Image dimensions must be 65535 or smaller.");

	vtfpp::VTF::CreationOptions options{};
	options.version = static_cast<uint32_t>(p_version);
	options.computeMips = p_generate_mips;
	options.computeThumbnail = p_generate_thumbnail;

	std::vector<std::byte> data = _to_byte_vector(image->get_data());
	auto created = vtfpp::VTF::create(std::span<const std::byte>(data.data(), data.size()), image_format, static_cast<uint16_t>(image->get_width()), static_cast<uint16_t>(image->get_height()), options);
	if (!static_cast<bool>(created)) {
		return ERR_CANT_CREATE;
	}

	close();
	texture = std::make_unique<vtfpp::VTF>(std::move(created));
	texture->setSRGB(p_srgb);
	source_path = String();
	return OK;
}

void SourcePPVTF::close() {
	texture.reset();
	source_path = String();
}

bool SourcePPVTF::is_open() const {
	return texture != nullptr;
}

String SourcePPVTF::get_path() const {
	return source_path;
}

bool SourcePPVTF::has_image() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, false, "SourcePPVTF must be opened before use.");
	return get_texture()->hasImageData();
}

Ref<Image> SourcePPVTF::get_image(int p_mip, int p_frame, int p_face, int p_slice) const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, Ref<Image>(), "SourcePPVTF must be opened before use.");

	const int width = static_cast<int>(get_texture()->getWidth(static_cast<uint8_t>(p_mip)));
	const int height = static_cast<int>(get_texture()->getHeight(static_cast<uint8_t>(p_mip)));
	const auto image_data = get_texture()->getImageDataAsRGBA8888(static_cast<uint8_t>(p_mip), static_cast<uint16_t>(p_frame), static_cast<uint8_t>(p_face), static_cast<uint16_t>(p_slice));
	return _to_rgba8_image(image_data, width, height);
}

bool SourcePPVTF::set_image(const Ref<Image> &p_image, int p_mip, int p_frame, int p_face, int p_slice) {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, false, "SourcePPVTF must be opened before use.");

	Ref<Image> image;
	vtfpp::ImageFormat image_format{};
	if (_normalize_image(p_image, image, image_format) != OK) {
		return false;
	}

	std::vector<std::byte> data = _to_byte_vector(image->get_data());
	return get_texture()->setImage(std::span<const std::byte>(data.data(), data.size()), image_format, static_cast<uint16_t>(image->get_width()), static_cast<uint16_t>(image->get_height()), {}, static_cast<uint8_t>(p_mip), static_cast<uint16_t>(p_frame), static_cast<uint8_t>(p_face), static_cast<uint16_t>(p_slice));
}

bool SourcePPVTF::has_thumbnail_data() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, false, "SourcePPVTF must be opened before use.");
	return get_texture()->hasThumbnailData();
}

Ref<Image> SourcePPVTF::get_thumbnail() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, Ref<Image>(), "SourcePPVTF must be opened before use.");

	const auto thumbnail_data = get_texture()->getThumbnailDataAsRGBA8888();
	return _to_rgba8_image(thumbnail_data, static_cast<int>(get_texture()->getThumbnailWidth()), static_cast<int>(get_texture()->getThumbnailHeight()));
}

bool SourcePPVTF::set_thumbnail(const Ref<Image> &p_image) {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, false, "SourcePPVTF must be opened before use.");

	Ref<Image> image;
	vtfpp::ImageFormat image_format{};
	if (_normalize_image(p_image, image, image_format) != OK) {
		return false;
	}

	std::vector<std::byte> data = _to_byte_vector(image->get_data());
	get_texture()->setThumbnail(std::span<const std::byte>(data.data(), data.size()), image_format, static_cast<uint16_t>(image->get_width()), static_cast<uint16_t>(image->get_height()));
	return true;
}

void SourcePPVTF::compute_thumbnail() {
	ERR_FAIL_COND_MSG(get_texture() == nullptr, "SourcePPVTF must be opened before use.");
	get_texture()->computeThumbnail();
}

void SourcePPVTF::remove_thumbnail() {
	ERR_FAIL_COND_MSG(get_texture() == nullptr, "SourcePPVTF must be opened before use.");
	get_texture()->removeThumbnail();
}

PackedByteArray SourcePPVTF::bake() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, PackedByteArray(), "SourcePPVTF must be opened before use.");
	return _to_packed_byte_array(get_texture()->bake());
}

bool SourcePPVTF::save(const String &p_path) {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, false, "SourcePPVTF must be opened before use.");

	if (!get_texture()->bake(std::filesystem::path(_to_utf8(p_path)))) {
		return false;
	}
	source_path = p_path;
	return true;
}

int SourcePPVTF::get_width(int p_mip) const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getWidth(static_cast<uint8_t>(p_mip)));
}

int SourcePPVTF::get_height(int p_mip) const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getHeight(static_cast<uint8_t>(p_mip)));
}

int SourcePPVTF::get_depth(int p_mip) const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getDepth(static_cast<uint8_t>(p_mip)));
}

int SourcePPVTF::get_mip_count() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getMipCount());
}

int SourcePPVTF::get_frame_count() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getFrameCount());
}

int SourcePPVTF::get_face_count() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getFaceCount());
}

int SourcePPVTF::get_format() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, FORMAT_EMPTY, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getFormat());
}

void SourcePPVTF::set_format(int p_format) {
	set_format_with_quality(p_format, -1.0f);
}

void SourcePPVTF::set_format_with_quality(int p_format, float p_quality) {
	ERR_FAIL_COND_MSG(get_texture() == nullptr, "SourcePPVTF must be opened before use.");
	get_texture()->setFormat(static_cast<vtfpp::ImageFormat>(p_format), {}, p_quality);
}

int SourcePPVTF::get_version() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getVersion());
}

void SourcePPVTF::set_version(int p_version) {
	ERR_FAIL_COND_MSG(get_texture() == nullptr, "SourcePPVTF must be opened before use.");
	ERR_FAIL_COND_MSG(p_version < 0, "Version must be non-negative.");
	get_texture()->setVersion(static_cast<uint32_t>(p_version));
}

int SourcePPVTF::get_flags() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, 0, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getFlags());
}

void SourcePPVTF::set_flags(int p_flags) {
	ERR_FAIL_COND_MSG(get_texture() == nullptr, "SourcePPVTF must be opened before use.");
	get_texture()->setFlags(static_cast<uint32_t>(p_flags));
}

bool SourcePPVTF::is_srgb() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, false, "SourcePPVTF must be opened before use.");
	return get_texture()->isSRGB();
}

void SourcePPVTF::set_srgb(bool p_srgb) {
	ERR_FAIL_COND_MSG(get_texture() == nullptr, "SourcePPVTF must be opened before use.");
	get_texture()->setSRGB(p_srgb);
}

int SourcePPVTF::get_platform() const {
	ERR_FAIL_COND_V_MSG(get_texture() == nullptr, PLATFORM_UNKNOWN, "SourcePPVTF must be opened before use.");
	return static_cast<int>(get_texture()->getPlatform());
}

void SourcePPVTF::set_platform(int p_platform) {
	ERR_FAIL_COND_MSG(get_texture() == nullptr, "SourcePPVTF must be opened before use.");
	get_texture()->setPlatform(static_cast<vtfpp::VTF::Platform>(p_platform));
}

void SourcePPVTF::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resolver", "resolver"), &SourcePPVTF::set_resolver);
	ClassDB::bind_method(D_METHOD("get_resolver"), &SourcePPVTF::get_resolver);
	ClassDB::bind_method(D_METHOD("set_resolver_game_id", "game_id"), &SourcePPVTF::set_resolver_game_id);
	ClassDB::bind_method(D_METHOD("get_resolver_game_id"), &SourcePPVTF::get_resolver_game_id);
	ClassDB::bind_method(D_METHOD("open", "path", "parse_header_only"), &SourcePPVTF::open, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("open_from_buffer", "data", "parse_header_only"), &SourcePPVTF::open_from_buffer, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("create", "width", "height", "format", "version", "srgb", "generate_mips", "generate_thumbnail"), &SourcePPVTF::create, DEFVAL(FORMAT_RGBA8888), DEFVAL(4), DEFVAL(false), DEFVAL(true), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("create_from_image", "image", "version", "srgb", "generate_mips", "generate_thumbnail"), &SourcePPVTF::create_from_image, DEFVAL(4), DEFVAL(false), DEFVAL(true), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("close"), &SourcePPVTF::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPVTF::is_open);

	ClassDB::bind_method(D_METHOD("get_path"), &SourcePPVTF::get_path);
	ClassDB::bind_method(D_METHOD("has_image"), &SourcePPVTF::has_image);
	ClassDB::bind_method(D_METHOD("get_image", "mip", "frame", "face", "slice"), &SourcePPVTF::get_image, DEFVAL(0), DEFVAL(0), DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("set_image", "image", "mip", "frame", "face", "slice"), &SourcePPVTF::set_image, DEFVAL(0), DEFVAL(0), DEFVAL(0), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("has_thumbnail_data"), &SourcePPVTF::has_thumbnail_data);
	ClassDB::bind_method(D_METHOD("get_thumbnail"), &SourcePPVTF::get_thumbnail);
	ClassDB::bind_method(D_METHOD("set_thumbnail", "image"), &SourcePPVTF::set_thumbnail);
	ClassDB::bind_method(D_METHOD("compute_thumbnail"), &SourcePPVTF::compute_thumbnail);
	ClassDB::bind_method(D_METHOD("remove_thumbnail"), &SourcePPVTF::remove_thumbnail);

	ClassDB::bind_method(D_METHOD("bake"), &SourcePPVTF::bake);
	ClassDB::bind_method(D_METHOD("save", "path"), &SourcePPVTF::save);

	ClassDB::bind_method(D_METHOD("get_width", "mip"), &SourcePPVTF::get_width, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_height", "mip"), &SourcePPVTF::get_height, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_depth", "mip"), &SourcePPVTF::get_depth, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_mip_count"), &SourcePPVTF::get_mip_count);
	ClassDB::bind_method(D_METHOD("get_frame_count"), &SourcePPVTF::get_frame_count);
	ClassDB::bind_method(D_METHOD("get_face_count"), &SourcePPVTF::get_face_count);

	ClassDB::bind_method(D_METHOD("get_format"), &SourcePPVTF::get_format);
	ClassDB::bind_method(D_METHOD("set_format", "format"), &SourcePPVTF::set_format);
	ClassDB::bind_method(D_METHOD("set_format_with_quality", "format", "quality"), &SourcePPVTF::set_format_with_quality, DEFVAL(-1.0f));
	ClassDB::bind_method(D_METHOD("get_version"), &SourcePPVTF::get_version);
	ClassDB::bind_method(D_METHOD("set_version", "version"), &SourcePPVTF::set_version);
	ClassDB::bind_method(D_METHOD("get_flags"), &SourcePPVTF::get_flags);
	ClassDB::bind_method(D_METHOD("set_flags", "flags"), &SourcePPVTF::set_flags);
	ClassDB::bind_method(D_METHOD("is_srgb"), &SourcePPVTF::is_srgb);
	ClassDB::bind_method(D_METHOD("set_srgb", "srgb"), &SourcePPVTF::set_srgb);
	ClassDB::bind_method(D_METHOD("get_platform"), &SourcePPVTF::get_platform);
	ClassDB::bind_method(D_METHOD("set_platform", "platform"), &SourcePPVTF::set_platform);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "resolver", PROPERTY_HINT_RESOURCE_TYPE, "SourcePPResolver"), "set_resolver", "get_resolver");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "resolver_game_id"), "set_resolver_game_id", "get_resolver_game_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "format"), "set_format", "get_format");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "version"), "set_version", "get_version");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flags"), "set_flags", "get_flags");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "srgb"), "set_srgb", "is_srgb");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "platform"), "set_platform", "get_platform");

	BIND_ENUM_CONSTANT(FORMAT_RGBA8888);
	BIND_ENUM_CONSTANT(FORMAT_RGB888);
	BIND_ENUM_CONSTANT(FORMAT_RGB565);
	BIND_ENUM_CONSTANT(FORMAT_I8);
	BIND_ENUM_CONSTANT(FORMAT_IA88);
	BIND_ENUM_CONSTANT(FORMAT_BGRA8888);
	BIND_ENUM_CONSTANT(FORMAT_DXT1);
	BIND_ENUM_CONSTANT(FORMAT_DXT3);
	BIND_ENUM_CONSTANT(FORMAT_DXT5);
	BIND_ENUM_CONSTANT(FORMAT_RGBA16161616F);
	BIND_ENUM_CONSTANT(FORMAT_RGBA16161616);
	BIND_ENUM_CONSTANT(FORMAT_R32F);
	BIND_ENUM_CONSTANT(FORMAT_RGB323232F);
	BIND_ENUM_CONSTANT(FORMAT_RGBA32323232F);
	BIND_ENUM_CONSTANT(FORMAT_RG1616F);
	BIND_ENUM_CONSTANT(FORMAT_RG3232F);
	BIND_ENUM_CONSTANT(FORMAT_RGBX8888);
	BIND_ENUM_CONSTANT(FORMAT_EMPTY);
	BIND_ENUM_CONSTANT(FORMAT_ATI2N);
	BIND_ENUM_CONSTANT(FORMAT_ATI1N);
	BIND_ENUM_CONSTANT(FORMAT_RGBA1010102);
	BIND_ENUM_CONSTANT(FORMAT_BGRA1010102);
	BIND_ENUM_CONSTANT(FORMAT_R16F);
	BIND_ENUM_CONSTANT(FORMAT_TITANFALL_BC6H);
	BIND_ENUM_CONSTANT(FORMAT_TITANFALL_BC7);
	BIND_ENUM_CONSTANT(FORMAT_STRATA_R8);
	BIND_ENUM_CONSTANT(FORMAT_STRATA_BC7);
	BIND_ENUM_CONSTANT(FORMAT_STRATA_BC6H);

	BIND_ENUM_CONSTANT(PLATFORM_UNKNOWN);
	BIND_ENUM_CONSTANT(PLATFORM_PC);
	BIND_ENUM_CONSTANT(PLATFORM_XBOX);
	BIND_ENUM_CONSTANT(PLATFORM_X360);
	BIND_ENUM_CONSTANT(PLATFORM_PS3_ORANGEBOX);
	BIND_ENUM_CONSTANT(PLATFORM_PS3_PORTAL2);
}