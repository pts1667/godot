/**************************************************************************/
/*  source_anim_player_utils.h                                            */
/**************************************************************************/

#pragma once

#include "source_mdl_animation_data.h"

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/math/math_funcs.h"
#include "core/math/transform_3d.h"

#include <mdlpp/mdlpp.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace source_anim_player_utils {

constexpr int STUDIO_LOOPING = 0x0001;
constexpr int STUDIO_AUTOPLAY = 0x0008;
constexpr int STUDIO_DELTA = 0x0004;
constexpr int STUDIO_LOCAL = 0x0200;
constexpr int STUDIO_POST = 0x0010;
constexpr int STUDIO_X = 0x00000001;
constexpr int STUDIO_Y = 0x00000002;
constexpr int STUDIO_Z = 0x00000004;
constexpr int STUDIO_XR = 0x00000008;
constexpr int STUDIO_YR = 0x00000010;
constexpr int STUDIO_ZR = 0x00000020;
constexpr int STUDIO_TYPES = 0x0003FFFF;

constexpr int STUDIO_AL_POST = 0x0010;
constexpr int STUDIO_AL_SPLINE = 0x0040;
constexpr int STUDIO_AL_XFADE = 0x0080;
constexpr int STUDIO_AL_NOBLEND = 0x0200;
constexpr int STUDIO_AL_LOCAL = 0x1000;
constexpr int STUDIO_AL_POSE = 0x4000;

constexpr int MAX_SEQUENCE_DEPTH = 16;
constexpr int MAX_BONE_CONTROLLER_INPUTS = 5;

inline Vector3 _to_vector3(const sourcepp::math::Vec3f &p_vector) {
	return Vector3(p_vector[0], p_vector[1], p_vector[2]);
}

inline Quaternion _sanitize_quaternion(const Quaternion &p_quaternion) {
	if (!p_quaternion.is_finite()) {
		return Quaternion();
	}
	const real_t length_squared = p_quaternion.length_squared();
	if (Math::is_zero_approx(length_squared)) {
		return Quaternion();
	}
	return p_quaternion.is_normalized() ? p_quaternion : p_quaternion.normalized();
}

inline Quaternion _to_quaternion(const sourcepp::math::Quat &p_quaternion) {
	return _sanitize_quaternion(Quaternion(p_quaternion[0], p_quaternion[1], p_quaternion[2], p_quaternion[3]));
}

