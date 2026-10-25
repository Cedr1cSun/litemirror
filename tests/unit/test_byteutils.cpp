// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include "doctest.h"
#include <string.h>
extern "C" {
#include "byteutils.h"
}

TEST_CASE("little-endian readers")
{
	unsigned char b[] = { 0x78, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a };
	CHECK(byteutils_get_int(b, 0) == 0x12345678);
	CHECK(byteutils_get_short(b, 0) == 0x5678);
	CHECK(byteutils_get_long(b, 0) == 0x9abcdef012345678ULL);
}

TEST_CASE("big-endian reader used for RTP and NTP fields")
{
	unsigned char b[] = { 0x12, 0x34, 0x56, 0x78 };
	CHECK(byteutils_read_int(b, 0) == 0x12345678ULL);
}

TEST_CASE("NTP timestamp round-trips through put/read")
{
	// 1 second plus 500000 microseconds after the Unix epoch
	const uint64_t us = 1500000;
	unsigned char b[8];
	byteutils_put_timeStamp(b, 0, us);

	// seconds field carries the 1900 epoch offset
	CHECK(byteutils_read_int(b, 0) == 2208988800ULL + 1);
	// fraction is half of 2^32 for .5 s
	CHECK(byteutils_read_int(b, 4) == 0x80000000ULL);

	// reading back drops the epoch offset only in the caller, so compare parts
	uint64_t back = byteutils_read_timeStamp(b, 0);
	CHECK(back / 1000000 == 2208988800ULL + 1);
	CHECK(back % 1000000 == doctest::Approx(500000).epsilon(0.001));
}

TEST_CASE("ntptopts converts 64-bit NTP to microseconds")
{
	uint64_t ntp = (3ULL << 32) | 0x80000000ULL;   // 3.5 s
	CHECK(ntptopts(ntp) == doctest::Approx(3500000).epsilon(0.001));
}
