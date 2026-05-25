#include <mdlpp/mdlpp.h>

#include <BufferStream.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace {

constexpr uint8_t STUDIO_ANIM_RAWPOS = 0x01;
constexpr uint8_t STUDIO_ANIM_RAWROT = 0x02;
constexpr uint8_t STUDIO_ANIM_ANIMPOS = 0x04;
constexpr uint8_t STUDIO_ANIM_ANIMROT = 0x08;
constexpr uint8_t STUDIO_ANIM_DELTA = 0x10;
constexpr uint8_t STUDIO_ANIM_RAWROT2 = 0x20;

struct RawAnimValue {
	union {
		struct {
			uint8_t valid;
			uint8_t total;
		} num;
		int16_t value;
	};
};
static_assert(sizeof(RawAnimValue) == sizeof(int16_t));

struct RawAnimValuePtr {
	int16_t offset[3];
};
static_assert(std::is_trivially_copyable_v<RawAnimValuePtr>);

struct RawAnimSections {
	int32_t animBlock;
	int32_t animIndex;
};
static_assert(std::is_trivially_copyable_v<RawAnimSections>);

struct RawCompressedIKError {
	float scale[6];
	int16_t offset[6];
};
static_assert(std::is_trivially_copyable_v<RawCompressedIKError>);

struct RawLocalHierarchy {
	int32_t bone;
	int32_t newParent;
	float start;
	float peak;
	float tail;
	float end;
	int32_t startFrame;
	int32_t localAnimIndex;
	int32_t unused[4];
};
static_assert(std::is_trivially_copyable_v<RawLocalHierarchy>);

struct ResolvedAnimStream {
	const std::byte *data = nullptr;
	std::size_t size = 0;
	uint64_t animOffset = 0;
	int localFrame = 0;
};

struct RawAnimHeader {
	uint8_t bone;
	uint8_t flags;
	int16_t nextOffset;
};
static_assert(sizeof(RawAnimHeader) == 4);

