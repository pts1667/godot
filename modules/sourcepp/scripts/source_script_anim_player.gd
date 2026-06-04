@tool
class_name SourceScriptAnimPlayer
extends Node

signal sequence_event(sequence_descriptor: int, event_index: int, event_name: String, event_id: int, options: String)
signal sequence_finished(sequence_descriptor: int)

const STUDIO_LOOPING := 0x0001
const STUDIO_AUTOPLAY := 0x0008
const STUDIO_DELTA := 0x0004
const STUDIO_LOCAL := 0x0200
const STUDIO_POST := 0x0010
const STUDIO_AL_SPLINE := 0x0040
const STUDIO_AL_XFADE := 0x0080
const STUDIO_AL_NOBLEND := 0x0200
const STUDIO_AL_LOCAL := 0x1000
const STUDIO_AL_POSE := 0x4000
const MAX_SEQUENCE_DEPTH := 16

@export var animation_data: SourceMDLAnimationData:
	set(value):
		animation_data = value
		_rebuild_bone_map()

@export_node_path("Skeleton3D") var skeleton_path: NodePath:
	set(value):
		skeleton_path = value
		_rebuild_bone_map()

@export var sequence_descriptor := -1
@export var blend_values := Vector2.ZERO
@export var ik_enabled := true
@export var ik_data_path: NodePath
@export var speed_scale := 1.0

var current_time := 0.0
var playing := false

var _skeleton: Skeleton3D
var _ik_data: Node
var _ik_data_root: Node
var _model_to_skeleton_bones: Array[int] = []
var _animation_by_index := {}


func _ready() -> void:
	_resolve_skeleton()
	_rebuild_bone_map()


func _process(delta: float) -> void:
	if playing:
		advance(delta)


func setup_from_imported_root(root: Node) -> bool:
	if root == null:
		return false

	var data: Variant = root.get_meta("sourcepp_animation_data", null)
	if data is SourceMDLAnimationData:
		animation_data = data

	var skeleton := _find_child_by_type(root, "Skeleton3D") as Skeleton3D
	if skeleton != null:
		skeleton_path = get_path_to(skeleton)

	_ensure_ik_data(root)
	return animation_data != null and skeleton != null


func get_sequence_names() -> PackedStringArray:
	if animation_data == null:
		return PackedStringArray()
	return animation_data.get_sequence_names()


func get_sequence_count() -> int:
	return 0 if animation_data == null else animation_data.get_sequence_count()


func find_sequence(sequence_name: StringName) -> int:
	return -1 if animation_data == null else animation_data.find_sequence(sequence_name)


func play(from_time := 0.0) -> int:
	if animation_data == null or not animation_data.has_required_data():
		return ERR_UNCONFIGURED
	if sequence_descriptor < 0 and animation_data.get_sequence_count() > 0:
		sequence_descriptor = 0
	if sequence_descriptor < 0 or sequence_descriptor >= animation_data.get_sequence_count():
		return ERR_INVALID_PARAMETER

	playing = true
	seek(from_time, true)
	return OK


func play_sequence(sequence_index: int, from_time := 0.0) -> int:
	sequence_descriptor = sequence_index
	return play(from_time)


func play_sequence_by_name(sequence_name: StringName, from_time := 0.0) -> int:
	var index := find_sequence(sequence_name)
	if index < 0:
		return ERR_DOES_NOT_EXIST
	return play_sequence(index, from_time)


func stop(reset := false) -> void:
	playing = false
	current_time = 0.0
	if reset:
		_reset_pose()
	elif sequence_descriptor >= 0:
		_apply_pose()
	_clear_ik_data()


func seek(time: float, update := true) -> void:
	current_time = maxf(time, 0.0)
	if update:
		_apply_pose()


