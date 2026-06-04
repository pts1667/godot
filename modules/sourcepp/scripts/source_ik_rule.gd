@tool
class_name SourceIKRule
extends Node

@export var animation_data: SourceMDLAnimationData
@export_node_path("Skeleton3D") var skeleton_path: NodePath
@export var sequence_index := -1
@export var animation_index := -1
@export var rule_index := -1
@export var chain := -1
@export var bone := -1
@export var slot := -1
@export var type := 0
@export var start := 0.0
@export var peak := 0.0
@export var tail := 0.0
@export var end := 0.0
@export var contact := 0.0
@export var height := 0.0
@export var radius := 0.0
@export var floor := 0.0
@export var drop := 0.0
@export var top := 0.0
@export var attachment := ""
@export var enabled := true
@export var behaviour: Resource

var rule_data: Dictionary = {}
var target_position := Vector3.ZERO
var target_rotation := Quaternion()
var current_cycle := 0.0
var current_weight := 0.0


func configure(data: SourceMDLAnimationData, skeleton: NodePath, source_sequence: int, source_rule: Dictionary, source_animation := -1, source_rule_index := -1, source_behaviour: Resource = null) -> void:
	animation_data = data
	skeleton_path = skeleton
	sequence_index = source_sequence
	animation_index = source_animation
	rule_index = source_rule_index
	rule_data = source_rule.duplicate(true)
	chain = int(rule_data.get("chain", -1))
	bone = int(rule_data.get("bone", -1))
	slot = int(rule_data.get("slot", chain))
	type = int(rule_data.get("type", 0))
	start = float(rule_data.get("start", 0.0))
	peak = float(rule_data.get("peak", start))
	tail = float(rule_data.get("tail", end))
	end = float(rule_data.get("end", 1.0))
	contact = float(rule_data.get("contact", 0.0))
	height = float(rule_data.get("height", 0.0))
	radius = float(rule_data.get("radius", 0.0))
	floor = float(rule_data.get("floor", 0.0))
	drop = float(rule_data.get("drop", 0.0))
	top = float(rule_data.get("top", 0.0))
	attachment = String(rule_data.get("attachment", ""))
	target_position = rule_data.get("position", Vector3.ZERO)
	target_rotation = rule_data.get("rotation", Quaternion())
	behaviour = source_behaviour
	name = "SourceIKRule_%d_%d" % [sequence_index, rule_index]


func update_rule(cycle: float, pose: Dictionary, model_to_skeleton_bones: Array[int] = []) -> void:
	if not enabled:
		return
	current_cycle = cycle
	current_weight = _resolve_weight(cycle)
	if behaviour != null and behaviour.has_method("apply_ik_rule"):
		behaviour.call("apply_ik_rule", self, pose, model_to_skeleton_bones)
	else:
		_apply_rule(pose)


func _apply_rule(_pose: Dictionary) -> void:
	# Fallback hook. Assign a behaviour Resource from AnimBehaviours to implement
	# rule-specific target lookup, latching, release, and event behavior.
	pass


func _resolve_weight(cycle: float) -> float:
	if end <= start:
		return 1.0
	if cycle < start or cycle > end:
		return 0.0
	if cycle < peak and not is_equal_approx(start, peak):
		return clampf((cycle - start) / (peak - start), 0.0, 1.0)
	if cycle > tail and not is_equal_approx(end, tail):
		return clampf((end - cycle) / (end - tail), 0.0, 1.0)
	return 1.0
