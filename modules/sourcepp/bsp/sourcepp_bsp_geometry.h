/**************************************************************************/
/*  sourcepp_bsp_geometry.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/math/plane.h"
#include "core/variant/array.h"
#include "scene/resources/mesh.h"

class ArrayMesh;

namespace SourcePPBSPGeometry {

bool compute_polygon_plane(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, Plane &r_plane);
bool simplify_polygon(const PackedVector3Array &p_vertices, PackedInt32Array &r_polygon, PackedVector2Array *r_uvs = nullptr);
void reverse_polygon(PackedInt32Array &r_polygon);
void reverse_polygon_uvs(PackedVector2Array &r_uvs);
PackedInt32Array triangulate_projected_polygon_preserve_winding(const PackedVector2Array &p_projected_vertices);
bool is_halfedge_compatible_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon);
void append_triangulated_polygon(const PackedVector3Array &p_vertices, const PackedInt32Array &p_polygon, const PackedVector2Array &p_uvs, int p_material_id, bool p_reverse_winding, Array &r_faces, PackedInt32Array &r_face_material_ids, Array &r_face_uvs);
PackedVector3Array build_collision_faces_from_mesh(const Ref<ArrayMesh> &p_mesh);

} // namespace SourcePPBSPGeometry
