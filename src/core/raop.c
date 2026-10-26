/**
 *  Copyright (C) 2011-2012  Juho Vähä-Herttua
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#define _CRT_RAND_S
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "raop.h"
#include "digest.h"
#include "plist/plist.h"
#include "raop_rtp.h"
#include "raop_rtp.h"
#include "pairing.h"
#include "httpd.h"

#include "global.h"
#include "fairplay.h"
#include "netutils.h"
#include "logger.h"
#include "compat.h"
#include "raop_rtp_mirror.h"
// #include <android/log.h>

struct raop_s {
	/* Callbacks for audio */
	raop_callbacks_t callbacks;

	/* Logger instance */
	logger_t *logger;

	/* Pairing, HTTP daemon and RSA key */
	pairing_t *pairing;
	httpd_t *httpd;

    unsigned short port;

	/* Identity reported in /info */
	char name[128];
	char hwaddr[6];
	int hwaddrlen;

	/* PIN (Digest password) wanted on SETUP */
	int pin_mode;
	char pin[64];
	char random_pin[5];        /* current RAOP_PIN_RANDOM code, "" when none is out */
	int pin_failures;          /* wrong answers to the current random code */
	struct raop_conn_s *pin_conn;   /* the connection the code was shown for */

	/* One session at a time */
	int replace_second;
	struct raop_conn_s *session_conn;   /* the connection that did the first SETUP */

	/* Display offered in /info */
	unsigned int display_width, display_height, display_fps;
};

struct raop_conn_s {
	raop_t *raop;
	raop_rtp_t *raop_rtp;
	raop_rtp_mirror_t *raop_rtp_mirror;
	fairplay_t *fairplay;
	pairing_session_t *pairing;

	unsigned char *local;
	int locallen;

	unsigned char *remote;
	int remotelen;

	char nonce[33];            /* Digest challenge for this connection */
	int authenticated;
};
typedef struct raop_conn_s raop_conn_t;

#include "raop_handlers.h"

static void *
conn_init(void *opaque, unsigned char *local, int locallen, unsigned char *remote, int remotelen)
{
	raop_t *raop = opaque;
	raop_conn_t *conn;

	assert(raop);

	conn = calloc(1, sizeof(raop_conn_t));
	if (!conn) {
		return NULL;
	}
	conn->raop = raop;
	conn->raop_rtp = NULL;
	conn->fairplay = fairplay_init(raop->logger);
	if (!conn->fairplay) {
		free(conn);
		return NULL;
	}
	conn->pairing = pairing_session_init(raop->pairing);
	if (!conn->pairing) {
		fairplay_destroy(conn->fairplay);
		free(conn);
		return NULL;
	}

	if (locallen == 4) {
		logger_log(conn->raop->logger, LOGGER_INFO,
		           "Local: %d.%d.%d.%d",
		           local[0], local[1], local[2], local[3]);
	} else if (locallen == 16) {
		logger_log(conn->raop->logger, LOGGER_INFO,
		           "Local: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
		           local[0], local[1], local[2], local[3], local[4], local[5], local[6], local[7],
		           local[8], local[9], local[10], local[11], local[12], local[13], local[14], local[15]);
	}
	if (remotelen == 4) {
		logger_log(conn->raop->logger, LOGGER_INFO,
		           "Remote: %d.%d.%d.%d",
		           remote[0], remote[1], remote[2], remote[3]);
	} else if (remotelen == 16) {
		logger_log(conn->raop->logger, LOGGER_INFO,
		           "Remote: %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
		           remote[0], remote[1], remote[2], remote[3], remote[4], remote[5], remote[6], remote[7],
		           remote[8], remote[9], remote[10], remote[11], remote[12], remote[13], remote[14], remote[15]);
	}

	conn->local = malloc(locallen);
	assert(conn->local);
	memcpy(conn->local, local, locallen);

	conn->remote = malloc(remotelen);
	assert(conn->remote);
	memcpy(conn->remote, remote, remotelen);

	conn->locallen = locallen;
	conn->remotelen = remotelen;

	digest_generate_nonce(conn->nonce, sizeof(conn->nonce));
	return conn;
}

