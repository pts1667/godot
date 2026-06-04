@tool
class_name SourceIKData
extends Node3D

signal sequence_ik_started(sequence_index: int)
signal sequence_ik_cleared(sequence_index: int)

@export var animation_data: SourceMDLAnimationData
@export_node_path("Skeleton3D") var skeleton_path: NodePath
@export var ik_enabled := true
@export var solver_influence := 1.0
@export var fabrik_iterations := 8

var active_sequence := -1
var active_cycle := 0.0

var _lock_script: Script
var _rule_script: Script
var _anim_behaviours_script: Script
var _anim_behaviours: Node
var _imported_root: Node
var _locks: Array[Node] = []
var _rules: Array[Node] = []
var _solvers := {}
var _model_to_skeleton_bones: Array[int] = []


func _ready() -> void:
	_load_child_scripts()


func setup_from_imported_root(root: Node) -> bool:
	if root == null:
		return false

	_imported_root = root
	var data: Variant = root.get_meta("sourcepp_animation_data", null)
	if data is SourceMDLAnimationData:
		animation_data = data

	var skeleton := _find_child_by_type(root, "Skeleton3D") as Skeleton3D
	if skeleton != null:
		skeleton_path = get_path_to(skeleton)

	_ensure_anim_behaviours(root)
	return animation_data != null and skeleton != null


func begin_sequence(sequence_index: int, sequence: Dictionary, pending_locks: Array[Dictionary]) -> void:
	if active_sequence == sequence_index and _same_lock_count(pending_locks):
		return

	clear_sequence()
	active_sequence = sequence_index
	_create_lock_nodes(pending_locks)
	_create_rule_nodes(sequence)
	sequence_ik_started.emit(active_sequence)


func clear_sequence() -> void:
	var previous_sequence := active_sequence
	for child in _locks:
		if is_instance_valid(child):
			child.queue_free()
	for child in _rules:
		if is_instance_valid(child):
			child.queue_free()
	_locks.clear()
	_rules.clear()
	_clear_solvers()
	active_sequence = -1
	if previous_sequence >= 0:
		sequence_ik_cleared.emit(previous_sequence)


func update_sequence(cycle: float, pending_locks: Array[Dictionary], pose: Dictionary, model_to_skeleton_bones: Array[int]) -> void:
	active_cycle = cycle
	if not ik_enabled:
		return
	_model_to_skeleton_bones = model_to_skeleton_bones.duplicate()
	_sync_lock_nodes(pending_locks)
	for lock_node in _locks:
		if is_instance_valid(lock_node) and lock_node.has_method("capture_from_pose"):
			lock_node.call("capture_from_pose", pose, model_to_skeleton_bones)
	for rule_node in _rules:
		if is_instance_valid(rule_node) and rule_node.has_method("update_rule"):
			rule_node.call("update_rule", active_cycle, pose, model_to_skeleton_bones)


func apply_ik() -> void:
	if not ik_enabled:
		return
	var skeleton := _resolve_skeleton()
	if skeleton == null:
		return
	skeleton.force_update_all_bone_transforms()
	_apply_solver_sources(_rules)
	_apply_solver_sources(_locks)


func get_active_locks() -> Array[Node]:
	return _locks.duplicate()


func get_active_rules() -> Array[Node]:
	return _rules.duplicate()


func get_active_solvers() -> Array[Node]:
	var out: Array[Node] = []
	for chain_key in _solvers:
		var entry: Dictionary = _solvers[chain_key]
		var modifier := entry.get("modifier") as Node
		if modifier != null and is_instance_valid(modifier):
			out.append(modifier)
	return out


func _load_child_scripts() -> void:
	if _lock_script == null:
		_lock_script = _load_sibling_script("source_ik_lock.gd")
	if _rule_script == null:
		_rule_script = _load_sibling_script("source_ik_rule.gd")
	if _anim_behaviours_script == null:
		_anim_behaviours_script = _load_sibling_script("source_anim_behaviours.gd")


func _load_sibling_script(file_name: String) -> Script:
	var script := get_script() as Script
	if script == null:
		return null
	var path := script.resource_path.get_base_dir().path_join(file_name)
	return load(path) as Script


func _create_lock_nodes(pending_locks: Array[Dictionary]) -> void:
	_load_child_scripts()
	for index in range(pending_locks.size()):
		var pending: Dictionary = pending_locks[index]
		var lock_data: Dictionary = pending.get("lock", {})
		var lock_node := Node3D.new()
		lock_node.name = "SourceIKLock_%d" % index
		if _lock_script != null:
			lock_node.set_script(_lock_script)
		add_child(lock_node)
		if lock_node.has_method("configure"):
			lock_node.call("configure", animation_data, skeleton_path, int(pending.get("sequence", active_sequence)), lock_data, int(pending.get("depth", 0)), bool(pending.get("autoplay", false)))
		_locks.append(lock_node)


