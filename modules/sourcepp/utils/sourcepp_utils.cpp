/**************************************************************************/
/*  sourcepp_utils.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_utils.h"

#include "modules/sourcepp/sourcepp_resolver.h"

#include "core/io/file_access.h"
#include "core/math/math_funcs.h"

namespace SourcePPUtils {

String normalize_source_path(const String &p_path) {
	return p_path.replace("\\", "/").strip_edges();
}

String ensure_extension(const String &p_path, const String &p_extension) {
	const String extension = p_extension.begins_with(".") ? p_extension.substr(1) : p_extension;
	return p_path.get_extension().to_lower() == extension.to_lower() ? p_path : p_path + "." + extension;
}

String strip_material_prefix(const String &p_path) {
	return p_path.begins_with("materials/") ? p_path.substr(10) : p_path;
}

bool path_exists_with_resolver(const String &p_path, const Ref<SourcePPResolver> &p_resolver, const String &p_game_id) {
	if (FileAccess::exists(p_path)) {
		return true;
	}
	if (!p_resolver.is_valid()) {
		return false;
	}
	return p_game_id.is_empty() ? p_resolver->has_file(p_path) : p_resolver->has_file(p_path, p_game_id);
}

Vector3 source_vector_to_godot_direction(const Vector3 &p_direction) {
	return Vector3(p_direction.x, p_direction.z, -p_direction.y);
}

Vector3 source_position_to_godot(const sourcepp::math::Vec3f &p_position) {
	return Vector3(p_position[0], p_position[2], -p_position[1]) * SOURCE_UNIT_TO_METERS;
}

Vector3 source_vector_to_vector3(const sourcepp::math::Vec3f &p_vector) {
	return Vector3(p_vector[0], p_vector[1], p_vector[2]);
}

Vector3 parse_source_vector_string(const String &p_value) {
	const PackedStringArray components = p_value.strip_edges().split(" ", false);
	if (components.size() < 3) {
		return Vector3();
	}
	return Vector3(components[0].to_float(), components[1].to_float(), components[2].to_float());
}

Basis source_angles_to_godot_basis(const Vector3 &p_source_angles) {
	const real_t sy = Math::sin(Math::deg_to_rad(p_source_angles.y));
	const real_t cy = Math::cos(Math::deg_to_rad(p_source_angles.y));
	const real_t sp = Math::sin(Math::deg_to_rad(p_source_angles.x));
	const real_t cp = Math::cos(Math::deg_to_rad(p_source_angles.x));
	const real_t sr = Math::sin(Math::deg_to_rad(p_source_angles.z));
	const real_t cr = Math::cos(Math::deg_to_rad(p_source_angles.z));

	const Vector3 source_x_axis(cp * cy, cp * sy, -sp);
	const Vector3 source_y_axis(sp * sr * cy - cr * sy, sp * sr * sy + cr * cy, sr * cp);
	const Vector3 source_z_axis(sp * cr * cy + sr * sy, sp * cr * sy - sr * cy, cr * cp);
	return Basis(source_vector_to_godot_direction(source_x_axis), source_vector_to_godot_direction(source_y_axis), source_vector_to_godot_direction(source_z_axis)).orthonormalized();
}

Quaternion sanitize_quaternion(const Quaternion &p_quaternion) {
	if (!p_quaternion.is_finite()) {
		return Quaternion();
	}
	const real_t length_squared = p_quaternion.length_squared();
	if (Math::is_zero_approx(length_squared)) {
		return Quaternion();
	}
	return p_quaternion.is_normalized() ? p_quaternion : p_quaternion.normalized();
}

Quaternion source_quaternion_to_quaternion(const sourcepp::math::Quat &p_quaternion) {
	return sanitize_quaternion(Quaternion(p_quaternion[0], p_quaternion[1], p_quaternion[2], p_quaternion[3]));
}

Transform3D source_matrix_to_transform_3d(const sourcepp::math::Mat3x4f &p_matrix) {
	return Transform3D(
			p_matrix[0][0], p_matrix[0][1], p_matrix[0][2],
			p_matrix[1][0], p_matrix[1][1], p_matrix[1][2],
			p_matrix[2][0], p_matrix[2][1], p_matrix[2][2],
			p_matrix[0][3], p_matrix[1][3], p_matrix[2][3]);
}

} // namespace SourcePPUtils
