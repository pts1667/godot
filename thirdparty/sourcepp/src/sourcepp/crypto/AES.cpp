#include <sourcepp/crypto/AES.h>

#include <array>
#include <cstring>

#include <mbedtls/aes.h>
#include <span>

using namespace sourcepp;

namespace sourcepp::crypto {

const std::array<std::byte, 16> NULL_IV{};

} // namespace sourcepp::crypto

bool crypto::decryptAES_CFB(std::span<std::byte> buffer, std::span<const std::byte> key, std::span<const std::byte> iv) {
	if (buffer.empty() || iv.size() != NULL_IV.size()) {
		return {};
	}
	if (key.size() != 16 && key.size() != 24 && key.size() != 32) {
		return {};
	}

	mbedtls_aes_context aes;
	mbedtls_aes_init(&aes);
	if (mbedtls_aes_setkey_enc(&aes, reinterpret_cast<const unsigned char*>(key.data()), static_cast<unsigned int>(key.size() * 8)) != 0) {
		mbedtls_aes_free(&aes);
		return {};
	}

	std::array<unsigned char, 16> iv_copy{};
	std::memcpy(iv_copy.data(), iv.data(), iv.size());
	size_t iv_offset = 0;
	const int result = mbedtls_aes_crypt_cfb128(&aes, MBEDTLS_AES_DECRYPT, buffer.size(), &iv_offset, iv_copy.data(), reinterpret_cast<const unsigned char*>(buffer.data()), reinterpret_cast<unsigned char*>(buffer.data()));
	mbedtls_aes_free(&aes);
	return result == 0;
}

bool crypto::encryptAES_CFB(std::span<std::byte> buffer, std::span<const std::byte> key, std::span<const std::byte> iv) {
	if (buffer.empty() || iv.size() != NULL_IV.size()) {
		return {};
	}
	if (key.size() != 16 && key.size() != 24 && key.size() != 32) {
		return {};
	}

	mbedtls_aes_context aes;
	mbedtls_aes_init(&aes);
	if (mbedtls_aes_setkey_enc(&aes, reinterpret_cast<const unsigned char*>(key.data()), static_cast<unsigned int>(key.size() * 8)) != 0) {
		mbedtls_aes_free(&aes);
		return {};
	}

	std::array<unsigned char, 16> iv_copy{};
	std::memcpy(iv_copy.data(), iv.data(), iv.size());
	size_t iv_offset = 0;
	const int result = mbedtls_aes_crypt_cfb128(&aes, MBEDTLS_AES_ENCRYPT, buffer.size(), &iv_offset, iv_copy.data(), reinterpret_cast<const unsigned char*>(buffer.data()), reinterpret_cast<unsigned char*>(buffer.data()));
	mbedtls_aes_free(&aes);
	return result == 0;
}
