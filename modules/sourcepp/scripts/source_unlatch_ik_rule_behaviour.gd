@tool
class_name SourceUnlatchIKRuleBehaviour
extends Resource

const IK_UNLATCH := 6

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
	return int(rule_data.get("type", 0)) == IK_UNLATCH


func apply_ik_rule(rule_node: Node, _pose: Dictionary, _model_to_skeleton_bones: Array[int]) -> void:
	if not enabled or rule_node == null or anim_behaviours == null:
		return
	if float(rule_node.get("current_weight")) > 0.0 and anim_behaviours.has_method("unlatch_ik_chain"):
		anim_behaviours.call("unlatch_ik_chain", rule_node)