inline String _from_utf8(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

inline String _get_bone_name(const mdlpp::StudioModel *p_model, int p_bone) {
	if (p_model != nullptr && p_bone >= 0 && p_bone < static_cast<int>(p_model->mdl.bones.size())) {
		return _from_utf8(p_model->mdl.bones[static_cast<size_t>(p_bone)].name);
	}
	return String();
}

inline float _get_rest_controller_value(const mdlpp::MDL::BoneController &p_controller) {
	return CLAMP(static_cast<float>(p_controller.rest) / 255.0f, 0.0f, 1.0f);
}

inline float _get_rest_controller_value(const SourceMDLAnimationData::BoneController &p_controller) {
	return CLAMP(static_cast<float>(p_controller.rest) / 255.0f, 0.0f, 1.0f);
}

inline Dictionary _make_bone_controller_info(const mdlpp::StudioModel *p_model, const mdlpp::MDL::BoneController &p_controller) {
	Dictionary out;
	out["bone"] = p_controller.bone;
	out["bone_name"] = _get_bone_name(p_model, p_controller.bone);
	out["type"] = p_controller.type;
	out["start"] = p_controller.start;
	out["end"] = p_controller.end;
	out["rest"] = p_controller.rest;
	out["rest_normalized"] = _get_rest_controller_value(p_controller);
	out["input_field"] = p_controller.inputField;
	return out;
}

inline Dictionary _make_bone_controller_info(const SourceMDLAnimationData *p_data, const SourceMDLAnimationData::BoneController &p_controller) {
	Dictionary out;
	out["bone"] = p_controller.bone;
	out["bone_name"] = (p_data != nullptr && p_controller.bone >= 0 && p_controller.bone < p_data->get_bone_names().size()) ? p_data->get_bone_names()[p_controller.bone] : String();
	out["type"] = p_controller.type;
	out["start"] = p_controller.start;
	out["end"] = p_controller.end;
	out["rest"] = p_controller.rest;
	out["rest_normalized"] = _get_rest_controller_value(p_controller);
	out["input_field"] = p_controller.inputField;
	return out;
}

inline Vector<uint8_t> _packed_to_vector(const PackedByteArray &p_bytes) {
	Vector<uint8_t> out;
	out.resize(p_bytes.size());
	if (!p_bytes.is_empty()) {
		std::memcpy(out.ptrw(), p_bytes.ptr(), static_cast<size_t>(p_bytes.size()));
	}
	return out;
}

inline String _resolve_anim_block_path(const String &p_model_path, const mdlpp::MDL::MDL &p_mdl) {
	if (p_mdl.animBlocks.size() <= 1 || p_mdl.animBlockName.empty()) {
		return String();
	}

	const String anim_block_name = String::utf8(p_mdl.animBlockName.c_str()).replace("\\", "/");
	if (anim_block_name.is_empty()) {
		return String();
	}
	if (FileAccess::exists(anim_block_name)) {
		return anim_block_name;
	}

	const String model_dir = p_model_path.get_base_dir();
	const String local_candidate = model_dir.path_join(anim_block_name.get_file());
	if (FileAccess::exists(local_candidate)) {
		return local_candidate;
	}

	const String relative_candidate = model_dir.path_join(anim_block_name);
	if (FileAccess::exists(relative_candidate)) {
		return relative_candidate;
	}

	const String fallback_candidate = p_model_path.get_basename() + ".ani";
	if (FileAccess::exists(fallback_candidate)) {
		return fallback_candidate;
	}

	return String();
}

inline float _simple_spline(float p_value) {
	const float clamped = CLAMP(p_value, 0.0f, 1.0f);
	return clamped * clamped * (3.0f - (2.0f * clamped));
}

inline float _get_axis_value(const mdlpp::MDL::SequenceDesc &p_sequence_desc, const Vector2 &p_blend_values, int p_pose_parameter) {
	if (p_sequence_desc.paramIndex[0] == p_pose_parameter) {
		return p_blend_values.x;
	}
	if (p_sequence_desc.paramIndex[1] == p_pose_parameter) {
		return p_blend_values.y;
	}
	return 0.0f;
}

inline float _get_axis_value(const SourceMDLAnimationData::SequenceDesc &p_sequence_desc, const Vector2 &p_blend_values, int p_pose_parameter) {
	if (p_sequence_desc.paramIndex[0] == p_pose_parameter) {
		return p_blend_values.x;
	}
	if (p_sequence_desc.paramIndex[1] == p_pose_parameter) {
		return p_blend_values.y;
	}
	return 0.0f;
}

inline void _resolve_axis_weights(const mdlpp::MDL::SequenceDesc &p_sequence_desc, int p_axis, float p_value, int &r_index_a, int &r_index_b, float &r_weight) {
	const int axis_size = MAX(p_sequence_desc.groupSize[p_axis], 1);
	r_index_a = 0;
	r_index_b = 0;
	r_weight = 0.0f;
	if (axis_size <= 1) {
		return;
	}

	const std::vector<float> &pose_keys = p_sequence_desc.poseKeys[p_axis];
	if (static_cast<int>(pose_keys.size()) >= axis_size) {
		const bool ascending = pose_keys.front() <= pose_keys.back();
		if ((ascending && p_value <= pose_keys.front()) || (!ascending && p_value >= pose_keys.front())) {
			return;
		}
		if ((ascending && p_value >= pose_keys[axis_size - 1]) || (!ascending && p_value <= pose_keys[axis_size - 1])) {
			r_index_a = axis_size - 1;
			r_index_b = axis_size - 1;
			return;
		}

		for (int i = 0; i < axis_size - 1; i++) {
			const float start = pose_keys[static_cast<size_t>(i)];
			const float end = pose_keys[static_cast<size_t>(i + 1)];
			const bool in_range = ascending ? (p_value >= start && p_value <= end) : (p_value <= start && p_value >= end);
			if (!in_range) {
				continue;
			}

			r_index_a = i;
			r_index_b = i + 1;
			const float denominator = end - start;
			r_weight = Math::is_zero_approx(denominator) ? 0.0f : CLAMP((p_value - start) / denominator, 0.0f, 1.0f);
			return;
		}
	}

	const float start = p_sequence_desc.paramStart[p_axis];
	const float end = p_sequence_desc.paramEnd[p_axis];
	const float denominator = end - start;
	if (Math::is_zero_approx(denominator)) {
		return;
	}

	const float coordinate = CLAMP(((p_value - start) / denominator) * static_cast<float>(axis_size - 1), 0.0f, static_cast<float>(axis_size - 1));
	r_index_a = static_cast<int>(Math::floor(coordinate));
	r_index_b = MIN(r_index_a + 1, axis_size - 1);
	r_weight = coordinate - static_cast<float>(r_index_a);
	if (r_index_a == r_index_b) {
		r_weight = 0.0f;
	}
}

inline void _resolve_axis_weights(const SourceMDLAnimationData::SequenceDesc &p_sequence_desc, int p_axis, float p_value, int &r_index_a, int &r_index_b, float &r_weight) {
	const int axis_size = MAX(p_sequence_desc.groupSize[p_axis], 1);
	r_index_a = 0;
	r_index_b = 0;
	r_weight = 0.0f;
	if (axis_size <= 1) {
		return;
	}

	const std::vector<float> &pose_keys = p_sequence_desc.poseKeys[p_axis];
	if (static_cast<int>(pose_keys.size()) >= axis_size) {
		const bool ascending = pose_keys.front() <= pose_keys.back();
		if ((ascending && p_value <= pose_keys.front()) || (!ascending && p_value >= pose_keys.front())) {
			return;
		}
		if ((ascending && p_value >= pose_keys[axis_size - 1]) || (!ascending && p_value <= pose_keys[axis_size - 1])) {
			r_index_a = axis_size - 1;
			r_index_b = axis_size - 1;
			return;
		}

		for (int i = 0; i < axis_size - 1; i++) {
			const float start = pose_keys[static_cast<size_t>(i)];
			const float end = pose_keys[static_cast<size_t>(i + 1)];
			const bool in_range = ascending ? (p_value >= start && p_value <= end) : (p_value <= start && p_value >= end);
			if (!in_range) {
				continue;
			}

			r_index_a = i;
			r_index_b = i + 1;
			const float denominator = end - start;
			r_weight = Math::is_zero_approx(denominator) ? 0.0f : CLAMP((p_value - start) / denominator, 0.0f, 1.0f);
			return;
		}
	}

	const float start = p_sequence_desc.paramStart[p_axis];
	const float end = p_sequence_desc.paramEnd[p_axis];
	const float denominator = end - start;
	if (Math::is_zero_approx(denominator)) {
		return;
	}

	const float coordinate = CLAMP(((p_value - start) / denominator) * static_cast<float>(axis_size - 1), 0.0f, static_cast<float>(axis_size - 1));
	r_index_a = static_cast<int>(Math::floor(coordinate));
	r_index_b = MIN(r_index_a + 1, axis_size - 1);
	r_weight = coordinate - static_cast<float>(r_index_a);
	if (r_index_a == r_index_b) {
		r_weight = 0.0f;
	}
}

inline float _get_sequence_bone_weight(const mdlpp::MDL::SequenceDesc &p_sequence_desc, int p_bone) {
	if (p_bone >= 0 && p_bone < static_cast<int>(p_sequence_desc.boneWeights.size())) {
		return CLAMP(p_sequence_desc.boneWeights[static_cast<size_t>(p_bone)], 0.0f, 1.0f);
	}
	return 1.0f;
}

inline float _get_sequence_bone_weight(const SourceMDLAnimationData::SequenceDesc &p_sequence_desc, int p_bone) {
	if (p_bone >= 0 && p_bone < static_cast<int>(p_sequence_desc.boneWeights.size())) {
		return CLAMP(p_sequence_desc.boneWeights[static_cast<size_t>(p_bone)], 0.0f, 1.0f);
	}
	return 1.0f;
}

inline Quaternion _apply_delta_rotation(const Quaternion &p_base, const Quaternion &p_delta, float p_weight, bool p_post) {
	Quaternion scaled_delta = Quaternion().slerp(p_delta, CLAMP(p_weight, 0.0f, 1.0f));
	return p_post ? (p_base * scaled_delta).normalized() : (scaled_delta * p_base).normalized();
}

inline bool _sequence_loops(const mdlpp::MDL::SequenceDesc &p_sequence_desc) {
	return (static_cast<int>(p_sequence_desc.flags) & STUDIO_LOOPING) != 0;
}

inline bool _sequence_loops(const SourceMDLAnimationData::SequenceDesc &p_sequence_desc) {
	return (p_sequence_desc.flags & STUDIO_LOOPING) != 0;
}

inline bool _sequence_is_delta(const mdlpp::MDL::SequenceDesc &p_sequence_desc) {
	return (static_cast<int>(p_sequence_desc.flags) & STUDIO_DELTA) != 0;
}

inline bool _sequence_is_delta(const SourceMDLAnimationData::SequenceDesc &p_sequence_desc) {
	return (p_sequence_desc.flags & STUDIO_DELTA) != 0;
}

inline bool _sequence_is_local(const mdlpp::MDL::SequenceDesc &p_sequence_desc) {
	return (static_cast<int>(p_sequence_desc.flags) & STUDIO_LOCAL) != 0;
}

inline bool _sequence_is_local(const SourceMDLAnimationData::SequenceDesc &p_sequence_desc) {
	return (p_sequence_desc.flags & STUDIO_LOCAL) != 0;
}

inline Transform3D _make_transform(const Vector3 &p_position, const Quaternion &p_rotation) {
	return Transform3D(Basis(p_rotation), p_position);
}

inline float _encode_bone_controller_value(const mdlpp::MDL::BoneController &p_controller, float p_value, float *r_applied_value = nullptr) {
	float value = p_value;
	if ((p_controller.type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR)) != 0) {
		if (p_controller.end < p_controller.start) {
			value = -value;
		}

		if (p_controller.start + 359.0f >= p_controller.end) {
			const float midpoint = (p_controller.start + p_controller.end) * 0.5f;
			if (value > midpoint + 180.0f) {
				value -= 360.0f;
			}
			if (value < midpoint - 180.0f) {
				value += 360.0f;
			}
		} else {
			if (value > 360.0f) {
				value -= Math::floor(value / 360.0f) * 360.0f;
			} else if (value < 0.0f) {
				value += Math::floor((-value / 360.0f) + 1.0f) * 360.0f;
			}
		}
	}

	const float denominator = p_controller.end - p_controller.start;
	float encoded = Math::is_zero_approx(denominator) ? 0.0f : (value - p_controller.start) / denominator;
	encoded = CLAMP(encoded, 0.0f, 1.0f);

	if (r_applied_value != nullptr) {
		float applied = ((1.0f - encoded) * p_controller.start) + (encoded * p_controller.end);
		if ((p_controller.type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR)) != 0 && p_controller.end < p_controller.start) {
			applied *= -1.0f;
		}
		*r_applied_value = applied;
	}

	return encoded;
}

