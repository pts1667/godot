#include <sourcepp/crypto/CRC32.h>

#include <BufferStream.h>
#include <zlib.h>

using namespace sourcepp;

uint32_t crypto::computeCRC32(std::span<const std::byte> buffer) {
	if (buffer.empty()) {
		return 0;
	}

	uint32_t final = static_cast<uint32_t>(crc32_z(0L, reinterpret_cast<const Bytef*>(buffer.data()), buffer.size()));
	BufferStream::swap_endian(&final);
	return final;
}
