@tool
class_name SourceSelfIKRuleBehaviour
extends Resource

const IK_SELF := 1

@export var enabled := true

var root: Node
var animation_data: SourceMDLAnimationData
var skeleton_path: NodePath
var anim_behaviours: Node


func setup(imported_root: Node, data: SourceMDLAnimationData, skeleton: NodePath, owner: Node = null) -> void:
	root = imported_root
	animation_data = data
	skeleton_path = skeleton
	anim_behaviours = owner


func can_handle_ik_rule(rule_data: Dictionary) -> bool:
	return int(rule_data.get("type", 0)) == IK_SELF


func apply_ik_rule(rule_node: Node, pose: Dictionary, model_to_skeleton_bones: Array[int]) -> void:
	if not enabled or rule_node == null or animation_data == null or float(rule_node.get("current_weight")) <= 0.0:
		return
	var global_transforms := _compute_pose_global_transforms(pose, model_to_skeleton_bones)
	var target_position: Vector3 = rule_node.get("target_position")
	var target_rotation: Quaternion = rule_node.get("target_rotation")
	var target_transform := Transform3D(Basis(target_rotation), target_position)
	var source_bone := int(rule_node.get("bone"))
	if source_bone >= 0 and source_bone < global_transforms.size():
		target_transform = global_transforms[source_bone] * target_transform
	var skeleton := _resolve_skeleton()
	if skeleton != null:
		target_transform = skeleton.global_transform * target_transform
	_set_target(rule_node, target_transform.origin, target_transform.basis.get_rotation_quaternion(), {"self_bone": source_bone})


func _set_target(rule_node: Node, position: Vector3, rotation: Quaternion, metadata: Dictionary) -> void:
	if anim_behaviours != null and anim_behaviours.has_method("set_ik_rule_target"):
		anim_behaviours.call("set_ik_rule_target", rule_node, position, rotation, metadata)
	else:
		rule_node.set("target_position", position)
		rule_node.set("target_rotation", rotation)


func _resolve_skeleton() -> Skeleton3D:
	if root == null:
		return null
	return root.get_node_or_null(skeleton_path) as Skeleton3D


func _compute_pose_global_transforms(pose: Dictionary, model_to_skeleton_bones: Array[int]) -> Array[Transform3D]:
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
	var parent := _find_model_parent(bone, model_to_skeleton_bones)
	if parent >= 0:
		transform = _compute_pose_global_transform(parent, positions, rotations, model_to_skeleton_bones, transforms, computed) * transform
	transforms[bone] = transform
	computed[bone] = true
	return transform


func _find_model_parent(model_bone: int, model_to_skeleton_bones: Array[int]) -> int:
	var skeleton := _resolve_skeleton()
	if skeleton == null or model_bone < 0 or model_bone >= model_to_skeleton_bones.size():
		return -1
	var skeleton_bone := model_to_skeleton_bones[model_bone]
	if skeleton_bone < 0:
		return -1
	var skeleton_parent := skeleton.get_bone_parent(skeleton_bone)
	return model_to_skeleton_bones.find(skeleton_parent) if skeleton_parent >= 0 else -1


func _sanitize_quaternion(value: Variant) -> Quaternion:
	var q: Quaternion = value
	if not q.is_finite() or is_zero_approx(q.length_squared()):
		return Quaternion()
	return q if q.is_normalized() else q.normalized()
