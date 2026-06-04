@tool
class_name SourceWorldIKRuleBehaviour
extends Resource

const IK_WORLD := 2

@export var enabled := true
@export var transform_by_imported_root := true

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
	return int(rule_data.get("type", 0)) == IK_WORLD


func apply_ik_rule(rule_node: Node, _pose: Dictionary, _model_to_skeleton_bones: Array[int]) -> void:
	if not enabled or rule_node == null or float(rule_node.get("current_weight")) <= 0.0:
		return
	var position: Vector3 = rule_node.get("target_position")
	var rotation: Quaternion = rule_node.get("target_rotation")
	var target_transform := Transform3D(Basis(rotation), position)
	if transform_by_imported_root and root is Node3D:
		target_transform = (root as Node3D).global_transform * target_transform
	if anim_behaviours != null and anim_behaviours.has_method("set_ik_rule_target"):
		anim_behaviours.call("set_ik_rule_target", rule_node, target_transform.origin, target_transform.basis.get_rotation_quaternion(), {"world": true})
	else:
		rule_node.set("target_position", target_transform.origin)
		rule_node.set("target_rotation", target_transform.basis.get_rotation_quaternion())
	if float(rule_node.get("current_weight")) >= 0.999 and anim_behaviours != null and anim_behaviours.has_method("latch_ik_rule_target"):
		anim_behaviours.call("latch_ik_rule_target", rule_node)
