#include "ds4crypto.h"
#include <pico/rand.h>

#include <cstring>
#include <vector>

static const char STEP_NAME_UNK[] = "unknown step";

using Step = LoadedDS4Key::Step;

static constexpr std::pair<LoadedDS4Key::Step, const char *> STEP_NAMES[] = {
	{Step::INTERNAL, "internal processing"},
	{Step::LOAD_PARSE_KEY, "key parsing"},
	{Step::LOAD_COMPLETE_KEY, "key completion"},
	{Step::LOAD_VERIFY_KEY, "key verification"},
	{Step::SIGN_HASH, "nonce hashing"},
	{Step::SIGN_PSS, "nonce signing"},
	{Step::SIGN_EXPORT_IDENTITY, "identity exporting"},
};

/* dogtopus: Some parts in this code were shamelessly stolen from RDS4Reboot, which was authored by
   me but licensed under LGPL. Therefore I release this specific version, without all the bells and
	 whistles from RDS4Reboot, under MIT license.
	 tl;dr: TBH IDRC just use it LMAO. */

LoadedDS4Key::LoadedDS4Key() : ready(false), serial{0}, identitySig{0} {
	mbedtls_rsa_init(&this->rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
	mbedtls_md_init(&this->sha256);
	mbedtls_entropy_init(&this->entropy);
	mbedtls_ctr_drbg_init(&this->drbg);
}

LoadedDS4Key::~LoadedDS4Key() {
	mbedtls_rsa_free(&this->rsa);
	mbedtls_md_free(&this->sha256);
	mbedtls_entropy_free(&this->entropy);
	mbedtls_ctr_drbg_free(&this->drbg);
}

bool LoadedDS4Key::load(const uint8_t* key, bool skipValidation) {
	// If anyone can make it zero copy, stay elegant and without hacks like this pleeeeeeeeease do it
	const auto ds4Key = reinterpret_cast<const DS4FullKeyBlockUnaligned*>(key);
	rng_128_t rngPreWarmBuffer;
	do {
		// Setup components
		this->error.step = Step::INTERNAL;

		this->error.mbedTlsError = mbedtls_md_setup(
			&this->sha256, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0
		);
		if (this->error.mbedTlsError < 0) break;

		this->error.mbedTlsError = mbedtls_ctr_drbg_seed(
			&this->drbg, mbedtls_entropy_func, &this->entropy, NULL, 0
		);
		if (this->error.mbedTlsError < 0) break;

		// Load and complete the DS4 key
		// mbedTLS doesn't support efficiently importing CRT keys so just do a NEPQ import instead.
		this->error.step = Step::LOAD_PARSE_KEY;
		this->error.mbedTlsError = mbedtls_rsa_import_raw(
			&this->rsa,
			ds4Key->identity.modulus, sizeof(ds4Key->identity.modulus),
			ds4Key->privateKey.p, sizeof(ds4Key->privateKey.p),
			ds4Key->privateKey.q, sizeof(ds4Key->privateKey.q),
			nullptr, 0, // let mbedTLS generate d from p, q and e.
			ds4Key->identity.exponent, sizeof(ds4Key->identity.exponent)
		);
		if (this->error.mbedTlsError < 0) break;

		this->error.step = Step::LOAD_COMPLETE_KEY;
		this->error.mbedTlsError = mbedtls_rsa_complete(&this->rsa);
		if (this->error.mbedTlsError < 0) break;

		if (!skipValidation) {
			// Verify the key
			this->error.step = Step::LOAD_VERIFY_KEY;
			this->error.mbedTlsError = mbedtls_rsa_check_pub_priv(&this->rsa, &this->rsa);
			if (this->error.mbedTlsError < 0) break;
		}

		// Copy over the serial and identity signature.
		// We don't need to keep another copy of public key since that is managed by mbedTLS.
		memcpy(this->serial, ds4Key->identity.serial, sizeof(ds4Key->identity.serial));
		memcpy(this->identitySig, ds4Key->identitySig, sizeof(ds4Key->identitySig));

		this->error.step = Step::DONE;
		this->ready = true;
	} while (false);

	return this->error.step == Step::DONE;
}

bool LoadedDS4Key::sign(const NonceBuffer nonce, ResponseBuffer& response) {
	uint8_t sha256Buf[32];
	do {
		// Digest
		// Not using the easy function here saves a bit of stack memory since no alloca()-alike is
		// issued.
		this->error.step = Step::SIGN_HASH;
		this->error.mbedTlsError = mbedtls_md_starts(&this->sha256);
		if (this->error.mbedTlsError < 0) break;

		this->error.mbedTlsError = mbedtls_md_update(&this->sha256, nonce, sizeof(NonceBuffer));
		if (this->error.mbedTlsError < 0) break;

		this->error.mbedTlsError = mbedtls_md_finish(&this->sha256, sha256Buf);
		if (this->error.mbedTlsError < 0) break;

		// Sign
		this->error.step = Step::SIGN_PSS;
		this->error.mbedTlsError = mbedtls_rsa_rsassa_pss_sign(
			&this->rsa, mbedtls_ctr_drbg_random, &this->drbg,
			MBEDTLS_RSA_PRIVATE, MBEDTLS_MD_SHA256,
			sizeof(sha256Buf), sha256Buf,
			response.sig
		);
		if (this->error.mbedTlsError < 0) break;

		this->error.step = Step::SIGN_EXPORT_IDENTITY;
		memcpy(response.identity.serial, this->serial, sizeof(response.identity.serial));

		this->error.mbedTlsError = mbedtls_rsa_export_raw(
			&this->rsa,
			response.identity.modulus, sizeof(response.identity.modulus),
			nullptr, 0,
			nullptr, 0,
			nullptr, 0,
			response.identity.exponent, sizeof(response.identity.exponent)
		);
		if (this->error.mbedTlsError < 0) break;

		memcpy(response.identitySig, this->identitySig, sizeof(response.identitySig));
		memset(response.padding, 0, sizeof(response.padding));

		this->error.step = Step::DONE;
	} while (false);

	return this->error.step == Step::DONE;
}

const char* LoadedDS4Key::getStepName() {
	for (const auto& entry : STEP_NAMES) {
		if (entry.first == this->error.step) {
			return entry.second;
		}
	}
	return STEP_NAME_UNK;
}