struct BoneWorldTransform {
	sourcepp::math::Vec3f position{};
	sourcepp::math::Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

[[nodiscard]] sourcepp::math::Quat _angle_quaternion(const sourcepp::math::Vec3f &p_angles) {
	const float half_x = p_angles[0] * 0.5f;
	const float half_y = p_angles[1] * 0.5f;
	const float half_z = p_angles[2] * 0.5f;

	const float sr = std::sin(half_x);
	const float cr = std::cos(half_x);
	const float sp = std::sin(half_y);
	const float cp = std::cos(half_y);
	const float sy = std::sin(half_z);
	const float cy = std::cos(half_z);

	const float sr_x_cp = sr * cp;
	const float cr_x_sp = cr * sp;
	const float cr_x_cp = cr * cp;
	const float sr_x_sp = sr * sp;

	return {
		(sr_x_cp * cy) - (cr_x_sp * sy),
		(cr_x_sp * cy) + (sr_x_cp * sy),
		(cr_x_cp * sy) - (sr_x_sp * cy),
		(cr_x_cp * cy) + (sr_x_sp * sy),
	};
}

void _align_quaternion(const sourcepp::math::Quat &p_reference, sourcepp::math::Quat &r_quat) {
	float positive = 0.0f;
	float negative = 0.0f;
	for (uint8_t i = 0; i < 4; i++) {
		const float delta = p_reference[i] - r_quat[i];
		const float sum = p_reference[i] + r_quat[i];
		positive += delta * delta;
		negative += sum * sum;
	}
	if (positive > negative) {
		for (uint8_t i = 0; i < 4; i++) {
			r_quat[i] = -r_quat[i];
		}
	}
}

[[nodiscard]] float _extract_anim_value(BufferStreamReadOnly &p_stream, uint64_t p_value_offset, int p_frame, float p_scale) {
	if (p_value_offset == 0) {
		return 0.0f;
	}
	if (p_frame < 0) {
		p_frame = 0;
	}

	uint64_t current_offset = p_value_offset;
	int frame = p_frame;
	while (true) {
		const RawAnimValue span = p_stream.at<RawAnimValue>(static_cast<int64_t>(current_offset));
		if (span.num.total == 0) {
			return 0.0f;
		}
		if (span.num.total > frame) {
			if (span.num.valid > frame) {
				return static_cast<float>(p_stream.at<RawAnimValue>(static_cast<int64_t>(current_offset + sizeof(RawAnimValue) * static_cast<uint64_t>(frame + 1))).value) * p_scale;
			}
			return static_cast<float>(p_stream.at<RawAnimValue>(static_cast<int64_t>(current_offset + sizeof(RawAnimValue) * static_cast<uint64_t>(span.num.valid))).value) * p_scale;
		}

		frame -= span.num.total;
		current_offset += sizeof(RawAnimValue) * static_cast<uint64_t>(span.num.valid + 1);
	}
}

[[nodiscard]] sourcepp::math::Quat _quat_normalized(const sourcepp::math::Quat &p_quat) {
	const float length_sq = p_quat.dot(p_quat);
	if (length_sq <= 0.0f) {
		return {0.0f, 0.0f, 0.0f, 1.0f};
	}
	return p_quat / std::sqrt(length_sq);
}

[[nodiscard]] sourcepp::math::Quat _quat_conjugated(const sourcepp::math::Quat &p_quat) {
	return {-p_quat[0], -p_quat[1], -p_quat[2], p_quat[3]};
}

[[nodiscard]] sourcepp::math::Quat _quat_multiplied(const sourcepp::math::Quat &p_a, const sourcepp::math::Quat &p_b) {
	return {
		p_a[3] * p_b[0] + p_a[0] * p_b[3] + p_a[1] * p_b[2] - p_a[2] * p_b[1],
		p_a[3] * p_b[1] - p_a[0] * p_b[2] + p_a[1] * p_b[3] + p_a[2] * p_b[0],
		p_a[3] * p_b[2] + p_a[0] * p_b[1] - p_a[1] * p_b[0] + p_a[2] * p_b[3],
		p_a[3] * p_b[3] - p_a[0] * p_b[0] - p_a[1] * p_b[1] - p_a[2] * p_b[2],
	};
}

[[nodiscard]] sourcepp::math::Vec3f _quat_rotate_vector(const sourcepp::math::Quat &p_quat, const sourcepp::math::Vec3f &p_vector) {
	const sourcepp::math::Quat normalized = _quat_normalized(p_quat);
	const sourcepp::math::Quat vector_quat{p_vector[0], p_vector[1], p_vector[2], 0.0f};
	const sourcepp::math::Quat rotated = _quat_multiplied(_quat_multiplied(normalized, vector_quat), _quat_conjugated(normalized));
	return {rotated[0], rotated[1], rotated[2]};
}

[[nodiscard]] sourcepp::math::Quat _quat_slerp(sourcepp::math::Quat p_from, sourcepp::math::Quat p_to, float p_weight) {
	float dot = p_from.dot(p_to);
	if (dot < 0.0f) {
		p_to = p_to * -1.0f;
		dot = -dot;
	}

	if (dot > 0.9995f) {
		return _quat_normalized((p_from * (1.0f - p_weight)) + (p_to * p_weight));
	}

	const float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
	const float sin_theta = std::sin(theta);
	if (std::abs(sin_theta) <= 1e-6f) {
		return _quat_normalized(p_from);
	}

	const float weight_from = std::sin((1.0f - p_weight) * theta) / sin_theta;
	const float weight_to = std::sin(p_weight * theta) / sin_theta;
	return _quat_normalized((p_from * weight_from) + (p_to * weight_to));
}

[[nodiscard]] float _simple_spline(float p_value) {
	const float clamped = std::clamp(p_value, 0.0f, 1.0f);
	return clamped * clamped * (3.0f - (2.0f * clamped));
}

[[nodiscard]] bool _resolve_local_hierarchy_stream(const mdlpp::StudioModel &p_model, const mdlpp::MDL::AnimDesc &p_anim_desc, const std::byte *&r_data, std::size_t &r_size, uint64_t &r_offset) {
	r_data = nullptr;
	r_size = 0;
	r_offset = 0;

	if (p_anim_desc.localHierarchyCount <= 0 || p_anim_desc.localHierarchyIndex <= 0) {
		return false;
	}

	if (p_anim_desc.animBlock == 0) {
		r_data = p_model.getMDLData().data();
		r_size = p_model.getMDLData().size();
		r_offset = p_anim_desc.fileOffset + static_cast<uint64_t>(p_anim_desc.localHierarchyIndex);
		return r_data != nullptr && r_offset < r_size;
	}

	const auto &anim_block_data = p_model.getAnimBlockData();
	if (anim_block_data.empty()) {
		return false;
	}
	if (p_anim_desc.animBlock < 0 || p_anim_desc.animBlock >= static_cast<int32_t>(p_model.mdl.animBlocks.size())) {
		return false;
	}

	const auto &anim_block = p_model.mdl.animBlocks[static_cast<size_t>(p_anim_desc.animBlock)];
	const uint64_t hierarchy_offset = static_cast<uint64_t>(anim_block.dataStart) + static_cast<uint64_t>(p_anim_desc.localHierarchyIndex);
	if (hierarchy_offset >= anim_block_data.size() || hierarchy_offset >= static_cast<uint64_t>(anim_block.dataEnd)) {
		return false;
	}

	r_data = anim_block_data.data();
	r_size = anim_block_data.size();
	r_offset = hierarchy_offset;
	return true;
}

void _decompress_local_hierarchy_animation(BufferStreamReadOnly &p_stream, uint64_t p_compressed_offset, int p_frame, sourcepp::math::Vec3f &r_position, sourcepp::math::Quat &r_rotation) {
	const RawCompressedIKError compressed = p_stream.at<RawCompressedIKError>(static_cast<int64_t>(p_compressed_offset));
	for (int axis = 0; axis < 3; axis++) {
		const uint64_t value_offset = compressed.offset[axis] > 0 ? p_compressed_offset + static_cast<uint64_t>(compressed.offset[axis]) : 0;
		r_position[axis] = _extract_anim_value(p_stream, value_offset, p_frame, compressed.scale[axis]);
	}

	sourcepp::math::Vec3f angles{};
	for (int axis = 0; axis < 3; axis++) {
		const uint64_t value_offset = compressed.offset[axis + 3] > 0 ? p_compressed_offset + static_cast<uint64_t>(compressed.offset[axis + 3]) : 0;
		angles[axis] = _extract_anim_value(p_stream, value_offset, p_frame, compressed.scale[axis + 3]);
	}
	r_rotation = _angle_quaternion(angles);
}

[[nodiscard]] BoneWorldTransform _compute_bone_world_transform(const mdlpp::StudioModel &p_model, const std::vector<sourcepp::math::Vec3f> &p_positions, const std::vector<sourcepp::math::Quat> &p_rotations, int p_bone, std::vector<BoneWorldTransform> &r_world_transforms, std::vector<uint8_t> &r_computed) {
	if (p_bone < 0 || p_bone >= static_cast<int>(p_positions.size())) {
		return {};
	}
	if (r_computed[static_cast<size_t>(p_bone)] != 0) {
		return r_world_transforms[static_cast<size_t>(p_bone)];
	}

	BoneWorldTransform transform;
	transform.position = p_positions[static_cast<size_t>(p_bone)];
	transform.rotation = _quat_normalized(p_rotations[static_cast<size_t>(p_bone)]);

	const int parent = p_model.mdl.bones[static_cast<size_t>(p_bone)].parent;
	if (parent >= 0) {
		const BoneWorldTransform parent_transform = _compute_bone_world_transform(p_model, p_positions, p_rotations, parent, r_world_transforms, r_computed);
		transform.position = parent_transform.position + _quat_rotate_vector(parent_transform.rotation, transform.position);
		transform.rotation = _quat_normalized(_quat_multiplied(parent_transform.rotation, transform.rotation));
	}

	r_world_transforms[static_cast<size_t>(p_bone)] = transform;
	r_computed[static_cast<size_t>(p_bone)] = 1;
	return transform;
}

void _apply_local_hierarchy(const mdlpp::StudioModel &p_model, const mdlpp::MDL::AnimDesc &p_anim_desc, int p_frame, float p_cycle, std::vector<sourcepp::math::Vec3f> &r_positions, std::vector<sourcepp::math::Quat> &r_rotations) {
	const std::byte *hierarchy_data = nullptr;
	std::size_t hierarchy_size = 0;
	uint64_t hierarchy_offset = 0;
	if (!_resolve_local_hierarchy_stream(p_model, p_anim_desc, hierarchy_data, hierarchy_size, hierarchy_offset)) {
		return;
	}

	BufferStreamReadOnly stream{hierarchy_data, hierarchy_size};
	for (int hierarchy_index = 0; hierarchy_index < p_anim_desc.localHierarchyCount; hierarchy_index++) {
		const uint64_t entry_offset = hierarchy_offset + static_cast<uint64_t>(hierarchy_index) * sizeof(RawLocalHierarchy);
		if (entry_offset + sizeof(RawLocalHierarchy) > hierarchy_size) {
			break;
		}

		const RawLocalHierarchy hierarchy = stream.at<RawLocalHierarchy>(static_cast<int64_t>(entry_offset));
		if (hierarchy.bone < 0 || hierarchy.bone >= static_cast<int>(p_model.mdl.bones.size())) {
			continue;
		}

		float weight = 1.0f;
		if (hierarchy.tail - hierarchy.peak < 1.0f) {
			float index = p_cycle;
			if (hierarchy.end > 1.0f && index < hierarchy.start) {
				index += 1.0f;
			}
			if (index < hierarchy.start || index >= hierarchy.end) {
				continue;
			}
			if (index < hierarchy.peak && hierarchy.start != hierarchy.peak) {
				weight = (index - hierarchy.start) / (hierarchy.peak - hierarchy.start);
			} else if (index > hierarchy.tail && hierarchy.end != hierarchy.tail) {
				weight = (hierarchy.end - index) / (hierarchy.end - hierarchy.tail);
			}
			weight = _simple_spline(weight);
		}

		const uint64_t compressed_offset = entry_offset + static_cast<uint64_t>(hierarchy.localAnimIndex);
		if (compressed_offset >= hierarchy_size) {
			continue;
		}

		sourcepp::math::Vec3f local_position{};
		sourcepp::math::Quat local_rotation{};
		_decompress_local_hierarchy_animation(stream, compressed_offset, std::max(p_frame - hierarchy.startFrame, 0), local_position, local_rotation);

		std::vector<BoneWorldTransform> world_transforms(r_positions.size());
		std::vector<uint8_t> computed(r_positions.size(), 0);
		const BoneWorldTransform current_world = _compute_bone_world_transform(p_model, r_positions, r_rotations, hierarchy.bone, world_transforms, computed);

		BoneWorldTransform target_world;
		if (hierarchy.newParent >= 0 && hierarchy.newParent < static_cast<int>(p_model.mdl.bones.size())) {
			const BoneWorldTransform new_parent_world = _compute_bone_world_transform(p_model, r_positions, r_rotations, hierarchy.newParent, world_transforms, computed);
			target_world.position = new_parent_world.position + _quat_rotate_vector(new_parent_world.rotation, local_position);
			target_world.rotation = _quat_normalized(_quat_multiplied(new_parent_world.rotation, local_rotation));
		} else {
			target_world.position = local_position;
			target_world.rotation = _quat_normalized(local_rotation);
		}

		sourcepp::math::Vec3f target_local_position = target_world.position;
		sourcepp::math::Quat target_local_rotation = target_world.rotation;
		const int original_parent = p_model.mdl.bones[static_cast<size_t>(hierarchy.bone)].parent;
		if (original_parent >= 0) {
			const BoneWorldTransform parent_world = _compute_bone_world_transform(p_model, r_positions, r_rotations, original_parent, world_transforms, computed);
			const sourcepp::math::Quat inverse_parent_rotation = _quat_conjugated(parent_world.rotation);
			target_local_position = _quat_rotate_vector(inverse_parent_rotation, target_world.position - parent_world.position);
			target_local_rotation = _quat_normalized(_quat_multiplied(inverse_parent_rotation, target_world.rotation));
		}

		if (weight >= 1.0f) {
			r_positions[static_cast<size_t>(hierarchy.bone)] = target_local_position;
			r_rotations[static_cast<size_t>(hierarchy.bone)] = target_local_rotation;
		} else {
			r_positions[static_cast<size_t>(hierarchy.bone)] = (r_positions[static_cast<size_t>(hierarchy.bone)] * (1.0f - weight)) + (target_local_position * weight);
			r_rotations[static_cast<size_t>(hierarchy.bone)] = _quat_slerp(r_rotations[static_cast<size_t>(hierarchy.bone)], target_local_rotation, weight);
		}
	}
}

[[nodiscard]] const mdlpp::MDL::AnimDesc * _get_anim_desc(const mdlpp::StudioModel &p_model, int p_anim_desc_index) {
	if (p_anim_desc_index < 0 || p_anim_desc_index >= static_cast<int>(p_model.mdl.animDescs.size())) {
		return nullptr;
	}
	return &p_model.mdl.animDescs[static_cast<size_t>(p_anim_desc_index)];
}

[[nodiscard]] bool _resolve_anim_stream(const mdlpp::StudioModel &p_model, const mdlpp::MDL::AnimDesc &p_anim_desc, int p_frame, ResolvedAnimStream &r_stream) {
	int32_t block = p_anim_desc.animBlock;
	int32_t index = p_anim_desc.animIndex;
	int section = 0;
	r_stream = {};
	r_stream.localFrame = p_frame;

	if (p_anim_desc.sectionFrames != 0) {
		if (p_anim_desc.frameCount > p_anim_desc.sectionFrames && p_frame == p_anim_desc.frameCount - 1) {
			r_stream.localFrame = 0;
			section = (p_anim_desc.frameCount / p_anim_desc.sectionFrames) + 1;
		} else {
			section = p_frame / p_anim_desc.sectionFrames;
			r_stream.localFrame -= section * p_anim_desc.sectionFrames;
		}

		if (p_anim_desc.sectionIndex <= 0) {
			return false;
		}

		BufferStreamReadOnly stream{p_model.getMDLData().data(), p_model.getMDLData().size()};
		const RawAnimSections anim_section = stream.at<RawAnimSections>(static_cast<int64_t>(p_anim_desc.fileOffset + static_cast<uint64_t>(p_anim_desc.sectionIndex) + sizeof(RawAnimSections) * static_cast<uint64_t>(section)));
		block = anim_section.animBlock;
		index = anim_section.animIndex;
	}

	if (index <= 0 || block < 0) {
		return false;
	}

	if (block == 0) {
		r_stream.data = p_model.getMDLData().data();
		r_stream.size = p_model.getMDLData().size();
		r_stream.animOffset = p_anim_desc.fileOffset + static_cast<uint64_t>(index);
		return r_stream.data != nullptr && r_stream.animOffset < r_stream.size;
	}

	if (block >= static_cast<int32_t>(p_model.mdl.animBlocks.size())) {
		return false;
	}

	const auto &anim_block = p_model.mdl.animBlocks[static_cast<size_t>(block)];
	const auto &anim_block_data = p_model.getAnimBlockData();
	if (anim_block_data.empty() || anim_block.dataStart < 0 || anim_block.dataEnd <= anim_block.dataStart) {
		return false;
	}

	const uint64_t anim_offset = static_cast<uint64_t>(anim_block.dataStart) + static_cast<uint64_t>(index);
	if (anim_offset >= anim_block_data.size() || anim_offset >= static_cast<uint64_t>(anim_block.dataEnd)) {
		return false;
	}

	r_stream.data = anim_block_data.data();
	r_stream.size = anim_block_data.size();
	r_stream.animOffset = anim_offset;
	return true;
}

}

