/**************************************************************************/
/*  sourcepp_bsp_entity_utils.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include <bsppp/EntityLump.h>

class ArrayMesh;
class CollisionObject3D;
class Node3D;

namespace SourcePPBSPEntityUtils {

Array parse_source_outputs(const bsppp::BSPEntityKeyValues &p_entity);

bool is_trigger_class(const String &p_classname);
bool is_body_class(const String &p_classname);
bool is_visual_only_class(const String &p_classname);
bool is_physics_entity_class(const String &p_classname);
bool is_static_entity_class(const String &p_classname);
bool is_dynamic_model_entity_class(const String &p_classname);
bool is_source_model_path(const String &p_model);
String normalize_source_model_path(const String &p_model);

Node3D *create_brush_entity_node(const String &p_classname);
Node3D *create_point_entity_node(const String &p_classname);
void setup_entity_node(Node3D *p_node, const String &p_classname, const String &p_targetname, int p_entity_index, int p_model_index, const Dictionary &p_keyvalues, const Array &p_outputs);
void configure_specific_node(Node3D *p_node, const Dictionary &p_keyvalues);

void add_geometry_child(Node3D *p_node, const Ref<ArrayMesh> &p_mesh, const String &p_source_path, int p_model_index, int p_entity_index, const Dictionary &p_asset_metadata);
void add_collision_child(Node3D *p_node, const Ref<ArrayMesh> &p_mesh, const String &p_source_path, int p_model_index, int p_entity_index, bool p_disabled = false);
void add_bounds_collision_child(CollisionObject3D *p_body, Node3D *p_model_node, const String &p_source_path, int p_entity_index, const String &p_model_path);

} // namespace SourcePPBSPEntityUtils
