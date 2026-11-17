// HTTP(S) with libcurl. On the console curl uses the system's TLS service
// and its certificates.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define NET_USER_AGENT "KaiRadio/" KAIRADIO_VERSION " (+https://github.com/mizuiroame/kairadio)"

// Starts sockets and curl. False if the network can't be used.
bool netInit(void);
void netExit(void);

typedef struct {
    char *data;    // NUL-terminated; free() it.
    size_t size;
    long status;   // HTTP status, 0 if no answer.
    char error[128];
} NetResponse;

// GETs url, up to max_bytes (more fails). cancel, if not NULL, stops the
// transfer when it becomes true. True for a 2xx answer that fits.
bool netGet(const char *url, size_t max_bytes, const volatile bool *cancel, NetResponse *out);
void netResponseFree(NetResponse *r);