using namespace mdlpp;
using namespace sourcepp;

bool StudioModel::open(const std::byte* mdlData, std::size_t mdlSize,
					   const std::byte* vtxData, std::size_t vtxSize,
                       const std::byte* vvdData, std::size_t vvdSize) {
	if (this->opened || !mdlData || !vtxData || !vvdData || !mdlSize || !vtxSize || !vvdSize) {
		return false;
	}
	if ((!this->mdl.open(mdlData, mdlSize) ||
		!this->vtx.open(vtxData, vtxSize, this->mdl)) ||
		!this->vvd.open(vvdData, vvdSize, this->mdl)) {
		return false;
	}
	this->mdlData.assign(mdlData, mdlData + mdlSize);
	this->animBlockData.clear();
	this->opened = true;
	return true;
}

bool StudioModel::open(const unsigned char* mdlData, std::size_t mdlSize,
					   const unsigned char* vtxData, std::size_t vtxSize,
                       const unsigned char* vvdData, std::size_t vvdSize) {
	return this->open(reinterpret_cast<const std::byte*>(mdlData), mdlSize,
					  reinterpret_cast<const std::byte*>(vtxData), vtxSize,
					  reinterpret_cast<const std::byte*>(vvdData), vvdSize);
}

bool StudioModel::open(const std::vector<std::byte>& mdlData,
					   const std::vector<std::byte>& vtxData,
                       const std::vector<std::byte>& vvdData) {
	return this->open(mdlData.data(), mdlData.size(),
					  vtxData.data(), vtxData.size(),
					  vvdData.data(), vvdData.size());
}

