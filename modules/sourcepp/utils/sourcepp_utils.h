/**************************************************************************/
/*  sourcepp_utils.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/math/quaternion.h"
#include "core/math/transform_3d.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

#include <sourcepp/Math.h>

class SourcePPResolver;

namespace SourcePPUtils {

constexpr float SOURCE_UNIT_TO_METERS = 0.0254f;

String normalize_source_path(const String &p_path);
String ensure_extension(const String &p_path, const String &p_extension);
String strip_material_prefix(const String &p_path);
bool path_exists_with_resolver(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id);

Vector3 source_vector_to_godot_direction(const Vector3 &p_direction);
Vector3 source_position_to_godot(const sourcepp::math::Vec3f &p_position);
Vector3 source_vector_to_vector3(const sourcepp::math::Vec3f &p_vector);
Vector3 parse_source_vector_string(const String &p_value);
Basis source_angles_to_godot_basis(const Vector3 &p_source_angles);

Quaternion sanitize_quaternion(const Quaternion &p_quaternion);
Quaternion source_quaternion_to_quaternion(const sourcepp::math::Quat &p_quaternion);
Transform3D source_matrix_to_transform_3d(const sourcepp::math::Mat3x4f &p_matrix);

} // namespace SourcePPUtils
