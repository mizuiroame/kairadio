// net.c for the host tests: answers from a table the tests fill, so the
// directory can be tested without a network.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_net.h"
#include "net.h"

#define FAKE_MAX 16

static struct {
    char url_part[128];
    char *body;
    long status;
} g_answers[FAKE_MAX];
static int g_count;
static int g_requests;

void fakeNetAnswer(const char *url_part, const char *body, long status)
{
    if (g_count < FAKE_MAX) {
        snprintf(g_answers[g_count].url_part, sizeof(g_answers[0].url_part), "%s", url_part);
        g_answers[g_count].body = strdup(body);
        g_answers[g_count].status = status;
        g_count++;
    }
}

void fakeNetReset(void)
{
    for (int i = 0; i < g_count; i++)
        free(g_answers[i].body);
    g_count = 0;
    g_requests = 0;
}

int fakeNetRequests(void)
{
    return g_requests;
}

bool netInit(void)
{
    return true;
}

void netExit(void)
{
}

bool netGet(const char *url, size_t max_bytes, const volatile bool *cancel, NetResponse *out)
{
    (void)cancel;
    memset(out, 0, sizeof(*out));
    g_requests++;
    for (int i = 0; i < g_count; i++) {
        if (!strstr(url, g_answers[i].url_part))
            continue;
        out->status = g_answers[i].status;
        if (out->status < 200 || out->status >= 300 || strlen(g_answers[i].body) > max_bytes) {
            snprintf(out->error, sizeof(out->error), "The server answered %ld", out->status);
            return false;
        }
        out->data = strdup(g_answers[i].body);
        out->size = strlen(out->data);
        return true;
    }
    snprintf(out->error, sizeof(out->error), "Couldn't resolve host name");
    return false;
}

void netResponseFree(NetResponse *r)
{
    free(r->data);
    r->data = NULL;
    r->size = 0;
}
