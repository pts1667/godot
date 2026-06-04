@tool
class_name SourceAnimBehaviours
extends Node

@export var animation_data: SourceMDLAnimationData
@export_node_path("Skeleton3D") var skeleton_path: NodePath
@export var ik_rule_behaviours: Array[Resource] = []
@export var event_behaviours: Array[Resource] = []

var imported_root: Node
var ik_chain_targets := {}
var ik_chain_latches := {}
var ik_chain_releases := {}


func _ready() -> void:
	_ensure_default_behaviours()


func setup_from_imported_root(root: Node, data: SourceMDLAnimationData = null, skeleton: NodePath = NodePath()) -> bool:
	imported_root = root
	if data != null:
		animation_data = data
	if not skeleton.is_empty():
		skeleton_path = skeleton
	if animation_data == null and root != null:
		var meta_data: Variant = root.get_meta("sourcepp_animation_data", null)
		if meta_data is SourceMDLAnimationData:
			animation_data = meta_data

	_ensure_default_behaviours()
	_setup_behaviour_resources()
	return imported_root != null and animation_data != null


func get_ik_rule_behaviour(rule_data: Dictionary) -> Resource:
	_ensure_default_behaviours()
	for behaviour in ik_rule_behaviours:
		if behaviour != null and bool(behaviour.get("enabled")) and behaviour.has_method("can_handle_ik_rule") and bool(behaviour.call("can_handle_ik_rule", rule_data)):
			return behaviour
	return null


func _ensure_default_behaviours() -> void:
	_ensure_default_ik_rule_behaviour("DefaultSelfIKRuleBehaviour", "source_self_ik_rule_behaviour.gd", 1)
	_ensure_default_ik_rule_behaviour("DefaultWorldIKRuleBehaviour", "source_world_ik_rule_behaviour.gd", 2)
	_ensure_default_ik_rule_behaviour("DefaultGroundIKRuleBehaviour", "source_ground_ik_rule_behaviour.gd", 3)
	_ensure_default_ik_rule_behaviour("DefaultReleaseIKRuleBehaviour", "source_release_ik_rule_behaviour.gd", 4)
	_ensure_default_ik_rule_behaviour("DefaultAttachmentIKRuleBehaviour", "source_attachment_ik_rule_behaviour.gd", 5)
	_ensure_default_ik_rule_behaviour("DefaultUnlatchIKRuleBehaviour", "source_unlatch_ik_rule_behaviour.gd", 6)


func set_ik_rule_target(rule_node: Node, target_position: Vector3, target_rotation: Quaternion, metadata: Dictionary = {}) -> void:
	if rule_node == null:
		return
	rule_node.set("target_position", target_position)
	rule_node.set("target_rotation", target_rotation)
	for key in metadata:
		rule_node.set_meta(StringName("sourcepp_%s" % String(key)), metadata[key])
	var key := get_ik_chain_key(int(rule_node.get("chain")), int(rule_node.get("slot")))
	ik_chain_targets[key] = {
		"position": target_position,
		"rotation": target_rotation,
		"weight": float(rule_node.get("current_weight")),
		"rule": rule_node.get_instance_id(),
		"metadata": metadata,
	}


func latch_ik_rule_target(rule_node: Node) -> void:
	if rule_node == null:
		return
	var key := get_ik_chain_key(int(rule_node.get("chain")), int(rule_node.get("slot")))
	ik_chain_latches[key] = {
		"position": rule_node.get("target_position"),
		"rotation": rule_node.get("target_rotation"),
		"rule": rule_node.get_instance_id(),
	}


func get_latched_ik_target(rule_node: Node) -> Dictionary:
	if rule_node == null:
		return {}
	var key := get_ik_chain_key(int(rule_node.get("chain")), int(rule_node.get("slot")))
	return ik_chain_latches.get(key, {})


func release_ik_chain(rule_node: Node, weight: float) -> void:
	if rule_node == null:
		return
	var key := get_ik_chain_key(int(rule_node.get("chain")), int(rule_node.get("slot")))
	ik_chain_releases[key] = maxf(float(ik_chain_releases.get(key, 0.0)), clampf(weight, 0.0, 1.0))
	if ik_chain_releases[key] >= 0.999:
		ik_chain_targets.erase(key)
		ik_chain_latches.erase(key)


func unlatch_ik_chain(rule_node: Node) -> void:
	if rule_node == null:
		return
	var key := get_ik_chain_key(int(rule_node.get("chain")), int(rule_node.get("slot")))
	ik_chain_latches.erase(key)


func get_ik_chain_key(chain: int, slot: int) -> String:
	return "%d:%d" % [chain, slot]


func _setup_behaviour_resources() -> void:
	for behaviour in ik_rule_behaviours:
		_setup_behaviour_resource(behaviour)
	for behaviour in event_behaviours:
		_setup_behaviour_resource(behaviour)


func _setup_behaviour_resource(behaviour: Resource) -> void:
	if behaviour != null and behaviour.has_method("setup"):
		behaviour.call("setup", imported_root if imported_root != null else get_parent(), animation_data, skeleton_path, self)


func _ensure_default_ik_rule_behaviour(resource_name: String, script_name: String, rule_type: int) -> void:
	for behaviour in ik_rule_behaviours:
		if behaviour != null and behaviour.has_method("can_handle_ik_rule") and bool(behaviour.call("can_handle_ik_rule", {"type": rule_type})):
			return

	var behaviour_script := _load_sibling_script(script_name)
	if behaviour_script == null:
		return
	var behaviour := Resource.new()
	behaviour.resource_name = resource_name
	behaviour.set_script(behaviour_script)
	ik_rule_behaviours.append(behaviour)
	_setup_behaviour_resource(behaviour)


func _load_sibling_script(file_name: String) -> Script:
	var script := get_script() as Script
	if script == null:
		return null
	var path := script.resource_path.get_base_dir().path_join(file_name)
	return load(path) as Script
