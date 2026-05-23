#include <sourcepp/crypto/SHA256.h>

#include <mbedtls/sha256.h>

using namespace sourcepp;

std::array<std::byte, 32> crypto::computeSHA256(std::span<const std::byte> buffer) {
	if (buffer.empty()) {
		return {};
	}

	std::array<std::byte, 32> final{};
	if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size(), reinterpret_cast<unsigned char*>(final.data()), 0) != 0) {
		return {};
	}
	return final;
}
