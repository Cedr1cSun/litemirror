// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// Plays the iOS side of pair-verify against the server-side pairing code,
// using the same primitives the server uses. This pins the wire format
// that tests/integration/rtsp_client.py reimplements in Python.
#include "doctest.h"
#include <string.h>
#include <stdint.h>
// ed25519/fixedint.h redefines the stdint types under C++; tell it they exist.
#define FIXEDINT_H_INCLUDED
extern "C" {
#include "pairing.h"
#include "aes_ctr.h"
#include "ed25519/ed25519.h"
#include "ed25519/sha512.h"
#include "curve25519/curve25519.h"
}

static const unsigned char BASEPOINT[32] = { 9 };

static void derive(const unsigned char shared[32], const char* salt, unsigned char out[16])
{
	unsigned char hash[64];
	sha512_context ctx;
	sha512_init(&ctx);
	sha512_update(&ctx, (const unsigned char*)salt, strlen(salt));
	sha512_update(&ctx, shared, 32);
	sha512_final(&ctx, hash);
	memcpy(out, hash, 16);
}

struct Client {
	unsigned char ed_pub[32], ed_priv[64];
	unsigned char ecdh_priv[32], ecdh_pub[32], shared[32];
	AES_CTR_CTX aes;

	Client()
	{
		unsigned char seed[32];
		REQUIRE(ed25519_create_seed(seed) == 0);
		ed25519_create_keypair(ed_pub, ed_priv, seed);
		REQUIRE(ed25519_create_seed(ecdh_priv) == 0);
		curve25519_donna(ecdh_pub, ecdh_priv, BASEPOINT);
	}

	void keys_from(const unsigned char server_ecdh[32])
	{
		unsigned char key[16], iv[16];
		curve25519_donna(shared, ecdh_priv, server_ecdh);
		derive(shared, "Pair-Verify-AES-Key", key);
		derive(shared, "Pair-Verify-AES-IV", iv);
		AES_ctr_set_key(&aes, key, iv, AES_MODE_128);
	}
};

TEST_CASE("pair-verify handshake succeeds for a well-behaved client")
{
	pairing_t* pairing = pairing_init_generate();
	REQUIRE(pairing != NULL);
	pairing_session_t* session = pairing_session_init(pairing);
	REQUIRE(session != NULL);

	// pair-verify is only allowed after pair-setup
	CHECK(pairing_session_check_handshake_status(session) != 0);
	pairing_session_set_setup_status(session);
	CHECK(pairing_session_check_handshake_status(session) == 0);

	Client client;

	// step 1: client sends its ECDH and Ed25519 public keys
	REQUIRE(pairing_session_handshake(session, client.ecdh_pub, client.ed_pub) == 0);
	unsigned char server_ecdh[32], server_sig[64];
	REQUIRE(pairing_session_get_public_key(session, server_ecdh) == 0);
	REQUIRE(pairing_session_get_signature(session, server_sig) == 0);

	// client decrypts the server signature and checks it
	client.keys_from(server_ecdh);
	AES_ctr_encrypt(&client.aes, server_sig, server_sig, 64);
	unsigned char msg[64], server_ed[32];
	memcpy(msg, server_ecdh, 32);
	memcpy(msg + 32, client.ecdh_pub, 32);
	pairing_get_public_key(pairing, server_ed);
	CHECK(ed25519_verify(server_sig, msg, 64, server_ed));

	// the server's shared secret must match ours
	unsigned char server_shared[32];
	pairing_get_ecdh_secret_key(session, server_shared);
	CHECK(memcmp(server_shared, client.shared, 32) == 0);

	// step 2: client signs (client ECDH || server ECDH) and encrypts with the
	// keystream that follows the first 64 bytes
	unsigned char client_sig[64];
	memcpy(msg, client.ecdh_pub, 32);
	memcpy(msg + 32, server_ecdh, 32);
	ed25519_sign(client_sig, msg, 64, client.ed_pub, client.ed_priv);
	AES_ctr_encrypt(&client.aes, client_sig, client_sig, 64);
	CHECK(pairing_session_finish(session, client_sig) == 0);
	// a finished session cannot be handshaken again
	CHECK(pairing_session_handshake(session, client.ecdh_pub, client.ed_pub) != 0);

	pairing_session_destroy(session);
	pairing_destroy(pairing);
}

TEST_CASE("pair-verify rejects a forged client signature")
{
	pairing_t* pairing = pairing_init_generate();
	pairing_session_t* session = pairing_session_init(pairing);
	pairing_session_set_setup_status(session);
	Client client;

	REQUIRE(pairing_session_handshake(session, client.ecdh_pub, client.ed_pub) == 0);
	unsigned char server_ecdh[32], server_sig[64];
	pairing_session_get_public_key(session, server_ecdh);
	pairing_session_get_signature(session, server_sig);
	client.keys_from(server_ecdh);
	AES_ctr_encrypt(&client.aes, server_sig, server_sig, 64);

	unsigned char forged[64];
	memset(forged, 0x5a, sizeof(forged));
	AES_ctr_encrypt(&client.aes, forged, forged, 64);
	CHECK(pairing_session_finish(session, forged) != 0);

	pairing_session_destroy(session);
	pairing_destroy(pairing);
}

TEST_CASE("a seeded pairing identity is stable across restarts")
{
	unsigned char seed[32];
	for (int i = 0; i < 32; i++) seed[i] = (unsigned char)i;
	pairing_t* a = pairing_init_seed(seed);
	pairing_t* b = pairing_init_seed(seed);
	unsigned char pa[32], pb[32];
	pairing_get_public_key(a, pa);
	pairing_get_public_key(b, pb);
	CHECK(memcmp(pa, pb, 32) == 0);
	pairing_destroy(a);
	pairing_destroy(b);
}
