#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Generic.h"

namespace mdlpp::MDL {

struct Bone {
	enum Flags : int32_t {
		FLAG_NONE = 0,
		FLAG_FIXED_ALIGNMENT = 0x00100000,
	};

	//int32_t nameIndex;
	std::string name;

	int32_t parent;
	std::array<int32_t, 6> boneController;
	sourcepp::math::Vec3f position;
	sourcepp::math::Quat rotationQuat;
	sourcepp::math::Vec3f rotationEuler;
	sourcepp::math::Vec3f positionScale;
	sourcepp::math::Vec3f rotationScale;
	sourcepp::math::Mat3x4f poseToBose;
	sourcepp::math::Quat alignment;
	Flags flags;
	int32_t procType;
	int32_t procIndex;
	int32_t physicsBone;

	//int32_t surfacePropNameIndex;
	std::string surfacePropName;

	int32_t contents;

	//int32_t _unused0[8];
};
SOURCEPP_BITFLAGS_ENUM(Bone::Flags)

struct BoneController {
	int32_t bone;
	int32_t type;
	float start;
	float end;
	int32_t rest;
	int32_t inputField;

	//int32_t _unused0[8];
};

struct HitboxSet {
	//int32_t nameIndex;
	std::string name;

	//int32_t hitboxCount;
	//int32_t hitboxIndex;
	std::vector<BBox> hitboxes;
};

struct Attachment {
	enum Flags : int32_t {
		FLAG_NONE = 0,
		FLAG_WORLD_ALIGN = 1 << 16,
	};

	//int32_t nameIndex;
	std::string name;

	Flags flags;
	int32_t bone;
	sourcepp::math::Mat3x4f local;

	//int32_t _unused0[8];
};
SOURCEPP_BITFLAGS_ENUM(Attachment::Flags)

struct AnimDesc {
	uint64_t fileOffset;

	enum Flags : int32_t {
		FLAG_NONE     = 0,
		FLAG_RAW_POS  = 1 << 0,
		FLAG_RAW_ROT  = 1 << 1,
		FLAG_ANIM_POS = 1 << 2,
		FLAG_ANIM_ROT = 1 << 3,
		FLAG_DELTA    = 1 << 4,
		FLAG_RAW_ROT2 = 1 << 5,
	};

	//int32_t basePointer;

	//int32_t nameIndex;
	std::string name;
	float fps;

	Flags flags;

	int32_t frameCount;

	//int32_t movementCount;
	//int32_t movementIndex;
	std::vector<Movement> movements;

	int32_t animBlock;
	int32_t animIndex;

	//int32_t ikRuleCount;
	//int32_t ikRuleIndex;
	int32_t ikRuleCount;
	int32_t ikRuleIndex;

	//int32_t animBlockIKRuleIndex;
	int32_t animBlockIKRuleIndex;

	//int32_t localHierarchyIndexCount;
	//int32_t localHierarchyIndex;
	int32_t localHierarchyCount;
	int32_t localHierarchyIndex;

	int32_t sectionIndex;
	int32_t sectionFrames;

	//int16_t zeroFrameSpan;
	//int16_t zeroFrameCount;
	int32_t zeroFrameIndex;
	//float zeroFrameStallTime;
	int16_t zeroFrameSpan;
	int16_t zeroFrameCount;
	float zeroFrameStallTime;
};
SOURCEPP_BITFLAGS_ENUM(AnimDesc::Flags)

struct AnimBlock {
	int32_t dataStart;
	int32_t dataEnd;
};

struct IncludeModel {
	//int32_t labelIndex;
	std::string label;

	//int32_t nameIndex;
	std::string name;
};

struct Event {
	float cycle;
	int32_t event;
	int32_t type;
	std::string options;
	std::string name;
};

struct AutoLayer {
	int16_t sequence;
	int16_t pose;
	int32_t flags;
	float start;
	float peak;
	float tail;
	float end;
};

struct ActivityModifier {
	std::string name;
};

struct IKLink {
	int32_t bone;
	sourcepp::math::Vec3f kneeDir;
};

struct IKChain {
	std::string name;
	int32_t linkType;
	std::vector<IKLink> links;
};

struct IKLock {
	int32_t chain;
	float positionWeight;
	float localQuaternionWeight;
	int32_t flags;
};

struct IKRule {
	int32_t index = -1;
	int32_t type = 0;
	int32_t chain = -1;
	int32_t bone = -1;
	int32_t slot = -1;
	float height = 0.0f;
	float radius = 0.0f;
	float floor = 0.0f;
	sourcepp::math::Vec3f position{};
	sourcepp::math::Quat rotation{};
	int32_t compressedIKErrorIndex = 0;
	int32_t startFrame = 0;
	int32_t ikErrorIndex = 0;
	float start = 0.0f;
	float peak = 0.0f;
	float tail = 0.0f;
	float end = 0.0f;
	float contact = 0.0f;
	float drop = 0.0f;
	float top = 0.0f;
	std::string attachment;
};

struct SequenceDesc {
	enum Flags : int32_t {
		FLAG_NONE = 0,
		FLAG_LOOPING = 1 << 0,
	};

