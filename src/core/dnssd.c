// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// mDNS service registration through Windows' own dnsapi.dll
// (DnsServiceRegister, Windows 10 1809 and later), so users need not
// install Bonjour. The functions are resolved at run time: on an older
// system dnssd_init() reports DNSSD_ERROR_PROCNOTFOUND instead of the
// process failing to start.

#include <winsock2.h>
#include <windows.h>
#include <windns.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "dnssd.h"
#include "dnssdint.h"
#include "global.h"
#include "utils.h"

#define MAX_SERVNAME 256
#define REGISTER_TIMEOUT_MS 5000

typedef PDNS_SERVICE_INSTANCE (WINAPI *ConstructInstance_t)(PCWSTR, PCWSTR, PIP4_ADDRESS, PIP6_ADDRESS,
                                                            WORD, WORD, WORD, DWORD, PCWSTR*, PCWSTR*);
typedef DWORD (WINAPI *Register_t)(PDNS_SERVICE_REGISTER_REQUEST, PDNS_SERVICE_CANCEL);
typedef VOID (WINAPI *FreeInstance_t)(PDNS_SERVICE_INSTANCE);

typedef struct service_s {
	struct dnssd_s *owner;
	PDNS_SERVICE_INSTANCE instance;
	DNS_SERVICE_REGISTER_REQUEST request;
	HANDLE done;       /* set by the completion callback */
	DWORD status;
	int pending;       /* registration issued, callback not yet consumed */
	int registered;
} service_t;

struct dnssd_s {
	HMODULE module;
	ConstructInstance_t construct;
	Register_t reg;
	Register_t dereg;
	FreeInstance_t free_instance;
	WCHAR hostname[256];
	service_t raop;
	service_t airplay;
};

static WCHAR *to_wide(const char *utf8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	WCHAR *out = len > 0 ? malloc(len * sizeof(WCHAR)) : NULL;
	if (out) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, len);
	return out;
}

static VOID WINAPI on_complete(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
	service_t *s = context;
	s->status = status;
	if (instance) s->owner->free_instance(instance);
	SetEvent(s->done);
}

/* Issues the registration; wait_service() collects the answer. Splitting the
 * two lets both services probe at the same time (mDNS probing takes ~0.75 s
 * each). Returns 0 when issued, -1 on failure. */
static int issue_service(service_t *s, const char *instance_name, unsigned short port,
                         const char *const *keys, const char *const *values, int count)
{
	dnssd_t *d = s->owner;
	WCHAR *wname = to_wide(instance_name);
	WCHAR **wkeys = calloc(count, sizeof(WCHAR*));
	WCHAR **wvalues = calloc(count, sizeof(WCHAR*));
	int i, ok = wname && wkeys && wvalues;

	for (i = 0; ok && i < count; i++) {
		wkeys[i] = to_wide(keys[i]);
		wvalues[i] = to_wide(values[i]);
		ok = wkeys[i] && wvalues[i];
	}
	if (ok) {
		/* NULL addresses: Windows announces every address of this host */
		s->instance = d->construct(wname, d->hostname, NULL, NULL, port, 0, 0,
		                           count, (PCWSTR*)wkeys, (PCWSTR*)wvalues);
	}
	for (i = 0; wkeys && wvalues && i < count; i++) {
		free(wkeys[i]);
		free(wvalues[i]);
	}
	free(wkeys);
	free(wvalues);
	free(wname);
	if (!s->instance) {
		return -1;
	}

	memset(&s->request, 0, sizeof(s->request));
	s->request.Version = DNS_QUERY_REQUEST_VERSION1;
	s->request.pServiceInstance = s->instance;
	s->request.pRegisterCompletionCallback = on_complete;
	s->request.pQueryContext = s;

	ResetEvent(s->done);
	s->status = ERROR_SUCCESS;
	if (d->reg(&s->request, NULL) != DNS_REQUEST_PENDING) {
		d->free_instance(s->instance);
		s->instance = NULL;
		return -1;
	}
	s->pending = 1;
	return 0;
}

/* Returns 1 when registered, -1 when Windows refused or did not answer. */
static int wait_service(service_t *s)
{
	dnssd_t *d = s->owner;
	if (!s->pending) {
		return s->registered ? 1 : -1;
	}
	s->pending = 0;
	if (WaitForSingleObject(s->done, REGISTER_TIMEOUT_MS) != WAIT_OBJECT_0) {
		/* still pending: the callback may yet touch the instance, leak it */
		s->instance = NULL;
		return -1;
	}
	if (s->status != ERROR_SUCCESS) {
		d->free_instance(s->instance);
		s->instance = NULL;
		return -1;
	}
	s->registered = 1;
	return 1;
}

static void unregister_service(service_t *s)
{
	dnssd_t *d = s->owner;
	if (s->pending) {
		wait_service(s);   /* stop() after a failed start(): let Windows finish first */
	}
	if (!s->registered) {
		return;
	}
	ResetEvent(s->done);
	if (d->dereg(&s->request, NULL) == DNS_REQUEST_PENDING) {
		WaitForSingleObject(s->done, REGISTER_TIMEOUT_MS);
	}
	d->free_instance(s->instance);
	s->instance = NULL;
	s->registered = 0;
}

