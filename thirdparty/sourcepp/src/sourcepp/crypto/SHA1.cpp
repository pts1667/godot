#include <sourcepp/crypto/SHA1.h>

#include <mbedtls/sha1.h>

using namespace sourcepp;

std::array<std::byte, 20> crypto::computeSHA1(std::span<const std::byte> buffer) {
	if (buffer.empty()) {
		return {};
	}

	std::array<std::byte, 20> final{};
	if (mbedtls_sha1(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size(), reinterpret_cast<unsigned char*>(final.data())) != 0) {
		return {};
	}
	return final;
}
