#include <sourcepp/crypto/RSA.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

#include <sourcepp/String.h>
#include <sourcepp/crypto/SHA256.h>

using namespace sourcepp;

namespace {

constexpr auto RNG_PERSONALIZATION = "sourcepp-rsa";

bool initialize_rng(mbedtls_entropy_context& entropy, mbedtls_ctr_drbg_context& ctr_drbg) {
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char*>(RNG_PERSONALIZATION), sizeof(RNG_PERSONALIZATION) - 1) != 0) {
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_entropy_free(&entropy);
		return false;
	}
	return true;
}

std::vector<std::byte> write_key_der(const mbedtls_pk_context& key, bool public_key, size_t suggested_size) {
	std::vector<unsigned char> der_buffer(std::max<size_t>(suggested_size, 1024));
	const int der_length = public_key ? mbedtls_pk_write_pubkey_der(&key, der_buffer.data(), der_buffer.size()) : mbedtls_pk_write_key_der(&key, der_buffer.data(), der_buffer.size());
	if (der_length <= 0) {
		return {};
	}

	std::vector<std::byte> out(der_length);
	std::memcpy(out.data(), der_buffer.data() + der_buffer.size() - der_length, der_length);
	return out;
}

} // namespace

std::pair<std::string, std::string> crypto::computeSHA256KeyPair(uint16_t size) {
	if (size < 8) {
		return {};
	}

	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	if (!initialize_rng(entropy, ctr_drbg)) {
		return {};
	}

	mbedtls_pk_context key;
	mbedtls_pk_init(&key);
	if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0) {
		mbedtls_pk_free(&key);
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_entropy_free(&entropy);
		return {};
	}
	if (mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg, size, 65537) != 0) {
		mbedtls_pk_free(&key);
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_entropy_free(&entropy);
		return {};
	}

	const auto private_key = write_key_der(key, false, static_cast<size_t>(size) * 2);
	const auto public_key = write_key_der(key, true, static_cast<size_t>(size));
	mbedtls_pk_free(&key);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);

	if (private_key.empty() || public_key.empty()) {
		return {};
	}

	return {
		string::encodeHex(private_key),
		string::encodeHex(public_key),
	};
}

bool crypto::verifySHA256PublicKey(std::span<const std::byte> buffer, std::span<const std::byte> publicKey, std::span<const std::byte> signature) {
	if (buffer.empty()) {
		return false;
	}

	mbedtls_pk_context key;
	mbedtls_pk_init(&key);
	if (mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char*>(publicKey.data()), publicKey.size()) != 0 || !mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA)) {
		mbedtls_pk_free(&key);
		return false;
	}

	const auto sha256 = computeSHA256(buffer);
	const bool verified = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(sha256.data()), sha256.size(), reinterpret_cast<const unsigned char*>(signature.data()), signature.size()) == 0;
	mbedtls_pk_free(&key);
	return verified;
}

std::vector<std::byte> crypto::signDataWithSHA256PrivateKey(std::span<const std::byte> buffer, std::span<const std::byte> privateKey) {
	if (buffer.empty()) {
		return {};
	}

	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	if (!initialize_rng(entropy, ctr_drbg)) {
		return {};
	}

	mbedtls_pk_context key;
	mbedtls_pk_init(&key);
	if (mbedtls_pk_parse_key(&key, reinterpret_cast<const unsigned char*>(privateKey.data()), privateKey.size(), nullptr, 0, mbedtls_ctr_drbg_random, &ctr_drbg) != 0 || !mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA)) {
		mbedtls_pk_free(&key);
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_entropy_free(&entropy);
		return {};
	}

	const auto sha256 = computeSHA256(buffer);
	std::vector<std::byte> signature(MBEDTLS_PK_SIGNATURE_MAX_SIZE);
	size_t signature_length = 0;
	if (mbedtls_pk_sign(&key, MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(sha256.data()), sha256.size(), reinterpret_cast<unsigned char*>(signature.data()), signature.size(), &signature_length, mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
		mbedtls_pk_free(&key);
		mbedtls_ctr_drbg_free(&ctr_drbg);
		mbedtls_entropy_free(&entropy);
		return {};
	}

	signature.resize(signature_length);
	mbedtls_pk_free(&key);
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
	return signature;
}
