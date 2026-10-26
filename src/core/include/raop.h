#ifndef RAOP_H
#define RAOP_H

#include "stream.h"
#if defined (WIN32) && defined(DLL_EXPORT)
# define RAOP_API __declspec(dllexport)
#else
# define RAOP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif


/* Define syslog style log levels */
#define RAOP_LOG_EMERG       0       /* system is unusable */
#define RAOP_LOG_ALERT       1       /* action must be taken immediately */
#define RAOP_LOG_CRIT        2       /* critical conditions */
#define RAOP_LOG_ERR         3       /* error conditions */
#define RAOP_LOG_WARNING     4       /* warning conditions */
#define RAOP_LOG_NOTICE      5       /* normal but significant condition */
#define RAOP_LOG_INFO        6       /* informational */
#define RAOP_LOG_DEBUG       7       /* debug-level messages */

#define raop_log_debug(raop, fmt, ...) raop_log(raop, RAOP_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define raop_log_info(raop, fmt, ...) raop_log(raop, RAOP_LOG_INFO, fmt, ##__VA_ARGS__)
#define raop_log_warn(raop, fmt, ...) raop_log(raop, RAOP_LOG_WARNING, fmt, ##__VA_ARGS__)
#define raop_log_err(raop, fmt, ...) raop_log(raop, RAOP_LOG_ERR, fmt, ##__VA_ARGS__)


typedef struct raop_s raop_t;

typedef void (*raop_log_callback_t)(void *cls, int level, const char *msg);

struct raop_callbacks_s {
	void* cls;

	void (*connected)(void* cls, const char* remoteName, const char* remoteDeviceId);
	void (*disconnected)(void* cls, const char* remoteName, const char* remoteDeviceId);
	void  (*audio_process)(void *cls, pcm_data_struct *data, const char* remoteName, const char* remoteDeviceId);
    void  (*video_process)(void *cls, h264_decode_struct *data, const char* remoteName, const char* remoteDeviceId);

	/* Optional but recommended callback functions */
	void  (*audio_flush)(void *cls, void *session, const char* remoteName, const char* remoteDeviceId);
	void  (*audio_set_volume)(void *cls, void *session, float volume, const char* remoteName, const char* remoteDeviceId);
	void  (*audio_set_metadata)(void *cls, void *session, const void *buffer, int buflen, const char* remoteName, const char* remoteDeviceId);
	void  (*audio_set_coverart)(void *cls, void *session, const void *buffer, int buflen, const char* remoteName, const char* remoteDeviceId);
	void  (*audio_remote_control_id)(void *cls, const char *dacp_id, const char *active_remote_header, const char* remoteName, const char* remoteDeviceId);
	void  (*audio_set_progress)(void *cls, void *session, unsigned int start, unsigned int curr, unsigned int end, const char* remoteName, const char* remoteDeviceId);
	/* RAOP_PIN_RANDOM: show this code to the user; NULL once the client got
	 * it right or went away */
	void  (*pin_show)(void *cls, const char *pin);
};
typedef struct raop_callbacks_s raop_callbacks_t;

#define RAOP_PIN_OFF    0
#define RAOP_PIN_RANDOM 1   /* four digits per connection, given through pin_show */
#define RAOP_PIN_FIXED  2   /* the pin passed to raop_set_pin */

RAOP_API raop_t *raop_init(int max_clients, raop_callbacks_t *callbacks);

RAOP_API void raop_set_log_level(raop_t *raop, int level);
RAOP_API void raop_set_log_callback(raop_t *raop, raop_log_callback_t callback, void *cls);
RAOP_API void raop_log(raop_t* raop, int level, const char* fmt, ...);
RAOP_API void raop_set_port(raop_t *raop, unsigned short port);
/* Name and MAC reported in /info; call before raop_start */
RAOP_API void raop_set_identity(raop_t *raop, const char *name, const char *hwaddr, int hwaddrlen);
/* Clients must answer a Digest challenge on SETUP, like an Apple TV with a
 * password. Call before raop_start; pin is used with RAOP_PIN_FIXED. */
RAOP_API void raop_set_pin(raop_t *raop, int mode, const char *pin);
/* A second client while one has a session: replace=0 answers 453 (busy),
 * replace=1 drops the first. Call before raop_start. */
RAOP_API void raop_set_second_device(raop_t *raop, int replace);
/* Listen on this IPv4 address only (network byte order; 0 = every address). Call before raop_start. */
RAOP_API void raop_set_address(raop_t *raop, unsigned long address);
RAOP_API int raop_get_pin_mode(raop_t *raop);
RAOP_API unsigned short raop_get_port(raop_t *raop);
RAOP_API void *raop_get_callback_cls(raop_t *raop);
RAOP_API int raop_start(raop_t *raop, unsigned short *port);
RAOP_API int raop_is_running(raop_t *raop);
RAOP_API void raop_stop(raop_t *raop);

RAOP_API void raop_destroy(raop_t *raop);

#ifdef __cplusplus
}
#endif
#endif