func advance(delta: float) -> void:
	if animation_data == null or sequence_descriptor < 0 or delta <= 0.0 or speed_scale <= 0.0:
		return

	var length := _get_sequence_length(sequence_descriptor)
	if length <= 0.0:
		_apply_pose()
		return

	var sequence: Dictionary = animation_data.get_sequence(sequence_descriptor)
	var previous_time := current_time
	var scaled_delta := delta * speed_scale
	var looped := false
	if _sequence_loops(sequence):
		current_time = fposmod(current_time + scaled_delta, length)
		looped = previous_time + scaled_delta >= length
	else:
		current_time = minf(current_time + scaled_delta, length)

	_emit_events(previous_time, current_time, looped)
	_apply_pose()

	if not _sequence_loops(sequence) and is_equal_approx(current_time, length):
		playing = false
		_clear_ik_data()
		sequence_finished.emit(sequence_descriptor)


func _apply_pose() -> void:
	if animation_data == null or sequence_descriptor < 0 or sequence_descriptor >= animation_data.get_sequence_count():
		return
	_resolve_skeleton()
	if _skeleton == null:
		return
	if _model_to_skeleton_bones.size() != animation_data.get_bone_count():
		_rebuild_bone_map()

	var pose := _make_pose(false)
	var pending_ik_locks: Array[Dictionary] = []
	var cycle := _get_normalized_cycle(sequence_descriptor, current_time)
	if not _accumulate_sequence(pose, sequence_descriptor, cycle, 1.0, pending_ik_locks, 0):
		return

	_accumulate_autoplay_sequences(pose, pending_ik_locks)
	_apply_bone_controllers(pose)
	_pre_apply_pose(pose)
	_update_ik_data(cycle, pending_ik_locks, pose)

	for model_bone in range(_model_to_skeleton_bones.size()):
		var skeleton_bone := _model_to_skeleton_bones[model_bone]
		if skeleton_bone < 0:
			continue
		_skeleton.set_bone_pose_position(skeleton_bone, pose["positions"][model_bone])
		_skeleton.set_bone_pose_rotation(skeleton_bone, pose["rotations"][model_bone])

	_skeleton.force_update_all_bone_transforms()
	_apply_ik_locks(pending_ik_locks)
	_post_apply_pose(pose)


func _pre_apply_pose(_pose: Dictionary) -> void:
	pass


func _post_apply_pose(_pose: Dictionary) -> void:
	pass


func _apply_ik_locks(_pending_ik_locks: Array[Dictionary]) -> void:
	if not ik_enabled:
		return
	_ensure_ik_data()
	if _ik_data != null and _ik_data.has_method("apply_ik"):
		_ik_data.call("apply_ik")


func _apply_bone_controllers(_pose: Dictionary) -> void:
	# Kept as a customization hook until Source controller axis semantics are mirrored here.
	pass


func _accumulate_sequence(pose: Dictionary, sequence_index: int, cycle: float, weight: float, pending_ik_locks: Array[Dictionary], depth: int) -> bool:
	if animation_data == null or sequence_index < 0 or sequence_index >= animation_data.get_sequence_count() or weight <= 0.0 or depth > MAX_SEQUENCE_DEPTH:
		return false

	var sequence: Dictionary = animation_data.get_sequence(sequence_index)
	for lock in sequence.get("ik_locks", []):
		pending_ik_locks.append({"sequence": sequence_index, "lock": lock, "depth": depth})

	var sequence_pose := _evaluate_sequence_pose(sequence_index, cycle, pending_ik_locks, depth)
	if sequence_pose.is_empty():
		return false

	_blend_pose(pose, sequence_pose, sequence, weight)

	var contributed := true
	for layer in sequence.get("auto_layers", []):
		if (int(layer.get("flags", 0)) & STUDIO_AL_LOCAL) != 0:
			continue

		var layer_cycle := cycle
		var layer_weight := weight
		if float(layer.get("start", 0.0)) != float(layer.get("end", 0.0)):
			var layer_result := _resolve_layer(sequence, layer, cycle, weight, false)
			if layer_result.is_empty():
				continue
			layer_cycle = float(layer_result["cycle"])
			layer_weight = float(layer_result["weight"])

		contributed = _accumulate_sequence(pose, int(layer.get("sequence", -1)), clampf(layer_cycle, 0.0, 1.0), layer_weight, pending_ik_locks, depth + 1) or contributed

	return contributed