dnssd_t *dnssd_init(int *error)
{
	dnssd_t *dnssd;
	DWORD len;

	if (error) *error = DNSSD_ERROR_NOERROR;

	dnssd = calloc(1, sizeof(dnssd_t));
	if (!dnssd) {
		if (error) *error = DNSSD_ERROR_OUTOFMEM;
		return NULL;
	}
	dnssd->raop.owner = dnssd;
	dnssd->airplay.owner = dnssd;

	dnssd->module = LoadLibraryW(L"dnsapi.dll");
	if (!dnssd->module) {
		if (error) *error = DNSSD_ERROR_LIBNOTFOUND;
		free(dnssd);
		return NULL;
	}
	dnssd->construct = (ConstructInstance_t)GetProcAddress(dnssd->module, "DnsServiceConstructInstance");
	dnssd->reg = (Register_t)GetProcAddress(dnssd->module, "DnsServiceRegister");
	dnssd->dereg = (Register_t)GetProcAddress(dnssd->module, "DnsServiceDeRegister");
	dnssd->free_instance = (FreeInstance_t)GetProcAddress(dnssd->module, "DnsServiceFreeInstance");
	dnssd->raop.done = CreateEventW(NULL, TRUE, FALSE, NULL);
	dnssd->airplay.done = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!dnssd->construct || !dnssd->reg || !dnssd->dereg || !dnssd->free_instance ||
	    !dnssd->raop.done || !dnssd->airplay.done) {
		if (error) *error = DNSSD_ERROR_PROCNOTFOUND;
		dnssd_destroy(dnssd);
		return NULL;
	}

	/* mDNS host names are "<computer>.local" */
	len = (DWORD)(sizeof(dnssd->hostname) / sizeof(WCHAR)) - 7;
	if (!GetComputerNameExW(ComputerNameDnsHostname, dnssd->hostname, &len)) {
		wcscpy_s(dnssd->hostname, 256, L"litemirror");
	}
	wcscat_s(dnssd->hostname, 256, L".local");

	return dnssd;
}

void dnssd_destroy(dnssd_t *dnssd)
{
	if (!dnssd) {
		return;
	}
	unregister_service(&dnssd->raop);
	unregister_service(&dnssd->airplay);
	if (dnssd->raop.done) CloseHandle(dnssd->raop.done);
	if (dnssd->airplay.done) CloseHandle(dnssd->airplay.done);
	if (dnssd->module) FreeLibrary(dnssd->module);
	free(dnssd);
}

int dnssd_register_raop(dnssd_t *dnssd, const char *name, unsigned short port,
                        const char *hwaddr, int hwaddrlen, int password)
{
	static const char *const keys[] = {
		"txtvers", "ch", "cn", "et", "sv", "da", "sr", "ss", "pw", "vn",
		"tp", "md", "vs", "sm", "ek", "sf", "am"
	};
	const char *const values[] = {
		RAOP_TXTVERS, RAOP_CH, RAOP_CN, RAOP_ET, RAOP_SV, RAOP_DA, RAOP_SR, RAOP_SS,
		password ? "true" : "false", RAOP_VN,
		RAOP_TP, RAOP_MD, GLOBAL_VERSION, RAOP_SM, RAOP_EK, password ? RAOP_SF_PW : RAOP_SF, GLOBAL_MODEL
	};
	char servname[MAX_SERVNAME];
	int n;

	assert(dnssd);
	assert(name);
	assert(hwaddr);

	/* RAOP instances are named "<MAC as hex>@<name>" */
	if (utils_hwaddr_raop(servname, sizeof(servname), hwaddr, hwaddrlen) < 0) {
		return -1;
	}
	n = (int)strlen(servname);   /* utils_hwaddr_raop's return value counts the NUL */
	if (snprintf(servname + n, sizeof(servname) - n, "@%s._raop._tcp.local", name) >= (int)(sizeof(servname) - n)) {
		return -2;
	}
	return issue_service(&dnssd->raop, servname, port, keys, values, sizeof(keys) / sizeof(keys[0]));
}

int dnssd_register_airplay(dnssd_t *dnssd, const char *name, unsigned short port,
                           const char *hwaddr, int hwaddrlen, int password)
{
	static const char *const keys[] = { "srcvers", "deviceid", "features", "model", "flags", "vv", "pw" };
	char deviceid[3 * MAX_HWADDR_LEN];
	const char *const values[] = { GLOBAL_VERSION, deviceid, "0x5A7FFFF7,0x1E", GLOBAL_MODEL,
	                               password ? RAOP_SF_PW : RAOP_SF, RAOP_VV, password ? "true" : "false" };
	char servname[MAX_SERVNAME];

	assert(dnssd);
	assert(name);
	assert(hwaddr);

	if (utils_hwaddr_airplay(deviceid, sizeof(deviceid), hwaddr, hwaddrlen) < 0) {
		return -1;
	}
	if (snprintf(servname, sizeof(servname), "%s._airplay._tcp.local", name) >= (int)sizeof(servname)) {
		return -2;
	}
	return issue_service(&dnssd->airplay, servname, port, keys, values, sizeof(keys) / sizeof(keys[0]));
}

int dnssd_wait(dnssd_t *dnssd)
{
	int raop, airplay;
	assert(dnssd);
	raop = wait_service(&dnssd->raop);
	airplay = wait_service(&dnssd->airplay);
	return (raop < 0 || airplay < 0) ? -1 : 0;
}

void dnssd_unregister_raop(dnssd_t *dnssd)
{
	assert(dnssd);
	unregister_service(&dnssd->raop);
}

void dnssd_unregister_airplay(dnssd_t *dnssd)
{
	assert(dnssd);
	unregister_service(&dnssd->airplay);
}
