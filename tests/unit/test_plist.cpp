// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// libplist is built from third_party/plist with MSVC; these cases catch a
// miscompiled binary-plist reader (packing, byte order) before the
// integration suite does.
#include "doctest.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "plist/plist.h"

TEST_CASE("binary plist survives a round trip through libplist")
{
	plist_t dict = plist_new_dict();
	plist_dict_set_item(dict, "name", plist_new_string("LiteMirror"));
	plist_dict_set_item(dict, "port", plist_new_uint(7000));
	plist_dict_set_item(dict, "ratio", plist_new_real(0.25));
	plist_dict_set_item(dict, "key", plist_new_data("\x01\x02\x03", 3));
	plist_t streams = plist_new_array();
	plist_array_append_item(streams, plist_new_uint(96));
	plist_dict_set_item(dict, "streams", streams);

	char* bin = NULL;
	uint32_t len = 0;
	plist_to_bin(dict, &bin, &len);
	REQUIRE(bin != NULL);
	CHECK(memcmp(bin, "bplist00", 8) == 0);

	plist_t back = NULL;
	plist_from_bin(bin, len, &back);
	REQUIRE(back != NULL);

	char* name = NULL;
	plist_get_string_val(plist_dict_get_item(back, "name"), &name);
	CHECK(strcmp(name, "LiteMirror") == 0);
	free(name);

	uint64_t port = 0;
	plist_get_uint_val(plist_dict_get_item(back, "port"), &port);
	CHECK(port == 7000);

	double ratio = 0;
	plist_get_real_val(plist_dict_get_item(back, "ratio"), &ratio);
	CHECK(ratio == 0.25);

	char* key = NULL;
	uint64_t keyLen = 0;
	plist_get_data_val(plist_dict_get_item(back, "key"), &key, &keyLen);
	CHECK(keyLen == 3);
	CHECK(memcmp(key, "\x01\x02\x03", 3) == 0);
	free(key);

	uint64_t type = 0;
	plist_get_uint_val(plist_array_get_item(plist_dict_get_item(back, "streams"), 0), &type);
	CHECK(type == 96);

	plist_free(back);
	plist_free(dict);
	free(bin);
}

TEST_CASE("libplist reads a plist produced by an iPhone")
{
	// {"et": 32, "timingPort": 51234} as python's plistlib writes it
	static const unsigned char phone[] = {
		'b','p','l','i','s','t','0','0',
		0xd2, 0x01, 0x02, 0x03, 0x04,
		0x52, 'e','t',
		0x5a, 't','i','m','i','n','g','P','o','r','t',
		0x10, 0x20,
		0x11, 0xc8, 0x22,
		0x08, 0x0d, 0x10, 0x1b, 0x1d,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
	};
	plist_t root = NULL;
	plist_from_bin((const char*)phone, sizeof(phone), &root);
	REQUIRE(root != NULL);

	uint64_t et = 0, port = 0;
	plist_get_uint_val(plist_dict_get_item(root, "et"), &et);
	plist_get_uint_val(plist_dict_get_item(root, "timingPort"), &port);
	CHECK(et == 32);
	CHECK(port == 51234);
	plist_free(root);
}

TEST_CASE("libplist rejects garbage instead of crashing")
{
	// plist_from_bin leaves the output untouched on failure, so callers
	// must start from NULL; the handlers in src/core do.
	plist_t root = NULL;
	plist_from_bin("not a plist at all, just text", 29, &root);
	CHECK(root == NULL);
	plist_from_bin("bplist00", 8, &root);   // header only, no trailer
	CHECK(root == NULL);
}
