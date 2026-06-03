/**************************************************************************/
/*  sourcepp_bsp_lump_utils.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp_lump_utils.h"

#include <cstring>

namespace {

constexpr size_t STATIC_PROP_V4_SIZE = 56;
constexpr size_t STATIC_PROP_V5_SIZE = 60;
constexpr size_t STATIC_PROP_V6_SIZE = 64;
constexpr size_t STATIC_PROP_V7_SIZE = 72;

} // namespace

namespace SourcePPBSPLumpUtils {

bool can_read_bytes(const std::vector<std::byte> &p_bytes, size_t p_offset, size_t p_size) {
	return p_offset <= p_bytes.size() && p_size <= p_bytes.size() - p_offset;
}

uint8_t read_u8(const std::vector<std::byte> &p_bytes, size_t p_offset) {
	return std::to_integer<uint8_t>(p_bytes[p_offset]);
}

uint16_t read_u16_le(const std::vector<std::byte> &p_bytes, size_t p_offset) {
	return static_cast<uint16_t>(read_u8(p_bytes, p_offset) | (static_cast<uint16_t>(read_u8(p_bytes, p_offset + 1)) << 8));
}

uint32_t read_u32_le(const std::vector<std::byte> &p_bytes, size_t p_offset) {
	return static_cast<uint32_t>(read_u8(p_bytes, p_offset)) |
			(static_cast<uint32_t>(read_u8(p_bytes, p_offset + 1)) << 8) |
			(static_cast<uint32_t>(read_u8(p_bytes, p_offset + 2)) << 16) |
			(static_cast<uint32_t>(read_u8(p_bytes, p_offset + 3)) << 24);
}

int32_t read_i32_le(const std::vector<std::byte> &p_bytes, size_t p_offset) {
	return static_cast<int32_t>(read_u32_le(p_bytes, p_offset));
}

float read_f32_le(const std::vector<std::byte> &p_bytes, size_t p_offset) {
	const uint32_t packed = read_u32_le(p_bytes, p_offset);
	float value = 0.0f;
	static_assert(sizeof(value) == sizeof(packed));
	memcpy(&value, &packed, sizeof(value));
	return value;
}

Vector3 read_source_vector3_le(const std::vector<std::byte> &p_bytes, size_t p_offset) {
	return Vector3(read_f32_le(p_bytes, p_offset), read_f32_le(p_bytes, p_offset + 4), read_f32_le(p_bytes, p_offset + 8));
}

String read_fixed_utf8_string(const std::vector<std::byte> &p_bytes, size_t p_offset, size_t p_length) {
	size_t end_offset = p_offset;
	const size_t max_offset = p_offset + p_length;
	while (end_offset < max_offset && std::to_integer<uint8_t>(p_bytes[end_offset]) != 0) {
		end_offset++;
	}
	return String::utf8(reinterpret_cast<const char *>(p_bytes.data()) + p_offset, static_cast<int>(end_offset - p_offset));
}

size_t static_prop_record_size(int p_version) {
	if (p_version == 4) {
		return STATIC_PROP_V4_SIZE;
	}
	if (p_version == 5) {
		return STATIC_PROP_V5_SIZE;
	}
	if (p_version == 6) {
		return STATIC_PROP_V6_SIZE;
	}
	if (p_version >= 7) {
		return STATIC_PROP_V7_SIZE;
	}
	return 0;
}

int displacement_power_vertex_count(int p_power) {
	const int side = (1 << p_power) + 1;
	return side * side;
}

int displacement_power_triangle_count(int p_power) {
	const int side_quads = 1 << p_power;
	return side_quads * side_quads * 2;
}

} // namespace SourcePPBSPLumpUtils