func _create_rule_nodes(sequence: Dictionary) -> void:
	_load_child_scripts()
	_ensure_anim_behaviours()
	var rules: Array = sequence.get("ik_rules", [])
	for index in range(rules.size()):
		var rule_data: Dictionary = rules[index]
		var rule_node := Node.new()
		rule_node.name = "SourceIKRule_%d" % index
		if _rule_script != null:
			rule_node.set_script(_rule_script)
		add_child(rule_node)
		var behaviour: Resource = null
		if _anim_behaviours != null and _anim_behaviours.has_method("get_ik_rule_behaviour"):
			behaviour = _anim_behaviours.call("get_ik_rule_behaviour", rule_data)
		if rule_node.has_method("configure"):
			rule_node.call("configure", animation_data, skeleton_path, active_sequence, rule_data, int(rule_data.get("animation", -1)), index, behaviour)
		_rules.append(rule_node)


func _ensure_anim_behaviours(root_hint: Node = null) -> Node:
	if _anim_behaviours != null and is_instance_valid(_anim_behaviours):
		return _anim_behaviours
	_load_child_scripts()
	var root := root_hint if root_hint != null else _imported_root
	if root == null:
		root = get_parent()
	if root == null:
		return null

	_anim_behaviours = root.get_node_or_null("AnimBehaviours")
	if _anim_behaviours == null:
		_anim_behaviours = Node.new()
		_anim_behaviours.name = "AnimBehaviours"
		if _anim_behaviours_script != null:
			_anim_behaviours.set_script(_anim_behaviours_script)
		root.add_child(_anim_behaviours)
	if _anim_behaviours != null and _anim_behaviours.has_method("setup_from_imported_root"):
		_anim_behaviours.call("setup_from_imported_root", root, animation_data, skeleton_path)
	return _anim_behaviours


func _sync_lock_nodes(pending_locks: Array[Dictionary]) -> void:
	if _same_lock_count(pending_locks):
		return
	for child in _locks:
		if is_instance_valid(child):
			child.queue_free()
	_locks.clear()
	_create_lock_nodes(pending_locks)


func _same_lock_count(pending_locks: Array[Dictionary]) -> bool:
	return _locks.size() == pending_locks.size()


func _apply_solver_sources(sources: Array[Node]) -> void:
	for source in sources:
		if not is_instance_valid(source) or not bool(source.get("enabled")):
			continue
		var chain := int(source.get("chain"))
		if chain < 0:
			continue
		var weight := _get_source_weight(source)
		if weight <= 0.0:
			continue
		var solver := _ensure_solver(chain)
		if solver.is_empty():
			continue
		_update_solver_target(solver, source)
		var modifier := solver.get("modifier") as Node
		if modifier != null and modifier.has_method("set_influence"):
			modifier.set("influence", clampf(weight * solver_influence, 0.0, 1.0))
		if modifier != null and modifier.has_method("process_modification"):
			modifier.call("process_modification", 0.0)
		var skeleton := _resolve_skeleton()
		if skeleton != null:
			skeleton.force_update_all_bone_transforms()


func _get_source_weight(source: Node) -> float:
	if source.has_method("capture_from_pose"):
		return clampf(float(source.get("position_weight")), 0.0, 1.0)
	if source.has_method("update_rule"):
		return clampf(float(source.get("current_weight")), 0.0, 1.0)
	return 1.0


func _ensure_solver(chain: int) -> Dictionary:
	if _solvers.has(chain):
		var existing: Dictionary = _solvers[chain]
		var existing_modifier := existing.get("modifier") as Node
		if existing_modifier != null and is_instance_valid(existing_modifier):
			return existing
		_solvers.erase(chain)

	var skeleton := _resolve_skeleton()
	if skeleton == null or animation_data == null or chain < 0 or chain >= animation_data.get_ik_chain_count():
		return {}
	var chain_data := animation_data.get_ik_chain(chain)
	var links: Array = chain_data.get("links", [])
	if links.size() < 2:
		return {}

	var root_model_bone := int((links.front() as Dictionary).get("bone", -1))
	var end_model_bone := int((links.back() as Dictionary).get("bone", -1))
	var root_bone := _model_to_skeleton_bone(root_model_bone)
	var end_bone := _model_to_skeleton_bone(end_model_bone)
	if root_bone < 0 or end_bone < 0:
		return {}

	var modifier: Node
	var target := Node3D.new()
	target.name = "Target"
	var pole: Node3D = null

	if links.size() == 3:
		var middle_model_bone := int((links[1] as Dictionary).get("bone", -1))
		var middle_bone := _model_to_skeleton_bone(middle_model_bone)
		if middle_bone < 0:
			return {}
		var two_bone := TwoBoneIK3D.new()
		two_bone.name = "SourceIKChain_%d_TwoBoneIK3D" % chain
		two_bone.setting_count = 1
		pole = Node3D.new()
		pole.name = "Pole"
		two_bone.add_child(target)
		two_bone.add_child(pole)
		two_bone.set_root_bone(0, root_bone)
		two_bone.set_middle_bone(0, middle_bone)
		two_bone.set_end_bone(0, end_bone)
		two_bone.set_target_node(0, NodePath("Target"))
		two_bone.set_pole_node(0, NodePath("Pole"))
		two_bone.set_pole_direction(0, SkeletonModifier3D.SECONDARY_DIRECTION_NONE)
		modifier = two_bone
	else:
		var fabrik := FABRIK3D.new()
		fabrik.name = "SourceIKChain_%d_FABRIK3D" % chain
		fabrik.setting_count = 1
		fabrik.add_child(target)
		fabrik.set_root_bone(0, root_bone)
		fabrik.set_end_bone(0, end_bone)
		fabrik.set_target_node(0, NodePath("Target"))
		fabrik.max_iterations = fabrik_iterations
		modifier = fabrik

	modifier.set("active", true)
	modifier.set("influence", solver_influence)
	skeleton.add_child(modifier)
	var solver := {
		"chain": chain,
		"modifier": modifier,
		"target": target,
		"pole": pole,
		"root_bone": root_bone,
		"end_bone": end_bone,
		"links": links,
	}
	_solvers[chain] = solver
	return solver


