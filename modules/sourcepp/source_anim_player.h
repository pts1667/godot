/**************************************************************************/
/*  source_anim_player.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "scene/main/node.h"

#include <mdlpp/structs/MDL.h>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mdlpp {
struct SampledAnimation;
struct StudioModel;
}

class Skeleton3D;

class SourceAnimPlayer : public Node {
	GDCLASS(SourceAnimPlayer, Node);

	struct PoseBuffer {
		Vector<Vector3> positions;
		Vector<Quaternion> rotations;
	};

	struct IKRuntimeChain {
		int chain_index = -1;
		int root_bone = -1;
		int middle_bone = -1;
		int end_bone = -1;
		float pole_distance = 0.0f;
		bool uses_two_bone = false;
		ObjectID modifier;
		ObjectID target;
		ObjectID pole;
	};

	struct PendingIKLock {
		int chain_index = -1;
		int depth = 0;
		float position_weight = 1.0f;
		float local_rotation_weight = 0.0f;
		Vector3 target_position;
		Quaternion target_rotation;
		Vector3 knee_position;
		Vector3 knee_direction;
	};

	std::unique_ptr<mdlpp::StudioModel> model;
	mutable std::unordered_map<int, std::unique_ptr<mdlpp::SampledAnimation>> sampled_animation_cache;
	Vector<IKRuntimeChain> ik_runtime_chains;

	String mdl_path;
	String vtx_path;
	String vvd_path;
	NodePath skeleton_path;
	ObjectID skeleton_cache;
	Vector<int> model_to_skeleton_bones;

	int sequence_descriptor = -1;
	Vector2 blend_values;
	bool ik_enabled = true;
	float speed_scale = 1.0f;
	bool playing = false;
	double playback_time = 0.0;

	static void _bind_methods();
	void _notification(int p_what);

	static std::string _to_utf8(const String &p_string);
	static std::vector<std::byte> _to_byte_vector(const Vector<uint8_t> &p_data);
	static String _derive_companion_path(const String &p_model_path, const PackedStringArray &p_candidates);

	Error _open_bytes(const Vector<uint8_t> &p_mdl_data, const Vector<uint8_t> &p_vtx_data, const Vector<uint8_t> &p_vvd_data, const Vector<uint8_t> &p_anim_block_data = Vector<uint8_t>());
	void _clear_ik_runtime();
	void _rebuild_ik_runtime();
	void _update_skeleton_cache();
	Skeleton3D *_get_skeleton() const;
	void _refresh_bone_map();
	void _reset_mapped_bone_poses();
	void _set_processing_enabled(bool p_enabled);

	void _initialize_pose_buffer(PoseBuffer &r_pose, bool p_delta) const;
	bool _ensure_sampled_animation(int p_animation_descriptor) const;
	const mdlpp::SampledAnimation *_get_sampled_animation(int p_animation_descriptor) const;
	double _get_sequence_cycles_per_second(int p_sequence_descriptor) const;
	float _get_normalized_cycle(int p_sequence_descriptor, double p_time) const;
	double _get_sequence_length(int p_sequence_descriptor) const;
	void _sample_animation_track(const mdlpp::SampledAnimation &p_animation, int p_bone, float p_cycle, bool p_looping, Vector3 &r_position, Quaternion &r_rotation) const;
	void _capture_ik_locks(const PoseBuffer &p_pose, const std::vector<mdlpp::MDL::IKLock> &p_ik_locks, int p_depth, Vector<PendingIKLock> &r_pending_locks) const;
	void _capture_sequence_locks(const PoseBuffer &p_pose, int p_sequence_descriptor, int p_depth, Vector<PendingIKLock> &r_pending_locks) const;
	void _apply_pending_ik_locks(const Vector<PendingIKLock> &p_pending_locks);
	void _blend_pose(PoseBuffer &r_pose, const PoseBuffer &p_sample, int p_sequence_descriptor, float p_weight) const;
	bool _evaluate_sequence_pose(int p_sequence_descriptor, float p_cycle, PoseBuffer &r_pose, Vector<PendingIKLock> *r_pending_locks = nullptr, int p_depth = 0) const;
	bool _accumulate_sequence(PoseBuffer &r_pose, int p_sequence_descriptor, float p_cycle, float p_weight, Vector<PendingIKLock> *r_pending_locks = nullptr, int p_depth = 0) const;
	void _accumulate_autoplay_sequences(PoseBuffer &r_pose, Vector<PendingIKLock> *r_pending_locks = nullptr) const;
	void _emit_sequence_events(double p_previous_time, double p_current_time, bool p_looped);
	void _apply_pose();

	void set_skeleton_path(const NodePath &p_path);
	NodePath get_skeleton_path() const;
	void set_sequence_descriptor(int p_sequence_descriptor);
	int get_sequence_descriptor() const;
	void set_blend_values(const Vector2 &p_blend_values);
	Vector2 get_blend_values() const;
	void set_ik_enabled(bool p_enabled);
	bool is_ik_enabled() const;
	void set_speed_scale(float p_speed_scale);
	float get_speed_scale() const;
	void set_current_time(double p_time);
	double get_current_time() const;

	virtual PackedStringArray get_configuration_warnings() const override;

public:
	SourceAnimPlayer();
	~SourceAnimPlayer() override;

	Error open(const String &p_mdl_path, const String &p_vtx_path = String(), const String &p_vvd_path = String());
	Error open_from_buffer(const PackedByteArray &p_mdl_data, const PackedByteArray &p_vtx_data, const PackedByteArray &p_vvd_data, const PackedByteArray &p_anim_block_data = PackedByteArray());
	void close();
	bool is_open() const;

	String get_mdl_path() const;
	String get_vtx_path() const;
	String get_vvd_path() const;
	int get_sequence_count() const;
	PackedStringArray get_sequence_names() const;
	bool has_sequence(const StringName &p_name) const;
	int find_sequence(const StringName &p_name) const;

	Error play(double p_from_time = 0.0);
	Error play_sequence(int p_sequence_descriptor, double p_from_time = 0.0);
	Error play_sequence_by_name(const StringName &p_name, double p_from_time = 0.0);
	void stop(bool p_reset = false);
	void seek(double p_time, bool p_update = true);
	void advance(double p_delta);
	bool is_playing() const;
};