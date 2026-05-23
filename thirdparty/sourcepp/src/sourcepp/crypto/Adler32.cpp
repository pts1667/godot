#include <sourcepp/crypto/Adler32.h>

#include <BufferStream.h>
#include <span>

using namespace sourcepp;

namespace {

constexpr uint32_t ADLER32_MOD = 65521;

} // namespace

uint32_t crypto::computeAdler32(std::span<const std::byte> buffer) {
	if (buffer.empty()) {
		return 0;
	}

	// Preserve SourcePP's existing zero-initialized Adler32 variant used by GCF.
	uint32_t lower = 0;
	uint32_t upper = 0;
	for (const std::byte byte : buffer) {
		lower = (lower + static_cast<uint8_t>(byte)) % ADLER32_MOD;
		upper = (upper + lower) % ADLER32_MOD;
	}

	uint32_t final = (upper << 16) | lower;
	BufferStream::swap_endian(&final);
	return final;
}
