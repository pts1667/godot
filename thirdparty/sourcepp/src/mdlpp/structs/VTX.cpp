#include <mdlpp/structs/VTX.h>

#include <BufferStream.h>

using namespace mdlpp::VTX;

const MaterialReplacement *VTX::findMaterialReplacement(int32_t lod, int16_t material_id) const {
	if (lod < 0 || lod >= static_cast<int32_t>(this->materialReplacementLists.size())) {
		return nullptr;
	}

	for (const MaterialReplacement &replacement : this->materialReplacementLists[static_cast<size_t>(lod)]) {
		if (replacement.materialID == material_id) {
			return &replacement;
		}
	}

	return nullptr;
}

bool VTX::open(const std::byte* data, std::size_t size, const MDL::MDL& mdl) {
	BufferStreamReadOnly stream{data, size};

	if (stream.read(this->version); this->version != 7) {
		return false;
	}

	stream
		.read(this->vertexCacheSize)
		.read(this->maxBonesPerStrip)
		.read(this->maxBonesPerTriangle)
		.read(this->maxBonesPerVertex);

	if (stream.read<int32_t>() != mdl.checksum) {
		return false;
	}

	stream.read(this->numLODs);
	this->materialReplacementLists.clear();
	this->materialReplacementLists.resize(this->numLODs > 0 ? static_cast<size_t>(this->numLODs) : 0);

	const auto materialReplacementListOffset = stream.read<int32_t>();

	const auto bodyPartCount = stream.read<int32_t>();
	const auto bodyPartOffset = stream.read<int32_t>();

	if (materialReplacementListOffset > 0) {
		for (int lod_index = 0; lod_index < this->numLODs; lod_index++) {
			const auto materialReplacementListPos = materialReplacementListOffset + lod_index * (sizeof(int32_t) * 2);
			stream.seek_u(materialReplacementListPos);

			const auto replacementCount = stream.read<int32_t>();
			const auto replacementOffset = stream.read<int32_t>();
			if (replacementCount <= 0 || replacementOffset <= 0) {
				continue;
			}

			auto &replacement_list = this->materialReplacementLists[static_cast<size_t>(lod_index)];
			replacement_list.reserve(static_cast<size_t>(replacementCount));
			const auto replacementListEntriesPos = materialReplacementListPos + replacementOffset;
			for (int replacement_index = 0; replacement_index < replacementCount; replacement_index++) {
				const auto replacementPos = replacementListEntriesPos + replacement_index * (sizeof(int16_t) + sizeof(int32_t));
				stream.seek_u(replacementPos);

				auto &replacement = replacement_list.emplace_back();
				stream.read(replacement.materialID);
				const auto replacementMaterialNameOffset = stream.read<int32_t>();
				if (replacementMaterialNameOffset > 0) {
					replacement.replacementMaterialName = stream.at_string_u(static_cast<uint64_t>(replacementPos + replacementMaterialNameOffset));
				}
			}
		}
	}

	for (int i = 0; i < bodyPartCount; i++) {
		const auto bodyPartPos = bodyPartOffset + i * ((sizeof(int32_t) * 2));
		stream.seek_u(bodyPartPos);

		auto& bodyPart = this->bodyParts.emplace_back();

		const auto modelCount = stream.read<int32_t>();
		const auto modelOffset = stream.read<int32_t>();

		for (int j = 0; j < modelCount; j++) {
			const auto modelPos = modelOffset + j * (sizeof(int32_t) * 2);
			stream.seek_u(bodyPartPos + modelPos);

			auto& model = bodyPart.models.emplace_back();

			const auto modelLODCount = stream.read<int32_t>();
			const auto modelLODOffset = stream.read<int32_t>();

			for (int k = 0; k < modelLODCount; k++) {
				const auto modelLODPos = modelLODOffset + k * (sizeof(int32_t) * 2 + sizeof(float));
				stream.seek_u(bodyPartPos + modelPos + modelLODPos);

				auto& modelLOD = model.modelLODs.emplace_back();

				const auto meshCount = stream.read<int32_t>();
				const auto meshOffset = stream.read<int32_t>();

				stream.read(modelLOD.switchDistance);

				for (int l = 0; l < meshCount; l++) {
					const auto meshPos = meshOffset + l * (sizeof(int32_t) * 2 + sizeof(Mesh::Flags));
					stream.seek_u(bodyPartPos + modelPos + modelLODPos + meshPos);

					auto& mesh = modelLOD.meshes.emplace_back();

					const auto stripGroupCount = stream.read<int32_t>();
					const auto stripGroupOffset = stream.read<int32_t>();

					stream.read(mesh.flags);

					for (int m = 0; m < stripGroupCount; m++) {
						int stripGroupNumInts = 6;
						if (mdl.version >= 49) {
							stripGroupNumInts += 2;
						}
						const auto stripGroupPos = stripGroupOffset + m * (sizeof(int32_t) * stripGroupNumInts + sizeof(StripGroup::Flags));
						stream.seek_u(bodyPartPos + modelPos + modelLODPos + meshPos + stripGroupPos);

						auto& stripGroup = mesh.stripGroups.emplace_back();

						const auto vertexCount = stream.read<int32_t>();
						const auto vertexOffset = stream.read<int32_t>();

						auto stripGroupCurrentPos = stream.tell();
						stream.seek_u(bodyPartPos + modelPos + modelLODPos + meshPos + stripGroupPos + vertexOffset);
						for (int n = 0; n < vertexCount; n++) {
							auto &vertex = stripGroup.vertices.emplace_back();
							stream
								.read(vertex.boneWeightIndex)
								.read(vertex.boneCount)
								.read(vertex.meshVertexID)
								.read(vertex.boneID);
						}
						stream.seek_u(stripGroupCurrentPos);

						const auto indexCount = stream.read<int32_t>();
						const auto indexOffset = stream.read<int32_t>();

						stripGroupCurrentPos = stream.tell();
						stream.seek_u(bodyPartPos + modelPos + modelLODPos + meshPos + stripGroupPos + indexOffset);
						for (int n = 0; n < indexCount; n++) {
							auto& index = stripGroup.indices.emplace_back();
							stream.read(index);
						}
						stream.seek_u(stripGroupCurrentPos);

						const auto stripCount = stream.read<int32_t>();
						const auto stripOffset = stream.read<int32_t>();

						stream.read(stripGroup.flags);

						if (mdl.version >= 49) {
							// mesh topology
							stream.skip<int32_t>(2);
						}

						stream.seek_u(bodyPartPos + modelPos + modelLODPos + meshPos + stripGroupPos + stripOffset);
						for (int n = 0; n < stripCount; n++) {
							const auto stripHeaderPos = stream.tell();
							auto& strip = stripGroup.strips.emplace_back();

							const auto indicesCount = stream.read<int32_t>();
							stream.read(strip.indicesOffset);
							strip.indices = std::span(stripGroup.indices.begin() + strip.indicesOffset, indicesCount);

							const auto verticesCount = stream.read<int32_t>();
							stream.read(strip.verticesOffset);
							strip.vertices = std::span(stripGroup.vertices.begin() + strip.verticesOffset, verticesCount);

							stream
								.read(strip.boneCount)
								.read(strip.flags);

							stream.read(strip.boneStateChangeCount);
							const auto boneStateChangeOffset = stream.read<int32_t>();

							if (mdl.version >= 49) {
								// mesh topology
								stream.skip<int32_t>(2);
							}

							const auto nextStripPos = stream.tell();
							if (strip.boneStateChangeCount > 0 && boneStateChangeOffset > 0) {
								stream.seek_u(stripHeaderPos + boneStateChangeOffset);
								strip.boneStateChanges.reserve(static_cast<size_t>(strip.boneStateChangeCount));
								for (int bone_state_change_index = 0; bone_state_change_index < strip.boneStateChangeCount; bone_state_change_index++) {
									auto &bone_state_change = strip.boneStateChanges.emplace_back();
									stream
										.read(bone_state_change.hardwareID)
										.read(bone_state_change.newBoneID);
								}
							}
							stream.seek_u(nextStripPos);
						}
					}
				}
			}
		}
	}

	return true;
}
