/**************************************************************************/
/*  sourcepp_bsp_entity.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp_entity.h"

#include "core/os/time.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/physics/collision_shape_3d.h"

namespace {

String _dict_string(const Dictionary &p_dict, const StringName &p_key, const String &p_default = String()) {
	const String key = String(p_key);
	return p_dict.has(key) ? String(p_dict[key]) : p_default;
}

double _dict_float(const Dictionary &p_dict, const StringName &p_key, double p_default = 0.0) {
	const String key = String(p_key);
	return p_dict.has(key) ? static_cast<double>(p_dict[key]) : p_default;
}

int _dict_int(const Dictionary &p_dict, const StringName &p_key, int p_default = 0) {
	const String key = String(p_key);
	return p_dict.has(key) ? static_cast<int>(p_dict[key]) : p_default;
}

bool _dict_bool(const Dictionary &p_dict, const StringName &p_key, bool p_default = false) {
	const String key = String(p_key);
	if (!p_dict.has(key)) {
		return p_default;
	}

	const Variant value = p_dict[key];
	if (value.get_type() == Variant::BOOL) {
		return value;
	}
	const String value_string = String(value).strip_edges().to_lower();
	return value_string == "1" || value_string == "true" || value_string == "yes";
}

void _apply_visible_recursive(Node *p_node, bool p_visible) {
	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	if (node_3d != nullptr) {
		node_3d->set_visible(p_visible);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_apply_visible_recursive(p_node->get_child(i), p_visible);
	}
}

void _apply_collision_disabled_recursive(Node *p_node, bool p_disabled) {
	CollisionShape3D *collision_shape = Object::cast_to<CollisionShape3D>(p_node);
	if (collision_shape != nullptr) {
		collision_shape->set_disabled(p_disabled);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_apply_collision_disabled_recursive(p_node->get_child(i), p_disabled);
	}
}

} // namespace

void SourcePPBrushEntity3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup_sourcepp_entity", "classname", "targetname", "entity_index", "model_index", "keyvalues", "outputs"), &SourcePPBrushEntity3D::setup_sourcepp_entity);
	ClassDB::bind_method(D_METHOD("set_sourcepp_classname", "classname"), &SourcePPBrushEntity3D::set_sourcepp_classname);
	ClassDB::bind_method(D_METHOD("get_sourcepp_classname"), &SourcePPBrushEntity3D::get_sourcepp_classname);
	ClassDB::bind_method(D_METHOD("set_sourcepp_targetname", "targetname"), &SourcePPBrushEntity3D::set_sourcepp_targetname);
	ClassDB::bind_method(D_METHOD("get_sourcepp_targetname"), &SourcePPBrushEntity3D::get_sourcepp_targetname);
	ClassDB::bind_method(D_METHOD("set_sourcepp_entity_index", "entity_index"), &SourcePPBrushEntity3D::set_sourcepp_entity_index);
	ClassDB::bind_method(D_METHOD("get_sourcepp_entity_index"), &SourcePPBrushEntity3D::get_sourcepp_entity_index);
	ClassDB::bind_method(D_METHOD("set_sourcepp_model_index", "model_index"), &SourcePPBrushEntity3D::set_sourcepp_model_index);
	ClassDB::bind_method(D_METHOD("get_sourcepp_model_index"), &SourcePPBrushEntity3D::get_sourcepp_model_index);
	ClassDB::bind_method(D_METHOD("set_sourcepp_keyvalues", "keyvalues"), &SourcePPBrushEntity3D::set_sourcepp_keyvalues);
	ClassDB::bind_method(D_METHOD("get_sourcepp_keyvalues"), &SourcePPBrushEntity3D::get_sourcepp_keyvalues);
	ClassDB::bind_method(D_METHOD("set_sourcepp_outputs", "outputs"), &SourcePPBrushEntity3D::set_sourcepp_outputs);
	ClassDB::bind_method(D_METHOD("get_sourcepp_outputs"), &SourcePPBrushEntity3D::get_sourcepp_outputs);
	ClassDB::bind_method(D_METHOD("set_entity_enabled", "enabled"), &SourcePPBrushEntity3D::set_entity_enabled);
	ClassDB::bind_method(D_METHOD("is_entity_enabled"), &SourcePPBrushEntity3D::is_entity_enabled);
	ClassDB::bind_method(D_METHOD("enable"), &SourcePPBrushEntity3D::enable);
	ClassDB::bind_method(D_METHOD("disable"), &SourcePPBrushEntity3D::disable);
	ClassDB::bind_method(D_METHOD("toggle_enabled"), &SourcePPBrushEntity3D::toggle_enabled);
	ClassDB::bind_method(D_METHOD("fire_source_output", "output_name", "activator"), &SourcePPBrushEntity3D::fire_source_output, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("get_keyvalue_string", "key", "default"), &SourcePPBrushEntity3D::get_keyvalue_string, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_keyvalue_float", "key", "default"), &SourcePPBrushEntity3D::get_keyvalue_float, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("get_keyvalue_int", "key", "default"), &SourcePPBrushEntity3D::get_keyvalue_int, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_keyvalue_bool", "key", "default"), &SourcePPBrushEntity3D::get_keyvalue_bool, DEFVAL(false));

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "sourcepp_classname"), "set_sourcepp_classname", "get_sourcepp_classname");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "sourcepp_targetname"), "set_sourcepp_targetname", "get_sourcepp_targetname");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sourcepp_entity_index"), "set_sourcepp_entity_index", "get_sourcepp_entity_index");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sourcepp_model_index"), "set_sourcepp_model_index", "get_sourcepp_model_index");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "sourcepp_keyvalues"), "set_sourcepp_keyvalues", "get_sourcepp_keyvalues");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "sourcepp_outputs"), "set_sourcepp_outputs", "get_sourcepp_outputs");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "entity_enabled"), "set_entity_enabled", "is_entity_enabled");

	ADD_SIGNAL(MethodInfo("source_output_fired", PropertyInfo(Variant::STRING_NAME, "output_name"), PropertyInfo(Variant::OBJECT, "activator")));
	ADD_SIGNAL(MethodInfo("entity_enabled_changed", PropertyInfo(Variant::BOOL, "enabled")));
}

void SourcePPBrushEntity3D::_copy_entity_state_to(SourcePPBrushEntity3D *p_entity) const {
	p_entity->setup_sourcepp_entity(sourcepp_classname, sourcepp_targetname, sourcepp_entity_index, sourcepp_model_index, sourcepp_keyvalues, sourcepp_outputs);
	p_entity->set_entity_enabled(enabled);
}

void SourcePPBrushEntity3D::_sourcepp_enabled_changed() {
	_apply_visible_recursive(this, enabled);
}

void SourcePPBrushEntity3D::setup_sourcepp_entity(const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs) {
	sourcepp_classname = p_classname;
	sourcepp_targetname = p_targetname;
	sourcepp_entity_index = p_entity_index;
	sourcepp_model_index = p_model_index;
	sourcepp_keyvalues = p_keyvalues;
	sourcepp_outputs = p_outputs;
	set_meta("sourcepp_bsp_entity_classname", sourcepp_classname);
	set_meta("sourcepp_bsp_entity_targetname", sourcepp_targetname);
	set_meta("sourcepp_bsp_entity_index", sourcepp_entity_index);
	set_meta("sourcepp_bsp_model_index", sourcepp_model_index);
	set_meta("sourcepp_bsp_entity_keyvalues", sourcepp_keyvalues);
	set_meta("sourcepp_bsp_entity_outputs", sourcepp_outputs);
}

void SourcePPBrushEntity3D::set_sourcepp_classname(const String &p_classname) { sourcepp_classname = p_classname; }
String SourcePPBrushEntity3D::get_sourcepp_classname() const { return sourcepp_classname; }
void SourcePPBrushEntity3D::set_sourcepp_targetname(const String &p_targetname) { sourcepp_targetname = p_targetname; }
String SourcePPBrushEntity3D::get_sourcepp_targetname() const { return sourcepp_targetname; }
void SourcePPBrushEntity3D::set_sourcepp_entity_index(int p_entity_index) { sourcepp_entity_index = p_entity_index; }
int SourcePPBrushEntity3D::get_sourcepp_entity_index() const { return sourcepp_entity_index; }
void SourcePPBrushEntity3D::set_sourcepp_model_index(int p_model_index) { sourcepp_model_index = p_model_index; }
int SourcePPBrushEntity3D::get_sourcepp_model_index() const { return sourcepp_model_index; }
void SourcePPBrushEntity3D::set_sourcepp_keyvalues(const Dictionary &p_keyvalues) { sourcepp_keyvalues = p_keyvalues; }
Dictionary SourcePPBrushEntity3D::get_sourcepp_keyvalues() const { return sourcepp_keyvalues; }
void SourcePPBrushEntity3D::set_sourcepp_outputs(const Array &p_outputs) { sourcepp_outputs = p_outputs; }
Array SourcePPBrushEntity3D::get_sourcepp_outputs() const { return sourcepp_outputs; }

void SourcePPBrushEntity3D::set_entity_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	_sourcepp_enabled_changed();
	emit_signal("entity_enabled_changed", enabled);
}
bool SourcePPBrushEntity3D::is_entity_enabled() const { return enabled; }
void SourcePPBrushEntity3D::enable() { set_entity_enabled(true); }
void SourcePPBrushEntity3D::disable() { set_entity_enabled(false); }
void SourcePPBrushEntity3D::toggle_enabled() { set_entity_enabled(!enabled); }
void SourcePPBrushEntity3D::fire_source_output(const StringName &p_output_name, Object *p_activator) { emit_signal("source_output_fired", p_output_name, p_activator); }
String SourcePPBrushEntity3D::get_keyvalue_string(const StringName &p_key, const String &p_default) const { return _dict_string(sourcepp_keyvalues, p_key, p_default); }
double SourcePPBrushEntity3D::get_keyvalue_float(const StringName &p_key, double p_default) const { return _dict_float(sourcepp_keyvalues, p_key, p_default); }
int SourcePPBrushEntity3D::get_keyvalue_int(const StringName &p_key, int p_default) const { return _dict_int(sourcepp_keyvalues, p_key, p_default); }
bool SourcePPBrushEntity3D::get_keyvalue_bool(const StringName &p_key, bool p_default) const { return _dict_bool(sourcepp_keyvalues, p_key, p_default); }

void SourcePPBrushArea3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup_sourcepp_entity", "classname", "targetname", "entity_index", "model_index", "keyvalues", "outputs"), &SourcePPBrushArea3D::setup_sourcepp_entity);
	ClassDB::bind_method(D_METHOD("set_entity_enabled", "enabled"), &SourcePPBrushArea3D::set_entity_enabled);
	ClassDB::bind_method(D_METHOD("is_entity_enabled"), &SourcePPBrushArea3D::is_entity_enabled);
	ClassDB::bind_method(D_METHOD("enable"), &SourcePPBrushArea3D::enable);
	ClassDB::bind_method(D_METHOD("disable"), &SourcePPBrushArea3D::disable);
	ClassDB::bind_method(D_METHOD("toggle_enabled"), &SourcePPBrushArea3D::toggle_enabled);
	ClassDB::bind_method(D_METHOD("touch_test"), &SourcePPBrushArea3D::touch_test);
	ClassDB::bind_method(D_METHOD("fire_source_output", "output_name", "activator"), &SourcePPBrushArea3D::fire_source_output, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_sourcepp_classname", "classname"), &SourcePPBrushArea3D::set_sourcepp_classname);
	ClassDB::bind_method(D_METHOD("get_sourcepp_classname"), &SourcePPBrushArea3D::get_sourcepp_classname);
	ClassDB::bind_method(D_METHOD("set_sourcepp_targetname", "targetname"), &SourcePPBrushArea3D::set_sourcepp_targetname);
	ClassDB::bind_method(D_METHOD("get_sourcepp_targetname"), &SourcePPBrushArea3D::get_sourcepp_targetname);
	ClassDB::bind_method(D_METHOD("set_sourcepp_entity_index", "entity_index"), &SourcePPBrushArea3D::set_sourcepp_entity_index);
	ClassDB::bind_method(D_METHOD("get_sourcepp_entity_index"), &SourcePPBrushArea3D::get_sourcepp_entity_index);
	ClassDB::bind_method(D_METHOD("set_sourcepp_model_index", "model_index"), &SourcePPBrushArea3D::set_sourcepp_model_index);
	ClassDB::bind_method(D_METHOD("get_sourcepp_model_index"), &SourcePPBrushArea3D::get_sourcepp_model_index);
	ClassDB::bind_method(D_METHOD("set_sourcepp_keyvalues", "keyvalues"), &SourcePPBrushArea3D::set_sourcepp_keyvalues);
	ClassDB::bind_method(D_METHOD("get_sourcepp_keyvalues"), &SourcePPBrushArea3D::get_sourcepp_keyvalues);
	ClassDB::bind_method(D_METHOD("set_sourcepp_outputs", "outputs"), &SourcePPBrushArea3D::set_sourcepp_outputs);
	ClassDB::bind_method(D_METHOD("get_sourcepp_outputs"), &SourcePPBrushArea3D::get_sourcepp_outputs);
	ClassDB::bind_method(D_METHOD("get_keyvalue_string", "key", "default"), &SourcePPBrushArea3D::get_keyvalue_string, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_keyvalue_float", "key", "default"), &SourcePPBrushArea3D::get_keyvalue_float, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("get_keyvalue_int", "key", "default"), &SourcePPBrushArea3D::get_keyvalue_int, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_keyvalue_bool", "key", "default"), &SourcePPBrushArea3D::get_keyvalue_bool, DEFVAL(false));

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "sourcepp_classname"), "set_sourcepp_classname", "get_sourcepp_classname");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "sourcepp_targetname"), "set_sourcepp_targetname", "get_sourcepp_targetname");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sourcepp_entity_index"), "set_sourcepp_entity_index", "get_sourcepp_entity_index");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sourcepp_model_index"), "set_sourcepp_model_index", "get_sourcepp_model_index");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "sourcepp_keyvalues"), "set_sourcepp_keyvalues", "get_sourcepp_keyvalues");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "sourcepp_outputs"), "set_sourcepp_outputs", "get_sourcepp_outputs");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "entity_enabled"), "set_entity_enabled", "is_entity_enabled");

	ADD_SIGNAL(MethodInfo("source_output_fired", PropertyInfo(Variant::STRING_NAME, "output_name"), PropertyInfo(Variant::OBJECT, "activator")));
	ADD_SIGNAL(MethodInfo("source_start_touch", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NODE_TYPE, "Node3D")));
	ADD_SIGNAL(MethodInfo("source_end_touch", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NODE_TYPE, "Node3D")));
	ADD_SIGNAL(MethodInfo("source_start_touch_all", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NODE_TYPE, "Node3D")));
	ADD_SIGNAL(MethodInfo("source_end_touch_all", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NODE_TYPE, "Node3D")));
	ADD_SIGNAL(MethodInfo("source_touching"));
	ADD_SIGNAL(MethodInfo("source_not_touching"));
	ADD_SIGNAL(MethodInfo("entity_enabled_changed", PropertyInfo(Variant::BOOL, "enabled")));
}

void SourcePPBrushArea3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE && !connected_body_signals) {
		connect("body_entered", callable_mp(this, &SourcePPBrushArea3D::_sourcepp_body_entered));
		connect("body_exited", callable_mp(this, &SourcePPBrushArea3D::_sourcepp_body_exited));
		connected_body_signals = true;
	}
}

void SourcePPBrushArea3D::_sourcepp_body_entered(Node3D *p_body) {
	if (!enabled) {
		return;
	}
	const bool was_empty = touching_count == 0;
	touching_count++;
	emit_signal("source_start_touch", p_body);
	fire_source_output("OnStartTouch", p_body);
	if (was_empty) {
		emit_signal("source_start_touch_all", p_body);
		fire_source_output("OnStartTouchAll", p_body);
	}
	_sourcepp_start_touch(p_body);
}

void SourcePPBrushArea3D::_sourcepp_body_exited(Node3D *p_body) {
	if (touching_count > 0) {
		touching_count--;
	}
	emit_signal("source_end_touch", p_body);
	fire_source_output("OnEndTouch", p_body);
	if (touching_count == 0) {
		emit_signal("source_end_touch_all", p_body);
		fire_source_output("OnEndTouchAll", p_body);
	}
	_sourcepp_end_touch(p_body);
}

void SourcePPBrushArea3D::_sourcepp_enabled_changed() {
	set_monitoring(enabled);
	set_monitorable(enabled);
}

void SourcePPBrushArea3D::_sourcepp_start_touch(Node3D *p_body) {}
void SourcePPBrushArea3D::_sourcepp_end_touch(Node3D *p_body) {}

void SourcePPBrushArea3D::setup_sourcepp_entity(const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs) {
	sourcepp_classname = p_classname;
	sourcepp_targetname = p_targetname;
	sourcepp_entity_index = p_entity_index;
	sourcepp_model_index = p_model_index;
	sourcepp_keyvalues = p_keyvalues;
	sourcepp_outputs = p_outputs;
	set_meta("sourcepp_bsp_entity_classname", sourcepp_classname);
	set_meta("sourcepp_bsp_entity_targetname", sourcepp_targetname);
	set_meta("sourcepp_bsp_entity_index", sourcepp_entity_index);
	set_meta("sourcepp_bsp_model_index", sourcepp_model_index);
	set_meta("sourcepp_bsp_entity_keyvalues", sourcepp_keyvalues);
	set_meta("sourcepp_bsp_entity_outputs", sourcepp_outputs);
}

void SourcePPBrushArea3D::set_sourcepp_classname(const String &p_classname) { sourcepp_classname = p_classname; }
String SourcePPBrushArea3D::get_sourcepp_classname() const { return sourcepp_classname; }
void SourcePPBrushArea3D::set_sourcepp_targetname(const String &p_targetname) { sourcepp_targetname = p_targetname; }
String SourcePPBrushArea3D::get_sourcepp_targetname() const { return sourcepp_targetname; }
void SourcePPBrushArea3D::set_sourcepp_entity_index(int p_entity_index) { sourcepp_entity_index = p_entity_index; }
int SourcePPBrushArea3D::get_sourcepp_entity_index() const { return sourcepp_entity_index; }
void SourcePPBrushArea3D::set_sourcepp_model_index(int p_model_index) { sourcepp_model_index = p_model_index; }
int SourcePPBrushArea3D::get_sourcepp_model_index() const { return sourcepp_model_index; }
void SourcePPBrushArea3D::set_sourcepp_keyvalues(const Dictionary &p_keyvalues) { sourcepp_keyvalues = p_keyvalues; }
Dictionary SourcePPBrushArea3D::get_sourcepp_keyvalues() const { return sourcepp_keyvalues; }
void SourcePPBrushArea3D::set_sourcepp_outputs(const Array &p_outputs) { sourcepp_outputs = p_outputs; }
Array SourcePPBrushArea3D::get_sourcepp_outputs() const { return sourcepp_outputs; }
void SourcePPBrushArea3D::set_entity_enabled(bool p_enabled) { if (enabled != p_enabled) { enabled = p_enabled; _sourcepp_enabled_changed(); emit_signal("entity_enabled_changed", enabled); } }
bool SourcePPBrushArea3D::is_entity_enabled() const { return enabled; }
void SourcePPBrushArea3D::enable() { set_entity_enabled(true); }
void SourcePPBrushArea3D::disable() { set_entity_enabled(false); }
void SourcePPBrushArea3D::toggle_enabled() { set_entity_enabled(!enabled); }
void SourcePPBrushArea3D::touch_test() { emit_signal(touching_count > 0 ? "source_touching" : "source_not_touching"); fire_source_output(touching_count > 0 ? "OnTouching" : "OnNotTouching", this); }
void SourcePPBrushArea3D::fire_source_output(const StringName &p_output_name, Object *p_activator) { emit_signal("source_output_fired", p_output_name, p_activator); }
String SourcePPBrushArea3D::get_keyvalue_string(const StringName &p_key, const String &p_default) const { return _dict_string(sourcepp_keyvalues, p_key, p_default); }
double SourcePPBrushArea3D::get_keyvalue_float(const StringName &p_key, double p_default) const { return _dict_float(sourcepp_keyvalues, p_key, p_default); }
int SourcePPBrushArea3D::get_keyvalue_int(const StringName &p_key, int p_default) const { return _dict_int(sourcepp_keyvalues, p_key, p_default); }
bool SourcePPBrushArea3D::get_keyvalue_bool(const StringName &p_key, bool p_default) const { return _dict_bool(sourcepp_keyvalues, p_key, p_default); }

void SourcePPBrushBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("setup_sourcepp_entity", "classname", "targetname", "entity_index", "model_index", "keyvalues", "outputs"), &SourcePPBrushBody3D::setup_sourcepp_entity);
	ClassDB::bind_method(D_METHOD("set_entity_enabled", "enabled"), &SourcePPBrushBody3D::set_entity_enabled);
	ClassDB::bind_method(D_METHOD("is_entity_enabled"), &SourcePPBrushBody3D::is_entity_enabled);
	ClassDB::bind_method(D_METHOD("enable"), &SourcePPBrushBody3D::enable);
	ClassDB::bind_method(D_METHOD("disable"), &SourcePPBrushBody3D::disable);
	ClassDB::bind_method(D_METHOD("toggle_enabled"), &SourcePPBrushBody3D::toggle_enabled);
	ClassDB::bind_method(D_METHOD("fire_source_output", "output_name", "activator"), &SourcePPBrushBody3D::fire_source_output, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_sourcepp_classname", "classname"), &SourcePPBrushBody3D::set_sourcepp_classname);
	ClassDB::bind_method(D_METHOD("get_sourcepp_classname"), &SourcePPBrushBody3D::get_sourcepp_classname);
	ClassDB::bind_method(D_METHOD("set_sourcepp_targetname", "targetname"), &SourcePPBrushBody3D::set_sourcepp_targetname);
	ClassDB::bind_method(D_METHOD("get_sourcepp_targetname"), &SourcePPBrushBody3D::get_sourcepp_targetname);
	ClassDB::bind_method(D_METHOD("set_sourcepp_entity_index", "entity_index"), &SourcePPBrushBody3D::set_sourcepp_entity_index);
	ClassDB::bind_method(D_METHOD("get_sourcepp_entity_index"), &SourcePPBrushBody3D::get_sourcepp_entity_index);
	ClassDB::bind_method(D_METHOD("set_sourcepp_model_index", "model_index"), &SourcePPBrushBody3D::set_sourcepp_model_index);
	ClassDB::bind_method(D_METHOD("get_sourcepp_model_index"), &SourcePPBrushBody3D::get_sourcepp_model_index);
	ClassDB::bind_method(D_METHOD("set_sourcepp_keyvalues", "keyvalues"), &SourcePPBrushBody3D::set_sourcepp_keyvalues);
	ClassDB::bind_method(D_METHOD("get_sourcepp_keyvalues"), &SourcePPBrushBody3D::get_sourcepp_keyvalues);
	ClassDB::bind_method(D_METHOD("set_sourcepp_outputs", "outputs"), &SourcePPBrushBody3D::set_sourcepp_outputs);
	ClassDB::bind_method(D_METHOD("get_sourcepp_outputs"), &SourcePPBrushBody3D::get_sourcepp_outputs);
	ClassDB::bind_method(D_METHOD("get_keyvalue_string", "key", "default"), &SourcePPBrushBody3D::get_keyvalue_string, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_keyvalue_float", "key", "default"), &SourcePPBrushBody3D::get_keyvalue_float, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("get_keyvalue_int", "key", "default"), &SourcePPBrushBody3D::get_keyvalue_int, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_keyvalue_bool", "key", "default"), &SourcePPBrushBody3D::get_keyvalue_bool, DEFVAL(false));
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "sourcepp_classname"), "set_sourcepp_classname", "get_sourcepp_classname");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "sourcepp_targetname"), "set_sourcepp_targetname", "get_sourcepp_targetname");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sourcepp_entity_index"), "set_sourcepp_entity_index", "get_sourcepp_entity_index");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sourcepp_model_index"), "set_sourcepp_model_index", "get_sourcepp_model_index");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "sourcepp_keyvalues"), "set_sourcepp_keyvalues", "get_sourcepp_keyvalues");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "sourcepp_outputs"), "set_sourcepp_outputs", "get_sourcepp_outputs");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "entity_enabled"), "set_entity_enabled", "is_entity_enabled");
	ADD_SIGNAL(MethodInfo("source_output_fired", PropertyInfo(Variant::STRING_NAME, "output_name"), PropertyInfo(Variant::OBJECT, "activator")));
	ADD_SIGNAL(MethodInfo("entity_enabled_changed", PropertyInfo(Variant::BOOL, "enabled")));
}

void SourcePPBrushBody3D::_sourcepp_enabled_changed() {
	_apply_visible_recursive(this, enabled);
	_apply_collision_disabled_recursive(this, !enabled);
}
void SourcePPBrushBody3D::setup_sourcepp_entity(const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs) { sourcepp_classname = p_classname; sourcepp_targetname = p_targetname; sourcepp_entity_index = p_entity_index; sourcepp_model_index = p_model_index; sourcepp_keyvalues = p_keyvalues; sourcepp_outputs = p_outputs; set_meta("sourcepp_bsp_entity_classname", sourcepp_classname); set_meta("sourcepp_bsp_entity_targetname", sourcepp_targetname); set_meta("sourcepp_bsp_entity_index", sourcepp_entity_index); set_meta("sourcepp_bsp_model_index", sourcepp_model_index); set_meta("sourcepp_bsp_entity_keyvalues", sourcepp_keyvalues); set_meta("sourcepp_bsp_entity_outputs", sourcepp_outputs); }
void SourcePPBrushBody3D::set_sourcepp_classname(const String &p_classname) { sourcepp_classname = p_classname; }
String SourcePPBrushBody3D::get_sourcepp_classname() const { return sourcepp_classname; }
void SourcePPBrushBody3D::set_sourcepp_targetname(const String &p_targetname) { sourcepp_targetname = p_targetname; }
String SourcePPBrushBody3D::get_sourcepp_targetname() const { return sourcepp_targetname; }
void SourcePPBrushBody3D::set_sourcepp_entity_index(int p_entity_index) { sourcepp_entity_index = p_entity_index; }
int SourcePPBrushBody3D::get_sourcepp_entity_index() const { return sourcepp_entity_index; }
void SourcePPBrushBody3D::set_sourcepp_model_index(int p_model_index) { sourcepp_model_index = p_model_index; }
int SourcePPBrushBody3D::get_sourcepp_model_index() const { return sourcepp_model_index; }
void SourcePPBrushBody3D::set_sourcepp_keyvalues(const Dictionary &p_keyvalues) { sourcepp_keyvalues = p_keyvalues; }
Dictionary SourcePPBrushBody3D::get_sourcepp_keyvalues() const { return sourcepp_keyvalues; }
void SourcePPBrushBody3D::set_sourcepp_outputs(const Array &p_outputs) { sourcepp_outputs = p_outputs; }
Array SourcePPBrushBody3D::get_sourcepp_outputs() const { return sourcepp_outputs; }
void SourcePPBrushBody3D::set_entity_enabled(bool p_enabled) { if (enabled != p_enabled) { enabled = p_enabled; _sourcepp_enabled_changed(); emit_signal("entity_enabled_changed", enabled); } }
bool SourcePPBrushBody3D::is_entity_enabled() const { return enabled; }
void SourcePPBrushBody3D::enable() { set_entity_enabled(true); }
void SourcePPBrushBody3D::disable() { set_entity_enabled(false); }
void SourcePPBrushBody3D::toggle_enabled() { set_entity_enabled(!enabled); }
void SourcePPBrushBody3D::fire_source_output(const StringName &p_output_name, Object *p_activator) { emit_signal("source_output_fired", p_output_name, p_activator); }
String SourcePPBrushBody3D::get_keyvalue_string(const StringName &p_key, const String &p_default) const { return _dict_string(sourcepp_keyvalues, p_key, p_default); }
double SourcePPBrushBody3D::get_keyvalue_float(const StringName &p_key, double p_default) const { return _dict_float(sourcepp_keyvalues, p_key, p_default); }
int SourcePPBrushBody3D::get_keyvalue_int(const StringName &p_key, int p_default) const { return _dict_int(sourcepp_keyvalues, p_key, p_default); }
bool SourcePPBrushBody3D::get_keyvalue_bool(const StringName &p_key, bool p_default) const { return _dict_bool(sourcepp_keyvalues, p_key, p_default); }

void SourcePPTriggerMultiple3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_wait", "wait"), &SourcePPTriggerMultiple3D::set_wait);
	ClassDB::bind_method(D_METHOD("get_wait"), &SourcePPTriggerMultiple3D::get_wait);
	ClassDB::bind_method(D_METHOD("trigger", "activator"), &SourcePPTriggerMultiple3D::trigger);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wait"), "set_wait", "get_wait");
	ADD_SIGNAL(MethodInfo("source_trigger", PropertyInfo(Variant::OBJECT, "activator", PROPERTY_HINT_NODE_TYPE, "Node3D")));
}

void SourcePPTriggerMultiple3D::_sourcepp_start_touch(Node3D *p_body) { trigger(p_body); }
void SourcePPTriggerMultiple3D::set_wait(double p_wait) { wait = p_wait == 0.0 ? 0.2 : p_wait; }
double SourcePPTriggerMultiple3D::get_wait() const { return wait; }
void SourcePPTriggerMultiple3D::trigger(Node3D *p_activator) {
	if (!is_entity_enabled()) {
		return;
	}
	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (wait > 0.0 && last_trigger_msec != 0 && now < last_trigger_msec + static_cast<uint64_t>(wait * 1000.0)) {
		return;
	}
	last_trigger_msec = now;
	emit_signal("source_trigger", p_activator);
	fire_source_output("OnTrigger", p_activator);
	if (wait < 0.0) {
		disable();
	}
}

void SourcePPTriggerOnce3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("reset_once"), &SourcePPTriggerOnce3D::reset_once);
	ClassDB::bind_method(D_METHOD("has_triggered"), &SourcePPTriggerOnce3D::has_triggered);
}

void SourcePPTriggerOnce3D::_sourcepp_start_touch(Node3D *p_body) {
	if (triggered) {
		return;
	}
	triggered = true;
	trigger(p_body);
	disable();
}
void SourcePPTriggerOnce3D::reset_once() { triggered = false; enable(); }
bool SourcePPTriggerOnce3D::has_triggered() const { return triggered; }

void SourcePPTriggerHurt3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_damage", "damage"), &SourcePPTriggerHurt3D::set_damage);
	ClassDB::bind_method(D_METHOD("get_damage"), &SourcePPTriggerHurt3D::get_damage);
	ClassDB::bind_method(D_METHOD("set_damage_cap", "damage_cap"), &SourcePPTriggerHurt3D::set_damage_cap);
	ClassDB::bind_method(D_METHOD("get_damage_cap"), &SourcePPTriggerHurt3D::get_damage_cap);
	ClassDB::bind_method(D_METHOD("set_damage_type", "damage_type"), &SourcePPTriggerHurt3D::set_damage_type);
	ClassDB::bind_method(D_METHOD("get_damage_type"), &SourcePPTriggerHurt3D::get_damage_type);
	ClassDB::bind_method(D_METHOD("set_damage_model", "damage_model"), &SourcePPTriggerHurt3D::set_damage_model);
	ClassDB::bind_method(D_METHOD("get_damage_model"), &SourcePPTriggerHurt3D::get_damage_model);
	ClassDB::bind_method(D_METHOD("set_no_damage_force", "no_damage_force"), &SourcePPTriggerHurt3D::set_no_damage_force);
	ClassDB::bind_method(D_METHOD("is_no_damage_force_enabled"), &SourcePPTriggerHurt3D::is_no_damage_force_enabled);
	ClassDB::bind_method(D_METHOD("set_hurt_interval", "hurt_interval"), &SourcePPTriggerHurt3D::set_hurt_interval);
	ClassDB::bind_method(D_METHOD("get_hurt_interval"), &SourcePPTriggerHurt3D::get_hurt_interval);
	ClassDB::bind_method(D_METHOD("request_hurt", "body"), &SourcePPTriggerHurt3D::request_hurt);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damage"), "set_damage", "get_damage");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damage_cap"), "set_damage_cap", "get_damage_cap");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "damage_type"), "set_damage_type", "get_damage_type");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "damage_model"), "set_damage_model", "get_damage_model");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "no_damage_force"), "set_no_damage_force", "is_no_damage_force_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hurt_interval"), "set_hurt_interval", "get_hurt_interval");
	ADD_SIGNAL(MethodInfo("hurt_requested", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NODE_TYPE, "Node3D"), PropertyInfo(Variant::FLOAT, "damage"), PropertyInfo(Variant::INT, "damage_type")));
	ADD_SIGNAL(MethodInfo("hurt_player_requested", PropertyInfo(Variant::OBJECT, "body", PROPERTY_HINT_NODE_TYPE, "Node3D"), PropertyInfo(Variant::FLOAT, "damage"), PropertyInfo(Variant::INT, "damage_type")));
}

void SourcePPTriggerHurt3D::_sourcepp_start_touch(Node3D *p_body) { request_hurt(p_body); }
void SourcePPTriggerHurt3D::set_damage(double p_damage) { damage = p_damage; }
double SourcePPTriggerHurt3D::get_damage() const { return damage; }
void SourcePPTriggerHurt3D::set_damage_cap(double p_damage_cap) { damage_cap = p_damage_cap; }
double SourcePPTriggerHurt3D::get_damage_cap() const { return damage_cap; }
void SourcePPTriggerHurt3D::set_damage_type(int p_damage_type) { damage_type = p_damage_type; }
int SourcePPTriggerHurt3D::get_damage_type() const { return damage_type; }
void SourcePPTriggerHurt3D::set_damage_model(int p_damage_model) { damage_model = p_damage_model; }
int SourcePPTriggerHurt3D::get_damage_model() const { return damage_model; }
void SourcePPTriggerHurt3D::set_no_damage_force(bool p_no_damage_force) { no_damage_force = p_no_damage_force; }
bool SourcePPTriggerHurt3D::is_no_damage_force_enabled() const { return no_damage_force; }
void SourcePPTriggerHurt3D::set_hurt_interval(double p_hurt_interval) { hurt_interval = MAX(0.0, p_hurt_interval); }
double SourcePPTriggerHurt3D::get_hurt_interval() const { return hurt_interval; }
void SourcePPTriggerHurt3D::request_hurt(Node3D *p_body) { emit_signal("hurt_requested", p_body, damage, damage_type); emit_signal("hurt_player_requested", p_body, damage, damage_type); fire_source_output("OnHurt", p_body); }

void SourcePPFuncBrush3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_solidity", "solidity"), &SourcePPFuncBrush3D::set_solidity);
	ClassDB::bind_method(D_METHOD("get_solidity"), &SourcePPFuncBrush3D::get_solidity);
	ClassDB::bind_method(D_METHOD("set_solid_bsp", "solid_bsp"), &SourcePPFuncBrush3D::set_solid_bsp);
	ClassDB::bind_method(D_METHOD("is_solid_bsp_enabled"), &SourcePPFuncBrush3D::is_solid_bsp_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "solidity"), "set_solidity", "get_solidity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "solid_bsp"), "set_solid_bsp", "is_solid_bsp_enabled");
}

void SourcePPFuncBrush3D::_sourcepp_enabled_changed() {
	_apply_visible_recursive(this, is_entity_enabled());
	const bool never_solid = solidity == 1;
	const bool always_solid = solidity == 2;
	_apply_collision_disabled_recursive(this, never_solid || (!always_solid && !is_entity_enabled()));
}
void SourcePPFuncBrush3D::set_solidity(int p_solidity) {
	solidity = p_solidity;
	_sourcepp_enabled_changed();
}
int SourcePPFuncBrush3D::get_solidity() const { return solidity; }
void SourcePPFuncBrush3D::set_solid_bsp(bool p_solid_bsp) { solid_bsp = p_solid_bsp; }
bool SourcePPFuncBrush3D::is_solid_bsp_enabled() const { return solid_bsp; }

void SourcePPFuncDoor3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_move_direction", "move_direction"), &SourcePPFuncDoor3D::set_move_direction);
	ClassDB::bind_method(D_METHOD("get_move_direction"), &SourcePPFuncDoor3D::get_move_direction);
	ClassDB::bind_method(D_METHOD("set_speed", "speed"), &SourcePPFuncDoor3D::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &SourcePPFuncDoor3D::get_speed);
	ClassDB::bind_method(D_METHOD("set_wait", "wait"), &SourcePPFuncDoor3D::set_wait);
	ClassDB::bind_method(D_METHOD("get_wait"), &SourcePPFuncDoor3D::get_wait);
	ClassDB::bind_method(D_METHOD("set_lip", "lip"), &SourcePPFuncDoor3D::set_lip);
	ClassDB::bind_method(D_METHOD("get_lip"), &SourcePPFuncDoor3D::get_lip);
	ClassDB::bind_method(D_METHOD("set_spawn_position", "spawn_position"), &SourcePPFuncDoor3D::set_spawn_position);
	ClassDB::bind_method(D_METHOD("get_spawn_position"), &SourcePPFuncDoor3D::get_spawn_position);
	ClassDB::bind_method(D_METHOD("set_locked", "locked"), &SourcePPFuncDoor3D::set_locked);
	ClassDB::bind_method(D_METHOD("is_locked"), &SourcePPFuncDoor3D::is_locked);
	ClassDB::bind_method(D_METHOD("set_open", "open"), &SourcePPFuncDoor3D::set_open);
	ClassDB::bind_method(D_METHOD("is_open"), &SourcePPFuncDoor3D::is_open);
	ClassDB::bind_method(D_METHOD("open", "activator"), &SourcePPFuncDoor3D::open, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("close", "activator"), &SourcePPFuncDoor3D::close, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("toggle", "activator"), &SourcePPFuncDoor3D::toggle, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("lock"), &SourcePPFuncDoor3D::lock);
	ClassDB::bind_method(D_METHOD("unlock"), &SourcePPFuncDoor3D::unlock);
	ADD_SIGNAL(MethodInfo("source_opened", PropertyInfo(Variant::OBJECT, "activator")));
	ADD_SIGNAL(MethodInfo("source_closed", PropertyInfo(Variant::OBJECT, "activator")));
	ADD_SIGNAL(MethodInfo("source_locked_use", PropertyInfo(Variant::OBJECT, "activator")));
}

void SourcePPFuncDoor3D::set_move_direction(const Vector3 &p_move_direction) { move_direction = p_move_direction; }
Vector3 SourcePPFuncDoor3D::get_move_direction() const { return move_direction; }
void SourcePPFuncDoor3D::set_speed(double p_speed) { speed = p_speed; }
double SourcePPFuncDoor3D::get_speed() const { return speed; }
void SourcePPFuncDoor3D::set_wait(double p_wait) { wait = p_wait; }
double SourcePPFuncDoor3D::get_wait() const { return wait; }
void SourcePPFuncDoor3D::set_lip(double p_lip) { lip = p_lip; }
double SourcePPFuncDoor3D::get_lip() const { return lip; }
void SourcePPFuncDoor3D::set_spawn_position(int p_spawn_position) { spawn_position = p_spawn_position; open_state = spawn_position == 1; }
int SourcePPFuncDoor3D::get_spawn_position() const { return spawn_position; }
void SourcePPFuncDoor3D::set_locked(bool p_locked) { locked = p_locked; }
bool SourcePPFuncDoor3D::is_locked() const { return locked; }
void SourcePPFuncDoor3D::set_open(bool p_open) { open_state = p_open; }
bool SourcePPFuncDoor3D::is_open() const { return open_state; }
void SourcePPFuncDoor3D::open(Object *p_activator) { if (locked) { emit_signal("source_locked_use", p_activator); fire_source_output("OnLockedUse", p_activator); return; } open_state = true; emit_signal("source_opened", p_activator); fire_source_output("OnOpen", p_activator); fire_source_output("OnFullyOpen", p_activator); }
void SourcePPFuncDoor3D::close(Object *p_activator) { open_state = false; emit_signal("source_closed", p_activator); fire_source_output("OnClose", p_activator); fire_source_output("OnFullyClosed", p_activator); }
void SourcePPFuncDoor3D::toggle(Object *p_activator) { is_open() ? close(p_activator) : open(p_activator); }
void SourcePPFuncDoor3D::lock() { locked = true; }
void SourcePPFuncDoor3D::unlock() { locked = false; }

void SourcePPFuncIllusionary3D::_bind_methods() {}

void SourcePPLadder3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_point0", "point"), &SourcePPLadder3D::set_point0);
	ClassDB::bind_method(D_METHOD("get_point0"), &SourcePPLadder3D::get_point0);
	ClassDB::bind_method(D_METHOD("set_point1", "point"), &SourcePPLadder3D::set_point1);
	ClassDB::bind_method(D_METHOD("get_point1"), &SourcePPLadder3D::get_point1);
	ClassDB::bind_method(D_METHOD("set_ladder_surface_properties", "surface_properties"), &SourcePPLadder3D::set_ladder_surface_properties);
	ClassDB::bind_method(D_METHOD("get_ladder_surface_properties"), &SourcePPLadder3D::get_ladder_surface_properties);
	ClassDB::bind_method(D_METHOD("set_fake_ladder", "fake_ladder"), &SourcePPLadder3D::set_fake_ladder);
	ClassDB::bind_method(D_METHOD("is_fake_ladder"), &SourcePPLadder3D::is_fake_ladder);
	ClassDB::bind_method(D_METHOD("player_got_on", "player"), &SourcePPLadder3D::player_got_on);
	ClassDB::bind_method(D_METHOD("player_got_off", "player"), &SourcePPLadder3D::player_got_off);
	ADD_SIGNAL(MethodInfo("player_got_on_ladder", PropertyInfo(Variant::OBJECT, "player", PROPERTY_HINT_NODE_TYPE, "Node3D")));
	ADD_SIGNAL(MethodInfo("player_got_off_ladder", PropertyInfo(Variant::OBJECT, "player", PROPERTY_HINT_NODE_TYPE, "Node3D")));
}

void SourcePPLadder3D::set_point0(const Vector3 &p_point) { point0 = p_point; }
Vector3 SourcePPLadder3D::get_point0() const { return point0; }
void SourcePPLadder3D::set_point1(const Vector3 &p_point) { point1 = p_point; }
Vector3 SourcePPLadder3D::get_point1() const { return point1; }
void SourcePPLadder3D::set_ladder_surface_properties(const String &p_surface_properties) { ladder_surface_properties = p_surface_properties; }
String SourcePPLadder3D::get_ladder_surface_properties() const { return ladder_surface_properties; }
void SourcePPLadder3D::set_fake_ladder(bool p_fake_ladder) { fake_ladder = p_fake_ladder; }
bool SourcePPLadder3D::is_fake_ladder() const { return fake_ladder; }
void SourcePPLadder3D::player_got_on(Node3D *p_player) { emit_signal("player_got_on_ladder", p_player); fire_source_output("OnPlayerGotOnLadder", p_player); }
void SourcePPLadder3D::player_got_off(Node3D *p_player) { emit_signal("player_got_off_ladder", p_player); fire_source_output("OnPlayerGotOffLadder", p_player); }

void SourcePPLadderDismount3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_ladder_target", "target"), &SourcePPLadderDismount3D::set_ladder_target);
	ClassDB::bind_method(D_METHOD("get_ladder_target"), &SourcePPLadderDismount3D::get_ladder_target);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "ladder_target"), "set_ladder_target", "get_ladder_target");
}

void SourcePPLadderDismount3D::set_ladder_target(const String &p_target) { ladder_target = p_target; }
String SourcePPLadderDismount3D::get_ladder_target() const { return ladder_target; }
