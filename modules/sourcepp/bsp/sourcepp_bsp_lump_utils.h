/**************************************************************************/
/*  sourcepp_bsp_lump_utils.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/math/vector3.h"
#include "core/string/ustring.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SourcePPBSPLumpUtils {

bool can_read_bytes(const std::vector<std::byte> &p_bytes, size_t p_offset, size_t p_size);
uint8_t read_u8(const std::vector<std::byte> &p_bytes, size_t p_offset);
uint16_t read_u16_le(const std::vector<std::byte> &p_bytes, size_t p_offset);
uint32_t read_u32_le(const std::vector<std::byte> &p_bytes, size_t p_offset);
int32_t read_i32_le(const std::vector<std::byte> &p_bytes, size_t p_offset);
float read_f32_le(const std::vector<std::byte> &p_bytes, size_t p_offset);
Vector3 read_source_vector3_le(const std::vector<std::byte> &p_bytes, size_t p_offset);
String read_fixed_utf8_string(const std::vector<std::byte> &p_bytes, size_t p_offset, size_t p_length);

size_t static_prop_record_size(int p_version);
int displacement_power_vertex_count(int p_power);
int displacement_power_triangle_count(int p_power);

} // namespace SourcePPBSPLumpUtils
