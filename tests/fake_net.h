// The host tests' stand-in for the network (fake_net.c).
#pragma once

// netGet answers URLs that contain url_part with body and status.
void fakeNetAnswer(const char *url_part, const char *body, long status);
void fakeNetReset(void);
int fakeNetRequests(void);
