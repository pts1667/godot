@tool
class_name SourceIKLock
extends Node3D

@export var animation_data: SourceMDLAnimationData
@export_node_path("Skeleton3D") var skeleton_path: NodePath
@export var sequence_index := -1
@export var chain := -1
@export var position_weight := 0.0
@export var local_quaternion_weight := 0.0
@export var flags := 0
@export var depth := 0
@export var autoplay := false
@export var enabled := true

var lock_data: Dictionary = {}
var target_position := Vector3.ZERO
var target_rotation := Quaternion()
var knee_position := Vector3.ZERO
var knee_direction := Vector3.ZERO

var _skeleton: Skeleton3D
var _target: Node3D
var _pole: Node3D


func configure(data: SourceMDLAnimationData, skeleton: NodePath, source_sequence: int, source_lock: Dictionary, source_depth := 0, source_autoplay := false) -> void:
	animation_data = data
	skeleton_path = skeleton
	sequence_index = source_sequence
	lock_data = source_lock.duplicate(true)
	chain = int(lock_data.get("chain", -1))
	position_weight = clampf(float(lock_data.get("position_weight", 0.0)), 0.0, 1.0)
	local_quaternion_weight = clampf(float(lock_data.get("local_quaternion_weight", 0.0)), 0.0, 1.0)
	flags = int(lock_data.get("flags", 0))
	depth = source_depth
	autoplay = source_autoplay
	name = "SourceIKLock_%s_%d" % ["autoplay" if autoplay else str(sequence_index), chain]
	_resolve_skeleton()
	_ensure_target_nodes()


func capture_from_pose(pose: Dictionary, model_to_skeleton_bones: Array[int]) -> bool:
	if not enabled or animation_data == null or chain < 0 or chain >= animation_data.get_ik_chain_count():
		return false
	var chain_data := animation_data.get_ik_chain(chain)
	var links: Array = chain_data.get("links", [])
	if links.is_empty():
		return false

	var end_bone := int(links.back().get("bone", -1))
	if end_bone < 0 or end_bone >= animation_data.get_bone_count():
		return false

	var global_transforms := _compute_pose_global_transforms(pose, model_to_skeleton_bones)
	if end_bone >= global_transforms.size():
		return false

	var end_transform: Transform3D = global_transforms[end_bone]
	target_position = end_transform.origin
	target_rotation = end_transform.basis.get_rotation_quaternion()

	if links.size() >= 3:
		var root_link: Dictionary = links[0]
		var knee_link: Dictionary = links[1]
		var root_bone := int(root_link.get("bone", -1))
		var knee_bone := int(knee_link.get("bone", -1))
		var source_knee_dir: Vector3 = root_link.get("knee_dir", Vector3.ZERO)
		if root_bone >= 0 and root_bone < global_transforms.size() and not source_knee_dir.is_zero_approx():
			knee_direction = global_transforms[root_bone].basis * source_knee_dir
			knee_direction = knee_direction.normalized()
		if knee_bone >= 0 and knee_bone < global_transforms.size():
			knee_position = global_transforms[knee_bone].origin

	_apply_target_nodes()
	return true


func apply_lock() -> void:
	if not enabled:
		return
	_apply_target_nodes()
	_apply_existing_modifier()


func get_target_node() -> Node3D:
	_ensure_target_nodes()
	return _target


func get_pole_node() -> Node3D:
	_ensure_target_nodes()
	return _pole


func _resolve_skeleton() -> void:
	_skeleton = get_node_or_null(skeleton_path) as Skeleton3D


func _ensure_target_nodes() -> void:
	if _target == null:
		_target = Node3D.new()
		_target.name = "Target"
		add_child(_target)
	if _pole == null:
		_pole = Node3D.new()
		_pole.name = "Pole"
		add_child(_pole)


func _apply_target_nodes() -> void:
	_ensure_target_nodes()
	var target_transform := Transform3D(Basis(target_rotation), target_position)
	if _target.is_inside_tree():
		_target.global_transform = target_transform
	else:
		_target.transform = target_transform
	var pole_position := knee_position
	if not knee_direction.is_zero_approx():
		pole_position += knee_direction.normalized()
	var pole_basis := _pole.global_transform.basis if _pole.is_inside_tree() else _pole.transform.basis
	var pole_transform := Transform3D(pole_basis, pole_position)
	if _pole.is_inside_tree():
		_pole.global_transform = pole_transform
	else:
		_pole.transform = pole_transform


func _apply_existing_modifier() -> void:
	var modifier := get_node_or_null("Modifier")
	if modifier != null and modifier.has_method("process_modification"):
		modifier.call("process_modification", 0.0)


func _compute_pose_global_transforms(pose: Dictionary, model_to_skeleton_bones: Array[int]) -> Array[Transform3D]:
	_resolve_skeleton()
	var transforms: Array[Transform3D] = []
	var computed: Array[bool] = []
	var positions: Array = pose.get("positions", [])
	var rotations: Array = pose.get("rotations", [])
	var bone_count: int = min(positions.size(), rotations.size())
	transforms.resize(bone_count)
	computed.resize(bone_count)
	for i in range(bone_count):
		computed[i] = false

	for bone in range(bone_count):
		_compute_pose_global_transform(bone, positions, rotations, model_to_skeleton_bones, transforms, computed)
	return transforms


func _compute_pose_global_transform(bone: int, positions: Array, rotations: Array, model_to_skeleton_bones: Array[int], transforms: Array[Transform3D], computed: Array[bool]) -> Transform3D:
	if bone < 0 or bone >= transforms.size():
		return Transform3D()
	if computed[bone]:
		return transforms[bone]

	var transform := Transform3D(Basis(_sanitize_quaternion(rotations[bone])), positions[bone])
	var parent := -1
	if _skeleton != null and bone < model_to_skeleton_bones.size():
		var skeleton_bone := model_to_skeleton_bones[bone]
		if skeleton_bone >= 0:
			var skeleton_parent := _skeleton.get_bone_parent(skeleton_bone)
			if skeleton_parent >= 0:
				parent = model_to_skeleton_bones.find(skeleton_parent)
	if parent >= 0:
		transform = _compute_pose_global_transform(parent, positions, rotations, model_to_skeleton_bones, transforms, computed) * transform

	transforms[bone] = transform
	computed[bone] = true
	return transform


func _sanitize_quaternion(value: Variant) -> Quaternion:
	var q: Quaternion = value
	if not q.is_finite() or is_zero_approx(q.length_squared()):
		return Quaternion()
	return q if q.is_normalized() else q.normalized()