static void
show_pin(raop_t *raop, raop_conn_t *conn, const char *pin)
{
	raop->pin_conn = pin ? conn : NULL;
	if (raop->callbacks.pin_show) {
		raop->callbacks.pin_show(raop->callbacks.cls, pin);
	}
}

/* The first SETUP of a client carries the session keys (eiv); later ones add streams */
static int
setup_opens_session(http_request_t *request)
{
	const char *data;
	int datalen, opens = 0;
	plist_t root = NULL;

	data = http_request_get_data(request, &datalen);
	if (data && datalen > 0) {
		plist_from_bin(data, datalen, &root);
	}
	if (root) {
		opens = plist_dict_get_item(root, "eiv") != NULL;
		plist_free(root);
	}
	return opens;
}

/* Digest authentication on SETUP, as an Apple TV with a password does it.
 * Returns 1 when the request may go on; 0 means answer with a 401 challenge.
 * A random code is shown while the challenge stands and changed after three
 * wrong answers, so it cannot be guessed over the network. */
static int
check_pin(raop_conn_t *conn, http_request_t *request)
{
	raop_t *raop = conn->raop;
	const char *authorization = http_request_get_header(request, "Authorization");
	const char *password = raop->pin_mode == RAOP_PIN_FIXED ? raop->pin : raop->random_pin;

	if (authorization && password[0] &&
	    digest_is_valid("raop", password, conn->nonce, http_request_get_method(request),
	                    http_request_get_url(request), authorization)) {
		conn->authenticated = 1;
		if (raop->pin_mode == RAOP_PIN_RANDOM) {
			raop->random_pin[0] = '\0';
			show_pin(raop, conn, NULL);
		}
		return 1;
	}
	if (authorization) {
		raop->pin_failures++;
		logger_log(raop->logger, LOGGER_WARNING, "Client sent a wrong PIN");
	}
	if (raop->pin_mode == RAOP_PIN_RANDOM) {
		if (!raop->random_pin[0] || raop->pin_failures >= 3) {
			unsigned int r = 0;
			rand_s(&r);
			snprintf(raop->random_pin, sizeof(raop->random_pin), "%04u", r % 10000);
			raop->pin_failures = 0;
		}
		show_pin(raop, conn, raop->random_pin);
	}
	return 0;
}