bool StudioModel::open(const std::vector<unsigned char>& mdlData,
					   const std::vector<unsigned char>& vtxData,
                       const std::vector<unsigned char>& vvdData) {
	return this->open(mdlData.data(), mdlData.size(),
	                  vtxData.data(), vtxData.size(),
	                  vvdData.data(), vvdData.size());
}

StudioModel::operator bool() const {
	return this->opened;
}

BakedModel StudioModel::processModelData(int currentLOD) const {
	BakedModel model;
	std::vector<int32_t> source_to_baked_vertex(this->vvd.vertices.size(), -1);

	static constexpr auto convertVertex = [](const VVD::Vertex& vertex) {
		BakedModel::Vertex baked_vertex{};
		baked_vertex.position = vertex.position;
		baked_vertex.normal = vertex.normal;
		baked_vertex.uv = vertex.uv;
		baked_vertex.tangent = vertex.tangent;
		for (size_t weight_index = 0; weight_index < vertex.boneWeight.bones.size() && weight_index < MAX_BONES_PER_VERTEX; weight_index++) {
			baked_vertex.bones[weight_index] = static_cast<int32_t>(vertex.boneWeight.bones[weight_index]);
			baked_vertex.weights[weight_index] = vertex.boneWeight.weight[weight_index];
		}
		return baked_vertex;
	};

	auto add_baked_vertex = [&](int32_t source_vertex_index) {
		if (source_vertex_index < 0 || source_vertex_index >= static_cast<int32_t>(this->vvd.vertices.size())) {
			return;
		}
		if (source_to_baked_vertex[static_cast<size_t>(source_vertex_index)] != -1) {
			return;
		}
		source_to_baked_vertex[static_cast<size_t>(source_vertex_index)] = static_cast<int32_t>(model.vertices.size());
		model.vertices.push_back(convertVertex(this->vvd.vertices[static_cast<size_t>(source_vertex_index)]));
	};

	if (this->vvd.fixups.empty()) {
		for (int32_t source_vertex_index = 0; source_vertex_index < static_cast<int32_t>(this->vvd.vertices.size()); source_vertex_index++) {
			add_baked_vertex(source_vertex_index);
		}
	} else {
		for (const auto& [LOD, sourceVertexID, vertexCount] : this->vvd.fixups) {
			if (LOD < currentLOD) {
				continue;
			}
			for (int32_t vertex_index = 0; vertex_index < vertexCount; vertex_index++) {
				add_baked_vertex(sourceVertexID + vertex_index);
			}
		}
	}

	for (int bodyPartIndex = 0; bodyPartIndex < this->mdl.bodyParts.size(); bodyPartIndex++) {
		auto& mdlBodyPart = this->mdl.bodyParts.at(bodyPartIndex);
		auto& vtxBodyPart = this->vtx.bodyParts.at(bodyPartIndex);

		for (int modelIndex = 0; modelIndex < mdlBodyPart.models.size(); modelIndex++) {
			auto& mdlModel = mdlBodyPart.models.at(modelIndex);
			auto& vtxModel = vtxBodyPart.models.at(modelIndex);

			if (mdlModel.verticesCount == 0) {
				continue;
			}

			for (int meshIndex = 0; meshIndex < mdlModel.meshes.size(); meshIndex++) {
				auto& mdlMesh = mdlModel.meshes.at(meshIndex);
				auto& vtxMesh = vtxModel.modelLODs.at(currentLOD).meshes.at(meshIndex);

				std::vector<uint32_t> indices;
				for (const auto& stripGroup : vtxMesh.stripGroups) {
					auto append_triangle = [&](uint16_t p_index_a, uint16_t p_index_b, uint16_t p_index_c) {
						const int32_t source_index_a = stripGroup.vertices.at(p_index_a).meshVertexID + mdlMesh.verticesOffset + mdlModel.verticesOffset;
						const int32_t source_index_b = stripGroup.vertices.at(p_index_b).meshVertexID + mdlMesh.verticesOffset + mdlModel.verticesOffset;
						const int32_t source_index_c = stripGroup.vertices.at(p_index_c).meshVertexID + mdlMesh.verticesOffset + mdlModel.verticesOffset;

						if (source_index_a < 0 || source_index_b < 0 || source_index_c < 0 ||
								source_index_a >= static_cast<int32_t>(source_to_baked_vertex.size()) ||
								source_index_b >= static_cast<int32_t>(source_to_baked_vertex.size()) ||
								source_index_c >= static_cast<int32_t>(source_to_baked_vertex.size())) {
							return;
						}

						const int32_t baked_index_a = source_to_baked_vertex[static_cast<size_t>(source_index_a)];
						const int32_t baked_index_b = source_to_baked_vertex[static_cast<size_t>(source_index_b)];
						const int32_t baked_index_c = source_to_baked_vertex[static_cast<size_t>(source_index_c)];
						if (baked_index_a < 0 || baked_index_b < 0 || baked_index_c < 0) {
							return;
						}

						indices.push_back(static_cast<uint32_t>(baked_index_a));
						indices.push_back(static_cast<uint32_t>(baked_index_b));
						indices.push_back(static_cast<uint32_t>(baked_index_c));
					};
					for (const auto& strip : stripGroup.strips) {
						// Remember to flip the winding order
						if (strip.flags & VTX::Strip::FLAG_IS_TRILIST) {
							for (int i = 0; i < strip.indices.size(); i += 3) {
								append_triangle(strip.indices[i], strip.indices[i + 2], strip.indices[i + 1]);
							}
						} else {
							for (int i = static_cast<int>(strip.indices.size()) - 1; i >= 2; i -= 3) {
								append_triangle(strip.indices[static_cast<size_t>(i)], strip.indices[static_cast<size_t>(i - 2)], strip.indices[static_cast<size_t>(i - 1)]);
							}
						}
					}
				}

				std::string material_name;
				if (mdlMesh.material >= 0 && mdlMesh.material < static_cast<int32_t>(this->mdl.materials.size())) {
					material_name = this->mdl.materials[static_cast<size_t>(mdlMesh.material)].name;
				}
				if (const VTX::MaterialReplacement *replacement = this->vtx.findMaterialReplacement(currentLOD, static_cast<int16_t>(mdlMesh.material)); replacement != nullptr && !replacement->replacementMaterialName.empty()) {
					material_name = replacement->replacementMaterialName;
				}

				model.meshes.push_back({std::move(indices), mdlMesh.material, std::move(material_name)});
			}
		}
	}

	return model;
}

