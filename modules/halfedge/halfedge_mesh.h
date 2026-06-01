/**************************************************************************/
/*  halfedge_mesh.h                                                       */
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

#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "scene/resources/mesh.h"

class ArrayMesh;

class HalfEdgeMesh : public RefCounted {
	GDCLASS(HalfEdgeMesh, RefCounted);

	struct InputFace {
		Vector<int> vertices;
	};

	struct VertexData {
		Vector3 position;
		int halfedge = -1;
	};

	struct HalfEdgeData {
		int origin = -1;
		int face = -1;
		int next = -1;
		int prev = -1;
		int twin = -1;
		int edge = -1;
	};

	struct EdgeData {
		int halfedge = -1;
	};

	struct FaceData {
		int halfedge = -1;
		Plane plane;
	};

	struct FaceProjection {
		Plane plane;
		Vector3 origin;
		Vector3 tangent;
		Vector3 bitangent;
	};

	Vector<Vector3> input_vertices;
	Vector<InputFace> input_faces;

	Vector<VertexData> vertices;
	Vector<HalfEdgeData> halfedges;
	Vector<EdgeData> edges;
	Vector<FaceData> faces;

	real_t coplanar_epsilon = 0.001f;
	real_t collinear_epsilon = 0.0001f;

	static void _bind_methods();

	static uint64_t _make_edge_key(int p_from, int p_to);
	static uint64_t _make_face_vertex_key(int p_face, int p_vertex);
	static uint64_t _make_undirected_edge_key(int p_a, int p_b);
	static Vector<int> _to_vector(const PackedInt32Array &p_array);
	static PackedInt32Array _to_packed_array(const Vector<int> &p_values);

	bool _parse_face_variant(const Variant &p_face, Vector<int> &r_face) const;
	bool _validate_face(const Vector<int> &p_face, String *r_error = nullptr) const;
	bool _get_vertex_incident_faces(int p_vertex, Vector<int> &r_faces) const;
	bool _compute_face_plane(const Vector<int> &p_face, Plane &r_plane) const;
	bool _get_face_projection(const Vector<int> &p_face, FaceProjection &r_projection) const;
	Vector<Vector2> _project_face_to_2d(const Vector<int> &p_face, const FaceProjection &p_projection) const;
	Vector2 _project_point_to_2d(const FaceProjection &p_projection, const Vector3 &p_point) const;
	Vector3 _unproject_point_from_2d(const FaceProjection &p_projection, const Vector2 &p_point) const;
	Vector2 _project_point_to_face_plane(const Plane &p_plane, const Vector3 &p_point) const;
	bool _is_face_simple(const Vector<int> &p_face) const;
	bool _simplify_face(Vector<int> &r_face) const;
	bool _is_polygon_simple_2d(const Vector<Vector2> &p_polygon) const;
	bool _is_point_in_or_on_polygon_2d(const Vector2 &p_point, const Vector<Vector2> &p_polygon) const;
	bool _is_segment_inside_polygon_2d(const Vector2 &p_from, const Vector2 &p_to, const Vector<Vector2> &p_polygon) const;
	bool _resolve_projected_polygon(const Vector<Vector2> &p_polygon, const Vector<Vector2> &p_outer_points, const Vector<int> &p_outer_indices, const Vector<Vector2> &p_inner_points, const Vector<int> &p_inner_indices, Vector<int> &r_face) const;
	bool _are_faces_coplanar(const Vector<int> &p_face_a, const Vector<int> &p_face_b) const;
	bool _try_merge_faces(const Vector<int> &p_face_a, const Vector<int> &p_face_b, Vector<int> &r_merged) const;
	void _merge_coplanar_faces(Vector<InputFace> &r_faces) const;
	Dictionary _append_component(const Vector<Vector3> &p_vertices, const Vector<InputFace> &p_faces);
	Error _rebuild_topology();
	Array _build_mesh_arrays() const;

public:
	void clear();

	void set_coplanar_epsilon(real_t p_epsilon);
	real_t get_coplanar_epsilon() const { return coplanar_epsilon; }

	void set_collinear_epsilon(real_t p_epsilon);
	real_t get_collinear_epsilon() const { return collinear_epsilon; }

	int get_vertex_count() const { return vertices.size(); }
	int get_halfedge_count() const { return halfedges.size(); }
	int get_edge_count() const { return edges.size(); }
	int get_face_count() const { return faces.size(); }

	int add_vertex(const Vector3 &p_position);
	void set_vertex_position(int p_vertex, const Vector3 &p_position);
	Vector3 get_vertex_position(int p_vertex) const;

	int add_face(const PackedInt32Array &p_vertex_indices);
	bool remove_face(int p_face);
	void compact_vertices();

	Error set_faces(const PackedVector3Array &p_vertices, const Array &p_faces);
	Error build_from_triangle_arrays(const PackedVector3Array &p_vertices, const PackedInt32Array &p_indices = PackedInt32Array());
	Error build_from_arrays(const Array &p_arrays);
	Error build_from_mesh(const Ref<Mesh> &p_mesh, int p_surface = 0);
	Dictionary add_box(const Vector3 &p_size = Vector3(1, 1, 1), const Transform3D &p_transform = Transform3D());
	Dictionary add_uv_sphere(real_t p_radius = 0.5f, int p_radial_segments = 16, int p_rings = 8, const Transform3D &p_transform = Transform3D());
	Dictionary add_icosphere(real_t p_radius = 0.5f, int p_subdivisions = 1, const Transform3D &p_transform = Transform3D());
	Dictionary add_cylinder(real_t p_radius = 0.5f, real_t p_height = 1.0f, int p_radial_segments = 16, const Transform3D &p_transform = Transform3D());

	PackedVector3Array get_vertices() const;
	Array get_faces() const;
	PackedInt32Array get_face_vertex_indices(int p_face) const;
	Dictionary get_face_data(int p_face) const;
	Dictionary get_face_projection(int p_face) const;
	Dictionary get_edge_data(int p_edge) const;
	Dictionary get_halfedge_data(int p_halfedge) const;
	PackedInt32Array cut(int p_face, const PackedVector2Array &p_points);
	PackedInt32Array extrude(int p_face, real_t p_distance);
	Dictionary bevel(const PackedInt32Array &p_edges);

	Array to_mesh_arrays() const;
	Ref<ArrayMesh> to_mesh() const;

	HalfEdgeMesh() = default;
};