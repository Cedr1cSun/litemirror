/**
 *  Copyright (C) 2018  Juho Vähä-Herttua
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

#include "plist/plist.h"
#include <ctype.h>
#include <stdlib.h>
#include "global.h"
#include "pairing.h"
#include "utils.h"
/* This file should be only included from raop.c as it defines static handler
 * functions and depends on raop internals */

typedef void (*raop_handler_t)(raop_conn_t *, http_request_t *,
                               http_response_t *, char **, int *);

static plist_t
info_audio_format(uint64_t type)
{
	plist_t d = plist_new_dict();
	plist_dict_set_item(d, "type", plist_new_uint(type));
	plist_dict_set_item(d, "audioInputFormats", plist_new_uint(0x1FFFFFC));
	plist_dict_set_item(d, "audioOutputFormats", plist_new_uint(0x1FFFFFC));
	return d;
}

static plist_t
info_audio_latency(uint64_t type)
{
	plist_t d = plist_new_dict();
	plist_dict_set_item(d, "type", plist_new_uint(type));
	plist_dict_set_item(d, "audioType", plist_new_string("default"));
	plist_dict_set_item(d, "inputLatencyMicros", plist_new_bool(0));
	plist_dict_set_item(d, "outputLatencyMicros", plist_new_bool(0));
	return d;
}

/* What an Apple TV answers to GET /info, with this receiver's identity.
 * The client keeps pk to recognise us, so it must be the pairing key. */