bool StudioModel::sampleAnimation(int animDescIndex, SampledAnimation& out) const {
	const MDL::AnimDesc *anim_desc = _get_anim_desc(*this, animDescIndex);
	if (!this->opened || anim_desc == nullptr || anim_desc->frameCount <= 0 || this->mdlData.empty()) {
		return false;
	}

	BufferStreamReadOnly stream{this->mdlData.data(), this->mdlData.size()};
	out = {};
	out.animationIndex = animDescIndex;
	out.fps = anim_desc->fps;
	out.frameCount = anim_desc->frameCount;
	out.flags = anim_desc->flags;
	out.tracks.resize(this->mdl.bones.size());

	const bool is_delta = (anim_desc->flags & MDL::AnimDesc::FLAG_DELTA) != 0;
	for (size_t bone_index = 0; bone_index < this->mdl.bones.size(); bone_index++) {
		const MDL::Bone &bone = this->mdl.bones[bone_index];
		SampledAnimationTrack &track = out.tracks[bone_index];
		track.bone = static_cast<int32_t>(bone_index);
		track.positions.assign(static_cast<size_t>(anim_desc->frameCount), is_delta ? math::Vec3f{} : bone.position);
		track.rotations.assign(static_cast<size_t>(anim_desc->frameCount), is_delta ? math::Quat{0.0f, 0.0f, 0.0f, 1.0f} : bone.rotationQuat);
	}

	for (int frame = 0; frame < anim_desc->frameCount; frame++) {
		ResolvedAnimStream resolved_stream;
		if (!_resolve_anim_stream(*this, *anim_desc, frame, resolved_stream)) {
			if (anim_desc->localHierarchyCount > 0) {
				std::vector<sourcepp::math::Vec3f> frame_positions(out.tracks.size());
				std::vector<sourcepp::math::Quat> frame_rotations(out.tracks.size());
				for (size_t bone_index = 0; bone_index < out.tracks.size(); bone_index++) {
					frame_positions[bone_index] = out.tracks[bone_index].positions[static_cast<size_t>(frame)];
					frame_rotations[bone_index] = out.tracks[bone_index].rotations[static_cast<size_t>(frame)];
				}
				const float cycle = anim_desc->frameCount > 1 ? static_cast<float>(frame) / static_cast<float>(anim_desc->frameCount - 1) : 0.0f;
				_apply_local_hierarchy(*this, *anim_desc, frame, cycle, frame_positions, frame_rotations);
				for (size_t bone_index = 0; bone_index < out.tracks.size(); bone_index++) {
					out.tracks[bone_index].positions[static_cast<size_t>(frame)] = frame_positions[bone_index];
					out.tracks[bone_index].rotations[static_cast<size_t>(frame)] = frame_rotations[bone_index];
				}
			}
			continue;
		}

		BufferStreamReadOnly frame_stream{resolved_stream.data, resolved_stream.size};
		uint64_t anim_offset = resolved_stream.animOffset;

		while (anim_offset > 0) {
			const RawAnimHeader header = frame_stream.at<RawAnimHeader>(static_cast<int64_t>(anim_offset));
			if (header.bone >= out.tracks.size()) {
				break;
			}

			const MDL::Bone &bone = this->mdl.bones[header.bone];
			SampledAnimationTrack &track = out.tracks[header.bone];
			const uint64_t data_offset = anim_offset + sizeof(RawAnimHeader);

			if (header.flags & STUDIO_ANIM_RAWROT) {
				track.rotations[frame] = frame_stream.at<math::QuatCompressed48>(static_cast<int64_t>(data_offset)).decompress();
			} else if (header.flags & STUDIO_ANIM_RAWROT2) {
				track.rotations[frame] = frame_stream.at<math::QuatCompressed64>(static_cast<int64_t>(data_offset)).decompress();
			} else if (header.flags & STUDIO_ANIM_ANIMROT) {
				const RawAnimValuePtr rot_values = frame_stream.at<RawAnimValuePtr>(static_cast<int64_t>(data_offset));
				math::Vec3f rotation_euler{};
				for (int axis = 0; axis < 3; axis++) {
					const uint64_t value_offset = rot_values.offset[axis] > 0 ? data_offset + static_cast<uint64_t>(rot_values.offset[axis]) : 0;
					rotation_euler[axis] = _extract_anim_value(frame_stream, value_offset, resolved_stream.localFrame, bone.rotationScale[axis]);
					if (!(header.flags & STUDIO_ANIM_DELTA)) {
						rotation_euler[axis] += bone.rotationEuler[axis];
					}
				}
				track.rotations[frame] = _angle_quaternion(rotation_euler);
				if (!(header.flags & STUDIO_ANIM_DELTA) && (bone.flags & MDL::Bone::FLAG_FIXED_ALIGNMENT)) {
					_align_quaternion(bone.alignment, track.rotations[frame]);
				}
			}

			if (header.flags & STUDIO_ANIM_RAWPOS) {
				const uint64_t position_offset = data_offset + ((header.flags & STUDIO_ANIM_RAWROT) ? sizeof(math::QuatCompressed48) : 0) + ((header.flags & STUDIO_ANIM_RAWROT2) ? sizeof(math::QuatCompressed64) : 0);
				track.positions[frame] = frame_stream.at<math::Vec3f16>(static_cast<int64_t>(position_offset)).template to<3, float>();
			} else if (header.flags & STUDIO_ANIM_ANIMPOS) {
				const uint64_t pos_values_offset = data_offset + ((header.flags & STUDIO_ANIM_ANIMROT) ? sizeof(RawAnimValuePtr) : 0);
				const RawAnimValuePtr pos_values = frame_stream.at<RawAnimValuePtr>(static_cast<int64_t>(pos_values_offset));
				for (int axis = 0; axis < 3; axis++) {
					const uint64_t value_offset = pos_values.offset[axis] > 0 ? pos_values_offset + static_cast<uint64_t>(pos_values.offset[axis]) : 0;
					track.positions[frame][axis] = _extract_anim_value(frame_stream, value_offset, resolved_stream.localFrame, bone.positionScale[axis]);
					if (!(header.flags & STUDIO_ANIM_DELTA)) {
						track.positions[frame][axis] += bone.position[axis];
					}
				}
			}

			if (header.nextOffset == 0) {
				break;
			}
			anim_offset += static_cast<uint64_t>(header.nextOffset);
		}

		if (anim_desc->localHierarchyCount > 0) {
			std::vector<sourcepp::math::Vec3f> frame_positions(out.tracks.size());
			std::vector<sourcepp::math::Quat> frame_rotations(out.tracks.size());
			for (size_t bone_index = 0; bone_index < out.tracks.size(); bone_index++) {
				frame_positions[bone_index] = out.tracks[bone_index].positions[static_cast<size_t>(frame)];
				frame_rotations[bone_index] = out.tracks[bone_index].rotations[static_cast<size_t>(frame)];
			}
			const float cycle = anim_desc->frameCount > 1 ? static_cast<float>(frame) / static_cast<float>(anim_desc->frameCount - 1) : 0.0f;
			_apply_local_hierarchy(*this, *anim_desc, frame, cycle, frame_positions, frame_rotations);
			for (size_t bone_index = 0; bone_index < out.tracks.size(); bone_index++) {
				out.tracks[bone_index].positions[static_cast<size_t>(frame)] = frame_positions[bone_index];
				out.tracks[bone_index].rotations[static_cast<size_t>(frame)] = frame_rotations[bone_index];
			}
		}
	}

	return true;
}
