/**************************************************************************/
/*  halfedge_mesh.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "halfedge_mesh.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/math/geometry_2d.h"
#include "core/math/math_funcs.h"

void HalfEdgeMesh::_bind_methods() {
	ClassDB::bind_method(D_METHOD("clear"), &HalfEdgeMesh::clear);
	ClassDB::bind_method(D_METHOD("set_coplanar_epsilon", "epsilon"), &HalfEdgeMesh::set_coplanar_epsilon);
	ClassDB::bind_method(D_METHOD("get_coplanar_epsilon"), &HalfEdgeMesh::get_coplanar_epsilon);
	ClassDB::bind_method(D_METHOD("set_collinear_epsilon", "epsilon"), &HalfEdgeMesh::set_collinear_epsilon);
	ClassDB::bind_method(D_METHOD("get_collinear_epsilon"), &HalfEdgeMesh::get_collinear_epsilon);

	ClassDB::bind_method(D_METHOD("get_vertex_count"), &HalfEdgeMesh::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_halfedge_count"), &HalfEdgeMesh::get_halfedge_count);
	ClassDB::bind_method(D_METHOD("get_edge_count"), &HalfEdgeMesh::get_edge_count);
	ClassDB::bind_method(D_METHOD("get_face_count"), &HalfEdgeMesh::get_face_count);

	ClassDB::bind_method(D_METHOD("add_vertex", "position"), &HalfEdgeMesh::add_vertex);
	ClassDB::bind_method(D_METHOD("set_vertex_position", "vertex", "position"), &HalfEdgeMesh::set_vertex_position);
	ClassDB::bind_method(D_METHOD("get_vertex_position", "vertex"), &HalfEdgeMesh::get_vertex_position);

	ClassDB::bind_method(D_METHOD("add_face", "vertex_indices"), &HalfEdgeMesh::add_face);
	ClassDB::bind_method(D_METHOD("remove_face", "face"), &HalfEdgeMesh::remove_face);
	ClassDB::bind_method(D_METHOD("compact_vertices"), &HalfEdgeMesh::compact_vertices);

	ClassDB::bind_method(D_METHOD("set_faces", "vertices", "faces"), &HalfEdgeMesh::set_faces);
	ClassDB::bind_method(D_METHOD("build_from_triangle_arrays", "vertices", "indices"), &HalfEdgeMesh::build_from_triangle_arrays, DEFVAL(PackedInt32Array()));
	ClassDB::bind_method(D_METHOD("build_from_arrays", "arrays"), &HalfEdgeMesh::build_from_arrays);
	ClassDB::bind_method(D_METHOD("build_from_mesh", "mesh", "surface"), &HalfEdgeMesh::build_from_mesh, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("add_box", "size", "transform"), &HalfEdgeMesh::add_box, DEFVAL(Vector3(1, 1, 1)), DEFVAL(Transform3D()));
	ClassDB::bind_method(D_METHOD("add_uv_sphere", "radius", "radial_segments", "rings", "transform"), &HalfEdgeMesh::add_uv_sphere, DEFVAL(0.5f), DEFVAL(16), DEFVAL(8), DEFVAL(Transform3D()));
	ClassDB::bind_method(D_METHOD("add_icosphere", "radius", "subdivisions", "transform"), &HalfEdgeMesh::add_icosphere, DEFVAL(0.5f), DEFVAL(1), DEFVAL(Transform3D()));
	ClassDB::bind_method(D_METHOD("add_cylinder", "radius", "height", "radial_segments", "transform"), &HalfEdgeMesh::add_cylinder, DEFVAL(0.5f), DEFVAL(1.0f), DEFVAL(16), DEFVAL(Transform3D()));

	ClassDB::bind_method(D_METHOD("get_vertices"), &HalfEdgeMesh::get_vertices);
	ClassDB::bind_method(D_METHOD("get_faces"), &HalfEdgeMesh::get_faces);
	ClassDB::bind_method(D_METHOD("get_face_vertex_indices", "face"), &HalfEdgeMesh::get_face_vertex_indices);
	ClassDB::bind_method(D_METHOD("get_face_data", "face"), &HalfEdgeMesh::get_face_data);
	ClassDB::bind_method(D_METHOD("get_face_projection", "face"), &HalfEdgeMesh::get_face_projection);
	ClassDB::bind_method(D_METHOD("get_edge_data", "edge"), &HalfEdgeMesh::get_edge_data);
	ClassDB::bind_method(D_METHOD("get_halfedge_data", "halfedge"), &HalfEdgeMesh::get_halfedge_data);
	ClassDB::bind_method(D_METHOD("cut", "face", "points"), &HalfEdgeMesh::cut);
	ClassDB::bind_method(D_METHOD("extrude", "face", "distance"), &HalfEdgeMesh::extrude);
	ClassDB::bind_method(D_METHOD("bevel", "edges"), &HalfEdgeMesh::bevel);

	ClassDB::bind_method(D_METHOD("to_mesh_arrays"), &HalfEdgeMesh::to_mesh_arrays);
	ClassDB::bind_method(D_METHOD("to_mesh"), &HalfEdgeMesh::to_mesh);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coplanar_epsilon"), "set_coplanar_epsilon", "get_coplanar_epsilon");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collinear_epsilon"), "set_collinear_epsilon", "get_collinear_epsilon");
}

uint64_t HalfEdgeMesh::_make_edge_key(int p_from, int p_to) {
	return (static_cast<uint64_t>(static_cast<uint32_t>(p_from)) << 32) | static_cast<uint32_t>(p_to);
}

uint64_t HalfEdgeMesh::_make_face_vertex_key(int p_face, int p_vertex) {
	return _make_edge_key(p_face, p_vertex);
}

uint64_t HalfEdgeMesh::_make_undirected_edge_key(int p_a, int p_b) {
	return p_a < p_b ? _make_edge_key(p_a, p_b) : _make_edge_key(p_b, p_a);
}

Vector<int> HalfEdgeMesh::_to_vector(const PackedInt32Array &p_array) {
	Vector<int> result;
	result.resize(p_array.size());
	for (int i = 0; i < p_array.size(); i++) {
		result.write[i] = p_array[i];
	}
	return result;
}

PackedInt32Array HalfEdgeMesh::_to_packed_array(const Vector<int> &p_values) {
	PackedInt32Array result;
	result.resize(p_values.size());
	for (int i = 0; i < p_values.size(); i++) {
		result.set(i, p_values[i]);
	}
	return result;
}

bool HalfEdgeMesh::_parse_face_variant(const Variant &p_face, Vector<int> &r_face) const {
	r_face.clear();
	if (p_face.get_type() == Variant::PACKED_INT32_ARRAY) {
		r_face = _to_vector((PackedInt32Array)p_face);
		return true;
	}
	if (p_face.get_type() != Variant::ARRAY) {
		return false;
	}

	Array face_array = p_face;
	r_face.resize(face_array.size());
	for (int i = 0; i < face_array.size(); i++) {
		if (face_array[i].get_type() != Variant::INT) {
			return false;
		}
		r_face.write[i] = int(face_array[i]);
	}
	return true;
}

bool HalfEdgeMesh::_validate_face(const Vector<int> &p_face, String *r_error) const {
	if (p_face.size() < 3) {
		if (r_error) {
			*r_error = "Faces must contain at least three vertices.";
		}
		return false;
	}

	for (int i = 0; i < p_face.size(); i++) {
		const int vertex = p_face[i];
		if (vertex < 0 || vertex >= input_vertices.size()) {
			if (r_error) {
				*r_error = "Face contains an out-of-range vertex index.";
			}
			return false;
		}
		const int next_vertex = p_face[(i + 1) % p_face.size()];
		if (vertex == next_vertex) {
			if (r_error) {
				*r_error = "Face contains a zero-length edge.";
			}
			return false;
		}
		for (int j = i + 1; j < p_face.size(); j++) {
			if (vertex == p_face[j]) {
				if (r_error) {
					*r_error = "Face contains duplicate vertex indices.";
				}
				return false;
			}
		}
	}

	Plane plane;
	if (!_compute_face_plane(p_face, plane)) {
		if (r_error) {
			*r_error = "Face is degenerate or not planar within the configured epsilon.";
		}
		return false;
	}

	if (!_is_face_simple(p_face)) {
		if (r_error) {
			*r_error = "Face self-intersects when projected to its plane.";
		}
		return false;
	}

	return true;
}

bool HalfEdgeMesh::_get_vertex_incident_faces(int p_vertex, Vector<int> &r_faces) const {
	r_faces.clear();
	ERR_FAIL_INDEX_V(p_vertex, vertices.size(), false);
	ERR_FAIL_COND_V(vertices[p_vertex].halfedge == -1, false);

	Vector<int> outgoing_halfedges;
	for (int halfedge_index = 0; halfedge_index < halfedges.size(); halfedge_index++) {
		if (halfedges[halfedge_index].origin == p_vertex) {
			outgoing_halfedges.push_back(halfedge_index);
		}
	}
	ERR_FAIL_COND_V(outgoing_halfedges.is_empty(), false);

	int start_halfedge = outgoing_halfedges[0];
	for (int i = 0; i < outgoing_halfedges.size(); i++) {
		const int previous_halfedge = halfedges[outgoing_halfedges[i]].prev;
		if (previous_halfedge >= 0 && halfedges[previous_halfedge].twin == -1) {
			start_halfedge = outgoing_halfedges[i];
			break;
		}
	}

	Vector<uint8_t> visited;
	visited.resize(halfedges.size());
	int current_halfedge = start_halfedge;
	while (true) {
		if (visited[current_halfedge]) {
			break;
		}
		visited.write[current_halfedge] = 1;
		r_faces.push_back(halfedges[current_halfedge].face);

		const int twin_halfedge = halfedges[current_halfedge].twin;
		if (twin_halfedge == -1) {
			break;
		}
		current_halfedge = halfedges[twin_halfedge].next;
		if (current_halfedge == start_halfedge) {
			break;
		}
	}

	ERR_FAIL_COND_V(r_faces.size() != outgoing_halfedges.size(), false);
	return true;
}

bool HalfEdgeMesh::_compute_face_plane(const Vector<int> &p_face, Plane &r_plane) const {
	if (p_face.size() < 3) {
		return false;
	}

	int origin = p_face[0];
	bool found_plane = false;
	for (int i = 1; i < p_face.size() - 1; i++) {
		const Vector3 &a = input_vertices[origin];
		const Vector3 &b = input_vertices[p_face[i]];
		const Vector3 &c = input_vertices[p_face[i + 1]];
		if ((b - a).cross(c - a).length() <= collinear_epsilon) {
			continue;
		}
		r_plane = Plane(a, b, c);
		found_plane = true;
		break;
	}

	if (!found_plane) {
		return false;
	}

	for (int i = 0; i < p_face.size(); i++) {
		if (Math::abs(r_plane.distance_to(input_vertices[p_face[i]])) > coplanar_epsilon) {
			return false;
		}
	}

	return true;
}

bool HalfEdgeMesh::_get_face_projection(const Vector<int> &p_face, FaceProjection &r_projection) const {
	ERR_FAIL_COND_V(p_face.size() < 3, false);
	ERR_FAIL_COND_V(!_compute_face_plane(p_face, r_projection.plane), false);

	r_projection.origin = input_vertices[p_face[0]];
	const Vector3 normal = r_projection.plane.normal.normalized();
	const Vector3 reference = Math::abs(normal.dot(Vector3(0, 1, 0))) < 0.999f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	r_projection.tangent = normal.cross(reference).normalized();
	r_projection.bitangent = normal.cross(r_projection.tangent).normalized();
	return true;
}

Vector<Vector2> HalfEdgeMesh::_project_face_to_2d(const Vector<int> &p_face, const FaceProjection &p_projection) const {
	Vector<Vector2> projected;
	projected.resize(p_face.size());
	for (int i = 0; i < p_face.size(); i++) {
		projected.write[i] = _project_point_to_2d(p_projection, input_vertices[p_face[i]]);
	}
	return projected;
}

Vector2 HalfEdgeMesh::_project_point_to_2d(const FaceProjection &p_projection, const Vector3 &p_point) const {
	const Vector3 local = p_point - p_projection.origin;
	return Vector2(p_projection.tangent.dot(local), p_projection.bitangent.dot(local));
}

Vector3 HalfEdgeMesh::_unproject_point_from_2d(const FaceProjection &p_projection, const Vector2 &p_point) const {
	return p_projection.origin + p_projection.tangent * p_point.x + p_projection.bitangent * p_point.y;
}

Vector2 HalfEdgeMesh::_project_point_to_face_plane(const Plane &p_plane, const Vector3 &p_point) const {
	const Vector3 normal = p_plane.normal.normalized();
	const Vector3 reference = Math::abs(normal.dot(Vector3(0, 1, 0))) < 0.999f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	const Vector3 tangent = normal.cross(reference).normalized();
	const Vector3 bitangent = normal.cross(tangent).normalized();
	return Vector2(tangent.dot(p_point), bitangent.dot(p_point));
}

bool HalfEdgeMesh::_is_polygon_simple_2d(const Vector<Vector2> &p_polygon) const {
	if (p_polygon.size() < 3) {
		return false;
	}

	for (int i = 0; i < p_polygon.size(); i++) {
		const Vector2 a0 = p_polygon[i];
		const Vector2 a1 = p_polygon[(i + 1) % p_polygon.size()];
		if (a0.distance_to(a1) <= collinear_epsilon) {
			return false;
		}
		for (int j = i + 1; j < p_polygon.size(); j++) {
			if (j == i || j == (i + 1) % p_polygon.size() || (i == 0 && j == p_polygon.size() - 1)) {
				continue;
			}
			const Vector2 b0 = p_polygon[j];
			const Vector2 b1 = p_polygon[(j + 1) % p_polygon.size()];
			if (Geometry2D::segment_intersects_segment(a0, a1, b0, b1, nullptr)) {
				return false;
			}
		}
	}

	return !Geometry2D::triangulate_polygon(p_polygon).is_empty();
}

bool HalfEdgeMesh::_is_point_in_or_on_polygon_2d(const Vector2 &p_point, const Vector<Vector2> &p_polygon) const {
	if (Geometry2D::is_point_in_polygon(p_point, p_polygon)) {
		return true;
	}

	for (int i = 0; i < p_polygon.size(); i++) {
		const Vector2 closest = Geometry2D::get_closest_point_to_segment(p_point, p_polygon[i], p_polygon[(i + 1) % p_polygon.size()]);
		if (closest.distance_to(p_point) <= collinear_epsilon) {
			return true;
		}
	}

	return false;
}

bool HalfEdgeMesh::_is_segment_inside_polygon_2d(const Vector2 &p_from, const Vector2 &p_to, const Vector<Vector2> &p_polygon) const {
	if (!_is_point_in_or_on_polygon_2d(p_from, p_polygon) || !_is_point_in_or_on_polygon_2d(p_to, p_polygon)) {
		return false;
	}

	Vector<Vector2> segment;
	segment.push_back(p_from);
	segment.push_back(p_to);
	const Vector<Vector<Vector2>> clipped = Geometry2D::clip_polyline_with_polygon(segment, p_polygon);
	if (clipped.size() != 1 || clipped[0].size() != 2) {
		return false;
	}

	const bool same_direction = clipped[0][0].distance_to(p_from) <= collinear_epsilon && clipped[0][1].distance_to(p_to) <= collinear_epsilon;
	const bool reversed_direction = clipped[0][0].distance_to(p_to) <= collinear_epsilon && clipped[0][1].distance_to(p_from) <= collinear_epsilon;
	return same_direction || reversed_direction;
}

bool HalfEdgeMesh::_resolve_projected_polygon(const Vector<Vector2> &p_polygon, const Vector<Vector2> &p_outer_points, const Vector<int> &p_outer_indices, const Vector<Vector2> &p_inner_points, const Vector<int> &p_inner_indices, Vector<int> &r_face) const {
	r_face.clear();
	if (p_polygon.size() < 3) {
		return false;
	}

	Vector<Vector2> normalized_polygon;
	normalized_polygon.reserve(p_polygon.size());
	for (int i = 0; i < p_polygon.size(); i++) {
		if (!normalized_polygon.is_empty() && normalized_polygon[normalized_polygon.size() - 1].distance_to(p_polygon[i]) <= collinear_epsilon) {
			continue;
		}
		normalized_polygon.push_back(p_polygon[i]);
	}
	if (normalized_polygon.size() >= 2 && normalized_polygon[0].distance_to(normalized_polygon[normalized_polygon.size() - 1]) <= collinear_epsilon) {
		normalized_polygon.remove_at(normalized_polygon.size() - 1);
	}

	for (int i = 0; i < normalized_polygon.size(); i++) {
		const Vector2 point = normalized_polygon[i];
		int resolved_index = -1;
		for (int j = 0; j < p_outer_points.size(); j++) {
			if (point.distance_to(p_outer_points[j]) <= collinear_epsilon) {
				resolved_index = p_outer_indices[j];
				break;
			}
		}
		if (resolved_index == -1) {
			for (int j = 0; j < p_inner_points.size(); j++) {
				if (point.distance_to(p_inner_points[j]) <= collinear_epsilon) {
					resolved_index = p_inner_indices[j];
					break;
				}
			}
		}
		if (resolved_index == -1) {
			return false;
		}
		if (!r_face.is_empty() && r_face[r_face.size() - 1] == resolved_index) {
			continue;
		}
		r_face.push_back(resolved_index);
	}

	if (!_simplify_face(r_face)) {
		return false;
	}

	return true;
}

bool HalfEdgeMesh::_is_face_simple(const Vector<int> &p_face) const {
	Plane plane;
	if (!_compute_face_plane(p_face, plane)) {
		return false;
	}

	Vector<Vector2> projected;
	projected.resize(p_face.size());
	for (int i = 0; i < p_face.size(); i++) {
		projected.write[i] = _project_point_to_face_plane(plane, input_vertices[p_face[i]]);
	}

	for (int i = 0; i < projected.size(); i++) {
		const Vector2 a0 = projected[i];
		const Vector2 a1 = projected[(i + 1) % projected.size()];
		for (int j = i + 1; j < projected.size(); j++) {
			if (j == i || j == (i + 1) % projected.size() || (i == 0 && j == projected.size() - 1)) {
				continue;
			}
			const Vector2 b0 = projected[j];
			const Vector2 b1 = projected[(j + 1) % projected.size()];
			if (Geometry2D::segment_intersects_segment(a0, a1, b0, b1, nullptr)) {
				return false;
			}
		}
	}

	return true;
}

bool HalfEdgeMesh::_simplify_face(Vector<int> &r_face) const {
	if (r_face.size() < 3) {
		return false;
	}

	bool changed = true;
	while (changed && r_face.size() >= 3) {
		changed = false;
		for (int i = 0; i < r_face.size(); i++) {
			const int prev_index = (i - 1 + r_face.size()) % r_face.size();
			const int next_index = (i + 1) % r_face.size();
			const int prev_vertex = r_face[prev_index];
			const int vertex = r_face[i];
			const int next_vertex = r_face[next_index];

			if (prev_vertex == vertex || vertex == next_vertex || prev_vertex == next_vertex) {
				r_face.remove_at(i);
				changed = true;
				break;
			}

			const Vector3 prev = input_vertices[prev_vertex];
			const Vector3 current = input_vertices[vertex];
			const Vector3 next = input_vertices[next_vertex];
			if ((current - prev).cross(next - current).length() <= collinear_epsilon) {
				r_face.remove_at(i);
				changed = true;
				break;
			}
		}
	}

	if (r_face.size() < 3) {
		return false;
	}

	for (int i = 0; i < r_face.size(); i++) {
		for (int j = i + 1; j < r_face.size(); j++) {
			if (r_face[i] == r_face[j]) {
				return false;
			}
		}
	}

	return true;
}

bool HalfEdgeMesh::_are_faces_coplanar(const Vector<int> &p_face_a, const Vector<int> &p_face_b) const {
	Plane plane_a;
	Plane plane_b;
	if (!_compute_face_plane(p_face_a, plane_a) || !_compute_face_plane(p_face_b, plane_b)) {
		return false;
	}

	if (Math::abs(plane_a.normal.dot(plane_b.normal)) < 0.999f) {
		return false;
	}

	for (int i = 0; i < p_face_b.size(); i++) {
		if (Math::abs(plane_a.distance_to(input_vertices[p_face_b[i]])) > coplanar_epsilon) {
			return false;
		}
	}

	return true;
}

bool HalfEdgeMesh::_try_merge_faces(const Vector<int> &p_face_a, const Vector<int> &p_face_b, Vector<int> &r_merged) const {
	r_merged.clear();
	if (!_are_faces_coplanar(p_face_a, p_face_b)) {
		return false;
	}

	for (int i = 0; i < p_face_a.size(); i++) {
		const int a0 = p_face_a[i];
		const int a1 = p_face_a[(i + 1) % p_face_a.size()];
		for (int j = 0; j < p_face_b.size(); j++) {
			const int b0 = p_face_b[j];
			const int b1 = p_face_b[(j + 1) % p_face_b.size()];
			if (a0 != b1 || a1 != b0) {
				continue;
			}

			Vector<int> merged;
			for (int index = (i + 1) % p_face_a.size(); index != i; index = (index + 1) % p_face_a.size()) {
				merged.push_back(p_face_a[index]);
			}
			merged.push_back(p_face_a[i]);

			for (int index = (j + 2) % p_face_b.size(); index != j; index = (index + 1) % p_face_b.size()) {
				merged.push_back(p_face_b[index]);
			}

			if (!_simplify_face(merged)) {
				continue;
			}
			if (!_validate_face(merged)) {
				continue;
			}
			r_merged = merged;
			return true;
		}
	}

	return false;
}

void HalfEdgeMesh::_merge_coplanar_faces(Vector<InputFace> &r_faces) const {
	bool merged = true;
	while (merged) {
		merged = false;
		for (int i = 0; i < r_faces.size() && !merged; i++) {
			for (int j = i + 1; j < r_faces.size(); j++) {
				Vector<int> merged_face;
				if (!_try_merge_faces(r_faces[i].vertices, r_faces[j].vertices, merged_face)) {
					continue;
				}
				r_faces.write[i].vertices = merged_face;
				r_faces.remove_at(j);
				merged = true;
				break;
			}
		}
	}
}

Dictionary HalfEdgeMesh::_append_component(const Vector<Vector3> &p_vertices, const Vector<InputFace> &p_faces) {
	ERR_FAIL_COND_V_MSG(p_vertices.is_empty(), Dictionary(), "Primitive component must provide vertices.");
	ERR_FAIL_COND_V_MSG(p_faces.is_empty(), Dictionary(), "Primitive component must provide faces.");

	const Vector<Vector3> original_vertices = input_vertices;
	const Vector<InputFace> original_faces = input_faces;
	const int vertex_base = input_vertices.size();
	const int face_base = input_faces.size();

	for (int i = 0; i < p_vertices.size(); i++) {
		input_vertices.push_back(p_vertices[i]);
	}

	for (int i = 0; i < p_faces.size(); i++) {
		InputFace appended_face;
		appended_face.vertices.resize(p_faces[i].vertices.size());
		for (int vertex_index = 0; vertex_index < p_faces[i].vertices.size(); vertex_index++) {
			appended_face.vertices.write[vertex_index] = vertex_base + p_faces[i].vertices[vertex_index];
		}
		input_faces.push_back(appended_face);
	}

	if (_rebuild_topology() != OK) {
		input_vertices = original_vertices;
		input_faces = original_faces;
		_rebuild_topology();
		ERR_FAIL_V_MSG(Dictionary(), "Failed to append primitive component to the half-edge mesh.");
	}

	PackedInt32Array vertex_indices;
	vertex_indices.resize(p_vertices.size());
	for (int i = 0; i < p_vertices.size(); i++) {
		vertex_indices.set(i, vertex_base + i);
	}

	PackedInt32Array face_indices;
	face_indices.resize(p_faces.size());
	for (int i = 0; i < p_faces.size(); i++) {
		face_indices.set(i, face_base + i);
	}

	Dictionary result;
	result["vertices"] = vertex_indices;
	result["faces"] = face_indices;
	return result;
}

Error HalfEdgeMesh::_rebuild_topology() {
	vertices.clear();
	halfedges.clear();
	edges.clear();
	faces.clear();

	vertices.resize(input_vertices.size());
	for (int i = 0; i < input_vertices.size(); i++) {
		vertices.write[i].position = input_vertices[i];
		vertices.write[i].halfedge = -1;
	}

	HashMap<uint64_t, int> directed_halfedges;
	for (int face_index = 0; face_index < input_faces.size(); face_index++) {
		String error;
		ERR_FAIL_COND_V_MSG(!_validate_face(input_faces[face_index].vertices, &error), ERR_INVALID_DATA, error);

		Plane plane;
		ERR_FAIL_COND_V(!_compute_face_plane(input_faces[face_index].vertices, plane), ERR_INVALID_DATA);

		const int halfedge_start = halfedges.size();
		FaceData face;
		face.halfedge = halfedge_start;
		face.plane = plane;
		faces.push_back(face);

		for (int i = 0; i < input_faces[face_index].vertices.size(); i++) {
			const int current_vertex = input_faces[face_index].vertices[i];
			const int next_vertex = input_faces[face_index].vertices[(i + 1) % input_faces[face_index].vertices.size()];
			const uint64_t edge_key = _make_edge_key(current_vertex, next_vertex);
			ERR_FAIL_COND_V_MSG(directed_halfedges.has(edge_key), ERR_INVALID_DATA, "Multiple faces reuse the same directed edge. Input must describe a manifold surface.");

			HalfEdgeData halfedge;
			halfedge.origin = current_vertex;
			halfedge.face = face_index;
			halfedges.push_back(halfedge);
			directed_halfedges.insert(edge_key, halfedges.size() - 1);

			if (vertices[current_vertex].halfedge == -1) {
				vertices.write[current_vertex].halfedge = halfedges.size() - 1;
			}
		}

		for (int i = 0; i < input_faces[face_index].vertices.size(); i++) {
			const int halfedge_index = halfedge_start + i;
			const int next = halfedge_start + ((i + 1) % input_faces[face_index].vertices.size());
			const int prev = halfedge_start + ((i - 1 + input_faces[face_index].vertices.size()) % input_faces[face_index].vertices.size());
			halfedges.write[halfedge_index].next = next;
			halfedges.write[halfedge_index].prev = prev;
		}
	}

	for (int halfedge_index = 0; halfedge_index < halfedges.size(); halfedge_index++) {
		if (halfedges[halfedge_index].edge != -1) {
			continue;
		}

		const int destination = halfedges[halfedges[halfedge_index].next].origin;
		const uint64_t reverse_key = _make_edge_key(destination, halfedges[halfedge_index].origin);
		const int edge_index = edges.size();
		EdgeData edge;
		edge.halfedge = halfedge_index;
		edges.push_back(edge);
		halfedges.write[halfedge_index].edge = edge_index;

		if (directed_halfedges.has(reverse_key)) {
			const int twin = directed_halfedges[reverse_key];
			halfedges.write[halfedge_index].twin = twin;
			halfedges.write[twin].twin = halfedge_index;
			halfedges.write[twin].edge = edge_index;
		}
	}

	return OK;
}

Array HalfEdgeMesh::_build_mesh_arrays() const {
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);

	PackedVector3Array packed_vertices;
	packed_vertices.resize(vertices.size());
	for (int i = 0; i < vertices.size(); i++) {
		packed_vertices.set(i, vertices[i].position);
	}

	PackedVector3Array normals;
	normals.resize(vertices.size());
	for (int i = 0; i < normals.size(); i++) {
		normals.set(i, Vector3());
	}

	PackedInt32Array indices;
	for (int face_index = 0; face_index < input_faces.size(); face_index++) {
		const Vector<int> &face = input_faces[face_index].vertices;
		if (face.size() < 3) {
			continue;
		}

		const Vector3 face_normal = faces[face_index].plane.normal.normalized();
		for (int vertex_index = 0; vertex_index < face.size(); vertex_index++) {
			normals.set(face[vertex_index], normals[face[vertex_index]] + face_normal);
		}

		if (face.size() == 3) {
			indices.push_back(face[0]);
			indices.push_back(face[1]);
			indices.push_back(face[2]);
			continue;
		}

		Vector<Vector2> projected;
		projected.resize(face.size());
		for (int i = 0; i < face.size(); i++) {
			projected.write[i] = _project_point_to_face_plane(faces[face_index].plane, vertices[face[i]].position);
		}
		const Vector<int> triangles = Geometry2D::triangulate_polygon(projected);
		for (int i = 0; i < triangles.size(); i++) {
			indices.push_back(face[triangles[i]]);
		}
	}

	for (int i = 0; i < normals.size(); i++) {
		Vector3 normal = normals[i];
		if (!normal.is_zero_approx()) {
			normal.normalize();
		}
		normals.set(i, normal);
	}

	arrays[Mesh::ARRAY_VERTEX] = packed_vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_INDEX] = indices;
	return arrays;
}

void HalfEdgeMesh::clear() {
	input_vertices.clear();
	input_faces.clear();
	vertices.clear();
	halfedges.clear();
	edges.clear();
	faces.clear();
}

void HalfEdgeMesh::set_coplanar_epsilon(real_t p_epsilon) {
	coplanar_epsilon = MAX((real_t)CMP_EPSILON, p_epsilon);
	if (!input_faces.is_empty()) {
		_rebuild_topology();
	}
}

void HalfEdgeMesh::set_collinear_epsilon(real_t p_epsilon) {
	collinear_epsilon = MAX((real_t)CMP_EPSILON, p_epsilon);
	if (!input_faces.is_empty()) {
		_rebuild_topology();
	}
}

int HalfEdgeMesh::add_vertex(const Vector3 &p_position) {
	input_vertices.push_back(p_position);
	_rebuild_topology();
	return input_vertices.size() - 1;
}

void HalfEdgeMesh::set_vertex_position(int p_vertex, const Vector3 &p_position) {
	ERR_FAIL_INDEX(p_vertex, input_vertices.size());
	input_vertices.write[p_vertex] = p_position;
	_rebuild_topology();
}

Vector3 HalfEdgeMesh::get_vertex_position(int p_vertex) const {
	ERR_FAIL_INDEX_V(p_vertex, vertices.size(), Vector3());
	return vertices[p_vertex].position;
}

int HalfEdgeMesh::add_face(const PackedInt32Array &p_vertex_indices) {
	InputFace face;
	face.vertices = _to_vector(p_vertex_indices);
	String error;
	ERR_FAIL_COND_V_MSG(!_validate_face(face.vertices, &error), -1, error);
	input_faces.push_back(face);
	Error rebuild_error = _rebuild_topology();
	ERR_FAIL_COND_V(rebuild_error != OK, -1);
	return input_faces.size() - 1;
}

bool HalfEdgeMesh::remove_face(int p_face) {
	ERR_FAIL_INDEX_V(p_face, input_faces.size(), false);
	input_faces.remove_at(p_face);
	ERR_FAIL_COND_V(_rebuild_topology() != OK, false);
	return true;
}

void HalfEdgeMesh::compact_vertices() {
	Vector<int> remap;
	remap.resize(input_vertices.size());
	for (int i = 0; i < remap.size(); i++) {
		remap.write[i] = -1;
	}

	Vector<Vector3> compacted_vertices;
	for (int face_index = 0; face_index < input_faces.size(); face_index++) {
		for (int i = 0; i < input_faces[face_index].vertices.size(); i++) {
			const int vertex = input_faces[face_index].vertices[i];
			if (remap[vertex] != -1) {
				continue;
			}
			remap.write[vertex] = compacted_vertices.size();
			compacted_vertices.push_back(input_vertices[vertex]);
		}
	}

	for (int face_index = 0; face_index < input_faces.size(); face_index++) {
		for (int i = 0; i < input_faces[face_index].vertices.size(); i++) {
			input_faces.write[face_index].vertices.write[i] = remap[input_faces[face_index].vertices[i]];
		}
	}

	input_vertices = compacted_vertices;
	_rebuild_topology();
}

Error HalfEdgeMesh::set_faces(const PackedVector3Array &p_vertices, const Array &p_faces) {
	clear();
	input_vertices.resize(p_vertices.size());
	for (int i = 0; i < p_vertices.size(); i++) {
		input_vertices.write[i] = p_vertices[i];
	}

	input_faces.resize(p_faces.size());
	for (int i = 0; i < p_faces.size(); i++) {
		Vector<int> parsed_face;
		ERR_FAIL_COND_V_MSG(!_parse_face_variant(p_faces[i], parsed_face), ERR_INVALID_PARAMETER, "Faces must be PackedInt32Array values or arrays of integers.");
		input_faces.write[i].vertices = parsed_face;
	}

	return _rebuild_topology();
}

Error HalfEdgeMesh::build_from_triangle_arrays(const PackedVector3Array &p_vertices, const PackedInt32Array &p_indices) {
	clear();
	input_vertices.resize(p_vertices.size());
	for (int i = 0; i < p_vertices.size(); i++) {
		input_vertices.write[i] = p_vertices[i];
	}

	PackedInt32Array indices = p_indices;
	if (indices.is_empty()) {
		ERR_FAIL_COND_V_MSG((p_vertices.size() % 3) != 0, ERR_INVALID_PARAMETER, "Vertices without indices must be supplied as consecutive triangles.");
		indices.resize(p_vertices.size());
		for (int i = 0; i < p_vertices.size(); i++) {
			indices.set(i, i);
		}
	}

	ERR_FAIL_COND_V_MSG((indices.size() % 3) != 0, ERR_INVALID_PARAMETER, "Triangle indices must be grouped in triplets.");

	Vector<InputFace> triangle_faces;
	for (int i = 0; i < indices.size(); i += 3) {
		InputFace face;
		face.vertices.push_back(indices[i + 0]);
		face.vertices.push_back(indices[i + 1]);
		face.vertices.push_back(indices[i + 2]);
		triangle_faces.push_back(face);
	}

	_merge_coplanar_faces(triangle_faces);
	input_faces = triangle_faces;
	return _rebuild_topology();
}

Error HalfEdgeMesh::build_from_arrays(const Array &p_arrays) {
	ERR_FAIL_COND_V_MSG(p_arrays.size() <= Mesh::ARRAY_VERTEX, ERR_INVALID_PARAMETER, "Mesh arrays must contain a vertex array.");
	ERR_FAIL_COND_V_MSG(p_arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY, ERR_INVALID_PARAMETER, "Mesh arrays must use PackedVector3Array for ARRAY_VERTEX.");
	PackedVector3Array surface_vertices = p_arrays[Mesh::ARRAY_VERTEX];
	PackedInt32Array indices;
	if (p_arrays.size() > Mesh::ARRAY_INDEX && p_arrays[Mesh::ARRAY_INDEX].get_type() != Variant::NIL) {
		ERR_FAIL_COND_V_MSG(p_arrays[Mesh::ARRAY_INDEX].get_type() != Variant::PACKED_INT32_ARRAY, ERR_INVALID_PARAMETER, "Mesh arrays must use PackedInt32Array for ARRAY_INDEX.");
		indices = p_arrays[Mesh::ARRAY_INDEX];
	}
	return build_from_triangle_arrays(surface_vertices, indices);
}

Error HalfEdgeMesh::build_from_mesh(const Ref<Mesh> &p_mesh, int p_surface) {
	ERR_FAIL_COND_V_MSG(p_mesh.is_null(), ERR_INVALID_PARAMETER, "Mesh must not be null.");
	ERR_FAIL_INDEX_V_MSG(p_surface, p_mesh->get_surface_count(), ERR_INVALID_PARAMETER, "Requested mesh surface is out of range.");
	ERR_FAIL_COND_V_MSG(p_mesh->surface_get_primitive_type(p_surface) != Mesh::PRIMITIVE_TRIANGLES, ERR_UNAVAILABLE, "HalfEdgeMesh currently imports triangle surfaces only.");
	return build_from_arrays(p_mesh->surface_get_arrays(p_surface));
}

Dictionary HalfEdgeMesh::add_box(const Vector3 &p_size, const Transform3D &p_transform) {
	ERR_FAIL_COND_V_MSG(p_size.x <= 0.0f || p_size.y <= 0.0f || p_size.z <= 0.0f, Dictionary(), "Box size components must be positive.");

	const Vector3 half_size = p_size * 0.5f;
	Vector<Vector3> primitive_vertices;
	primitive_vertices.push_back(p_transform.xform(Vector3(-half_size.x, -half_size.y, -half_size.z)));
	primitive_vertices.push_back(p_transform.xform(Vector3(half_size.x, -half_size.y, -half_size.z)));
	primitive_vertices.push_back(p_transform.xform(Vector3(half_size.x, half_size.y, -half_size.z)));
	primitive_vertices.push_back(p_transform.xform(Vector3(-half_size.x, half_size.y, -half_size.z)));
	primitive_vertices.push_back(p_transform.xform(Vector3(-half_size.x, -half_size.y, half_size.z)));
	primitive_vertices.push_back(p_transform.xform(Vector3(half_size.x, -half_size.y, half_size.z)));
	primitive_vertices.push_back(p_transform.xform(Vector3(half_size.x, half_size.y, half_size.z)));
	primitive_vertices.push_back(p_transform.xform(Vector3(-half_size.x, half_size.y, half_size.z)));

	Vector<InputFace> primitive_faces;
	InputFace back;
	back.vertices.push_back(1);
	back.vertices.push_back(0);
	back.vertices.push_back(3);
	back.vertices.push_back(2);
	primitive_faces.push_back(back);
	InputFace front;
	front.vertices.push_back(4);
	front.vertices.push_back(5);
	front.vertices.push_back(6);
	front.vertices.push_back(7);
	primitive_faces.push_back(front);
	InputFace left;
	left.vertices.push_back(0);
	left.vertices.push_back(4);
	left.vertices.push_back(7);
	left.vertices.push_back(3);
	primitive_faces.push_back(left);
	InputFace right;
	right.vertices.push_back(5);
	right.vertices.push_back(1);
	right.vertices.push_back(2);
	right.vertices.push_back(6);
	primitive_faces.push_back(right);
	InputFace bottom;
	bottom.vertices.push_back(0);
	bottom.vertices.push_back(1);
	bottom.vertices.push_back(5);
	bottom.vertices.push_back(4);
	primitive_faces.push_back(bottom);
	InputFace top;
	top.vertices.push_back(7);
	top.vertices.push_back(6);
	top.vertices.push_back(2);
	top.vertices.push_back(3);
	primitive_faces.push_back(top);

	return _append_component(primitive_vertices, primitive_faces);
}

Dictionary HalfEdgeMesh::add_uv_sphere(real_t p_radius, int p_radial_segments, int p_rings, const Transform3D &p_transform) {
	ERR_FAIL_COND_V_MSG(p_radius <= 0.0f, Dictionary(), "UV sphere radius must be positive.");
	ERR_FAIL_COND_V_MSG(p_radial_segments < 3, Dictionary(), "UV sphere requires at least three radial segments.");
	ERR_FAIL_COND_V_MSG(p_rings < 2, Dictionary(), "UV sphere requires at least two ring segments.");
	const real_t pi = (real_t)3.14159265358979323846;
	const real_t tau = (real_t)6.28318530717958647692;

	Vector<Vector3> primitive_vertices;
	Vector<InputFace> primitive_faces;

	primitive_vertices.push_back(p_transform.xform(Vector3(0, p_radius, 0)));
	for (int ring_index = 1; ring_index < p_rings; ring_index++) {
		const real_t phi = pi * (real_t)ring_index / (real_t)p_rings;
		const real_t y = Math::cos(phi) * p_radius;
		const real_t ring_radius = Math::sin(phi) * p_radius;
		for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
			const real_t theta = tau * (real_t)segment_index / (real_t)p_radial_segments;
			const Vector3 point(Math::cos(theta) * ring_radius, y, Math::sin(theta) * ring_radius);
			primitive_vertices.push_back(p_transform.xform(point));
		}
	}
	const int north_pole = 0;
	primitive_vertices.push_back(p_transform.xform(Vector3(0, -p_radius, 0)));
	const int south_pole = primitive_vertices.size() - 1;

	auto _ring_vertex = [&](int p_ring, int p_segment) {
		return 1 + p_ring * p_radial_segments + (p_segment % p_radial_segments);
	};

	for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
		InputFace top_triangle;
		top_triangle.vertices.push_back(north_pole);
		top_triangle.vertices.push_back(_ring_vertex(0, segment_index + 1));
		top_triangle.vertices.push_back(_ring_vertex(0, segment_index));
		primitive_faces.push_back(top_triangle);
	}

	for (int ring_index = 0; ring_index < p_rings - 2; ring_index++) {
		for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
			InputFace quad;
			quad.vertices.push_back(_ring_vertex(ring_index, segment_index));
			quad.vertices.push_back(_ring_vertex(ring_index, segment_index + 1));
			quad.vertices.push_back(_ring_vertex(ring_index + 1, segment_index + 1));
			quad.vertices.push_back(_ring_vertex(ring_index + 1, segment_index));
			primitive_faces.push_back(quad);
		}
	}

	for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
		InputFace bottom_triangle;
		bottom_triangle.vertices.push_back(south_pole);
		bottom_triangle.vertices.push_back(_ring_vertex(p_rings - 2, segment_index));
		bottom_triangle.vertices.push_back(_ring_vertex(p_rings - 2, segment_index + 1));
		primitive_faces.push_back(bottom_triangle);
	}

	return _append_component(primitive_vertices, primitive_faces);
}

Dictionary HalfEdgeMesh::add_icosphere(real_t p_radius, int p_subdivisions, const Transform3D &p_transform) {
	ERR_FAIL_COND_V_MSG(p_radius <= 0.0f, Dictionary(), "Icosphere radius must be positive.");
	ERR_FAIL_COND_V_MSG(p_subdivisions < 0, Dictionary(), "Icosphere subdivisions must be non-negative.");

	const real_t phi = (1.0f + Math::sqrt(5.0f)) * 0.5f;
	Vector<Vector3> primitive_vertices;
	primitive_vertices.push_back(Vector3(-1, phi, 0));
	primitive_vertices.push_back(Vector3(1, phi, 0));
	primitive_vertices.push_back(Vector3(-1, -phi, 0));
	primitive_vertices.push_back(Vector3(1, -phi, 0));
	primitive_vertices.push_back(Vector3(0, -1, phi));
	primitive_vertices.push_back(Vector3(0, 1, phi));
	primitive_vertices.push_back(Vector3(0, -1, -phi));
	primitive_vertices.push_back(Vector3(0, 1, -phi));
	primitive_vertices.push_back(Vector3(phi, 0, -1));
	primitive_vertices.push_back(Vector3(phi, 0, 1));
	primitive_vertices.push_back(Vector3(-phi, 0, -1));
	primitive_vertices.push_back(Vector3(-phi, 0, 1));
	for (int i = 0; i < primitive_vertices.size(); i++) {
		primitive_vertices.write[i] = primitive_vertices[i].normalized() * p_radius;
	}

	Vector<InputFace> primitive_faces;
	int base_faces[20][3] = {
		{ 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
		{ 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
		{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
		{ 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
	};
	for (int i = 0; i < 20; i++) {
		InputFace face;
		face.vertices.push_back(base_faces[i][0]);
		face.vertices.push_back(base_faces[i][1]);
		face.vertices.push_back(base_faces[i][2]);
		primitive_faces.push_back(face);
	}

	for (int subdivision = 0; subdivision < p_subdivisions; subdivision++) {
		HashMap<uint64_t, int> midpoint_cache;
		Vector<InputFace> subdivided_faces;
		subdivided_faces.reserve(primitive_faces.size() * 4);

		auto _get_midpoint = [&](int p_a, int p_b) {
			const uint64_t edge_key = _make_undirected_edge_key(p_a, p_b);
			if (midpoint_cache.has(edge_key)) {
				return midpoint_cache[edge_key];
			}
			const Vector3 midpoint = ((primitive_vertices[p_a] + primitive_vertices[p_b]) * 0.5f).normalized() * p_radius;
			primitive_vertices.push_back(midpoint);
			const int midpoint_index = primitive_vertices.size() - 1;
			midpoint_cache.insert(edge_key, midpoint_index);
			return midpoint_index;
		};

		for (int face_index = 0; face_index < primitive_faces.size(); face_index++) {
			const int a = primitive_faces[face_index].vertices[0];
			const int b = primitive_faces[face_index].vertices[1];
			const int c = primitive_faces[face_index].vertices[2];
			const int ab = _get_midpoint(a, b);
			const int bc = _get_midpoint(b, c);
			const int ca = _get_midpoint(c, a);

			InputFace face0;
			face0.vertices.push_back(a);
			face0.vertices.push_back(ab);
			face0.vertices.push_back(ca);
			subdivided_faces.push_back(face0);

			InputFace face1;
			face1.vertices.push_back(b);
			face1.vertices.push_back(bc);
			face1.vertices.push_back(ab);
			subdivided_faces.push_back(face1);

			InputFace face2;
			face2.vertices.push_back(c);
			face2.vertices.push_back(ca);
			face2.vertices.push_back(bc);
			subdivided_faces.push_back(face2);

			InputFace face3;
			face3.vertices.push_back(ab);
			face3.vertices.push_back(bc);
			face3.vertices.push_back(ca);
			subdivided_faces.push_back(face3);
		}

		primitive_faces = subdivided_faces;
	}

	for (int i = 0; i < primitive_vertices.size(); i++) {
		primitive_vertices.write[i] = p_transform.xform(primitive_vertices[i]);
	}

	return _append_component(primitive_vertices, primitive_faces);
}

Dictionary HalfEdgeMesh::add_cylinder(real_t p_radius, real_t p_height, int p_radial_segments, const Transform3D &p_transform) {
	ERR_FAIL_COND_V_MSG(p_radius <= 0.0f, Dictionary(), "Cylinder radius must be positive.");
	ERR_FAIL_COND_V_MSG(p_height <= 0.0f, Dictionary(), "Cylinder height must be positive.");
	ERR_FAIL_COND_V_MSG(p_radial_segments < 3, Dictionary(), "Cylinder requires at least three radial segments.");
	const real_t tau = (real_t)6.28318530717958647692;

	Vector<Vector3> primitive_vertices;
	const real_t half_height = p_height * 0.5f;
	for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
		const real_t theta = tau * (real_t)segment_index / (real_t)p_radial_segments;
		const real_t x = Math::cos(theta) * p_radius;
		const real_t z = Math::sin(theta) * p_radius;
		primitive_vertices.push_back(p_transform.xform(Vector3(x, -half_height, z)));
	}
	for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
		const real_t theta = tau * (real_t)segment_index / (real_t)p_radial_segments;
		const real_t x = Math::cos(theta) * p_radius;
		const real_t z = Math::sin(theta) * p_radius;
		primitive_vertices.push_back(p_transform.xform(Vector3(x, half_height, z)));
	}

	Vector<InputFace> primitive_faces;
	InputFace bottom_cap;
	for (int segment_index = p_radial_segments - 1; segment_index >= 0; segment_index--) {
		bottom_cap.vertices.push_back(segment_index);
	}
	primitive_faces.push_back(bottom_cap);

	for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
		InputFace quad;
		const int next_segment = (segment_index + 1) % p_radial_segments;
		quad.vertices.push_back(segment_index);
		quad.vertices.push_back(next_segment);
		quad.vertices.push_back(p_radial_segments + next_segment);
		quad.vertices.push_back(p_radial_segments + segment_index);
		primitive_faces.push_back(quad);
	}

	InputFace top_cap;
	for (int segment_index = 0; segment_index < p_radial_segments; segment_index++) {
		top_cap.vertices.push_back(p_radial_segments + segment_index);
	}
	primitive_faces.push_back(top_cap);

	return _append_component(primitive_vertices, primitive_faces);
}

PackedVector3Array HalfEdgeMesh::get_vertices() const {
	PackedVector3Array result;
	result.resize(vertices.size());
	for (int i = 0; i < vertices.size(); i++) {
		result.set(i, vertices[i].position);
	}
	return result;
}

Array HalfEdgeMesh::get_faces() const {
	Array result;
	for (int i = 0; i < input_faces.size(); i++) {
		result.push_back(_to_packed_array(input_faces[i].vertices));
	}
	return result;
}

PackedInt32Array HalfEdgeMesh::get_face_vertex_indices(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, input_faces.size(), PackedInt32Array());
	return _to_packed_array(input_faces[p_face].vertices);
}

Dictionary HalfEdgeMesh::get_face_data(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, faces.size(), Dictionary());
	Dictionary data;
	data["halfedge"] = faces[p_face].halfedge;
	data["plane"] = faces[p_face].plane;
	data["normal"] = faces[p_face].plane.normal;
	data["vertices"] = _to_packed_array(input_faces[p_face].vertices);
	return data;
}

Dictionary HalfEdgeMesh::get_face_projection(int p_face) const {
	ERR_FAIL_INDEX_V(p_face, input_faces.size(), Dictionary());
	FaceProjection projection;
	ERR_FAIL_COND_V(!_get_face_projection(input_faces[p_face].vertices, projection), Dictionary());

	PackedVector2Array projected_vertices;
	projected_vertices.resize(input_faces[p_face].vertices.size());
	for (int i = 0; i < input_faces[p_face].vertices.size(); i++) {
		projected_vertices.set(i, _project_point_to_2d(projection, input_vertices[input_faces[p_face].vertices[i]]));
	}

	Dictionary data;
	data["origin"] = projection.origin;
	data["tangent"] = projection.tangent;
	data["bitangent"] = projection.bitangent;
	data["normal"] = projection.plane.normal;
	data["vertices"] = projected_vertices;
	return data;
}

Dictionary HalfEdgeMesh::get_edge_data(int p_edge) const {
	ERR_FAIL_INDEX_V(p_edge, edges.size(), Dictionary());
	const int halfedge_index = edges[p_edge].halfedge;
	const int destination = halfedges[halfedges[halfedge_index].next].origin;
	PackedInt32Array edge_vertices;
	edge_vertices.push_back(halfedges[halfedge_index].origin);
	edge_vertices.push_back(destination);

	Dictionary data;
	data["halfedge"] = halfedge_index;
	data["vertices"] = edge_vertices;
	data["boundary"] = halfedges[halfedge_index].twin == -1;
	return data;
}

Dictionary HalfEdgeMesh::get_halfedge_data(int p_halfedge) const {
	ERR_FAIL_INDEX_V(p_halfedge, halfedges.size(), Dictionary());
	Dictionary data;
	data["origin"] = halfedges[p_halfedge].origin;
	data["destination"] = halfedges[halfedges[p_halfedge].next].origin;
	data["face"] = halfedges[p_halfedge].face;
	data["edge"] = halfedges[p_halfedge].edge;
	data["next"] = halfedges[p_halfedge].next;
	data["prev"] = halfedges[p_halfedge].prev;
	data["twin"] = halfedges[p_halfedge].twin;
	data["boundary"] = halfedges[p_halfedge].twin == -1;
	return data;
}

PackedInt32Array HalfEdgeMesh::cut(int p_face, const PackedVector2Array &p_points) {
	ERR_FAIL_INDEX_V(p_face, input_faces.size(), PackedInt32Array());
	ERR_FAIL_COND_V_MSG(p_points.size() < 3, PackedInt32Array(), "Cut requires at least three ordered points.");

	const Vector<int> base_face = input_faces[p_face].vertices;
	FaceProjection projection;
	ERR_FAIL_COND_V(!_get_face_projection(base_face, projection), PackedInt32Array());

	const Vector<Vector2> outer_polygon = _project_face_to_2d(base_face, projection);
	Vector<Vector2> cut_polygon;
	cut_polygon.resize(p_points.size());
	for (int i = 0; i < p_points.size(); i++) {
		cut_polygon.write[i] = p_points[i];
	}

	ERR_FAIL_COND_V_MSG(!_is_polygon_simple_2d(cut_polygon), PackedInt32Array(), "Cut points must form a simple polygon on the face plane.");
	for (int i = 0; i < cut_polygon.size(); i++) {
		ERR_FAIL_COND_V_MSG(!_is_point_in_or_on_polygon_2d(cut_polygon[i], outer_polygon), PackedInt32Array(), "Cut points must lie inside the target face.");
		const Vector2 from = cut_polygon[i];
		const Vector2 to = cut_polygon[(i + 1) % cut_polygon.size()];
		ERR_FAIL_COND_V_MSG(!_is_segment_inside_polygon_2d(from, to, outer_polygon), PackedInt32Array(), "Cut edges must stay inside the target face boundary.");
	}

	const Vector<Vector<Vector2>> clipped_polygons = Geometry2D::clip_polygons(outer_polygon, cut_polygon);
	ERR_FAIL_COND_V_MSG(clipped_polygons.is_empty(), PackedInt32Array(), "Cut polygon must remove a non-empty region from the face.");

	const bool outer_clockwise = Geometry2D::is_polygon_clockwise(outer_polygon);
	if (Geometry2D::is_polygon_clockwise(cut_polygon) != outer_clockwise) {
		cut_polygon.reverse();
	}

	const Vector<Vector3> original_vertices = input_vertices;
	const Vector<InputFace> original_faces = input_faces;

	Vector<int> cut_vertex_indices;
	cut_vertex_indices.resize(cut_polygon.size());
	for (int i = 0; i < cut_polygon.size(); i++) {
		input_vertices.push_back(_unproject_point_from_2d(projection, cut_polygon[i]));
		cut_vertex_indices.write[i] = input_vertices.size() - 1;
	}

	Vector<InputFace> replacement_faces;
	replacement_faces.reserve(clipped_polygons.size() + 1);
	for (int i = 0; i < clipped_polygons.size(); i++) {
		InputFace replacement_face;
		ERR_FAIL_COND_V_MSG(!_resolve_projected_polygon(clipped_polygons[i], outer_polygon, base_face, cut_polygon, cut_vertex_indices, replacement_face.vertices), PackedInt32Array(), "Failed to resolve the clipped cut polygon back to face indices.");
		replacement_faces.push_back(replacement_face);
	}

	InputFace inner_face;
	inner_face.vertices = cut_vertex_indices;
	replacement_faces.push_back(inner_face);

	input_faces.write[p_face] = replacement_faces[0];
	PackedInt32Array created_faces;
	created_faces.push_back(p_face);
	for (int i = 1; i < replacement_faces.size(); i++) {
		input_faces.push_back(replacement_faces[i]);
		created_faces.push_back(input_faces.size() - 1);
	}

	if (_rebuild_topology() != OK) {
		input_vertices = original_vertices;
		input_faces = original_faces;
		_rebuild_topology();
		ERR_FAIL_V_MSG(PackedInt32Array(), "Cut produced invalid topology for the target face.");
	}

	return created_faces;
}

PackedInt32Array HalfEdgeMesh::extrude(int p_face, real_t p_distance) {
	ERR_FAIL_INDEX_V(p_face, input_faces.size(), PackedInt32Array());
	ERR_FAIL_COND_V_MSG(input_faces[p_face].vertices.size() < 3, PackedInt32Array(), "Extrude requires a face with at least three vertices.");

	const Vector<int> base_face = input_faces[p_face].vertices;
	const Vector3 extrusion = faces[p_face].plane.normal.normalized() * p_distance;
	const Vector<Vector3> original_vertices = input_vertices;
	const Vector<InputFace> original_faces = input_faces;

	Vector<int> top_face;
	top_face.resize(base_face.size());
	for (int i = 0; i < base_face.size(); i++) {
		input_vertices.push_back(input_vertices[base_face[i]] + extrusion);
		top_face.write[i] = input_vertices.size() - 1;
	}

	PackedInt32Array created_faces;
	InputFace top;
	top.vertices = top_face;
	input_faces.push_back(top);
	created_faces.push_back(input_faces.size() - 1);

	for (int i = 0; i < base_face.size(); i++) {
		InputFace side_face;
		side_face.vertices.push_back(base_face[i]);
		side_face.vertices.push_back(base_face[(i + 1) % base_face.size()]);
		side_face.vertices.push_back(top_face[(i + 1) % top_face.size()]);
		side_face.vertices.push_back(top_face[i]);
		input_faces.push_back(side_face);
		created_faces.push_back(input_faces.size() - 1);
	}

	if (_rebuild_topology() != OK) {
		input_vertices = original_vertices;
		input_faces = original_faces;
		_rebuild_topology();
		ERR_FAIL_V_MSG(PackedInt32Array(), "Extrude produced invalid topology for the target face.");
	}

	return created_faces;
}

Dictionary HalfEdgeMesh::bevel(const PackedInt32Array &p_edges) {
	ERR_FAIL_COND_V_MSG(p_edges.is_empty(), Dictionary(), "Bevel requires at least one selected edge.");

	const Vector<Vector3> original_vertices = input_vertices;
	const Vector<InputFace> original_faces = input_faces;

	HashMap<int, bool> selected_edges;
	HashMap<int, bool> selected_vertices;
	for (int i = 0; i < p_edges.size(); i++) {
		const int edge_index = p_edges[i];
		ERR_FAIL_INDEX_V(edge_index, edges.size(), Dictionary());
		ERR_FAIL_COND_V_MSG(selected_edges.has(edge_index), Dictionary(), "Bevel edge selection must not contain duplicates.");
		const int halfedge_index = edges[edge_index].halfedge;
		ERR_FAIL_COND_V_MSG(halfedges[halfedge_index].twin == -1, Dictionary(), "Bevel currently requires interior manifold edges.");
		selected_edges.insert(edge_index, true);
		selected_vertices.insert(halfedges[halfedge_index].origin, true);
		selected_vertices.insert(halfedges[halfedges[halfedge_index].next].origin, true);
	}

	HashMap<uint64_t, int> corner_duplicates;
	HashMap<int, Vector<int>> vertex_face_duplicates;
	HashMap<int, Vector<int>> vertex_face_faces;
	for (const KeyValue<int, bool> &selected_vertex : selected_vertices) {
		Vector<int> ordered_faces;
		ERR_FAIL_COND_V_MSG(!_get_vertex_incident_faces(selected_vertex.key, ordered_faces), Dictionary(), "Failed to resolve the incident faces for a beveled vertex.");
		ERR_FAIL_COND_V_MSG(ordered_faces.size() < 3, Dictionary(), "Bevel vertices must have at least three incident faces.");

		Vector<int> duplicates;
		duplicates.resize(ordered_faces.size());
		for (int i = 0; i < ordered_faces.size(); i++) {
			input_vertices.push_back(input_vertices[selected_vertex.key]);
			const int duplicate_index = input_vertices.size() - 1;
			duplicates.write[i] = duplicate_index;
			corner_duplicates.insert(_make_face_vertex_key(ordered_faces[i], selected_vertex.key), duplicate_index);
		}

		Vector<int> reversed_faces = ordered_faces;
		reversed_faces.reverse();
		Vector<int> reversed_duplicates = duplicates;
		reversed_duplicates.reverse();
		vertex_face_faces.insert(selected_vertex.key, reversed_faces);
		vertex_face_duplicates.insert(selected_vertex.key, reversed_duplicates);
	}

	HashMap<int, bool> replaced_faces_set;
	for (const KeyValue<int, bool> &selected_vertex : selected_vertices) {
		const Vector<int> &incident_faces = vertex_face_faces[selected_vertex.key];
		for (int i = 0; i < incident_faces.size(); i++) {
			replaced_faces_set.insert(incident_faces[i], true);
		}
	}

	for (int face_index = 0; face_index < input_faces.size(); face_index++) {
		for (int vertex_offset = 0; vertex_offset < input_faces[face_index].vertices.size(); vertex_offset++) {
			const int vertex_index = input_faces[face_index].vertices[vertex_offset];
			const uint64_t corner_key = _make_face_vertex_key(face_index, vertex_index);
			if (corner_duplicates.has(corner_key)) {
				input_faces.write[face_index].vertices.write[vertex_offset] = corner_duplicates[corner_key];
			}
		}
	}

	PackedInt32Array vertex_faces_result;
	for (const KeyValue<int, Vector<int>> &entry : vertex_face_duplicates) {
		InputFace vertex_face;
		vertex_face.vertices = entry.value;
		input_faces.push_back(vertex_face);
		vertex_faces_result.push_back(input_faces.size() - 1);
	}

	PackedInt32Array edge_faces_result;
	for (const KeyValue<int, bool> &selected_edge : selected_edges) {
		const int halfedge_index = edges[selected_edge.key].halfedge;
		const int twin_halfedge = halfedges[halfedge_index].twin;
		const int face_a = halfedges[halfedge_index].face;
		const int face_b = halfedges[twin_halfedge].face;
		const int vertex_a = halfedges[halfedge_index].origin;
		const int vertex_b = halfedges[halfedges[halfedge_index].next].origin;

		InputFace edge_face;
		edge_face.vertices.push_back(corner_duplicates[_make_face_vertex_key(face_a, vertex_b)]);
		edge_face.vertices.push_back(corner_duplicates[_make_face_vertex_key(face_a, vertex_a)]);
		edge_face.vertices.push_back(corner_duplicates[_make_face_vertex_key(face_b, vertex_a)]);
		edge_face.vertices.push_back(corner_duplicates[_make_face_vertex_key(face_b, vertex_b)]);
		input_faces.push_back(edge_face);
		edge_faces_result.push_back(input_faces.size() - 1);
	}

	if (_rebuild_topology() != OK) {
		input_vertices = original_vertices;
		input_faces = original_faces;
		_rebuild_topology();
		ERR_FAIL_V_MSG(Dictionary(), "Bevel produced invalid topology for the selected edges.");
	}

	PackedInt32Array replaced_faces_result;
	for (const KeyValue<int, bool> &replaced_face : replaced_faces_set) {
		replaced_faces_result.push_back(replaced_face.key);
	}

	Dictionary result;
	result["edge_faces"] = edge_faces_result;
	result["vertex_faces"] = vertex_faces_result;
	result["replaced_faces"] = replaced_faces_result;
	return result;
}

Array HalfEdgeMesh::to_mesh_arrays() const {
	return _build_mesh_arrays();
}

Ref<ArrayMesh> HalfEdgeMesh::to_mesh() const {
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, _build_mesh_arrays());
	return mesh;
}