func _evaluate_sequence_pose(sequence_index: int, cycle: float, pending_ik_locks: Array[Dictionary], depth: int) -> Dictionary:
	if animation_data == null or sequence_index < 0 or sequence_index >= animation_data.get_sequence_count() or depth > MAX_SEQUENCE_DEPTH:
		return {}

	var sequence: Dictionary = animation_data.get_sequence(sequence_index)
	var is_delta := _sequence_is_delta(sequence)
	var pose := _make_pose(is_delta)
	var group_size: Vector2i = sequence.get("group_size", Vector2i(1, 1))
	var x := _resolve_axis_weights(sequence, 0, blend_values.x)
	var y := _resolve_axis_weights(sequence, 1, blend_values.y)
	var group_x := maxi(group_size.x, 1)
	var blend_cells := [
		{"index": int(y["a"]) * group_x + int(x["a"]), "weight": (1.0 - float(x["weight"])) * (1.0 - float(y["weight"]))},
		{"index": int(y["a"]) * group_x + int(x["b"]), "weight": float(x["weight"]) * (1.0 - float(y["weight"]))},
		{"index": int(y["b"]) * group_x + int(x["a"]), "weight": (1.0 - float(x["weight"])) * float(y["weight"])},
		{"index": int(y["b"]) * group_x + int(x["b"]), "weight": float(x["weight"]) * float(y["weight"])},
	]

	var animation_indices: PackedInt32Array = sequence.get("animation_indices", PackedInt32Array())
	var contributed := false
	for cell in blend_cells:
		var cell_weight := float(cell["weight"])
		var cell_index := int(cell["index"])
		if cell_weight <= 0.0 or cell_index < 0 or cell_index >= animation_indices.size():
			continue

		var animation := _get_animation_by_index(animation_indices[cell_index])
		if animation.is_empty():
			continue

		var sample_pose := _sample_animation_pose(animation, cycle, _sequence_loops(sequence), is_delta)
		if sample_pose.is_empty():
			continue

		_blend_sample_into_pose(pose, sample_pose, sequence, cell_weight)
		contributed = true

	if _sequence_is_local(sequence):
		for layer in sequence.get("auto_layers", []):
			if (int(layer.get("flags", 0)) & STUDIO_AL_LOCAL) == 0:
				continue
			var layer_cycle := cycle
			var layer_weight := 1.0
			if float(layer.get("start", 0.0)) != float(layer.get("end", 0.0)):
				var layer_result := _resolve_layer(sequence, layer, cycle, 1.0, true)
				if layer_result.is_empty():
					continue
				layer_cycle = float(layer_result["cycle"])
				layer_weight = float(layer_result["weight"])
			contributed = _accumulate_sequence(pose, int(layer.get("sequence", -1)), clampf(layer_cycle, 0.0, 1.0), layer_weight, pending_ik_locks, depth + 1) or contributed

	return pose if contributed else {}


func _sample_animation_pose(animation: Dictionary, cycle: float, looping: bool, delta: bool) -> Dictionary:
	var pose := _make_pose(delta)
	var tracks: Array = animation.get("tracks", [])
	for bone_index in range(min(tracks.size(), pose["positions"].size())):
		var sample := _sample_track(tracks[bone_index], cycle, int(animation.get("frame_count", 0)), looping)
		pose["positions"][bone_index] = sample["position"]
		pose["rotations"][bone_index] = sample["rotation"]
	return pose


func _sample_track(track: Dictionary, cycle: float, frame_count: int, looping: bool) -> Dictionary:
	var positions: PackedVector3Array = track.get("positions", PackedVector3Array())
	var rotations: Array = track.get("rotations", [])
	var available: int = min(positions.size(), rotations.size())
	if frame_count <= 0 or available <= 0:
		return {"position": Vector3.ZERO, "rotation": Quaternion()}

	var frame := clampf(cycle, 0.0, 1.0) * float(maxi(frame_count - 1, 0))
	var frame_a := clampi(floori(frame), 0, available - 1)
	var frame_b := frame_a + 1
	if frame_b >= available:
		frame_b = 0 if looping and available > 1 else frame_a
	var weight := frame - float(frame_a)

	return {
		"position": positions[frame_a].lerp(positions[frame_b], weight),
		"rotation": _sanitize_quaternion(rotations[frame_a]).slerp(_sanitize_quaternion(rotations[frame_b]), weight).normalized(),
	}


