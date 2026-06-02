/**************************************************************************/
/*  sourcepp_bsp_shape.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "scene/resources/3d/shape_3d.h"

class BSPShape3D : public Shape3D {
	GDCLASS(BSPShape3D, Shape3D);

	struct BSPData {
		PackedVector3Array faces;
		AABB bounds;
		bool backface_collision = false;
	};

	BSPData data;

	void _recalculate_bounds();

protected:
	static void _bind_methods();

	virtual void _update_shape() override;

public:
	void set_faces(const PackedVector3Array &p_faces);
	PackedVector3Array get_faces() const;

	void set_backface_collision_enabled(bool p_enabled);
	bool is_backface_collision_enabled() const;

	void set_data(const Dictionary &p_data);
	Dictionary get_data() const;

	AABB get_bounds() const;
	int get_face_count() const;

	virtual Vector<Vector3> get_debug_mesh_lines() const override;
	virtual Ref<ArrayMesh> get_debug_arraymesh_faces(const Color &p_modulate) const override;
	virtual real_t get_enclosing_radius() const override;

	BSPShape3D();
};
