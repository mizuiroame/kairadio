// Station logos: decoded from PNG, JPEG, GIF, BMP or ICO (with a PNG
// inside), fitted into a square and laid on a white tile, as most logos are
// drawn for a light background. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOGO_SIZE 192  // Pixels per side.
#define LOGO_FILE_MAX (1u << 20)
#define LOGO_MAX_DIMENSION 1024  // Pixels per side; larger logos are refused (shown at 192).

// Decodes data into a LOGO_SIZE x LOGO_SIZE image (0xAABBGGRR). False if
// it isn't an image KaiRadio reads, or it is empty.
bool logoDecode(const uint8_t *data, size_t size, uint32_t *out);