func _blend_sample_into_pose(pose: Dictionary, sample_pose: Dictionary, sequence: Dictionary, weight: float) -> void:
	if _sequence_is_delta(sequence):
		var post_delta := (int(sequence.get("flags", 0)) & STUDIO_POST) != 0
		for bone_index in range(pose["positions"].size()):
			pose["positions"][bone_index] += sample_pose["positions"][bone_index] * weight
			pose["rotations"][bone_index] = _apply_delta_rotation(pose["rotations"][bone_index], sample_pose["rotations"][bone_index], weight, post_delta)
	else:
		for bone_index in range(pose["positions"].size()):
			pose["positions"][bone_index] = pose["positions"][bone_index].lerp(sample_pose["positions"][bone_index], weight)
			pose["rotations"][bone_index] = pose["rotations"][bone_index].slerp(sample_pose["rotations"][bone_index], weight).normalized()


func _blend_pose(pose: Dictionary, sample_pose: Dictionary, sequence: Dictionary, weight: float) -> void:
	var bone_weights: PackedFloat32Array = sequence.get("bone_weights", PackedFloat32Array())
	var is_delta := _sequence_is_delta(sequence)
	var post_delta := (int(sequence.get("flags", 0)) & STUDIO_POST) != 0
	for bone_index in range(pose["positions"].size()):
		var bone_weight := clampf(weight * _get_bone_weight(bone_weights, bone_index), 0.0, 1.0)
		if bone_weight <= 0.0:
			continue
		if is_delta:
			pose["positions"][bone_index] += sample_pose["positions"][bone_index] * bone_weight
			pose["rotations"][bone_index] = _apply_delta_rotation(pose["rotations"][bone_index], sample_pose["rotations"][bone_index], bone_weight, post_delta)
		else:
			pose["positions"][bone_index] = pose["positions"][bone_index].lerp(sample_pose["positions"][bone_index], bone_weight)
			pose["rotations"][bone_index] = pose["rotations"][bone_index].slerp(sample_pose["rotations"][bone_index], bone_weight).normalized()


func _accumulate_autoplay_sequences(pose: Dictionary, pending_ik_locks: Array[Dictionary]) -> void:
	if animation_data == null:
		return
	for lock in animation_data.get_ik_autoplay_locks():
		pending_ik_locks.append({"sequence": -1, "lock": lock, "depth": MAX_SEQUENCE_DEPTH + 1, "autoplay": true})
	for sequence_index in range(animation_data.get_sequence_count()):
		var sequence: Dictionary = animation_data.get_sequence(sequence_index)
		if (int(sequence.get("flags", 0)) & STUDIO_AUTOPLAY) == 0:
			continue
		var cycles_per_second := _get_sequence_cycles_per_second(sequence_index)
		if cycles_per_second <= 0.0:
			continue
		var cycle := fposmod(current_time * cycles_per_second, 1.0)
		_accumulate_sequence(pose, sequence_index, cycle, 1.0, pending_ik_locks, 0)


func _make_pose(delta: bool) -> Dictionary:
	var positions: Array[Vector3] = []
	var rotations: Array[Quaternion] = []
	var bone_count: int = 0 if animation_data == null else animation_data.get_bone_count()
	positions.resize(bone_count)
	rotations.resize(bone_count)
	for bone_index in range(bone_count):
		if delta:
			positions[bone_index] = Vector3.ZERO
			rotations[bone_index] = Quaternion()
			continue
		var skeleton_bone := _model_to_skeleton_bones[bone_index] if bone_index < _model_to_skeleton_bones.size() else -1
		if _skeleton != null and skeleton_bone >= 0:
			var rest := _skeleton.get_bone_rest(skeleton_bone)
			positions[bone_index] = rest.origin
			rotations[bone_index] = rest.basis.get_rotation_quaternion()
		else:
			positions[bone_index] = Vector3.ZERO
			rotations[bone_index] = Quaternion()
	return {"positions": positions, "rotations": rotations}


