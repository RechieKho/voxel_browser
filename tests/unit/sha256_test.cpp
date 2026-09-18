#include <doctest/doctest.h>

#include "vb/core/sha256.hpp"

using namespace vb::core;

TEST_CASE("sha256_hex matches known vectors") {
	CHECK(sha256_hex("") ==
			"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(sha256_hex("abc") ==
			"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(sha256_hex(
				  "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
			"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256_hex is deterministic and sensitive to every byte") {
	CHECK(sha256_hex("hello") == sha256_hex("hello"));
	CHECK(sha256_hex("hello") != sha256_hex("hellp"));
	CHECK(sha256_hex(std::string(200, 'a')) !=
			sha256_hex(std::string(199, 'a')));
}
