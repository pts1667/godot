#include <sourcepp/crypto/MD5.h>

#include <mbedtls/md5.h>

using namespace sourcepp;

std::array<std::byte, 16> crypto::computeMD5(std::span<const std::byte> buffer) {
	if (buffer.empty()) {
		return {};
	}

	std::array<std::byte, 16> final{};
	if (mbedtls_md5(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size(), reinterpret_cast<unsigned char*>(final.data())) != 0) {
		return {};
	}
	return final;
}
