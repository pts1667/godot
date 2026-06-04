#include <mdlpp/structs/MDL.h>

#include <BufferStream.h>
#include <sourcepp/parser/Binary.h>

using namespace mdlpp::MDL;
using namespace sourcepp;

constexpr int32_t MDL_ID = parser::binary::makeFourCC("IDST");

bool MDL::open(const std::byte* data, std::size_t size) {
	BufferStreamReadOnly stream{data, size};

	if (stream.read<int32_t>() != MDL_ID) {
		return false;
	}

	if (stream.read(this->version); this->version < 44 || this->version > 49) {
		return false;
	}

	stream
		.read(this->checksum)
		.read(this->name, 64)
		.skip<int32_t>() // dataLength
		.read(this->eyePosition)
		.read(this->illuminationPosition)
		.read(this->hullMin)
		.read(this->hullMax)
		.read(this->viewBBoxMin)
		.read(this->viewBBoxMax);

	this->flags = static_cast<Flags>(stream.read<int32_t>());

	const auto boneCount = stream.read<int32_t>();
	const auto boneOffset = stream.read<int32_t>();

	const auto boneControllerCount = stream.read<int32_t>();
	const auto boneControllerOffset = stream.read<int32_t>();

	const auto hitboxSetCount = stream.read<int32_t>();
	const auto hitboxSetOffset = stream.read<int32_t>();

	const auto animDescCount = stream.read<int32_t>();
	const auto animDescOffset = stream.read<int32_t>();

	const auto sequenceDescCount = stream.read<int32_t>();
	const auto sequenceDescOffset = stream.read<int32_t>();

	stream
		.read(this->activityListVersion)
		.read(this->eventsIndexed);

	const auto materialCount = stream.read<int32_t>();
	const auto materialOffset = stream.read<int32_t>();

	const auto materialDirCount = stream.read<int32_t>();
	const auto materialDirOffset = stream.read<int32_t>();

	const auto skinReferenceCount = stream.read<int32_t>();
	const auto skinReferenceFamilyCount = stream.read<int32_t>();
	const auto skinReferenceOffset = stream.read<int32_t>();

	const auto bodyPartCount = stream.read<int32_t>();
	const auto bodyPartOffset = stream.read<int32_t>();

	const auto attachmentCount = stream.read<int32_t>();
	const auto attachmentOffset = stream.read<int32_t>();

	stream.skip<int32_t>(9);
	const auto ikChainCount = stream.read<int32_t>();
	const auto ikChainOffset = stream.read<int32_t>();
	stream.skip<int32_t>(4);
	stream.skip<int32_t>();
	stream.skip<int32_t>(2);
	const auto ikAutoplayLockCount = stream.read<int32_t>();
	const auto ikAutoplayLockOffset = stream.read<int32_t>();
	stream.skip<float>();
	stream.skip<int32_t>();
	const auto includeModelCount = stream.read<int32_t>();
	const auto includeModelIndex = stream.read<int32_t>();
	stream.skip<int32_t>();
	const auto animBlockNameIndex = stream.read<int32_t>();
	const auto animBlockCount = stream.read<int32_t>();
	const auto animBlockIndex = stream.read<int32_t>();

	// Done reading sequentially, start seeking to offsets

	if (animBlockNameIndex > 0) {
		this->animBlockName = stream.at_string_u(static_cast<uint64_t>(animBlockNameIndex));
	}

	if (animBlockCount > 0 && animBlockIndex > 0) {
		stream.seek_u(animBlockIndex);
		for (int i = 0; i < animBlockCount; i++) {
			auto &animBlock = this->animBlocks.emplace_back();
			stream.read(animBlock.dataStart).read(animBlock.dataEnd);
		}
	}

	if (includeModelCount > 0 && includeModelIndex > 0) {
		stream.seek_u(includeModelIndex);
		for (int i = 0; i < includeModelCount; i++) {
			const auto includeModelPos = stream.tell();
			auto &includeModel = this->includeModels.emplace_back();
			parser::binary::readStringAtOffset(stream, includeModel.label);
			parser::binary::readStringAtOffset(stream, includeModel.name, std::ios::cur, sizeof(int32_t) * 2);
			stream.seek_u(includeModelPos + sizeof(int32_t) * 2);
		}
	}

	stream.seek(boneOffset);
	for (int i = 0; i < boneCount; i++) {
		auto& bone = this->bones.emplace_back();

		parser::binary::readStringAtOffset(stream, bone.name);
		stream
			.read(bone.parent)
			.read(bone.boneController)
			.read(bone.position)
			.read(bone.rotationQuat)
			.read(bone.rotationEuler)
			.read(bone.positionScale)
			.read(bone.rotationScale)
			.read(bone.poseToBose)
			.read(bone.alignment)
			.read(bone.flags)
			.read(bone.procType)
			.read(bone.procIndex)
			.read(bone.physicsBone);
		parser::binary::readStringAtOffset(stream, bone.surfacePropName, std::ios::cur, sizeof(int32_t) * 12 + sizeof(math::Vec3f) * 4 + sizeof(math::Quat) * 2 + sizeof(math::Mat3x4f) + sizeof(Bone::Flags));
		stream.read(bone.contents);

		// _unused0
		stream.skip<int32_t>(8);
	}

	stream.seek(boneControllerOffset);
	for (int i = 0; i < boneControllerCount; i++) {
		this->boneControllers.push_back(stream.read<BoneController>());

		// _unused0
		stream.skip<int32_t>(8);
	}

	for (int i = 0; i < hitboxSetCount; i++) {
		const auto hitboxSetPos = hitboxSetOffset + i * (sizeof(int32_t) * 3);
		stream.seek_u(hitboxSetPos);

		auto& hitboxSet = this->hitboxSets.emplace_back();

		parser::binary::readStringAtOffset(stream, hitboxSet.name);
		const auto hitboxCount = stream.read<int32_t>();
		const auto hitboxOffset = stream.read<int32_t>();

		for (int j = 0; j < hitboxCount; j++) {
			const auto hitboxPos = hitboxOffset + j * (sizeof(int32_t) * 11 + sizeof(math::Vec3f) * 2);
			stream.seek_u(hitboxSetPos + hitboxPos);

			auto& hitbox = hitboxSet.hitboxes.emplace_back();

			stream
				.read(hitbox.bone)
				.read(hitbox.group)
				.read(hitbox.bboxMin)
				.read(hitbox.bboxMax);

			// note: we don't know what model versions use absolute vs. relative offsets here
			//       and this is unimportant, so skip parsing the bbox name here
			//readStringAtOffset(stream, hitbox.name, std::ios::cur, sizeof(int32_t) * 3 + sizeof(Vec3f) * 2);
			stream.skip<int32_t>();
			hitbox.name = "";

			// _unused0
			stream.skip<int32_t>(8);
		}
	}

	if (animDescCount > 0 && animDescOffset > 0) {
		stream.seek(animDescOffset);
		for (int i = 0; i < animDescCount; i++) {
			const auto animDescPos = stream.tell();
			auto& animDesc = this->animDescs.emplace_back();
			animDesc.fileOffset = animDescPos;

			stream.skip<int32_t>();
			parser::binary::readStringAtOffset(stream, animDesc.name);
			stream
				.read(animDesc.fps)
				.read(animDesc.flags)
				.read(animDesc.frameCount);

			const auto movementCount = stream.read<int32_t>();
			const auto movementOffset = stream.read<int32_t>();

			stream.skip<int32_t>(6);
			stream
				.read(animDesc.animBlock)
				.read(animDesc.animIndex);
			stream
				.read(animDesc.ikRuleCount)
				.read(animDesc.ikRuleIndex)
				.read(animDesc.animBlockIKRuleIndex);
			stream.read(animDesc.localHierarchyCount);
			stream.read(animDesc.localHierarchyIndex);
			stream.read(animDesc.sectionIndex);
			stream.read(animDesc.sectionFrames);
			stream
				.read(animDesc.zeroFrameSpan)
				.read(animDesc.zeroFrameCount);
			stream.read(animDesc.zeroFrameIndex);
			stream.read(animDesc.zeroFrameStallTime);

			if (movementCount > 0 && movementOffset > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(animDescPos + movementOffset);
				for (int j = 0; j < movementCount; j++) {
					auto& movement = animDesc.movements.emplace_back();
					stream
						.read(movement.endFrame)
						.read(movement.flags)
						.read(movement.velocityStart)
						.read(movement.velocityEnd)
						.read(movement.yawEnd)
						.read(movement.movement)
						.read(movement.relativePosition);
				}
				stream.seek_u(returnPos);
			}
		}
	}

	if (sequenceDescCount > 0 && sequenceDescOffset > 0) {
		stream.seek(sequenceDescOffset);
		for (int i = 0; i < sequenceDescCount; i++) {
			const auto sequenceDescPos = stream.tell();
			auto& sequenceDesc = this->sequenceDescs.emplace_back();

			stream.skip<int32_t>();
			parser::binary::readStringAtOffset(stream, sequenceDesc.label);
			parser::binary::readStringAtOffset(stream, sequenceDesc.activityName);
			stream
				.read(sequenceDesc.flags)
				.read(sequenceDesc.activity)
				.read(sequenceDesc.activityWeight)
				.read(sequenceDesc.eventCount);
			const auto eventIndex = stream.read<int32_t>();
			stream
				.read(sequenceDesc.boundingBoxMin)
				.read(sequenceDesc.boundingBoxMax)
				.read(sequenceDesc.blendCount);

			const auto animIndexOffset = stream.read<int32_t>();

			stream
				.read(sequenceDesc.movementIndex)
				.read(sequenceDesc.groupSize)
				.read(sequenceDesc.paramIndex)
				.read(sequenceDesc.paramStart)
				.read(sequenceDesc.paramEnd)
				.read(sequenceDesc.paramParent)
				.read(sequenceDesc.fadeInTime)
				.read(sequenceDesc.fadeOutTime)
				.read(sequenceDesc.localEntryNode)
				.read(sequenceDesc.localExitNode)
				.read(sequenceDesc.nodeFlags)
				.read(sequenceDesc.entryPhase)
				.read(sequenceDesc.exitPhase)
				.read(sequenceDesc.lastFrame)
				.read(sequenceDesc.nextSequence)
				.read(sequenceDesc.pose)
				.read(sequenceDesc.ikRuleCount)
				.read(sequenceDesc.autoLayerCount);
			const auto autoLayerIndex = stream.read<int32_t>();
			stream
				.read(sequenceDesc.weightListIndex)
				.read(sequenceDesc.poseKeyIndex)
				.read(sequenceDesc.ikLockCount);
			const auto ikLockIndex = stream.read<int32_t>();
			const auto keyValueIndex = stream.read<int32_t>();
			const auto keyValueSize = stream.read<int32_t>();
			stream.read(sequenceDesc.cyclePoseIndex);
			const auto activityModifierIndex = stream.read<int32_t>();
			const auto activityModifierCount = stream.read<int32_t>();
			stream.skip<int32_t>(5);

			const auto blendGroupWidth = sequenceDesc.groupSize[0];
			const auto blendGroupHeight = sequenceDesc.groupSize[1];
			if (animIndexOffset > 0 && blendGroupWidth > 0 && blendGroupHeight > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(sequenceDescPos + animIndexOffset);
				sequenceDesc.animationIndices.reserve(static_cast<size_t>(blendGroupWidth * blendGroupHeight));
				for (int j = 0; j < blendGroupWidth * blendGroupHeight; j++) {
					sequenceDesc.animationIndices.push_back(stream.read<int16_t>());
				}
				stream.seek_u(returnPos);
			}

			if (eventIndex > 0 && sequenceDesc.eventCount > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(sequenceDescPos + eventIndex);
				for (int j = 0; j < sequenceDesc.eventCount; j++) {
					const auto eventPos = stream.tell();
					auto& event = sequenceDesc.events.emplace_back();
					stream
						.read(event.cycle)
						.read(event.event)
						.read(event.type)
						.read(event.options, 64);
					const auto eventNameIndex = stream.read<int32_t>();
					if (eventNameIndex > 0) {
						event.name = stream.at_string_u(static_cast<uint64_t>(eventPos) + static_cast<uint64_t>(eventNameIndex));
					}
				}
				stream.seek_u(returnPos);
			}

			if (autoLayerIndex > 0 && sequenceDesc.autoLayerCount > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(sequenceDescPos + autoLayerIndex);
				for (int j = 0; j < sequenceDesc.autoLayerCount; j++) {
					auto& autoLayer = sequenceDesc.autoLayers.emplace_back();
					stream
						.read(autoLayer.sequence)
						.read(autoLayer.pose)
						.read(autoLayer.flags)
						.read(autoLayer.start)
						.read(autoLayer.peak)
						.read(autoLayer.tail)
						.read(autoLayer.end);
				}
				stream.seek_u(returnPos);
			}

			if (sequenceDesc.weightListIndex > 0 && boneCount > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(sequenceDescPos + sequenceDesc.weightListIndex);
				sequenceDesc.boneWeights.reserve(static_cast<size_t>(boneCount));
				for (int j = 0; j < boneCount; j++) {
					sequenceDesc.boneWeights.push_back(stream.read<float>());
				}
				stream.seek_u(returnPos);
			}

			if (ikLockIndex > 0 && sequenceDesc.ikLockCount > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(sequenceDescPos + ikLockIndex);
				for (int j = 0; j < sequenceDesc.ikLockCount; j++) {
					auto &ikLock = sequenceDesc.ikLocks.emplace_back();
					stream
						.read(ikLock.chain)
						.read(ikLock.positionWeight)
						.read(ikLock.localQuaternionWeight)
						.read(ikLock.flags);
					stream.skip<int32_t>(4);
				}
				stream.seek_u(returnPos);
			}

			if (sequenceDesc.poseKeyIndex > 0 && blendGroupWidth > 0) {
				for (int param = 0; param < 2; param++) {
					if (sequenceDesc.paramIndex[param] < 0) {
						continue;
					}
					sequenceDesc.poseKeys[param].reserve(static_cast<size_t>(blendGroupWidth));
					for (int key = 0; key < blendGroupWidth; key++) {
						const auto poseKeyPos = static_cast<uint64_t>(sequenceDescPos + sequenceDesc.poseKeyIndex) + sizeof(float) * static_cast<uint64_t>(param * blendGroupWidth + key);
						sequenceDesc.poseKeys[param].push_back(stream.at<float>(static_cast<int64_t>(poseKeyPos)));
					}
				}
			}

			if (keyValueIndex > 0 && keyValueSize > 0) {
				sequenceDesc.keyValueText = stream.at_string_u(static_cast<uint64_t>(keyValueSize), true, static_cast<uint64_t>(sequenceDescPos) + static_cast<uint64_t>(keyValueIndex));
			}

			if (activityModifierIndex > 0 && activityModifierCount > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(sequenceDescPos + activityModifierIndex);
				for (int j = 0; j < activityModifierCount; j++) {
					const auto modifierPos = stream.tell();
					auto& modifier = sequenceDesc.activityModifiers.emplace_back();
					const auto modifierNameIndex = stream.read<int32_t>();
					if (modifierNameIndex > 0) {
						modifier.name = stream.at_string_u(static_cast<uint64_t>(modifierPos) + static_cast<uint64_t>(modifierNameIndex));
					}
				}
				stream.seek_u(returnPos);
			}
		}
	}

	if (ikChainCount > 0 && ikChainOffset > 0) {
		for (int i = 0; i < ikChainCount; i++) {
			const auto ikChainPos = static_cast<uint64_t>(ikChainOffset) + static_cast<uint64_t>(i) * sizeof(int32_t) * 4;
			stream.seek_u(ikChainPos);

			auto &ikChain = this->ikChains.emplace_back();
			parser::binary::readStringAtOffset(stream, ikChain.name);
			stream.read(ikChain.linkType);
			const auto linkCount = stream.read<int32_t>();
			const auto linkIndex = stream.read<int32_t>();

			if (linkCount > 0 && linkIndex > 0) {
				const auto returnPos = stream.tell();
				stream.seek_u(ikChainPos + static_cast<uint64_t>(linkIndex));
				for (int j = 0; j < linkCount; j++) {
					auto &ikLink = ikChain.links.emplace_back();
					stream.read(ikLink.bone).read(ikLink.kneeDir);
					stream.skip<math::Vec3f>();
				}
				stream.seek_u(returnPos);
			}
		}
	}

	if (ikAutoplayLockCount > 0 && ikAutoplayLockOffset > 0) {
		stream.seek_u(ikAutoplayLockOffset);
		for (int i = 0; i < ikAutoplayLockCount; i++) {
			auto &ikLock = this->ikAutoplayLocks.emplace_back();
			stream
				.read(ikLock.chain)
				.read(ikLock.positionWeight)
				.read(ikLock.localQuaternionWeight)
				.read(ikLock.flags);
			stream.skip<int32_t>(4);
		}
	}

	stream.seek(materialOffset);
	for (int i = 0; i < materialCount; i++) {
		auto& material = this->materials.emplace_back();

		parser::binary::readStringAtOffset(stream, material.name);
		stream.read(material.flags);

		// used
		stream.skip<int32_t>();
		// _unused0
		stream.skip<int32_t>(13);
	}

	stream.seek(materialDirOffset);
	for (int i = 0; i < materialDirCount; i++) {
		auto& materialDir = this->materialDirectories.emplace_back();

		parser::binary::readStringAtOffset(stream, materialDir, std::ios::beg, 0);
	}

    stream.seek(skinReferenceOffset);
    for (int i = 0; i < skinReferenceFamilyCount; i++) {
        std::vector<int16_t> skinFamily;
        skinFamily.reserve(skinReferenceCount);
		for (int j = 0; j < skinReferenceCount; j++) {
            skinFamily.push_back(stream.read<int16_t>());
        }
        this->skins.push_back(std::move(skinFamily));
    }

	for (int i = 0; i < bodyPartCount; i++) {
		const auto bodyPartPos = bodyPartOffset + i * (sizeof(int32_t) * 4);
		stream.seek_u(bodyPartPos);

		auto& bodyPart = this->bodyParts.emplace_back();

		parser::binary::readStringAtOffset(stream, bodyPart.name);

		const auto modelsCount = stream.read<int32_t>();
		stream.skip<int32_t>(); // base
		const auto modelsOffset = stream.read<int32_t>();

		for (int j = 0; j < modelsCount; j++) {
			auto modelPos = modelsOffset + j * (64 + sizeof(float) + sizeof(int32_t) * 20);
			stream.seek_u(bodyPartPos + modelPos);

			auto& model = bodyPart.models.emplace_back();

			stream
				.read(model.name, 64)
				.read(model.type)
				.read(model.boundingRadius);

			const auto meshesCount = stream.read<int32_t>();
			const auto meshesOffset = stream.read<int32_t>();

			stream
				.read(model.verticesCount)
				.read(model.verticesOffset);

			for (int k = 0; k < meshesCount; k++) {
				const auto meshPos = meshesOffset + k * (sizeof(int32_t) * (18 + MAX_LOD_COUNT) + sizeof(math::Vec3f));
				stream.seek_u(bodyPartPos + modelPos + meshPos);

				auto& mesh = model.meshes.emplace_back();

				stream
					.read(mesh.material)
					.skip<int32_t>()
					.read(mesh.verticesCount)
					.read(mesh.verticesOffset)
					.skip<int32_t>(2)
					.read(mesh.materialType)
					.read(mesh.materialParam)
					.read(mesh.meshID)
					.read(mesh.center);
			}
		}
	}

	if (attachmentCount > 0 && attachmentOffset > 0) {
		stream.seek(attachmentOffset);
		for (int i = 0; i < attachmentCount; i++) {
			auto& attachment = this->attachments.emplace_back();

			parser::binary::readStringAtOffset(stream, attachment.name);
			stream
				.read(attachment.flags)
				.read(attachment.bone)
				.read(attachment.local);
			stream.skip<int32_t>(8);
		}
	}

	return true;
}
