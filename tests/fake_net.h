// The host tests' stand-in for the network (fake_net.c).
#pragma once

// netGet answers URLs that contain url_part with body and status.
void fakeNetAnswer(const char *url_part, const char *body, long status);
void fakeNetReset(void);
int fakeNetRequests(void);

#include <stdbool.h>
#include <stdint.h>

// netStream serves a made-up file of size bytes at URLs containing url_part:
// byte k is fakeNetFileByte(k). ranges: it starts where asked (else always at
// 0); drop_after: the connection drops once after that many bytes (-1 never).
void fakeNetFile(const char *url_part, int64_t size, bool ranges, int64_t drop_after);
uint8_t fakeNetFileByte(int64_t offset);
int fakeNetStreams(void);  // Requests made since fakeNetFile.