func _update_solver_target(solver: Dictionary, source: Node) -> void:
	var skeleton := _resolve_skeleton()
	if skeleton == null:
		return
	var target := solver.get("target") as Node3D
	if target == null:
		return

	var source_position: Vector3 = source.get("target_position")
	var source_rotation: Quaternion = source.get("target_rotation")
	var source_transform := Transform3D(Basis(source_rotation), source_position)
	var is_rule := source.has_method("update_rule")
	var skeleton_xform := skeleton.global_transform if skeleton.is_inside_tree() else skeleton.transform
	var target_global := source_transform if is_rule else skeleton_xform * source_transform
	_set_node_global_or_local(target, target_global)

	var pole := solver.get("pole") as Node3D
	if pole != null:
		var is_lock := source.has_method("capture_from_pose")
		var pole_position: Vector3 = source.get("knee_position") if is_lock else _estimate_pole_position(solver)
		var knee_direction: Vector3 = source.get("knee_direction") if is_lock else Vector3.ZERO
		if is_lock and not knee_direction.is_zero_approx():
			pole_position += knee_direction.normalized() * _get_chain_length(solver.get("links", []))
		var pole_basis := pole.global_transform.basis if pole.is_inside_tree() else pole.transform.basis
		var pole_global := Transform3D(pole_basis, skeleton_xform * pole_position if is_lock else pole_position)
		_set_node_global_or_local(pole, pole_global)


func _set_node_global_or_local(node: Node3D, global_xform: Transform3D) -> void:
	if node.is_inside_tree():
		node.global_transform = global_xform
		return
	var parent_3d := node.get_parent() as Node3D
	if parent_3d != null and parent_3d.is_inside_tree():
		node.transform = parent_3d.global_transform.affine_inverse() * global_xform
	else:
		node.transform = global_xform


func _estimate_pole_position(solver: Dictionary) -> Vector3:
	var skeleton := _resolve_skeleton()
	if skeleton == null:
		return Vector3.ZERO
	var links: Array = solver.get("links", [])
	if links.size() < 3:
		return Vector3.ZERO
	var middle_bone := _model_to_skeleton_bone(int((links[1] as Dictionary).get("bone", -1)))
	if middle_bone < 0:
		return Vector3.ZERO
	return skeleton.get_bone_global_pose(middle_bone).origin


func _get_chain_length(links: Array) -> float:
	var skeleton := _resolve_skeleton()
	if skeleton == null:
		return 1.0
	var length := 0.0
	var previous_bone := -1
	for link in links:
		var model_bone := int((link as Dictionary).get("bone", -1))
		var skeleton_bone := _model_to_skeleton_bone(model_bone)
		if skeleton_bone < 0:
			continue
		if previous_bone >= 0:
			length += skeleton.get_bone_global_pose(previous_bone).origin.distance_to(skeleton.get_bone_global_pose(skeleton_bone).origin)
		previous_bone = skeleton_bone
	return maxf(length, 1.0)


func _model_to_skeleton_bone(model_bone: int) -> int:
	return _model_to_skeleton_bones[model_bone] if model_bone >= 0 and model_bone < _model_to_skeleton_bones.size() else -1


func _clear_solvers() -> void:
	for chain_key in _solvers:
		var entry: Dictionary = _solvers[chain_key]
		var modifier := entry.get("modifier") as Node
		if modifier != null and is_instance_valid(modifier):
			modifier.queue_free()
	_solvers.clear()


func _resolve_skeleton() -> Skeleton3D:
	if skeleton_path.is_empty():
		return null
	return get_node_or_null(skeleton_path) as Skeleton3D


func _find_child_by_type(node: Node, class_name_to_find: String) -> Node:
	if node.is_class(class_name_to_find):
		return node
	for child in node.get_children():
		var found := _find_child_by_type(child, class_name_to_find)
		if found != null:
			return found
	return null