inline float _encode_bone_controller_value(const SourceMDLAnimationData::BoneController &p_controller, float p_value, float *r_applied_value = nullptr) {
	float value = p_value;
	if ((p_controller.type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR)) != 0) {
		if (p_controller.end < p_controller.start) {
			value = -value;
		}

		if (p_controller.start + 359.0f >= p_controller.end) {
			const float midpoint = (p_controller.start + p_controller.end) * 0.5f;
			if (value > midpoint + 180.0f) {
				value -= 360.0f;
			}
			if (value < midpoint - 180.0f) {
				value += 360.0f;
			}
		} else {
			if (value > 360.0f) {
				value -= Math::floor(value / 360.0f) * 360.0f;
			} else if (value < 0.0f) {
				value += Math::floor((-value / 360.0f) + 1.0f) * 360.0f;
			}
		}
	}

	const float denominator = p_controller.end - p_controller.start;
	float encoded = Math::is_zero_approx(denominator) ? 0.0f : (value - p_controller.start) / denominator;
	encoded = CLAMP(encoded, 0.0f, 1.0f);

	if (r_applied_value != nullptr) {
		float applied = ((1.0f - encoded) * p_controller.start) + (encoded * p_controller.end);
		if ((p_controller.type & (STUDIO_XR | STUDIO_YR | STUDIO_ZR)) != 0 && p_controller.end < p_controller.start) {
			applied *= -1.0f;
		}
		*r_applied_value = applied;
	}

	return encoded;
}

} // namespace source_anim_player_utils
