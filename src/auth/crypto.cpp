#include "vb/auth/crypto.hpp"

#include <algorithm>
#include <cstring>

#if defined(VB_WITH_AUTH)
#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#endif

namespace vb::auth {

#if defined(VB_WITH_AUTH)

namespace {

struct Bn {
	mbedtls_mpi v;
	Bn() { mbedtls_mpi_init(&v); }
	~Bn() { mbedtls_mpi_free(&v); }
	Bn(const Bn &) = delete;
	Bn &operator=(const Bn &) = delete;
};

bool verify_rs256(const PublicKey &key, const std::array<std::uint8_t, 32> &hash,
		std::span<const std::uint8_t> sig) {
	mbedtls_rsa_context rsa;
	mbedtls_rsa_init(&rsa);
	bool ok = false;
	do {
		if (key.rsa_n.empty() || key.rsa_e.empty()) {
			break;
		}
		if (mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256) != 0) {
			break;
		}
		if (mbedtls_rsa_import_raw(&rsa, key.rsa_n.data(), key.rsa_n.size(), nullptr, 0,
					nullptr, 0, nullptr, 0, key.rsa_e.data(), key.rsa_e.size()) != 0) {
			break;
		}
		if (mbedtls_rsa_complete(&rsa) != 0 || mbedtls_rsa_check_pubkey(&rsa) != 0) {
			break;
		}
		const std::size_t len = mbedtls_rsa_get_len(&rsa);
		if (len * 8 < kMinRsaBits || len * 8 > kMaxRsaBits || sig.size() != len) {
			break;
		}
		ok = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256,
					 static_cast<unsigned>(hash.size()), hash.data(), sig.data()) == 0;
	} while (false);
	mbedtls_rsa_free(&rsa);
	return ok;
}

// 0x04 || X(32) || Y(32), left-padding each coordinate.
bool uncompressed_point(const PublicKey &key, std::uint8_t out[65]) {
	if (key.ec_x.empty() || key.ec_y.empty() || key.ec_x.size() > 32 ||
			key.ec_y.size() > 32) {
		return false;
	}
	std::memset(out, 0, 65);
	out[0] = 0x04;
	std::memcpy(out + 1 + (32 - key.ec_x.size()), key.ec_x.data(), key.ec_x.size());
	std::memcpy(out + 33 + (32 - key.ec_y.size()), key.ec_y.data(), key.ec_y.size());
	return true;
}

bool verify_es256(const PublicKey &key, const std::array<std::uint8_t, 32> &hash,
		std::span<const std::uint8_t> sig) {
	if (sig.size() != 64) {
		return false;
	}
	std::uint8_t pt[65];
	if (!uncompressed_point(key, pt)) {
		return false;
	}
	mbedtls_ecp_group grp;
	mbedtls_ecp_point q;
	mbedtls_ecp_group_init(&grp);
	mbedtls_ecp_point_init(&q);
	Bn r;
	Bn s;
	bool ok = false;
	do {
		if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
			break;
		}
		if (mbedtls_ecp_point_read_binary(&grp, &q, pt, sizeof(pt)) != 0 ||
				mbedtls_ecp_check_pubkey(&grp, &q) != 0) {
			break;
		}
		if (mbedtls_mpi_read_binary(&r.v, sig.data(), 32) != 0 ||
				mbedtls_mpi_read_binary(&s.v, sig.data() + 32, 32) != 0) {
			break;
		}
		ok = mbedtls_ecdsa_verify(&grp, hash.data(), hash.size(), &q, &r.v, &s.v) == 0;
	} while (false);
	mbedtls_ecp_point_free(&q);
	mbedtls_ecp_group_free(&grp);
	return ok;
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::string_view data) {
	std::array<std::uint8_t, 32> out{};
	mbedtls_sha256(reinterpret_cast<const unsigned char *>(data.data()), data.size(),
			out.data(), 0);
	return out;
}

bool verify_signature(const PublicKey &key, std::string_view signing_input,
		std::span<const std::uint8_t> signature) {
	const auto hash = sha256(signing_input);
	switch (key.alg) {
		case JwsAlg::kRS256:
			return verify_rs256(key, hash, signature);
		case JwsAlg::kES256:
			return verify_es256(key, hash, signature);
	}
	return false;
}

std::vector<std::uint8_t> random_bytes(std::size_t n) {
	std::vector<std::uint8_t> out(n);
	mbedtls_entropy_context ent;
	mbedtls_entropy_init(&ent);
	bool ok = true;
	for (std::size_t off = 0; off < n && ok;) {
		const std::size_t chunk = std::min<std::size_t>(n - off, MBEDTLS_ENTROPY_BLOCK_SIZE);
		ok = mbedtls_entropy_func(&ent, out.data() + off, chunk) == 0;
		off += chunk;
	}
	mbedtls_entropy_free(&ent);
	if (!ok) {
		out.clear();
	}
	return out;
}

