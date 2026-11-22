// The notice bubble (notice.h): at the top, centered, over everything.
#pragma once

#include "notice.h"

// Draws n slid in by slide (0 out of sight above, 1 in place), and its tap target.
void uiDrawNotice(const Notice *n, float slide);
