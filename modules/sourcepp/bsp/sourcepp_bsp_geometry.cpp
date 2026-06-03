/**************************************************************************/
/*  sourcepp_bsp_geometry.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "sourcepp_bsp_geometry.h"

#include "core/math/geometry_2d.h"
#include "core/math/math_funcs.h"
#include "scene/resources/mesh.h"

namespace {

constexpr float BSP_HALFEDGE_COPLANAR_EPSILON = 0.001f;
constexpr float BSP_HALFEDGE_COLLINEAR_EPSILON = 0.0001f;

bool _project_bsp_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, PackedVector2Array &r_projected_vertices) {
	Plane plane;
	if (!SourcePPBSPGeometry::compute_polygon_plane(p_vertices, p_polygon, plane)) {
		return false;
	}

	r_projected_vertices.resize(p_polygon.size());
	const Vector3 normal = plane.normal.normalized();
	const Vector3 reference = Math::abs(normal.dot(Vector3(0, 1, 0))) < 0.999f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
	const Vector3 tangent = normal.cross(reference).normalized();
	const Vector3 bitangent = normal.cross(tangent).normalized();

	for (int i = 0; i < p_polygon.size(); i++) {
		const Vector3 &vertex = p_vertices[p_polygon[i]];
		if (Math::abs(plane.distance_to(vertex)) > BSP_HALFEDGE_COPLANAR_EPSILON) {
			return false;
		}
		r_projected_vertices.set(i, Vector2(tangent.dot(vertex), bitangent.dot(vertex)));
	}

	return true;
}

} // namespace

namespace SourcePPBSPGeometry {

bool compute_polygon_plane(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, Plane &r_plane) {
	if (p_polygon.size() < 3) {
		return false;
	}

	const int origin_index = p_polygon[0];
	for (int i = 1; i < p_polygon.size() - 1; i++) {
		const Vector3 &a = p_vertices[origin_index];
		const Vector3 &b = p_vertices[p_polygon[i]];
		const Vector3 &c = p_vertices[p_polygon[i + 1]];
		if ((b - a).cross(c - a).length() <= BSP_HALFEDGE_COLLINEAR_EPSILON) {
			continue;
		}
		r_plane = Plane(a, b, c);
		return true;
	}

	return false;
}

bool simplify_polygon(const PackedVector3Array &p_vertices, PackedInt32Array &r_polygon, PackedVector2Array *r_uvs) {
	if (r_polygon.size() < 3) {
		return false;
	}

	bool changed = true;
	while (changed && r_polygon.size() >= 3) {
		changed = false;
		for (int i = 0; i < r_polygon.size(); i++) {
			const int prev_index = (i - 1 + r_polygon.size()) % r_polygon.size();
			const int next_index = (i + 1) % r_polygon.size();
			const int prev_vertex = r_polygon[prev_index];
			const int vertex = r_polygon[i];
			const int next_vertex = r_polygon[next_index];

			if (prev_vertex == vertex || vertex == next_vertex || prev_vertex == next_vertex) {
				r_polygon.remove_at(i);
				if (r_uvs != nullptr && i < r_uvs->size()) {
					r_uvs->remove_at(i);
				}
				changed = true;
				break;
			}

			const Vector3 &prev = p_vertices[prev_vertex];
			const Vector3 &current = p_vertices[vertex];
			const Vector3 &next = p_vertices[next_vertex];
			if ((current - prev).cross(next - current).length() <= BSP_HALFEDGE_COLLINEAR_EPSILON) {
				r_polygon.remove_at(i);
				if (r_uvs != nullptr && i < r_uvs->size()) {
					r_uvs->remove_at(i);
				}
				changed = true;
				break;
			}
		}
	}

	if (r_polygon.size() < 3) {
		return false;
	}

	for (int i = 0; i < r_polygon.size(); i++) {
		for (int j = i + 1; j < r_polygon.size(); j++) {
			if (r_polygon[i] == r_polygon[j]) {
				return false;
			}
		}
	}

	return true;
}

void reverse_polygon(PackedInt32Array &r_polygon) {
	for (int i = 1; i < (r_polygon.size() + 1) / 2; i++) {
		const int opposite_index = r_polygon.size() - i;
		const int value = r_polygon[i];
		r_polygon.set(i, r_polygon[opposite_index]);
		r_polygon.set(opposite_index, value);
	}
}

void reverse_polygon_uvs(PackedVector2Array &r_uvs) {
	for (int i = 1; i < (r_uvs.size() + 1) / 2; i++) {
		const int opposite_index = r_uvs.size() - i;
		const Vector2 value = r_uvs[i];
		r_uvs.set(i, r_uvs[opposite_index]);
		r_uvs.set(opposite_index, value);
	}
}

PackedInt32Array triangulate_projected_polygon_preserve_winding(const PackedVector2Array &p_projected_vertices) {
	PackedInt32Array triangulated_indices = Geometry2D::triangulate_polygon(p_projected_vertices);
	if (!triangulated_indices.is_empty()) {
		return triangulated_indices;
	}

	PackedVector2Array reversed_projected_vertices;
	reversed_projected_vertices.resize(p_projected_vertices.size());
	for (int i = 0; i < p_projected_vertices.size(); i++) {
		reversed_projected_vertices.set(i, p_projected_vertices[p_projected_vertices.size() - 1 - i]);
	}

	triangulated_indices = Geometry2D::triangulate_polygon(reversed_projected_vertices);
	if (triangulated_indices.is_empty()) {
		return triangulated_indices;
	}

	PackedInt32Array remapped_indices;
	remapped_indices.resize(triangulated_indices.size());
	for (int i = 0; i < triangulated_indices.size(); i += 3) {
		if (i + 2 >= triangulated_indices.size()) {
			break;
		}

		remapped_indices.set(i, p_projected_vertices.size() - 1 - triangulated_indices[i]);
		remapped_indices.set(i + 1, p_projected_vertices.size() - 1 - triangulated_indices[i + 2]);
		remapped_indices.set(i + 2, p_projected_vertices.size() - 1 - triangulated_indices[i + 1]);
	}

	return remapped_indices;
}

bool is_halfedge_compatible_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon) {
	PackedVector2Array projected_vertices;
	if (!_project_bsp_polygon(p_vertices, p_polygon, projected_vertices)) {
		return false;
	}

	return !triangulate_projected_polygon_preserve_winding(projected_vertices).is_empty();
}

void append_triangulated_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, const PackedVector2Array &p_uvs, int p_material_id, bool p_reverse_winding, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs) {
	PackedVector2Array projected_vertices;
	if (!_project_bsp_polygon(p_vertices, p_polygon, projected_vertices)) {
		return;
	}

	const PackedInt32Array triangulated_indices = triangulate_projected_polygon_preserve_winding(projected_vertices);
	for (int i = 0; i < triangulated_indices.size(); i += 3) {
		if (i + 2 >= triangulated_indices.size()) {
			break;
		}

		PackedInt32Array triangle;
		PackedVector2Array triangle_uvs;
		triangle.push_back(p_polygon[triangulated_indices[i]]);
		triangle.push_back(p_polygon[triangulated_indices[i + 1]]);
		triangle.push_back(p_polygon[triangulated_indices[i + 2]]);
		triangle_uvs.push_back(p_uvs[triangulated_indices[i]]);
		triangle_uvs.push_back(p_uvs[triangulated_indices[i + 1]]);
		triangle_uvs.push_back(p_uvs[triangulated_indices[i + 2]]);
		if (p_reverse_winding) {
			const int value = triangle[1];
			triangle.set(1, triangle[2]);
			triangle.set(2, value);
			const Vector2 uv = triangle_uvs[1];
			triangle_uvs.set(1, triangle_uvs[2]);
			triangle_uvs.set(2, uv);
		}

		Plane triangle_plane;
		if (!compute_polygon_plane(p_vertices, triangle, triangle_plane)) {
			continue;
		}

		r_faces.push_back(triangle);
		r_face_material_ids.push_back(p_material_id);
		r_face_uvs.push_back(triangle_uvs);
	}
}

PackedVector3Array build_collision_faces_from_mesh(const Ref<ArrayMesh> &p_mesh) {
	PackedVector3Array faces;
	if (p_mesh.is_null()) {
		return faces;
	}

	for (int surface_index = 0; surface_index < p_mesh->get_surface_count(); surface_index++) {
		if (p_mesh->surface_get_primitive_type(surface_index) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}

		const Array arrays = p_mesh->surface_get_arrays(surface_index);
		if (arrays.size() <= Mesh::ARRAY_VERTEX || arrays[Mesh::ARRAY_VERTEX].get_type() != Variant::PACKED_VECTOR3_ARRAY) {
			continue;
		}

		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		if (arrays.size() > Mesh::ARRAY_INDEX && arrays[Mesh::ARRAY_INDEX].get_type() == Variant::PACKED_INT32_ARRAY) {
			const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
			for (int i = 0; i + 2 < indices.size(); i += 3) {
				const int a = indices[i + 0];
				const int b = indices[i + 1];
				const int c = indices[i + 2];
				if (a < 0 || b < 0 || c < 0 || a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
					continue;
				}
				faces.push_back(vertices[a]);
				faces.push_back(vertices[b]);
				faces.push_back(vertices[c]);
			}
		} else {
			for (int i = 0; i + 2 < vertices.size(); i += 3) {
				faces.push_back(vertices[i + 0]);
				faces.push_back(vertices[i + 1]);
				faces.push_back(vertices[i + 2]);
			}
		}
	}

	return faces;
}

} // namespace SourcePPBSPGeometry