static void
conn_request(void *ptr, http_request_t *request, http_response_t **response)
{
	raop_conn_t *conn = ptr;
    logger_log(conn->raop->logger, LOGGER_DEBUG, "conn_request");
	const char *method;
	const char *url;
	const char *cseq;

	char *response_data = NULL;
	int response_datalen = 0;

	method = http_request_get_method(request);
	url = http_request_get_url(request);
	cseq = http_request_get_header(request, "CSeq");
	if (!method || !cseq) {
		return;
	}

	if (!strcmp(method, "SETUP") && setup_opens_session(request)) {
		raop_t *raop = conn->raop;
		if (raop->session_conn && raop->session_conn != conn) {
			if (!raop->replace_second) {
				logger_log(raop->logger, LOGGER_INFO, "Second client refused: a session is running");
				*response = http_response_init("RTSP/1.0", 453, "Not Enough Bandwidth");
				http_response_add_header(*response, "CSeq", cseq);
				http_response_add_header(*response, "Server", "AirTunes/220.68");
				http_response_set_disconnect(*response, 1);
				http_response_finish(*response, NULL, 0);
				return;
			}
			logger_log(raop->logger, LOGGER_INFO, "Second client replaces the running session");
			httpd_close_connection(raop->httpd, raop->session_conn);
		}
		raop->session_conn = conn;
	}
	if (!strcmp(method, "SETUP") && conn->raop->pin_mode && !conn->authenticated && !check_pin(conn, request)) {
		char challenge[80];
		snprintf(challenge, sizeof(challenge), "Digest realm=\"raop\", nonce=\"%s\"", conn->nonce);
		*response = http_response_init("RTSP/1.0", 401, "Unauthorized");
		http_response_add_header(*response, "CSeq", cseq);
		http_response_add_header(*response, "Server", "AirTunes/220.68");
		http_response_add_header(*response, "WWW-Authenticate", challenge);
		http_response_finish(*response, NULL, 0);
		return;
	}

	*response = http_response_init("RTSP/1.0", 200, "OK");

	http_response_add_header(*response, "CSeq", cseq);
	http_response_add_header(*response, "Server", "AirTunes/220.68");

	logger_log(conn->raop->logger, LOGGER_DEBUG, "Handling request %s with URL %s", method, url);
	raop_handler_t handler = NULL;
	if (!strcmp(method, "GET") && !strcmp(url, "/info")) {
		handler = &raop_handler_info;
	} else if (!strcmp(method, "POST") && !strcmp(url, "/pair-setup")) {
		handler = &raop_handler_pairsetup;
	} else if (!strcmp(method, "POST") && !strcmp(url, "/pair-verify")) {
		handler = &raop_handler_pairverify;
	} else if (!strcmp(method, "POST") && !strcmp(url, "/fp-setup")) {
		handler = &raop_handler_fpsetup;
	} else if (!strcmp(method, "OPTIONS")) {
		handler = &raop_handler_options;
	} else if (!strcmp(method, "SETUP")) {
		handler = &raop_handler_setup;
	} else if (!strcmp(method, "GET_PARAMETER")) {
		handler = &raop_handler_get_parameter;
	} else if (!strcmp(method, "SET_PARAMETER")) {
		handler = &raop_handler_set_parameter;
	} else if (!strcmp(method, "POST") && !strcmp(url, "/feedback")) {
		handler = &raop_handler_feedback;
	} else if (!strcmp(method, "RECORD")) {
        handler = &raop_handler_record;
	} else if (!strcmp(method, "FLUSH")) {
		const char *rtpinfo;
		int next_seq = -1;

		rtpinfo = http_request_get_header(request, "RTP-Info");
		if (rtpinfo) {
			logger_log(conn->raop->logger, LOGGER_INFO, "Flush with RTP-Info: %s", rtpinfo);
			if (!strncmp(rtpinfo, "seq=", 4)) {
				next_seq = strtol(rtpinfo+4, NULL, 10);
			}
		}
		if (conn->raop_rtp) {
			raop_rtp_flush(conn->raop_rtp, next_seq);
		} else {
			logger_log(conn->raop->logger, LOGGER_WARNING, "RAOP not initialized at FLUSH");
		}
	} else if (!strcmp(method, "TEARDOWN")) {
		handler = &raop_handler_teardown;
	}
	if (handler != NULL) {
		handler(conn, request, *response, &response_data, &response_datalen);
	}
	http_response_finish(*response, response_data, response_datalen);
	if (response_data) {
		free(response_data);
		response_data = NULL;
		response_datalen = 0;
	}
}

static void
conn_destroy(void *ptr)
{
	raop_conn_t *conn = ptr;

	if (conn->raop->pin_conn == conn) {
		/* the client gave up before entering the code: take it off the screen */
		conn->raop->random_pin[0] = '\0';
		show_pin(conn->raop, conn, NULL);
	}
	if (conn->raop->session_conn == conn) {
		conn->raop->session_conn = NULL;
	}
	if (conn->raop_rtp) {
		/* This is done in case TEARDOWN was not called */
		raop_rtp_destroy(conn->raop_rtp);
	}
    if (conn->raop_rtp_mirror) {
        /* This is done in case TEARDOWN was not called */
        raop_rtp_mirror_destroy(conn->raop_rtp_mirror);
    }
	free(conn->local);
	free(conn->remote);
	pairing_session_destroy(conn->pairing);
	fairplay_destroy(conn->fairplay);
	free(conn);
}

