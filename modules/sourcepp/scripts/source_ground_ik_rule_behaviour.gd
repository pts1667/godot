@tool
class_name SourceGroundIKRuleBehaviour
extends Resource

const IK_GROUND := 3

@export var enabled := true
@export_flags_3d_physics var collision_mask := 0xffffffff
@export var ray_height_scale := 1.0
@export var ray_drop_scale := 1.0
@export var min_ray_range := 1.0

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
	return int(rule_data.get("type", 0)) == IK_GROUND


func apply_ik_rule(rule_node: Node, pose: Dictionary, model_to_skeleton_bones: Array[int]) -> void:
	if not enabled or rule_node == null or animation_data == null:
		return
	if not can_handle_ik_rule(rule_node.get("rule_data")):
		return
	if float(rule_node.get("current_weight")) <= 0.0:
		return

	var chain := int(rule_node.get("chain"))
	if chain < 0 or chain >= animation_data.get_ik_chain_count():
		return
	var chain_data := animation_data.get_ik_chain(chain)
	var links: Array = chain_data.get("links", [])
	if links.is_empty():
		return

	var global_transforms := _compute_pose_global_transforms(pose, model_to_skeleton_bones)
	var end_bone := int((links.back() as Dictionary).get("bone", -1))
	if end_bone < 0 or end_bone >= global_transforms.size():
		return

	var skeleton := _resolve_skeleton()
	var skeleton_xform := skeleton.global_transform if skeleton != null else Transform3D()
	var end_transform: Transform3D = skeleton_xform * global_transforms[end_bone]
	var chain_length := _get_chain_length(links, global_transforms)
	var source_height := maxf(float(rule_node.get("height")), chain_length) * ray_height_scale
	var source_drop := maxf(float(rule_node.get("drop")), chain_length) * ray_drop_scale
	var source_floor := float(rule_node.get("floor"))
	var ray_range := maxf(source_height + source_drop, min_ray_range)

	var ray_from := end_transform.origin + Vector3.UP * source_height
	var ray_to := ray_from - Vector3.UP * ray_range
	var hit := _intersect_ground(ray_from, ray_to)
	if hit.is_empty():
		return

	var target_position: Vector3 = hit.get("position", end_transform.origin)
	target_position += Vector3.UP * source_floor
	var target_rotation := end_transform.basis.get_rotation_quaternion()
	if anim_behaviours != null and anim_behaviours.has_method("set_ik_rule_target"):
		anim_behaviours.call("set_ik_rule_target", rule_node, target_position, target_rotation, {"ground_hit": hit})
	else:
		rule_node.set("target_position", target_position)
		rule_node.set("target_rotation", target_rotation)
		rule_node.set_meta("sourcepp_ground_hit", hit)
	if float(rule_node.get("current_weight")) >= 0.999 and anim_behaviours != null and anim_behaviours.has_method("latch_ik_rule_target"):
		anim_behaviours.call("latch_ik_rule_target", rule_node)


func _resolve_skeleton() -> Skeleton3D:
	if root == null:
		return null
	return root.get_node_or_null(skeleton_path) as Skeleton3D


func _intersect_ground(ray_from: Vector3, ray_to: Vector3) -> Dictionary:
	if root == null or root.get_world_3d() == null:
		return {}
	var query := PhysicsRayQueryParameters3D.create(ray_from, ray_to)
	query.collision_mask = collision_mask
	return root.get_world_3d().direct_space_state.intersect_ray(query)


func _get_chain_length(links: Array, global_transforms: Array[Transform3D]) -> float:
	var length := 0.0
	var previous := -1
	for link in links:
		var link_data: Dictionary = link
		var bone := int(link_data.get("bone", -1))
		if bone < 0 or bone >= global_transforms.size():
			continue
		if previous >= 0:
			length += global_transforms[previous].origin.distance_to(global_transforms[bone].origin)
		previous = bone
	return length


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
