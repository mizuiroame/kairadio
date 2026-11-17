// Station logos, fetched and decoded in a background thread and kept in a
// small cache. The UI asks for the logos it draws each frame; those not
// ready yet are queued, newest first, and drawn once they arrive.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "logo.h"

#define LOGO_CACHE 64

bool logosInit(void);
void logosExit(void);

// The logo at url, or NULL while it loads or if it can't be had. The
// pixels stay valid until logosEndFrame. Call between frames' draws only.
const uint32_t *logosGet(const char *url);
// Ends a frame: logos not asked for lately may be dropped after this.
void logosEndFrame(void);