namespace testing {

struct TestSigner::Impl {
	PublicKey pub;
	mbedtls_entropy_context ent;
	mbedtls_ctr_drbg_context drbg;
	mbedtls_rsa_context rsa;
	mbedtls_ecp_group grp;
	Bn d;
	bool is_rsa = true;

	Impl() {
		mbedtls_entropy_init(&ent);
		mbedtls_ctr_drbg_init(&drbg);
		mbedtls_rsa_init(&rsa);
		mbedtls_ecp_group_init(&grp);
		mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, nullptr, 0);
	}
	~Impl() {
		mbedtls_ecp_group_free(&grp);
		mbedtls_rsa_free(&rsa);
		mbedtls_ctr_drbg_free(&drbg);
		mbedtls_entropy_free(&ent);
	}
};

TestSigner::TestSigner() :
		impl_(new Impl) {}
TestSigner::~TestSigner() { delete impl_; }
TestSigner::TestSigner(TestSigner &&o) noexcept :
		impl_(o.impl_) { o.impl_ = nullptr; }
TestSigner &TestSigner::operator=(TestSigner &&o) noexcept {
	if (this != &o) {
		delete impl_;
		impl_ = o.impl_;
		o.impl_ = nullptr;
	}
	return *this;
}

TestSigner TestSigner::rsa(std::size_t bits) {
	TestSigner t;
	Impl &i = *t.impl_;
	i.is_rsa = true;
	mbedtls_rsa_set_padding(&i.rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256);
	mbedtls_rsa_gen_key(&i.rsa, mbedtls_ctr_drbg_random, &i.drbg,
			static_cast<unsigned>(bits), 65537);
	Bn n, e;
	mbedtls_rsa_export(&i.rsa, &n.v, nullptr, nullptr, nullptr, &e.v);
	i.pub.alg = JwsAlg::kRS256;
	i.pub.rsa_n.resize(mbedtls_mpi_size(&n.v));
	mbedtls_mpi_write_binary(&n.v, i.pub.rsa_n.data(), i.pub.rsa_n.size());
	i.pub.rsa_e.resize(mbedtls_mpi_size(&e.v));
	mbedtls_mpi_write_binary(&e.v, i.pub.rsa_e.data(), i.pub.rsa_e.size());
	return t;
}

TestSigner TestSigner::p256() {
	TestSigner t;
	Impl &i = *t.impl_;
	i.is_rsa = false;
	mbedtls_ecp_group_load(&i.grp, MBEDTLS_ECP_DP_SECP256R1);
	mbedtls_ecp_point q;
	mbedtls_ecp_point_init(&q);
	mbedtls_ecp_gen_keypair(&i.grp, &i.d.v, &q, mbedtls_ctr_drbg_random, &i.drbg);
	std::uint8_t buf[65];
	std::size_t olen = 0;
	mbedtls_ecp_point_write_binary(&i.grp, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &olen, buf,
			sizeof(buf));
	mbedtls_ecp_point_free(&q);
	i.pub.alg = JwsAlg::kES256;
	i.pub.ec_x.assign(buf + 1, buf + 33);
	i.pub.ec_y.assign(buf + 33, buf + 65);
	return t;
}

JwsAlg TestSigner::alg() const { return impl_->pub.alg; }
const PublicKey &TestSigner::public_key() const { return impl_->pub; }

std::vector<std::uint8_t> TestSigner::sign(std::string_view signing_input) const {
	const auto hash = sha256(signing_input);
	if (impl_->is_rsa) {
		std::vector<std::uint8_t> sig(mbedtls_rsa_get_len(&impl_->rsa));
		if (mbedtls_rsa_pkcs1_sign(&impl_->rsa, mbedtls_ctr_drbg_random, &impl_->drbg,
					MBEDTLS_MD_SHA256, 32, hash.data(), sig.data()) != 0) {
			return {};
		}
		return sig;
	}
	Bn r, s;
	if (mbedtls_ecdsa_sign(&impl_->grp, &r.v, &s.v, &impl_->d.v, hash.data(), hash.size(),
				mbedtls_ctr_drbg_random, &impl_->drbg) != 0) {
		return {};
	}
	std::vector<std::uint8_t> sig(64);
	mbedtls_mpi_write_binary(&r.v, sig.data(), 32);
	mbedtls_mpi_write_binary(&s.v, sig.data() + 32, 32);
	return sig;
}

} // namespace testing

#else // !VB_WITH_AUTH: every primitive fails closed.

std::array<std::uint8_t, 32> sha256(std::string_view) { return {}; }
bool verify_signature(const PublicKey &, std::string_view, std::span<const std::uint8_t>) {
	return false;
}
std::vector<std::uint8_t> random_bytes(std::size_t) { return {}; }

#endif

} // namespace vb::auth