func _rebuild_bone_map() -> void:
	_resolve_skeleton()
	_model_to_skeleton_bones.clear()
	_animation_by_index.clear()
	if animation_data == null:
		return

	var names := animation_data.get_bone_names()
	_model_to_skeleton_bones.resize(names.size())
	for bone_index in range(names.size()):
		_model_to_skeleton_bones[bone_index] = _skeleton.find_bone(names[bone_index]) if _skeleton != null else -1

	for data_index in range(animation_data.get_animation_count()):
		var animation: Dictionary = animation_data.get_animation(data_index)
		_animation_by_index[int(animation.get("animation_index", -1))] = animation


func _resolve_skeleton() -> void:
	_skeleton = get_node_or_null(skeleton_path) as Skeleton3D
	if _skeleton == null and get_parent() is Skeleton3D:
		_skeleton = get_parent() as Skeleton3D


func _ensure_ik_data(root_hint: Node = null) -> Node:
	if _ik_data != null and is_instance_valid(_ik_data):
		return _ik_data
	if not ik_data_path.is_empty():
		_ik_data = get_node_or_null(ik_data_path)
		if _ik_data != null:
			return _ik_data

	var root := root_hint
	if root == null:
		root = _ik_data_root if _ik_data_root != null and is_instance_valid(_ik_data_root) else get_parent()
	if root == null:
		return null

	_ik_data_root = root
	_ik_data = root.get_node_or_null("SourceIKData")
	if _ik_data == null:
		var node := Node3D.new()
		node.name = "SourceIKData"
		var script := _load_sibling_script("source_ik_data.gd")
		if script != null:
			node.set_script(script)
		root.add_child(node)
		_ik_data = node
	if _ik_data != null:
		ik_data_path = get_path_to(_ik_data)
		if _ik_data.has_method("setup_from_imported_root"):
			_ik_data.call("setup_from_imported_root", root)
		_ik_data.set("ik_enabled", ik_enabled)
	return _ik_data


func _update_ik_data(cycle: float, pending_ik_locks: Array[Dictionary], pose: Dictionary) -> void:
	if not ik_enabled:
		return
	_ensure_ik_data()
	if _ik_data == null:
		return
	if _ik_data.has_method("begin_sequence"):
		var sequence: Dictionary = animation_data.get_sequence(sequence_descriptor)
		_ik_data.call("begin_sequence", sequence_descriptor, sequence, pending_ik_locks)
	if _ik_data.has_method("update_sequence"):
		_ik_data.call("update_sequence", cycle, pending_ik_locks, pose, _model_to_skeleton_bones)


func _clear_ik_data() -> void:
	_ensure_ik_data()
	if _ik_data != null and _ik_data.has_method("clear_sequence"):
		_ik_data.call("clear_sequence")


func _load_sibling_script(file_name: String) -> Script:
	var script := get_script() as Script
	if script == null:
		return null
	var path := script.resource_path.get_base_dir().path_join(file_name)
	return load(path) as Script


func _reset_pose() -> void:
	_resolve_skeleton()
	if _skeleton != null:
		_skeleton.reset_bone_poses()


func _get_animation_by_index(animation_index: int) -> Dictionary:
	if _animation_by_index.has(animation_index):
		return _animation_by_index[animation_index]
	if animation_data == null:
		return {}
	return animation_data.get_animation_by_index(animation_index)


