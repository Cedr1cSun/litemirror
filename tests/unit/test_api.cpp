// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include "doctest.h"
#include <string.h>
#include <set>
#include <string>
#include "litemirror.h"

static lm_callbacks no_callbacks()
{
	lm_callbacks cb;
	memset(&cb, 0, sizeof(cb));
	return cb;
}

TEST_CASE("create rejects null arguments")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	CHECK(lm_server_create(NULL, &server) == LM_E_INVALID_ARG);
	CHECK(lm_server_create(&cb, NULL) == LM_E_INVALID_ARG);
	CHECK(server == NULL);
}

TEST_CASE("start validates the configuration")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	REQUIRE(server != NULL);

	CHECK(lm_server_start(server, NULL) == LM_E_INVALID_ARG);

	lm_config cfg = { "", 0, 0 };
	CHECK(lm_server_start(server, &cfg) == LM_E_INVALID_ARG);

	std::string longName(128, 'x');
	cfg.name = longName.c_str();
	CHECK(lm_server_start(server, &cfg) == LM_E_INVALID_ARG);

	cfg.name = NULL;
	CHECK(lm_server_start(server, &cfg) == LM_E_INVALID_ARG);

	lm_server_destroy(server);
}

TEST_CASE("start twice reports LM_E_RUNNING and stop allows a restart")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);

	lm_config cfg = { "LiteMirrorUnit", 0, 0 };   // 0: the OS picks the ports
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_start(server, &cfg) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_start(server, &cfg) == LM_OK);
	lm_server_destroy(server);   // still running: destroy must stop it first
}

TEST_CASE("stop and destroy accept NULL")
{
	lm_server_stop(NULL);
	lm_server_destroy(NULL);
}

TEST_CASE("every error code has its own message")
{
	std::set<std::string> seen;
	for (int e = LM_OK; e <= LM_E_NO_H264_DECODER; e++) {
		const char* msg = lm_error_string((lm_error)e);
		REQUIRE(msg != NULL);
		CHECK(strlen(msg) > 0);
		CHECK(seen.insert(msg).second);
	}
	CHECK(lm_error_string((lm_error)999) != NULL);
}
