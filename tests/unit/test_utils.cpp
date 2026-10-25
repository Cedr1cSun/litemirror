// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include "doctest.h"
#include <string.h>
extern "C" {
#include "utils.h"
}

static const char MAC[6] = { 0x48, 0x5d, 0x60, 0x7c, (char)0xee, 0x22 };

TEST_CASE("RAOP service name uses upper-case hex without separators")
{
	char out[32];
	CHECK(utils_hwaddr_raop(out, sizeof(out), MAC, sizeof(MAC)) == 13);
	CHECK(strcmp(out, "485D607CEE22") == 0);
}

TEST_CASE("AirPlay TXT deviceid uses lower-case colon-separated hex")
{
	char out[32];
	CHECK(utils_hwaddr_airplay(out, sizeof(out), MAC, sizeof(MAC)) == 18);
	CHECK(strcmp(out, "48:5d:60:7c:ee:22") == 0);
}

TEST_CASE("hwaddr formatters refuse a short buffer")
{
	char out[8];
	CHECK(utils_hwaddr_raop(out, sizeof(out), MAC, sizeof(MAC)) == -1);
	CHECK(utils_hwaddr_airplay(out, sizeof(out), MAC, sizeof(MAC)) == -1);
}
