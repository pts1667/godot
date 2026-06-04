@tool
class_name SourceAnimBehaviour
extends Resource

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


func can_handle_ik_rule(_rule_data: Dictionary) -> bool:
	return false


func apply_ik_rule(_rule_node: Node, _pose: Dictionary, _model_to_skeleton_bones: Array[int]) -> void:
	pass
