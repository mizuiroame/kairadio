// HTTP(S) with libcurl. On the console curl uses the system's TLS service
// and its certificates.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_USER_AGENT "KaiRadio/" KAIRADIO_VERSION " (+https://github.com/mizuiroame/kairadio)"

// Starts sockets and curl. False if the network can't be used.
bool netInit(void);
void netExit(void);

typedef struct {
    char *data;    // NUL-terminated; free() it.
    size_t size;
    long status;   // HTTP status, 0 if no answer.
    char error[256];  // In the language in use.
} NetResponse;

// GETs url, up to max_bytes (more fails). cancel, if not NULL, stops the
// transfer when it becomes true. True for a 2xx answer that fits.
bool netGet(const char *url, size_t max_bytes, const volatile bool *cancel, NetResponse *out);
// GETs url into the file at path (made anew), up to max_bytes; for downloads
// that may take minutes: it fails only when nothing comes for a while.
// *progress (may be NULL) goes from 0 to 1 when the size is known. On failure
// the file is removed and out->error says why; out->size is the bytes written.
bool netDownloadFile(const char *url, const char *path, size_t max_bytes, const volatile bool *cancel,
                     volatile float *progress, NetResponse *out);
void netResponseFree(NetResponse *r);

// What a streamed answer is, known before its first bytes.
typedef struct {
    long status;              // 206 when it starts where asked.
    int64_t start;            // The offset in the file of the first byte given.
    int64_t total;            // The file's size; -1 if the server doesn't say.
    char content_type[64];
} NetStreamHead;

// Takes the bytes as they come; false stops the transfer.
typedef bool (*NetStreamSink)(const NetStreamHead *head, const void *data, size_t size, void *user);

// GETs url from byte from on (an HTTP range; a server without ranges sends
// the whole file, which head->start then says), handing the bytes to sink.
// For audio that may take long: it fails only when nothing comes for a
// while. True when it all came. False on a failure (error says why), or
// when cancel or the sink stopped it (error empty).
bool netStream(const char *url, int64_t from, const volatile bool *cancel, NetStreamSink sink, void *user,
               NetStreamHead *head, char *error, size_t error_size);