	//int32_t basePointer;

	//int32_t labelIndex;
	//int32_t activityLabelIndex;
	std::string label;
	std::string activityName;

	Flags flags;

	//int32_t activity;
	//int32_t activityWeight;
	int32_t activity;
	int32_t activityWeight;

	//int32_t eventCount;
	//int32_t eventIndex;
	int32_t eventCount;
	std::vector<Event> events;

	sourcepp::math::Vec3f boundingBoxMin;
	sourcepp::math::Vec3f boundingBoxMax;

	int32_t blendCount;

	std::vector<int16_t> animationIndices;

	int32_t movementIndex;

	std::array<int32_t, 2> groupSize;
	std::array<int32_t, 2> paramIndex;
	std::array<float, 2> paramStart;
	std::array<float, 2> paramEnd;
	int32_t paramParent;

	float fadeInTime;
	float fadeOutTime;

	int32_t localEntryNode;
	int32_t localExitNode;

	int32_t nodeFlags;

	float entryPhase;
	float exitPhase;

	float lastFrame;

	int32_t nextSequence;
	int32_t pose;

	int32_t ikRuleCount;

	//int32_t autoLayerCount;
	//int32_t autoLayerIndex;
	int32_t autoLayerCount;
	std::vector<AutoLayer> autoLayers;

	int32_t weightListIndex;
	std::vector<float> boneWeights;

	int32_t poseKeyIndex;
	std::array<std::vector<float>, 2> poseKeys;

	//int32_t ikLockCount;
	//int32_t ikLockIndex;
	int32_t ikLockCount;
	std::vector<IKLock> ikLocks;

	//int32_t keyValueIndex;
	//int32_t keyValueSize;
	std::string keyValueText;

	int32_t cyclePoseIndex;
	std::vector<ActivityModifier> activityModifiers;

	//int32_t _unused0[7];
};
SOURCEPP_BITFLAGS_ENUM(SequenceDesc::Flags)

struct Material {
	enum Flags : int32_t {
		FLAG_NONE = 0,
		// todo(flags): Material (Texture in MDL)
	};

	//int32_t nameIndex;
	std::string name;

	Flags flags;

	//int32_t used; // No idea what this is
	//int32_t _unused0[13];
};
SOURCEPP_BITFLAGS_ENUM(Material::Flags)

struct Mesh {
	int32_t material;

	//int32_t modelOffset;

	// These do not map to raw memory
	int32_t verticesCount;
	int32_t verticesOffset;

	//int32_t flexesCount;
	//int32_t flexesOffset;

	int32_t materialType;
	int32_t materialParam;

	int32_t meshID;

	sourcepp::math::Vec3f center;

	//int32_t modelVertexData;
	//int32_t numLODVertexes[MAX_LOD_COUNT];
	//int32_t _unused[8];
};

struct Model {
	//char name[64];
	std::string name;

	int32_t type;

	float boundingRadius;

	//int32_t meshesCount;
	//int32_t meshesOffset;
	std::vector<Mesh> meshes;

	// These do not map to raw memory
	int32_t verticesCount;
	int32_t verticesOffset;
	//int32_t tangentsOffset;

	//int32_t attachmentsCount;
	//int32_t attachmentsOffset;

	//int32_t eyeballsCount;
	//int32_t eyeballsOffset;

	//int32_t _unused0[10];
};

struct BodyPart {
	//int32_t nameOffset;
	std::string name;

	//int32_t modelsCount;
	int32_t base; // No idea what this is, might as well expose it
	//int32_t modelsOffset;
	std::vector<Model> models;
};

struct MDL {
	[[nodiscard]] bool open(const std::byte* data, std::size_t size);

