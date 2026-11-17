// Drawing country flags, decoded when first shown and kept for a while.
#pragma once

#include <stdbool.h>

// Draws the flag of a country code in a w x h layout box (4:3 looks right).
// False, drawing nothing, if there's no flag for it.
bool uiDrawFlag(int x, int y, int w, int h, const char *code);
