/**************************************************************************/
/*  sourcepp_bsp_entity.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/animatable_body_3d.h"
#include "scene/3d/physics/area_3d.h"

class SourcePPBrushEntity3D : public Node3D {
	GDCLASS(SourcePPBrushEntity3D, Node3D);

	String sourcepp_classname;
	String sourcepp_targetname;
	int sourcepp_entity_index = -1;
	int sourcepp_model_index = -1;
	Dictionary sourcepp_keyvalues;
	Array sourcepp_outputs;
	bool enabled = true;

protected:
	static void _bind_methods();
	void _copy_entity_state_to(SourcePPBrushEntity3D *p_entity) const;
	virtual void _sourcepp_enabled_changed();

public:
	void setup_sourcepp_entity(const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs);

	void set_sourcepp_classname(const String &p_classname);
	String get_sourcepp_classname() const;
	void set_sourcepp_targetname(const String &p_targetname);
	String get_sourcepp_targetname() const;
	void set_sourcepp_entity_index(int p_entity_index);
	int get_sourcepp_entity_index() const;
	void set_sourcepp_model_index(int p_model_index);
	int get_sourcepp_model_index() const;
	void set_sourcepp_keyvalues(const Dictionary &p_keyvalues);
	Dictionary get_sourcepp_keyvalues() const;
	void set_sourcepp_outputs(const Array &p_outputs);
	Array get_sourcepp_outputs() const;

	void set_entity_enabled(bool p_enabled);
	bool is_entity_enabled() const;
	void enable();
	void disable();
	void toggle_enabled();

	void fire_source_output(const StringName &p_output_name, Object *p_activator = nullptr);

	String get_keyvalue_string(const StringName &p_key, const String &p_default = String()) const;
	double get_keyvalue_float(const StringName &p_key, double p_default = 0.0) const;
	int get_keyvalue_int(const StringName &p_key, int p_default = 0) const;
	bool get_keyvalue_bool(const StringName &p_key, bool p_default = false) const;
};

class SourcePPBrushArea3D : public Area3D {
	GDCLASS(SourcePPBrushArea3D, Area3D);

	String sourcepp_classname;
	String sourcepp_targetname;
	int sourcepp_entity_index = -1;
	int sourcepp_model_index = -1;
	Dictionary sourcepp_keyvalues;
	Array sourcepp_outputs;
	bool enabled = true;
	bool connected_body_signals = false;
	int touching_count = 0;

	void _sourcepp_body_entered(Node3D *p_body);
	void _sourcepp_body_exited(Node3D *p_body);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _sourcepp_enabled_changed();
	virtual void _sourcepp_start_touch(Node3D *p_body);
	virtual void _sourcepp_end_touch(Node3D *p_body);

public:
	void setup_sourcepp_entity(const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs);

	void set_sourcepp_classname(const String &p_classname);
	String get_sourcepp_classname() const;
	void set_sourcepp_targetname(const String &p_targetname);
	String get_sourcepp_targetname() const;
	void set_sourcepp_entity_index(int p_entity_index);
	int get_sourcepp_entity_index() const;
	void set_sourcepp_model_index(int p_model_index);
	int get_sourcepp_model_index() const;
	void set_sourcepp_keyvalues(const Dictionary &p_keyvalues);
	Dictionary get_sourcepp_keyvalues() const;
	void set_sourcepp_outputs(const Array &p_outputs);
	Array get_sourcepp_outputs() const;

	void set_entity_enabled(bool p_enabled);
	bool is_entity_enabled() const;
	void enable();
	void disable();
	void toggle_enabled();
	void touch_test();

	void fire_source_output(const StringName &p_output_name, Object *p_activator = nullptr);
	String get_keyvalue_string(const StringName &p_key, const String &p_default = String()) const;
	double get_keyvalue_float(const StringName &p_key, double p_default = 0.0) const;
	int get_keyvalue_int(const StringName &p_key, int p_default = 0) const;
	bool get_keyvalue_bool(const StringName &p_key, bool p_default = false) const;
};

class SourcePPBrushBody3D : public AnimatableBody3D {
	GDCLASS(SourcePPBrushBody3D, AnimatableBody3D);

	String sourcepp_classname;
	String sourcepp_targetname;
	int sourcepp_entity_index = -1;
	int sourcepp_model_index = -1;
	Dictionary sourcepp_keyvalues;
	Array sourcepp_outputs;
	bool enabled = true;

protected:
	static void _bind_methods();
	virtual void _sourcepp_enabled_changed();

public:
	void setup_sourcepp_entity(const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs);

	void set_sourcepp_classname(const String &p_classname);
	String get_sourcepp_classname() const;
	void set_sourcepp_targetname(const String &p_targetname);
	String get_sourcepp_targetname() const;
	void set_sourcepp_entity_index(int p_entity_index);
	int get_sourcepp_entity_index() const;
	void set_sourcepp_model_index(int p_model_index);
	int get_sourcepp_model_index() const;
	void set_sourcepp_keyvalues(const Dictionary &p_keyvalues);
	Dictionary get_sourcepp_keyvalues() const;
	void set_sourcepp_outputs(const Array &p_outputs);
	Array get_sourcepp_outputs() const;

	void set_entity_enabled(bool p_enabled);
	bool is_entity_enabled() const;
	void enable();
	void disable();
	void toggle_enabled();

	void fire_source_output(const StringName &p_output_name, Object *p_activator = nullptr);
	String get_keyvalue_string(const StringName &p_key, const String &p_default = String()) const;
	double get_keyvalue_float(const StringName &p_key, double p_default = 0.0) const;
	int get_keyvalue_int(const StringName &p_key, int p_default = 0) const;
	bool get_keyvalue_bool(const StringName &p_key, bool p_default = false) const;
};

class SourcePPTriggerMultiple3D : public SourcePPBrushArea3D {
	GDCLASS(SourcePPTriggerMultiple3D, SourcePPBrushArea3D);

	double wait = 0.2;
	uint64_t last_trigger_msec = 0;

protected:
	static void _bind_methods();
	virtual void _sourcepp_start_touch(Node3D *p_body) override;

public:
	void set_wait(double p_wait);
	double get_wait() const;
	void trigger(Node3D *p_activator);
};

class SourcePPTriggerOnce3D : public SourcePPTriggerMultiple3D {
	GDCLASS(SourcePPTriggerOnce3D, SourcePPTriggerMultiple3D);

	bool triggered = false;

protected:
	static void _bind_methods();
	virtual void _sourcepp_start_touch(Node3D *p_body) override;

public:
	void reset_once();
	bool has_triggered() const;
};

class SourcePPTriggerHurt3D : public SourcePPBrushArea3D {
	GDCLASS(SourcePPTriggerHurt3D, SourcePPBrushArea3D);

	double damage = 10.0;
	double damage_cap = 20.0;
	int damage_type = 0;
	int damage_model = 0;
	bool no_damage_force = false;
	double hurt_interval = 0.5;

protected:
	static void _bind_methods();
	virtual void _sourcepp_start_touch(Node3D *p_body) override;

public:
	void set_damage(double p_damage);
	double get_damage() const;
	void set_damage_cap(double p_damage_cap);
	double get_damage_cap() const;
	void set_damage_type(int p_damage_type);
	int get_damage_type() const;
	void set_damage_model(int p_damage_model);
	int get_damage_model() const;
	void set_no_damage_force(bool p_no_damage_force);
	bool is_no_damage_force_enabled() const;
	void set_hurt_interval(double p_hurt_interval);
	double get_hurt_interval() const;
	void request_hurt(Node3D *p_body);
};

class SourcePPFuncBrush3D : public SourcePPBrushBody3D {
	GDCLASS(SourcePPFuncBrush3D, SourcePPBrushBody3D);

	int solidity = 0;
	bool solid_bsp = false;

protected:
	static void _bind_methods();
	virtual void _sourcepp_enabled_changed() override;

public:
	void set_solidity(int p_solidity);
	int get_solidity() const;
	void set_solid_bsp(bool p_solid_bsp);
	bool is_solid_bsp_enabled() const;
};

class SourcePPFuncDoor3D : public SourcePPBrushBody3D {
	GDCLASS(SourcePPFuncDoor3D, SourcePPBrushBody3D);

	Vector3 move_direction = Vector3(0, 1, 0);
	double speed = 100.0;
	double wait = 4.0;
	double lip = 0.0;
	int spawn_position = 0;
	bool locked = false;
	bool open_state = false;

protected:
	static void _bind_methods();

public:
	void set_move_direction(const Vector3 &p_move_direction);
	Vector3 get_move_direction() const;
	void set_speed(double p_speed);
	double get_speed() const;
	void set_wait(double p_wait);
	double get_wait() const;
	void set_lip(double p_lip);
	double get_lip() const;
	void set_spawn_position(int p_spawn_position);
	int get_spawn_position() const;
	void set_locked(bool p_locked);
	bool is_locked() const;
	void set_open(bool p_open);
	bool is_open() const;
	void open(Object *p_activator = nullptr);
	void close(Object *p_activator = nullptr);
	void toggle(Object *p_activator = nullptr);
	void lock();
	void unlock();
};

class SourcePPFuncIllusionary3D : public SourcePPBrushEntity3D {
	GDCLASS(SourcePPFuncIllusionary3D, SourcePPBrushEntity3D);

protected:
	static void _bind_methods();
};

class SourcePPLadder3D : public SourcePPBrushEntity3D {
	GDCLASS(SourcePPLadder3D, SourcePPBrushEntity3D);

	Vector3 point0;
	Vector3 point1;
	String ladder_surface_properties;
	bool fake_ladder = false;

protected:
	static void _bind_methods();

public:
	void set_point0(const Vector3 &p_point);
	Vector3 get_point0() const;
	void set_point1(const Vector3 &p_point);
	Vector3 get_point1() const;
	void set_ladder_surface_properties(const String &p_surface_properties);
	String get_ladder_surface_properties() const;
	void set_fake_ladder(bool p_fake_ladder);
	bool is_fake_ladder() const;
	void player_got_on(Node3D *p_player);
	void player_got_off(Node3D *p_player);
};

class SourcePPLadderDismount3D : public SourcePPBrushEntity3D {
	GDCLASS(SourcePPLadderDismount3D, SourcePPBrushEntity3D);

	String ladder_target;

protected:
	static void _bind_methods();

public:
	void set_ladder_target(const String &p_target);
	String get_ladder_target() const;
};