	enum Flags : int32_t {
		FLAG_NONE                           = 0,
		FLAG_AUTOGENERATED_HITBOX           = 1 <<  0,
		FLAG_FORCE_OPAQUE                   = 1 <<  2,
		FLAG_TRANSLUCENT_TWO_PASS           = 1 <<  3,
		FLAG_STATIC_PROP                    = 1 <<  4,
		FLAG_HAS_SHADOW_LOD                 = 1 <<  6,
		FLAG_USE_SHADOW_LOD_MATERIALS       = 1 <<  8,
		FLAG_OBSOLETE                       = 1 <<  9,
		FLAG_NO_FORCED_FADE                 = 1 << 11,
		FLAG_FORCE_PHONEME_CROSSFADE        = 1 << 12,
		FLAG_CONSTANT_DIRECTIONAL_LIGHT_DOT = 1 << 13,
		FLAG_FLEXES_CONVERTED               = 1 << 14,
		FLAG_BUILT_IN_PREVIEW_MODE          = 1 << 15,
		FLAG_DO_NOT_CAST_SHADOWS            = 1 << 17,
		FLAG_CAST_TEXTURE_SHADOWS           = 1 << 18,
		FLAG_SUBDIVISION_SURFACE            = 1 << 19,
		FLAG_VERT_ANIM_FIXED_POINT_SCALE    = 1 << 21,
		FLAG_EXTRA_VERTEX_DATA              = 1 << 26,
	};

	//int32_t id;
	int32_t version;
	int32_t checksum;

	//char name[64];
	std::string name;
	//int32_t dataLength;

	sourcepp::math::Vec3f eyePosition;
	sourcepp::math::Vec3f illuminationPosition;
	sourcepp::math::Vec3f hullMin;
	sourcepp::math::Vec3f hullMax;
	sourcepp::math::Vec3f viewBBoxMin;
	sourcepp::math::Vec3f viewBBoxMax;

	Flags flags;

	//int32_t boneCount;
	//int32_t boneOffset;
	std::vector<Bone> bones;

	//int32_t boneControllerCount;
	//int32_t boneControllerOffset;
	std::vector<BoneController> boneControllers;

	//int32_t hitboxCount;
	//int32_t hitboxOffset;
	std::vector<HitboxSet> hitboxSets;

	//int32_t localAnimationCount;
	//int32_t localAnimationOffset;
	std::vector<AnimDesc> animDescs;

	//int32_t localSequenceCount;
	//int32_t localSequenceOffset;
	std::vector<SequenceDesc> sequenceDescs;

	int32_t activityListVersion;
	int32_t eventsIndexed;

	//int32_t materialCount;
	//int32_t materialOffset;
	std::vector<Material> materials;

	//int32_t materialDirCount;
	//int32_t materialDirOffset;
	std::vector<std::string> materialDirectories;

	//int32_t skinReferenceCount;
	//int32_t skinReferenceFamilyCount;
	//int32_t skinReferenceIndex;
	// Each vector is an individual skin, which holds indices into the materials vector
	std::vector<std::vector<int16_t>> skins;

	//int32_t bodyPartCount;
	//int32_t bodyPartOffset;
	std::vector<BodyPart> bodyParts;

	//int32_t attachmentCount;
	//int32_t attachmentOffset;
	std::vector<Attachment> attachments;

	//int32_t localNodeCount;
	//int32_t localNodeIndex;
	//int32_t localNodeNameIndex;

	//int32_t flexDescCount;
	//int32_t flexDescIndex;

	//int32_t flexControllerCount;
	//int32_t flexControllerIndex;

	//int32_t flexRulesCount;
	//int32_t flexRulesIndex;

	//int32_t ikChainCount;
	//int32_t ikChainIndex;
	std::vector<IKChain> ikChains;

	//int32_t mouthsCount;
	//int32_t mouthsIndex;

	//int32_t localPoseParamCount;
	//int32_t localPoseParamIndex;

	//int32_t surfacePropertyIndex;

	//int32_t keyValueIndex;
	//int32_t keyValueCount;

	//int32_t ikLockCount;
	//int32_t ikLockIndex;
	std::vector<IKLock> ikAutoplayLocks;

	//float mass;
	//int32_t contentsFlags;

	//int32_t includeModelCount;
	//int32_t includeModelIndex;
	std::vector<IncludeModel> includeModels;

	//int32_t virtualModel;

	//int32_t animationBlocksNameIndex;
	std::string animBlockName;

	//int32_t animationBlocksCount;
	//int32_t animationBlocksIndex;
	std::vector<AnimBlock> animBlocks;

	//int32_t animationBlockModel;

	//int32_t boneTableNameIndex;

	//int32_t vertexBase;
	//int32_t offsetBase;

	//std::byte directionalDotProduct;

	//uint8_t rootLOD;
	//uint8_t numAllowedRootLODs;

	//std::byte _unused0;
	//int32_t _unused1;

	//int32_t flexControllerUICount;
	//int32_t flexControllerUIIndex;

	//float vertAnimFixedPointScale;
	//int32_t _unused2;

	// todo: header 2
	//int32_t header2Offset;

	//int32_t _unused3;
};
SOURCEPP_BITFLAGS_ENUM(MDL::Flags)

} // namespace mdlpp::MDL
