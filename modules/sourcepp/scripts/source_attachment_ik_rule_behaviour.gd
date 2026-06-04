@tool
class_name SourceAttachmentIKRuleBehaviour
extends Resource

const IK_ATTACHMENT := 5

@export var enabled := true
@export var fallback_to_rule_transform := true

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
	return int(rule_data.get("type", 0)) == IK_ATTACHMENT


func apply_ik_rule(rule_node: Node, _pose: Dictionary, _model_to_skeleton_bones: Array[int]) -> void:
	if not enabled or rule_node == null or float(rule_node.get("current_weight")) <= 0.0:
		return
	var attachment_name := String(rule_node.get("attachment"))
	var target := _find_attachment_target(attachment_name)
	if target != null:
		var transform := (target as Node3D).global_transform
		_set_target(rule_node, transform.origin, transform.basis.get_rotation_quaternion(), {"attachment": attachment_name, "attachment_node": target.get_path()})
		return
	if fallback_to_rule_transform:
		var position: Vector3 = rule_node.get("target_position")
		var rotation: Quaternion = rule_node.get("target_rotation")
		_set_target(rule_node, position, rotation, {"attachment": attachment_name, "attachment_missing": true})


func _set_target(rule_node: Node, position: Vector3, rotation: Quaternion, metadata: Dictionary) -> void:
	if anim_behaviours != null and anim_behaviours.has_method("set_ik_rule_target"):
		anim_behaviours.call("set_ik_rule_target", rule_node, position, rotation, metadata)
	else:
		rule_node.set("target_position", position)
		rule_node.set("target_rotation", rotation)


func _find_attachment_target(attachment_name: String) -> Node3D:
	if attachment_name.is_empty():
		return null
	if root != null:
		var local := _find_named_node(root, attachment_name)
		if local is Node3D:
			return local
		if root.get_tree() != null and root.get_tree().root != null:
			var global := _find_named_node(root.get_tree().root, attachment_name)
			if global is Node3D:
				return global
	return null


func _find_named_node(node: Node, wanted_name: String) -> Node:
	if node.name == wanted_name:
		return node
	for child in node.get_children():
		var found := _find_named_node(child, wanted_name)
		if found != null:
			return found
	return null