static void
raop_handler_info(raop_conn_t *conn,
					   http_request_t *request, http_response_t *response,
					   char **response_data, int *response_datalen)
{
	raop_t *raop = conn->raop;
	char deviceid[3 * MAX_HWADDR_LEN] = "00:00:00:00:00:00";
	unsigned char mac[6] = {0};
	char pi[40];
	unsigned char pk[32];
	plist_t root, arr, display;
	char *bin = NULL;
	uint32_t len = 0;

	if (raop->hwaddrlen > 0) {
		utils_hwaddr_airplay(deviceid, sizeof(deviceid), raop->hwaddr, raop->hwaddrlen);
		memcpy(mac, raop->hwaddr, raop->hwaddrlen < 6 ? raop->hwaddrlen : 6);
	}
	/* pairing identity: any UUID that stays the same for this receiver */
	snprintf(pi, sizeof(pi), "2e388006-13ba-4041-9a67-%02x%02x%02x%02x%02x%02x",
	         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	pairing_get_public_key(raop->pairing, pk);

	root = plist_new_dict();
	plist_dict_set_item(root, "deviceID", plist_new_string(deviceid));
	plist_dict_set_item(root, "macAddress", plist_new_string(deviceid));
	plist_dict_set_item(root, "name", plist_new_string(raop->name));
	plist_dict_set_item(root, "model", plist_new_string(GLOBAL_MODEL));
	plist_dict_set_item(root, "sourceVersion", plist_new_string(GLOBAL_VERSION));
	plist_dict_set_item(root, "pi", plist_new_string(pi));
	plist_dict_set_item(root, "pk", plist_new_data((const char *)pk, sizeof(pk)));
	plist_dict_set_item(root, "features", plist_new_uint(0x1E5A7FFFF7ULL));
	plist_dict_set_item(root, "statusFlags", plist_new_uint(raop->pin_mode ? 0x44 : 0x4));
	plist_dict_set_item(root, "vv", plist_new_uint(2));
	plist_dict_set_item(root, "keepAliveLowPower", plist_new_uint(1));
	plist_dict_set_item(root, "keepAliveSendStatsAsBody", plist_new_uint(1));

	display = plist_new_dict();
	plist_dict_set_item(display, "uuid", plist_new_string("e0ff8a27-6738-3d56-8a16-cc53aacee925"));
	plist_dict_set_item(display, "width", plist_new_uint(raop->display_width));
	plist_dict_set_item(display, "height", plist_new_uint(raop->display_height));
	plist_dict_set_item(display, "widthPixels", plist_new_uint(raop->display_width));
	plist_dict_set_item(display, "heightPixels", plist_new_uint(raop->display_height));
	plist_dict_set_item(display, "widthPhysical", plist_new_bool(0));
	plist_dict_set_item(display, "heightPhysical", plist_new_bool(0));
	plist_dict_set_item(display, "refreshRate", plist_new_uint(raop->display_fps));
	plist_dict_set_item(display, "maxFPS", plist_new_uint(raop->display_fps));   /* the phone caps its frame rate at this */
	plist_dict_set_item(display, "features", plist_new_uint(14));
	plist_dict_set_item(display, "overscanned", plist_new_bool(0));
	plist_dict_set_item(display, "rotation", plist_new_bool(0));
	arr = plist_new_array();
	plist_array_append_item(arr, display);
	plist_dict_set_item(root, "displays", arr);

	arr = plist_new_array();
	plist_array_append_item(arr, info_audio_format(100));
	plist_array_append_item(arr, info_audio_format(101));
	plist_dict_set_item(root, "audioFormats", arr);

	arr = plist_new_array();
	plist_array_append_item(arr, info_audio_latency(100));
	plist_array_append_item(arr, info_audio_latency(101));
	plist_dict_set_item(root, "audioLatencies", arr);

	plist_to_bin(root, &bin, &len);
	plist_free(root);
	if (bin) {
		*response_data = malloc(len);
		if (*response_data) {
			memcpy(*response_data, bin, len);
			*response_datalen = len;
			http_response_add_header(response, "Content-Type", "application/x-apple-binary-plist");
		}
		free(bin);
	}
}

static void
raop_handler_pairsetup(raop_conn_t *conn,
                       http_request_t *request, http_response_t *response,
                       char **response_data, int *response_datalen)
{
	unsigned char public_key[32];
	const char *data;
	int datalen;

	data = http_request_get_data(request, &datalen);
	if (datalen != 32) {
		logger_log(conn->raop->logger, LOGGER_ERR, "Invalid pair-setup data");
		return;
	}

	pairing_get_public_key(conn->raop->pairing, public_key);
    pairing_session_set_setup_status(conn->pairing);

	*response_data = malloc(sizeof(public_key));
	if (*response_data) {
		http_response_add_header(response, "Content-Type", "application/octet-stream");
		memcpy(*response_data, public_key, sizeof(public_key));
		*response_datalen = sizeof(public_key);
	}
}

static void
raop_handler_pairverify(raop_conn_t *conn,
                        http_request_t *request, http_response_t *response,
                        char **response_data, int *response_datalen)
{
	unsigned char public_key[32];
	unsigned char signature[64];
	const unsigned char *data;
	int datalen;

	data = (unsigned char *) http_request_get_data(request, &datalen);
	if (datalen < 4) {
		logger_log(conn->raop->logger, LOGGER_ERR, "Invalid pair-verify data");
		return;
	}
	switch (data[0]) {
	case 1:
		/* Step 1 needs pair-setup first; step 2 is checked by pairing_session_finish */
		if (pairing_session_check_handshake_status(conn->pairing)) {
			logger_log(conn->raop->logger, LOGGER_ERR, "pair-verify before pair-setup");
			return;
		}
		if (datalen != 4 + 32 + 32) {
			logger_log(conn->raop->logger, LOGGER_ERR, "Invalid pair-verify data");
			return;
		}
		/* We can fall through these errors, the result will just be garbage... */
		if (pairing_session_handshake(conn->pairing, data + 4, data + 4 + 32)) {
			logger_log(conn->raop->logger, LOGGER_ERR, "Error initializing pair-verify handshake");
		}
		if (pairing_session_get_public_key(conn->pairing, public_key)) {
			logger_log(conn->raop->logger, LOGGER_ERR, "Error getting ECDH public key");
		}
		if (pairing_session_get_signature(conn->pairing, signature)) {
			logger_log(conn->raop->logger, LOGGER_ERR, "Error getting ED25519 signature");
		}
		*response_data = malloc(sizeof(public_key) + sizeof(signature));
		if (*response_data) {
			http_response_add_header(response, "Content-Type", "application/octet-stream");
			memcpy(*response_data, public_key, sizeof(public_key));
			memcpy(*response_data + sizeof(public_key), signature, sizeof(signature));
			*response_datalen = sizeof(public_key) + sizeof(signature);
		}
		break;
	case 0:
		if (datalen != 4 + 64) {
			logger_log(conn->raop->logger, LOGGER_ERR, "Invalid pair-verify data");
			return;
		}

		if (pairing_session_finish(conn->pairing, data + 4)) {
			logger_log(conn->raop->logger, LOGGER_ERR, "Incorrect pair-verify signature");
			http_response_set_disconnect(response, 1);
			return;
		}
        http_response_add_header(response, "Content-Type", "application/octet-stream");
		break;
	}
}

static void
raop_handler_fpsetup(raop_conn_t *conn,
                        http_request_t *request, http_response_t *response,
                        char **response_data, int *response_datalen)
{
	const unsigned char *data;
	int datalen;

	data = (unsigned char *) http_request_get_data(request, &datalen);
	if (datalen == 16) {
		*response_data = malloc(142);
		if (*response_data) {
            http_response_add_header(response, "Content-Type", "application/octet-stream");
			if (!fairplay_setup(conn->fairplay, data, (unsigned char *) *response_data)) {
				*response_datalen = 142;
			} else {
				// Handle error?
				free(*response_data);
				*response_data = NULL;
			}
		}
	} else if (datalen == 164) {
		*response_data = malloc(32);
		if (*response_data) {
            http_response_add_header(response, "Content-Type", "application/octet-stream");
			if (!fairplay_handshake(conn->fairplay, data, (unsigned char *) *response_data)) {
				*response_datalen = 32;
			} else {
				// Handle error?
				free(*response_data);
				*response_data = NULL;
			}
		}
	} else {
		logger_log(conn->raop->logger, LOGGER_ERR, "Invalid fp-setup data length");
		return;
	}
}

static void
raop_handler_options(raop_conn_t *conn,
                     http_request_t *request, http_response_t *response,
                     char **response_data, int *response_datalen)
{
	http_response_add_header(response, "Public", "SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER");
}

// SETUP rtsp://192.168.1.125/6265229603576367238 RTSP/1.0
//Content-Length: 533
//Content-Type: application/x-apple-binary-plist
//CSeq: 6
//DACP-ID: 915531CAB703EF08
//Active-Remote: 2292691321
//User-Agent: AirPlay/387.2
static void
raop_handler_setup(raop_conn_t *conn,
                   http_request_t *request, http_response_t *response,
                   char **response_data, int *response_datalen)
{
    unsigned short remote_cport=0, remote_tport=0;

    const char *transport;
    char buffer[1024];
    int use_udp;
    const char *dacp_id;
    const char *active_remote_header;

    const char *data;
    int datalen;

    data = http_request_get_data(request, &datalen);

    dacp_id = http_request_get_header(request, "DACP-ID");
    active_remote_header = http_request_get_header(request, "Active-Remote");

    if (dacp_id && active_remote_header) {
        logger_log(conn->raop->logger, LOGGER_DEBUG, "DACP-ID: %s", dacp_id);
        logger_log(conn->raop->logger, LOGGER_DEBUG, "Active-Remote: %s", active_remote_header);
        if (conn->raop_rtp) {
            raop_rtp_remote_control_id(conn->raop_rtp, dacp_id, active_remote_header);
        }
    }

    transport = http_request_get_header(request, "Transport");
    if (transport) {
        logger_log(conn->raop->logger, LOGGER_INFO, "Transport: %s", transport);
        use_udp = strncmp(transport, "RTP/AVP/TCP", 11);
    } else {
        logger_log(conn->raop->logger, LOGGER_INFO, "Transport: null");
        use_udp = 0;
    }


    // 解析bplist
    plist_t root_node = NULL;
    plist_from_bin(data, datalen, &root_node);
    plist_t streams_note = plist_dict_get_item(root_node, "streams");
	plist_t eiv_note = plist_dict_get_item(root_node, "eiv");  // 只有第一次調用存在

	plist_t stream_note = plist_array_get_item(streams_note, 0); // 非第一次调用存在
	plist_t stream_id_note = NULL; // 第二次调用时存在
	plist_t audio_format_note = NULL; // 第三次调用存在
	if (stream_note) {
		stream_id_note = plist_dict_get_item(stream_note, "streamConnectionID");
		audio_format_note = plist_dict_get_item(stream_note, "audioFormat");
	}

	if (eiv_note) { // 第一次调用SETUP。
		unsigned char aesiv[16];
		unsigned char aeskey[16];
        logger_log(conn->raop->logger, LOGGER_DEBUG, "SETUP 1");
        // 第一次setup
		/*
<plist version="1.0">
<dict>
	<key>et</key>
	<integer>32</integer>
	<key>eiv</key>
	<data>
		Bp5tRB8BOW/MWVJSGzALLw==
	</data>
	<key>timingProtocol</key>
	<string>NTP</string>
	<key>sessionUUID</key>
	<string>43C10532-7CBC-419E-9BB3-528F7D6F9AE0</string>
	<key>osName</key>
	<string>iPhone OS</string>
	<key>osBuildVersion</key>
	<string>16A404</string>
	<key>sourceVersion</key>
	<string>371.4.7</string>
	<key>timingPort</key>
	<integer>60373</integer>
	<key>isScreenMirroringSession</key>
	<true/>
	<key>osVersion</key>
	<string>12.0.1</string>
	<key>ekey</key>
	<data>
		RlBMWQECAQAAAAA8AAAAALFuVD0C1qvRjZI5wtJY4v0AAAAQd5dKdzn2dNJ2ysNpS4VjnfmFHlRqEnXFqUeXzEtMDLIdF/5Y
	</data>
	<key>deviceID</key>
	<string>DC:0C:5C:B7:D6:DA</string>
	<key>model</key>
	<string>iPhone9,1</string>
	<key>name</key>
	<string>xxx的 iPhone</string>
	<key>macAddress</key>
	<string>DC:0C:5C:B7:D6:D8</string>
</dict>
</plist>
		*/
        plist_t eiv_note = plist_dict_get_item(root_node, "eiv");
        char* eiv= NULL;
        uint64_t eiv_len = 0;
        plist_get_data_val(eiv_note, &eiv, &eiv_len);
        logger_log(conn->raop->logger, LOGGER_DEBUG, "eiv_len = %llu", eiv_len);

        plist_t ekey_note = plist_dict_get_item(root_node, "ekey");
        char* ekey= NULL;
        uint64_t ekey_len = 0;
        if (ekey_note) plist_get_data_val(ekey_note, &ekey, &ekey_len);
        logger_log(conn->raop->logger, LOGGER_DEBUG, "ekey_len = %llu", ekey_len);

        /* libplist getters assert on a NULL node and the key material has a
         * fixed size, so refuse anything else instead of reading past it */
        if (eiv_len != 16 || ekey_len != 72) {
            logger_log(conn->raop->logger, LOGGER_ERR, "SETUP needs a 16-byte eiv and a 72-byte ekey, dropping client");
            http_response_set_disconnect(response, 1);
            free(eiv);
            free(ekey);
            plist_free(root_node);
            return;
        }
        memcpy(aesiv, eiv, 16);

        // 时间port
		uint64_t timing_rport = 0;
        plist_t time_note = plist_dict_get_item(root_node, "timingPort");
        if (time_note) plist_get_uint_val(time_note, &timing_rport);
		logger_log(conn->raop->logger, LOGGER_DEBUG, "timing_rport = %llu", timing_rport);

        // ekey是72字节
        int ret = fairplay_decrypt(conn->fairplay, ekey, aeskey);
        logger_log(conn->raop->logger, LOGGER_DEBUG, "fairplay_decrypt ret = %d", ret);
		unsigned char ecdh_secret[32];
        pairing_get_ecdh_secret_key(conn->pairing, ecdh_secret);

		// name
		char* name = NULL;
		plist_t name_node = plist_dict_get_item(root_node, "name");
		if (name_node) plist_get_string_val(name_node, &name);

		char* deviceId = NULL;
		plist_t device_id_node = plist_dict_get_item(root_node, "deviceID");
		if (device_id_node) plist_get_string_val(device_id_node, &deviceId);

        conn->raop_rtp = raop_rtp_init(conn->raop->logger, &conn->raop->callbacks, conn->remote, conn->remotelen, 
			name, deviceId,
			aeskey, aesiv, ecdh_secret, timing_rport);
		conn->raop_rtp_mirror = raop_rtp_mirror_init(conn->raop->logger, &conn->raop->callbacks, conn->remote, conn->remotelen, 
			name, deviceId,
			aeskey, ecdh_secret, timing_rport);
		if (name != NULL) {
			free(name); name = NULL;
		}
		if (deviceId != NULL) {
			free(deviceId); deviceId = NULL;
		}
	}
	else if (stream_id_note) {
		unsigned short tport=0, dport=0;
        logger_log(conn->raop->logger, LOGGER_DEBUG, "SETUP 2");
		/*
<plist version="1.0">
<dict>
	<key>streams</key>
	<array>
		<dict>
			<key>type</key>
			<integer>110</integer>
			<key>timestampInfo</key>
			<array>
				<dict>
					<key>name</key>
					<string>SubSu</string>
				</dict>
				<dict>
					<key>name</key>
					<string>BePxT</string>
				</dict>
				<dict>
					<key>name</key>
					<string>AfPxT</string>
				</dict>
				<dict>
					<key>name</key>
					<string>BefEn</string>
				</dict>
				<dict>
					<key>name</key>
					<string>EmEnc</string>
				</dict>
			</array>
			<key>streamConnectionID</key>
			<integer>4964383553955644435</integer>
		</dict>
	</array>
</dict>
</plist>
		*/
		plist_t stream_note = plist_array_get_item(streams_note, 0);
		plist_t type_note = plist_dict_get_item(stream_note, "type");
		if (type_note != NULL) {
			uint64_t type;
			plist_get_uint_val(type_note, &type);
			logger_log(conn->raop->logger, LOGGER_DEBUG, "type = %llu", type);
		}
		plist_t stream_id_note = plist_dict_get_item(stream_note, "streamConnectionID");
		uint64_t streamConnectionID = 0;
		if (stream_id_note != NULL) {
			plist_get_uint_val(stream_id_note, &streamConnectionID);
			logger_log(conn->raop->logger, LOGGER_DEBUG, "streamConnectionID = %llu", streamConnectionID);
		}

        if (conn->raop_rtp_mirror) {
			raop_rtp_init_mirror_aes(conn->raop_rtp_mirror, streamConnectionID);
			raop_rtp_start_mirror(conn->raop_rtp_mirror, use_udp, remote_tport, &tport, &dport);
            logger_log(conn->raop->logger, LOGGER_DEBUG, "RAOP initialized success");
        } else {
            logger_log(conn->raop->logger, LOGGER_ERR, "RAOP not initialized at SETUP, playing will fail! [Setup2: raop_rtp_mirror is NULL]");
            http_response_set_disconnect(response, 1);
        }
        plist_t r_node = plist_new_dict();
        plist_t s_node = plist_new_array();
        plist_t s_sub_node = plist_new_dict();
        plist_t data_port_node = plist_new_uint(dport);
        plist_t type_node = plist_new_uint(110);
        plist_t event_port_node = plist_new_uint(conn->raop->port);
        plist_t timing_port_node = plist_new_uint(tport);
        plist_dict_set_item(s_sub_node, "dataPort", data_port_node);
        plist_dict_set_item(s_sub_node, "type", type_node);
        plist_array_append_item(s_node, s_sub_node);
        plist_dict_set_item(r_node, "eventPort", event_port_node);
        plist_dict_set_item(r_node, "timingPort", timing_port_node);
        plist_dict_set_item(r_node, "streams", s_node);
        uint32_t len = 0;
        char* rsp = NULL;
        plist_to_bin(r_node, &rsp, &len);
        logger_log(conn->raop->logger, LOGGER_DEBUG, "SETUP 2 len = %d", len);
        http_response_add_header(response, "Content-Type", "application/x-apple-binary-plist");
        *response_data = malloc(len);
        memcpy(*response_data, rsp, len);
        *response_datalen = len;

		plist_free(r_node);
		r_node = NULL;
		if (rsp != NULL) {
			free(rsp);
			rsp = NULL;
		}
		logger_log(conn->raop->logger, LOGGER_INFO, "dport = %d, tport = %d", dport, tport);
    } else if (audio_format_note) {
        logger_log(conn->raop->logger, LOGGER_DEBUG, "SETUP 3");
        unsigned short cport = 0, tport = 0, dport = 0;

        if (conn->raop_rtp) {
            raop_rtp_start_audio(conn->raop_rtp, use_udp, remote_cport, remote_tport, &cport, &tport, &dport);
            logger_log(conn->raop->logger, LOGGER_DEBUG, "RAOP initialized success");
        } else {
            logger_log(conn->raop->logger, LOGGER_ERR, "RAOP not initialized at SETUP, playing will fail![Setup3: raop_rtp is NULL]");
            http_response_set_disconnect(response, 1);
        }
        // 需要返回端口
//		/**
//		 * <dict>
//	<key>streams</key>
//	<array>
//		<dict>
//			<key>dataPort</key>
//			<integer>42820</integer>
//			<key>controlPort</key>
//			<integer>46440</integer>
//			<key>type</key>
//			<integer>96</integer>
//		</dict>
//	</array>
//
//	<key>timingPort</key>
//	<integer>46440</integer>
//</dict>
//</plist>
//		 */
		plist_t r_node = plist_new_dict();
		plist_t s_node = plist_new_array();
		plist_t s_sub_node = plist_new_dict();
		plist_t data_port_node = plist_new_uint(dport);
		plist_t type_node = plist_new_uint(96);
		plist_t control_port_node = plist_new_uint(cport);
		plist_t timing_port_node = plist_new_uint(tport);
		plist_dict_set_item(s_sub_node, "dataPort", data_port_node);
		plist_dict_set_item(s_sub_node, "type", type_node);
		plist_dict_set_item(s_sub_node, "controlPort", control_port_node);
		plist_array_append_item(s_node, s_sub_node);
		plist_dict_set_item(r_node, "timingPort", timing_port_node);
		plist_dict_set_item(r_node, "streams", s_node);
		uint32_t len = 0;
		char* rsp = NULL;
		plist_to_bin(r_node, &rsp, &len);
		logger_log(conn->raop->logger, LOGGER_DEBUG, "SETUP 3 len = %d", len);
		http_response_add_header(response, "Content-Type", "application/x-apple-binary-plist");
		*response_data = malloc(len);
		memcpy(*response_data, rsp, len);
		*response_datalen = len;

		plist_free(r_node);
		r_node = NULL;
		if (rsp != NULL) {
			free(rsp);
			rsp = NULL;
		}
		logger_log(conn->raop->logger, LOGGER_INFO, "dport = %d, tport = %d, cport = %d", dport, tport, cport);
    }
	else {
		// unknown stream
		logger_log(conn->raop->logger, LOGGER_WARNING, "Unknown stream");
		http_response_add_header(response, "Content-Type", "application/x-apple-binary-plist");
	}

	if (root_node) {
		plist_free(root_node);
		root_node = NULL;
	}
}

static void
raop_handler_get_parameter(raop_conn_t *conn,
                           http_request_t *request, http_response_t *response,
                           char **response_data, int *response_datalen)
{
	const char *content_type;
	const char *data;
	int datalen;

	content_type = http_request_get_header(request, "Content-Type");
	data = http_request_get_data(request, &datalen);
	if (!content_type) {
		return;   /* nothing to act on */
	}
	if (!strcmp(content_type, "text/parameters")) {
		const char *current = data;

		while (current) {
			const char *next;
			int handled = 0;

			/* This is a bit ugly, but seems to be how airport works too */
			if (!strncmp(current, "volume\r\n", 8)) {
				const char volume[] = "volume: 0.0\r\n";

				http_response_add_header(response, "Content-Type", "text/parameters");
				*response_data = strdup(volume);
				if (*response_data) {
					*response_datalen = strlen(*response_data);
				}
				handled = 1;
			}

			next = strstr(current, "\r\n");
			if (next && !handled) {
				logger_log(conn->raop->logger, LOGGER_WARNING,
				           "Found an unknown parameter: %.*s", (next - current), current);
				current = next + 2;
			} else if (next) {
				current = next + 2;
			} else {
				current = NULL;
			}
		}
	}
}

static void
raop_handler_set_parameter(raop_conn_t *conn,
                           http_request_t *request, http_response_t *response,
                           char **response_data, int *response_datalen)
{
	const char *content_type;
	const char *data;
	int datalen;

	content_type = http_request_get_header(request, "Content-Type");
	data = http_request_get_data(request, &datalen);
	if (!content_type) {
		return;   /* nothing to act on */
	}
	if (!strcmp(content_type, "text/parameters")) {
		char *datastr;
		datastr = calloc(1, datalen+1);
		if (data && datastr && conn->raop_rtp) {
			memcpy(datastr, data, datalen);
			if (!strncmp(datastr, "volume: ", 8)) {
				float vol = 0.0;
				sscanf(datastr+8, "%f", &vol);
				raop_rtp_set_volume(conn->raop_rtp, vol);
			} else if (!strncmp(datastr, "progress: ", 10)) {
				unsigned int start, curr, end;
				sscanf(datastr+10, "%u/%u/%u", &start, &curr, &end);
				raop_rtp_set_progress(conn->raop_rtp, start, curr, end);
			}
		} else if (!conn->raop_rtp) {
			logger_log(conn->raop->logger, LOGGER_WARNING, "RAOP not initialized at SET_PARAMETER");
		}
		free(datastr);
	} else if (!strcmp(content_type, "image/jpeg") || !strcmp(content_type, "image/png")) {
		logger_log(conn->raop->logger, LOGGER_INFO, "Got image data of %d bytes", datalen);
		if (conn->raop_rtp) {
			raop_rtp_set_coverart(conn->raop_rtp, data, datalen);
		} else {
			logger_log(conn->raop->logger, LOGGER_WARNING, "RAOP not initialized at SET_PARAMETER coverart");
		}
	} else if (!strcmp(content_type, "application/x-dmap-tagged")) {
		logger_log(conn->raop->logger, LOGGER_INFO, "Got metadata of %d bytes", datalen);
		if (conn->raop_rtp) {
			raop_rtp_set_metadata(conn->raop_rtp, data, datalen);
		} else {
			logger_log(conn->raop->logger, LOGGER_WARNING, "RAOP not initialized at SET_PARAMETER metadata");
		}
	}
}


static void
raop_handler_feedback(raop_conn_t *conn,
                           http_request_t *request, http_response_t *response,
                           char **response_data, int *response_datalen)
{
    logger_log(conn->raop->logger, LOGGER_DEBUG, "raop_handler_feedback");
}

static void
raop_handler_record(raop_conn_t *conn,
                      http_request_t *request, http_response_t *response,
                      char **response_data, int *response_datalen)
{
    logger_log(conn->raop->logger, LOGGER_DEBUG, "raop_handler_record");
    http_response_add_header(response, "Audio-Latency", "11025");
    http_response_add_header(response, "Audio-Jack-Status", "connected; type=analog");
}

static void
raop_handler_teardown(raop_conn_t* conn,
	http_request_t* request, http_response_t* response,
	char** response_data, int* response_datalen)
{
	const char* data;
	int datalen;

	data = http_request_get_data(request, &datalen);

	plist_t root_node = NULL;
	plist_from_bin(data, datalen, &root_node);
	plist_t streams_note = plist_dict_get_item(root_node, "streams");

	uint64_t type = 0;
	if (streams_note) {
		plist_t stream_note = plist_array_get_item(streams_note, 0);
		if (stream_note) {
			plist_t type_node = plist_dict_get_item(stream_note, "type");
			if (type_node) plist_get_uint_val(type_node, &type);
			logger_log(conn->raop->logger, LOGGER_DEBUG, "type = %llu", type);
		}
	}
	if (type == 96) {
		logger_log(conn->raop->logger, LOGGER_DEBUG, "teardown raop");
		if (conn->raop_rtp) raop_rtp_stop(conn->raop_rtp);
	}
	else if (type == 110) {
		logger_log(conn->raop->logger, LOGGER_DEBUG, "teardown mirror");
		if (conn->raop_rtp_mirror) raop_rtp_mirror_stop(conn->raop_rtp_mirror);
	}
	else {
		logger_log(conn->raop->logger, LOGGER_DEBUG, "teardown client");
		if (conn->raop->session_conn == conn) {
			conn->raop->session_conn = NULL;
		}
		if (conn->raop_rtp) {
			/* Destroy our RTP session */
			raop_rtp_destroy(conn->raop_rtp);
			conn->raop_rtp = NULL;
		}
		if (conn->raop_rtp_mirror) {
			/* Destroy our mirror session */
			raop_rtp_mirror_destroy(conn->raop_rtp_mirror);
			conn->raop_rtp_mirror = NULL;
		}
	}
	logger_log(conn->raop->logger, LOGGER_DEBUG, "teardown end");

	plist_free(root_node);
}