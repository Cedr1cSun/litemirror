// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include "doctest.h"
#include <string>
#include <string.h>
#include <stdlib.h>
extern "C" {
#include "base64.h"
}

static std::string encode(base64_t* b64, const char* text)
{
	int len = base64_encoded_length(b64, (int)strlen(text));
	char* out = (char*)malloc(len);
	int n = base64_encode(b64, out, (const unsigned char*)text, (int)strlen(text));
	std::string s(out, n);
	free(out);
	return s;
}

TEST_CASE("base64 encodes RFC 4648 vectors with padding")
{
	CHECK(encode(NULL, "") == "");
	CHECK(encode(NULL, "f") == "Zg==");
	CHECK(encode(NULL, "fo") == "Zm8=");
	CHECK(encode(NULL, "foo") == "Zm9v");
	CHECK(encode(NULL, "foobar") == "Zm9vYmFy");
}

TEST_CASE("base64 without padding, as used in RAOP headers")
{
	base64_t* b64 = base64_init(NULL, 0, 0);
	REQUIRE(b64 != NULL);
	CHECK(encode(b64, "f") == "Zg");
	CHECK(encode(b64, "fo") == "Zm8");
	CHECK(encode(b64, "foob") == "Zm9vYg");
	base64_destroy(b64);
}

TEST_CASE("base64 decode round-trips binary data")
{
	unsigned char raw[256];
	for (int i = 0; i < 256; i++) raw[i] = (unsigned char)i;

	int len = base64_encoded_length(NULL, sizeof(raw));
	char* enc = (char*)malloc(len);
	int enclen = base64_encode(NULL, enc, raw, sizeof(raw));

	unsigned char* dec = NULL;
	int declen = base64_decode(NULL, &dec, enc, enclen);
	REQUIRE(declen == (int)sizeof(raw));
	CHECK(memcmp(dec, raw, sizeof(raw)) == 0);

	free(dec);
	free(enc);
}

TEST_CASE("base64_init rejects a bad alphabet")
{
	CHECK(base64_init("too short", 1, 0) == NULL);
}
