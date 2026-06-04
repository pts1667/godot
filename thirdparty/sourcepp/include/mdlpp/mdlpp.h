#pragma once

#include <vector>

#include "structs/MDL.h"
#include "structs/VTX.h"
#include "structs/VVD.h"

namespace mdlpp {

/**
 * A more accessible version of StudioModel's vertex data, so it can be rendered or converted more easily.
 */
struct BakedModel {
	struct Vertex {
		sourcepp::math::Vec3f position;
		sourcepp::math::Vec3f normal;
		sourcepp::math::Vec2f uv;
		sourcepp::math::Vec4f tangent{};
		std::array<int32_t, 4> bones{};
		std::array<float, 4> weights{};
	};
	std::vector<Vertex> vertices;

	struct Mesh {
		std::vector<uint32_t> indices;
		int32_t materialIndex = -1;
		std::string materialName;
	};
	std::vector<Mesh> meshes;
};

struct SampledAnimationTrack {
	int32_t bone = -1;
	std::vector<sourcepp::math::Vec3f> positions;
	std::vector<sourcepp::math::Quat> rotations;
};

struct SampledAnimation {
	int32_t animationIndex = -1;
	float fps = 0.0f;
	int32_t frameCount = 0;
	MDL::AnimDesc::Flags flags = MDL::AnimDesc::FLAG_NONE;
	std::vector<SampledAnimationTrack> tracks;
};

struct StudioModel {
	[[nodiscard]] bool open(const std::byte* mdlData, std::size_t mdlSize,
							const std::byte* vtxData, std::size_t vtxSize,
							const std::byte* vvdData, std::size_t vvdSize);

	[[nodiscard]] bool open(const unsigned char* mdlData, std::size_t mdlSize,
							const unsigned char* vtxData, std::size_t vtxSize,
							const unsigned char* vvdData, std::size_t vvdSize);

	[[nodiscard]] bool open(const std::vector<std::byte>& mdlData,
							const std::vector<std::byte>& vtxData,
							const std::vector<std::byte>& vvdData);

	[[nodiscard]] bool open(const std::vector<unsigned char>& mdlData,
							const std::vector<unsigned char>& vtxData,
							const std::vector<unsigned char>& vvdData);

	[[nodiscard]] bool openMDLOnly(const std::byte* mdlData, std::size_t mdlSize);
	[[nodiscard]] bool openMDLOnly(const unsigned char* mdlData, std::size_t mdlSize);
	[[nodiscard]] bool openMDLOnly(const std::vector<std::byte>& mdlData);
	[[nodiscard]] bool openMDLOnly(const std::vector<unsigned char>& mdlData);

	[[nodiscard]] explicit operator bool() const;

	[[nodiscard]] BakedModel processModelData(int currentLOD = ROOT_LOD) const;
	[[nodiscard]] bool sampleAnimation(int animDescIndex, SampledAnimation& out) const;
	[[nodiscard]] std::vector<MDL::IKRule> getAnimationIKRules(int animDescIndex) const;
	[[nodiscard]] const std::vector<std::byte> &getMDLData() const { return this->mdlData; }
	[[nodiscard]] const std::vector<std::byte> &getAnimBlockData() const { return this->animBlockData; }
	void setAnimBlockData(const std::vector<std::byte> &p_anim_block_data) { this->animBlockData = p_anim_block_data; }

	MDL::MDL mdl;
	VTX::VTX vtx;
	VVD::VVD vvd;

private:
	std::vector<std::byte> mdlData;
	std::vector<std::byte> animBlockData;
	bool opened = false;
};

} // namespace mdlpp