raop_t *
raop_init(int max_clients, raop_callbacks_t *callbacks)
{
	raop_t *raop;
	pairing_t *pairing;
	httpd_t *httpd;
	httpd_callbacks_t httpd_cbs;

	assert(callbacks);
	assert(max_clients > 0);
	assert(max_clients < 100);

	/* Initialize the network */
	if (netutils_init() < 0) {
		return NULL;
	}

	/* Validate the callbacks structure */
	if (!callbacks->audio_process) {
		return NULL;
	}

	/* Allocate the raop_t structure */
	raop = calloc(1, sizeof(raop_t));
	if (!raop) {
		return NULL;
	}

	/* Initialize the logger */
	raop->logger = logger_init();
	pairing = pairing_init_generate();
	if (!pairing) {
		free(raop);
		return NULL;
	}

	/* Set HTTP callbacks to our handlers */
	memset(&httpd_cbs, 0, sizeof(httpd_cbs));
	httpd_cbs.opaque = raop;
	httpd_cbs.conn_init = &conn_init;
	httpd_cbs.conn_request = &conn_request;
	httpd_cbs.conn_destroy = &conn_destroy;

	/* Initialize the http daemon */
	httpd = httpd_init(raop->logger, &httpd_cbs, max_clients);
	if (!httpd) {
		pairing_destroy(pairing);
		free(raop);
		return NULL;
	}
	/* Copy callbacks structure */
	memcpy(&raop->callbacks, callbacks, sizeof(raop_callbacks_t));
	raop->pairing = pairing;
	raop->httpd = httpd;
	raop->display_width = 1920;
	raop->display_height = 1080;
	raop->display_fps = 60;
	return raop;
}

void
raop_destroy(raop_t *raop)
{
	if (raop) {
		raop_stop(raop);

		pairing_destroy(raop->pairing);
		httpd_destroy(raop->httpd);
		logger_destroy(raop->logger);
		free(raop);

		/* Cleanup the network */
		netutils_cleanup();
	}
}

int
raop_is_running(raop_t *raop)
{
	assert(raop);

	return httpd_is_running(raop->httpd);
}

void
raop_set_log_level(raop_t *raop, int level)
{
	assert(raop);

	logger_set_level(raop->logger, level);
}

void
raop_set_port(raop_t *raop, unsigned short port)
{
    assert(raop);
    raop->port = port;
}

unsigned short
raop_get_port(raop_t *raop)
{
    assert(raop);
    return raop->port;
}

void *
raop_get_callback_cls(raop_t *raop)
{
    assert(raop);
    return raop->callbacks.cls;
}

void
raop_set_identity(raop_t *raop, const char *name, const char *hwaddr, int hwaddrlen)
{
	assert(raop);

	if (name) {
		strncpy(raop->name, name, sizeof(raop->name) - 1);
		raop->name[sizeof(raop->name) - 1] = '\0';
	}
	if (hwaddr && hwaddrlen > 0 && hwaddrlen <= (int)sizeof(raop->hwaddr)) {
		memcpy(raop->hwaddr, hwaddr, hwaddrlen);
		raop->hwaddrlen = hwaddrlen;
	}
}

void
raop_set_pin(raop_t *raop, int mode, const char *pin)
{
	assert(raop);

	raop->pin_mode = mode;
	raop->pin[0] = '\0';
	if (mode == RAOP_PIN_FIXED && pin) {
		strncpy(raop->pin, pin, sizeof(raop->pin) - 1);
		raop->pin[sizeof(raop->pin) - 1] = '\0';
	}
	raop->random_pin[0] = '\0';
	raop->pin_failures = 0;
	raop->pin_conn = NULL;
}

void
raop_set_display(raop_t *raop, unsigned int width, unsigned int height, unsigned int fps)
{
	assert(raop);
	raop->display_width = width;
	raop->display_height = height;
	raop->display_fps = fps;
}

void
raop_set_address(raop_t *raop, unsigned long address)
{
	assert(raop);
	httpd_set_address(raop->httpd, address);
}

void
raop_set_second_device(raop_t *raop, int replace)
{
	assert(raop);
	raop->replace_second = replace;
}

int
raop_get_pin_mode(raop_t *raop)
{
	assert(raop);
	return raop->pin_mode;
}

void
raop_set_log_callback(raop_t *raop, raop_log_callback_t callback, void *cls)
{
	assert(raop);

	logger_set_callback(raop->logger, callback, cls);
}

void raop_log(raop_t* raop, int level, const char* fmt, ...)
{
	static char buffer[4096];
	va_list ap;

	buffer[sizeof(buffer) - 1] = '\0';
	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, ap);
	va_end(ap);

	logger_log(raop->logger, level, buffer);
}

int
raop_start(raop_t *raop, unsigned short *port)
{
	assert(raop);
	assert(port);
	return httpd_start(raop->httpd, port);
}

void
raop_stop(raop_t *raop)
{
	assert(raop);
	httpd_stop(raop->httpd);
}

