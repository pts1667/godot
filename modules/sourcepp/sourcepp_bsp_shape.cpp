/**************************************************************************/
/*  sourcepp_bsp_shape.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp_shape.h"

#include "core/object/class_db.h"
#include "scene/resources/mesh.h"
#include "servers/physics_3d/physics_server_3d.h"

void BSPShape3D::_recalculate_bounds() {
	if (data.faces.is_empty()) {
		data.bounds = AABB();
		return;
	}

	data.bounds = AABB(data.faces[0], Vector3());
	for (int i = 1; i < data.faces.size(); i++) {
		data.bounds.expand_to(data.faces[i]);
	}
}

void BSPShape3D::_update_shape() {
	Dictionary shape_data;
	shape_data["faces"] = data.faces;
	shape_data["backface_collision"] = data.backface_collision;
	PhysicsServer3D::get_singleton()->shape_set_data(get_shape(), shape_data);

	Shape3D::_update_shape();
}

void BSPShape3D::set_faces(const PackedVector3Array &p_faces) {
	ERR_FAIL_COND_MSG((p_faces.size() % 3) != 0, "BSP collision faces must be supplied as triangles.");

	data.faces = p_faces;
	_recalculate_bounds();
	_update_shape();
	emit_changed();
}

PackedVector3Array BSPShape3D::get_faces() const {
	return data.faces;
}

void BSPShape3D::set_backface_collision_enabled(bool p_enabled) {
	if (data.backface_collision == p_enabled) {
		return;
	}

	data.backface_collision = p_enabled;
	_update_shape();
	emit_changed();
}

bool BSPShape3D::is_backface_collision_enabled() const {
	return data.backface_collision;
}

void BSPShape3D::set_data(const Dictionary &p_data) {
	PackedVector3Array faces;
	if (p_data.has("faces")) {
		faces = p_data["faces"];
	}

	data.backface_collision = p_data.has("backface_collision") ? static_cast<bool>(p_data["backface_collision"]) : false;
	set_faces(faces);
}

Dictionary BSPShape3D::get_data() const {
	Dictionary out;
	out["faces"] = data.faces;
	out["bounds"] = data.bounds;
	out["backface_collision"] = data.backface_collision;
	out["face_count"] = get_face_count();
	return out;
}

AABB BSPShape3D::get_bounds() const {
	return data.bounds;
}

int BSPShape3D::get_face_count() const {
	return data.faces.size() / 3;
}

Vector<Vector3> BSPShape3D::get_debug_mesh_lines() const {
	Vector<Vector3> points;
	points.resize(data.faces.size() * 2);

	int write_index = 0;
	for (int i = 0; i < data.faces.size(); i += 3) {
		points.write[write_index++] = data.faces[i + 0];
		points.write[write_index++] = data.faces[i + 1];
		points.write[write_index++] = data.faces[i + 1];
		points.write[write_index++] = data.faces[i + 2];
		points.write[write_index++] = data.faces[i + 2];
		points.write[write_index++] = data.faces[i + 0];
	}

	return points;
}

Ref<ArrayMesh> BSPShape3D::get_debug_arraymesh_faces(const Color &p_modulate) const {
	PackedColorArray colors;
	colors.resize(data.faces.size());
	for (int i = 0; i < colors.size(); i++) {
		colors.set(i, p_modulate);
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = data.faces;
	arrays[Mesh::ARRAY_COLOR] = colors;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

real_t BSPShape3D::get_enclosing_radius() const {
	real_t radius_squared = 0.0;
	for (int i = 0; i < data.faces.size(); i++) {
		radius_squared = MAX(radius_squared, data.faces[i].length_squared());
	}
	return Math::sqrt(radius_squared);
}

void BSPShape3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_faces", "faces"), &BSPShape3D::set_faces);
	ClassDB::bind_method(D_METHOD("get_faces"), &BSPShape3D::get_faces);
	ClassDB::bind_method(D_METHOD("set_backface_collision_enabled", "enabled"), &BSPShape3D::set_backface_collision_enabled);
	ClassDB::bind_method(D_METHOD("is_backface_collision_enabled"), &BSPShape3D::is_backface_collision_enabled);
	ClassDB::bind_method(D_METHOD("set_data", "data"), &BSPShape3D::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &BSPShape3D::get_data);
	ClassDB::bind_method(D_METHOD("get_bounds"), &BSPShape3D::get_bounds);
	ClassDB::bind_method(D_METHOD("get_face_count"), &BSPShape3D::get_face_count);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "backface_collision"), "set_backface_collision_enabled", "is_backface_collision_enabled");
}

BSPShape3D::BSPShape3D() :
		Shape3D(PhysicsServer3D::get_singleton()->shape_create(PhysicsServer3D::SHAPE_CONCAVE_POLYGON)) {
}
