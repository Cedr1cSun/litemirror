// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include "doctest.h"
#include <string.h>
extern "C" {
#include "http_request.h"
}

static const char SETUP[] =
	"SETUP rtsp://192.168.1.125/6265229603576367238 RTSP/1.0\r\n"
	"Content-Length: 5\r\n"
	"Content-Type: application/x-apple-binary-plist\r\n"
	"CSeq: 6\r\n"
	"User-Agent: AirPlay/387.2\r\n"
	"\r\n"
	"hello";

TEST_CASE("parses an RTSP request delivered in one piece")
{
	http_request_t* req = http_request_init();
	REQUIRE(req != NULL);
	CHECK(http_request_add_data(req, SETUP, (int)strlen(SETUP)) >= 0);
	CHECK(http_request_is_complete(req));
	CHECK(!http_request_has_error(req));
	CHECK(strcmp(http_request_get_method(req), "SETUP") == 0);
	CHECK(strcmp(http_request_get_url(req), "rtsp://192.168.1.125/6265229603576367238") == 0);
	CHECK(strcmp(http_request_get_header(req, "CSeq"), "6") == 0);
	CHECK(http_request_get_header(req, "X-Missing") == NULL);

	int len = 0;
	const char* body = http_request_get_data(req, &len);
	CHECK(len == 5);
	CHECK(memcmp(body, "hello", 5) == 0);
	http_request_destroy(req);
}

TEST_CASE("request split across TCP reads completes only at the end")
{
	http_request_t* req = http_request_init();
	int half = (int)strlen(SETUP) / 2;
	http_request_add_data(req, SETUP, half);
	CHECK(!http_request_is_complete(req));
	http_request_add_data(req, SETUP + half, (int)strlen(SETUP) - half);
	CHECK(http_request_is_complete(req));
	CHECK(strcmp(http_request_get_header(req, "CSeq"), "6") == 0);
	http_request_destroy(req);
}

TEST_CASE("garbage is reported as an error")
{
	http_request_t* req = http_request_init();
	http_request_add_data(req, "\x01\x02 not http\r\n\r\n", 15);
	CHECK(http_request_has_error(req));
	http_request_destroy(req);
}