func _get_sequence_cycles_per_second(sequence_index: int) -> float:
	if animation_data == null or sequence_index < 0 or sequence_index >= animation_data.get_sequence_count():
		return 0.0
	var sequence: Dictionary = animation_data.get_sequence(sequence_index)
	var group_size: Vector2i = sequence.get("group_size", Vector2i(1, 1))
	var group_x := maxi(group_size.x, 1)
	var group_y := maxi(group_size.y, 1)
	var animation_indices: PackedInt32Array = sequence.get("animation_indices", PackedInt32Array())
	var weighted_fps := 0.0
	var total_weight := 0.0
	for y in range(group_y):
		for x in range(group_x):
			var cell := y * group_x + x
			if cell >= animation_indices.size():
				continue
			var animation: Dictionary = _get_animation_by_index(animation_indices[cell])
			if animation.is_empty():
				continue
			var fps := float(animation.get("fps", 0.0))
			var frames := int(animation.get("frame_count", 0))
			if fps <= 0.0 or frames <= 1:
				continue
			weighted_fps += fps / float(frames - 1)
			total_weight += 1.0
	return 0.0 if total_weight <= 0.0 else weighted_fps / total_weight


func _get_sequence_length(sequence_index: int) -> float:
	var cps := _get_sequence_cycles_per_second(sequence_index)
	return 0.0 if cps <= 0.0 else 1.0 / cps


func _get_normalized_cycle(sequence_index: int, time: float) -> float:
	var length := _get_sequence_length(sequence_index)
	if length <= 0.0:
		return 0.0
	var sequence: Dictionary = animation_data.get_sequence(sequence_index)
	if _sequence_loops(sequence):
		return fposmod(time, length) / length
	return clampf(time / length, 0.0, 1.0)


func _resolve_axis_weights(sequence: Dictionary, axis: int, value: float) -> Dictionary:
	var group_size: Vector2i = sequence.get("group_size", Vector2i(1, 1))
	var axis_size := maxi(group_size[axis], 1)
	if axis_size <= 1:
		return {"a": 0, "b": 0, "weight": 0.0}

	var pose_keys: Array = sequence.get("pose_keys", [])
	if axis < pose_keys.size() and pose_keys[axis].size() >= axis_size:
		var keys: PackedFloat32Array = pose_keys[axis]
		var ascending := keys[0] <= keys[axis_size - 1]
		if (ascending and value <= keys[0]) or (not ascending and value >= keys[0]):
			return {"a": 0, "b": 0, "weight": 0.0}
		if (ascending and value >= keys[axis_size - 1]) or (not ascending and value <= keys[axis_size - 1]):
			return {"a": axis_size - 1, "b": axis_size - 1, "weight": 0.0}
		for i in range(axis_size - 1):
			var start := keys[i]
			var end := keys[i + 1]
			var in_range := (value >= start and value <= end) if ascending else (value <= start and value >= end)
			if in_range:
				var denominator := end - start
				return {"a": i, "b": i + 1, "weight": 0.0 if is_zero_approx(denominator) else clampf((value - start) / denominator, 0.0, 1.0)}

	var param_start: Vector2 = sequence.get("param_start", Vector2.ZERO)
	var param_end: Vector2 = sequence.get("param_end", Vector2.ZERO)
	var denominator := param_end[axis] - param_start[axis]
	if is_zero_approx(denominator):
		return {"a": 0, "b": 0, "weight": 0.0}
	var coordinate := clampf(((value - param_start[axis]) / denominator) * float(axis_size - 1), 0.0, float(axis_size - 1))
	var a := floori(coordinate)
	var b := mini(a + 1, axis_size - 1)
	return {"a": a, "b": b, "weight": 0.0 if a == b else coordinate - float(a)}


