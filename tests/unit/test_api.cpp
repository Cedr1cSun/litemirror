// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
#include "doctest.h"
#include <windows.h>
#include <string.h>
#include <set>
#include <string>
#include <vector>
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
	for (int e = LM_OK; e <= LM_E_ADDRESS; e++) {
		const char* msg = lm_error_string((lm_error)e);
		REQUIRE(msg != NULL);
		CHECK(strlen(msg) > 0);
		CHECK(seen.insert(msg).second);
	}
	CHECK(lm_error_string((lm_error)999) != NULL);
}

TEST_CASE("set_audio switches built-in playback only while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_audio(NULL, 1) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_audio(server, 1) == LM_OK);
	CHECK(lm_server_set_audio(server, 1) == LM_OK);   // already on

	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_audio(server, 0) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_audio(server, 0) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_pin validates its arguments and only works while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_pin(NULL, LM_PIN_OFF, NULL) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_pin(server, (lm_pin_mode)7, NULL) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_pin(server, LM_PIN_FIXED, NULL) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_pin(server, LM_PIN_FIXED, "") == LM_E_INVALID_ARG);
	std::string longPin(64, '1');
	CHECK(lm_server_set_pin(server, LM_PIN_FIXED, longPin.c_str()) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_pin(server, LM_PIN_FIXED, "1234") == LM_OK);
	CHECK(lm_server_set_pin(server, LM_PIN_RANDOM, NULL) == LM_OK);

	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_pin(server, LM_PIN_OFF, NULL) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_pin(server, LM_PIN_OFF, NULL) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_second_device only works while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_second_device(NULL, 1) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_second_device(server, 3) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_second_device(server, LM_SECOND_ASK) == LM_OK);
	CHECK(lm_server_set_second_device(server, 1) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_second_device(server, 0) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_second_device(server, 0) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_address takes a dotted IPv4 address and only while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_address(NULL, "127.0.0.1") == LM_E_INVALID_ARG);
	CHECK(lm_server_set_address(server, "eth0") == LM_E_INVALID_ARG);
	CHECK(lm_server_set_address(server, "::1") == LM_E_INVALID_ARG);
	CHECK(lm_server_set_address(server, "127.0.0.1") == LM_OK);
	CHECK(lm_server_set_address(server, "") == LM_OK);      // back to every address
	CHECK(lm_server_set_address(server, NULL) == LM_OK);

	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_address(server, NULL) == LM_E_RUNNING);
	lm_server_stop(server);

	// well formed but not an address of this machine: start says so instead of "port in use"
	CHECK(lm_server_set_address(server, "203.0.113.1") == LM_OK);
	CHECK(lm_server_start(server, &cfg) == LM_E_ADDRESS);
	CHECK(lm_server_set_address(server, "127.0.0.1") == LM_OK);
	CHECK(lm_server_start(server, &cfg) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_display wants a real size and frame rate and only while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_display(NULL, 1280, 720, 30) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_display(server, 0, 720, 30) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_display(server, 1280, 0, 30) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_display(server, 1280, 720, 0) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_display(server, 1280, 720, 30) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_display(server, 1920, 1080, 60) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_display(server, 1920, 1080, 60) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_hw_decode only works while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_hw_decode(NULL, 0) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_hw_decode(server, 0) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_hw_decode(server, 1) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_hw_decode(server, 1) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_colour checks its ranges and may be changed while running")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	lm_colour c = { 0, 0, 0, 0, 0, 0 };
	CHECK(lm_server_set_colour(NULL, &c) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_colour(server, NULL) == LM_E_INVALID_ARG);
	c.matrix = 3;
	CHECK(lm_server_set_colour(server, &c) == LM_E_INVALID_ARG);
	c.matrix = 0;
	c.range = -1;
	CHECK(lm_server_set_colour(server, &c) == LM_E_INVALID_ARG);
	c.range = 0;
	c.hue = 1.5f;
	CHECK(lm_server_set_colour(server, &c) == LM_E_INVALID_ARG);
	c.hue = -1.0f;
	c.contrast = 1.0f;
	CHECK(lm_server_set_colour(server, &c) == LM_OK);

	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);
	CHECK(lm_server_set_window(server, hwnd) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	c.brightness = 0.25f;
	CHECK(lm_server_set_colour(server, &c) == LM_OK);   // live: the user drags a slider while mirroring
	lm_server_destroy(server);
	DestroyWindow(hwnd);
}

TEST_CASE("set_low_latency only works while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_low_latency(NULL, 0) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_low_latency(server, 0) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_low_latency(server, 1) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_low_latency(server, 1) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("audio devices can be listed and one chosen while stopped")
{
	int count = lm_audio_devices(NULL, 0);
	CHECK(count >= 0);
	if (count > 0) {
		std::vector<lm_audio_device> list(count);
		CHECK(lm_audio_devices(list.data(), count) == count);
		CHECK(strlen(list[0].id) > 0);
		CHECK(strlen(list[0].name) > 0);
		lm_audio_device one;
		CHECK(lm_audio_devices(&one, 1) == count);   // the count is the total, not what fitted
	}

	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_audio_device(NULL, "") == LM_E_INVALID_ARG);
	CHECK(lm_server_set_audio_device(server, NULL) == LM_OK);
	CHECK(lm_server_set_audio_device(server, "{0.0.0.00000000}.{0}") == LM_OK);   // checked when it plays, not here
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_audio_device(server, "") == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_audio_device(server, "") == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_follow_volume only works while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_follow_volume(NULL, 1) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_follow_volume(server, 0) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_follow_volume(server, 1) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_follow_volume(server, 1) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_scaler takes 0 or 1 and may be changed while running")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_scaler(NULL, 1) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_scaler(server, 2) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_scaler(server, 1) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_scaler(server, 0) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("set_super_resolution takes 0 to 3 and may be changed while running")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	CHECK(lm_server_set_super_resolution(NULL, 1) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_super_resolution(server, 4) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_super_resolution(server, -1) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_super_resolution(server, 1) == LM_OK);
	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_super_resolution(server, 3) == LM_OK);
	CHECK(lm_server_set_super_resolution(server, 0) == LM_OK);
	lm_server_destroy(server);
}

TEST_CASE("gpu_has_vendor knows which GPUs are in this machine")
{
	CHECK(lm_gpu_has_vendor(0) == 0);
	CHECK(lm_gpu_has_vendor(0x1234) == 0);
	// at least one of the three desktop GPU makers renders this desktop
	CHECK((lm_gpu_has_vendor(0x10DE) + lm_gpu_has_vendor(0x8086) + lm_gpu_has_vendor(0x1002)) >= 1);
}

TEST_CASE("set_window takes a real window and only while stopped")
{
	lm_callbacks cb = no_callbacks();
	lm_server* server = NULL;
	REQUIRE(lm_server_create(&cb, &server) == LM_OK);
	HWND hwnd = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
	REQUIRE(hwnd != NULL);

	CHECK(lm_server_set_window(NULL, hwnd) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_window(server, (void*)0x1234) == LM_E_INVALID_ARG);
	CHECK(lm_server_set_window(server, hwnd) == LM_OK);

	lm_config cfg = { "LiteMirrorUnit", 0, 0 };
	REQUIRE(lm_server_start(server, &cfg) == LM_OK);
	CHECK(lm_server_set_window(server, NULL) == LM_E_RUNNING);
	lm_server_stop(server);
	CHECK(lm_server_set_window(server, NULL) == LM_OK);

	lm_server_destroy(server);
	DestroyWindow(hwnd);
}