func _resolve_layer(sequence: Dictionary, layer: Dictionary, cycle: float, parent_weight: float, local_layer: bool) -> Dictionary:
	var start := float(layer.get("start", 0.0))
	var peak := float(layer.get("peak", 0.0))
	var tail := float(layer.get("tail", 0.0))
	var end := float(layer.get("end", 0.0))
	var flags := int(layer.get("flags", 0))
	var index := cycle
	if (flags & STUDIO_AL_POSE) != 0:
		index = _get_axis_value(sequence, int(layer.get("pose", 0)))
	if index < start or index >= end:
		return {}

	var s := 1.0
	if index < peak and start != peak:
		s = (index - start) / (peak - start)
	elif index > tail and end != tail:
		s = (end - index) / (end - tail)
	if (flags & STUDIO_AL_SPLINE) != 0:
		s = _simple_spline(s)

	var layer_weight := s
	if not local_layer:
		if (flags & STUDIO_AL_XFADE) != 0 and index > tail:
			var denominator := 1.0 - parent_weight + s * parent_weight
			layer_weight = 0.0 if is_zero_approx(denominator) else (s * parent_weight) / denominator
		elif (flags & STUDIO_AL_NOBLEND) == 0:
			layer_weight = parent_weight * s

	var layer_cycle := cycle
	if (flags & STUDIO_AL_POSE) == 0:
		layer_cycle = (cycle - start) / (end - start)
	return {"cycle": layer_cycle, "weight": layer_weight}


func _get_axis_value(sequence: Dictionary, pose_parameter: int) -> float:
	var param_index: Vector2i = sequence.get("param_index", Vector2i(-1, -1))
	if param_index.x == pose_parameter:
		return blend_values.x
	if param_index.y == pose_parameter:
		return blend_values.y
	return 0.0


func _get_bone_weight(weights: PackedFloat32Array, bone: int) -> float:
	return clampf(weights[bone], 0.0, 1.0) if bone >= 0 and bone < weights.size() else 1.0


func _sequence_loops(sequence: Dictionary) -> bool:
	return (int(sequence.get("flags", 0)) & STUDIO_LOOPING) != 0


func _sequence_is_delta(sequence: Dictionary) -> bool:
	return (int(sequence.get("flags", 0)) & STUDIO_DELTA) != 0


func _sequence_is_local(sequence: Dictionary) -> bool:
	return (int(sequence.get("flags", 0)) & STUDIO_LOCAL) != 0


func _apply_delta_rotation(base: Quaternion, delta: Quaternion, weight: float, post: bool) -> Quaternion:
	var scaled_delta := Quaternion().slerp(delta, clampf(weight, 0.0, 1.0))
	return (base * scaled_delta).normalized() if post else (scaled_delta * base).normalized()


func _sanitize_quaternion(value: Variant) -> Quaternion:
	var q: Quaternion = value
	if not q.is_finite() or is_zero_approx(q.length_squared()):
		return Quaternion()
	return q if q.is_normalized() else q.normalized()


func _simple_spline(value: float) -> float:
	var clamped := clampf(value, 0.0, 1.0)
	return clamped * clamped * (3.0 - 2.0 * clamped)


func _emit_events(previous_time: float, new_time: float, looped: bool) -> void:
	if animation_data == null or sequence_descriptor < 0:
		return
	var length := _get_sequence_length(sequence_descriptor)
	if length <= 0.0:
		return
	var sequence: Dictionary = animation_data.get_sequence(sequence_descriptor)
	var previous_cycle := clampf(previous_time / length, 0.0, 1.0)
	var new_cycle := clampf(new_time / length, 0.0, 1.0)
	if looped:
		_emit_events_in_range(sequence, previous_cycle, 1.0)
		_emit_events_in_range(sequence, 0.0, new_cycle)
	elif new_cycle >= previous_cycle:
		_emit_events_in_range(sequence, previous_cycle, new_cycle)


func _emit_events_in_range(sequence: Dictionary, start_cycle: float, end_cycle: float) -> void:
	var events: Array = sequence.get("events", [])
	for event_index in range(events.size()):
		var event: Dictionary = events[event_index]
		var cycle := float(event.get("cycle", 0.0))
		if cycle >= start_cycle and cycle < end_cycle:
			sequence_event.emit(sequence_descriptor, event_index, String(event.get("name", "")), int(event.get("event", 0)), String(event.get("options", "")))


func _find_child_by_type(node: Node, class_name_to_find: String) -> Node:
	if node.is_class(class_name_to_find):
		return node
	for child in node.get_children():
		var found := _find_child_by_type(child, class_name_to_find)
		if found != null:
			return found
	return null
